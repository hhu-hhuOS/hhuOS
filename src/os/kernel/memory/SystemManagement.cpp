/*
 * Copyright (C) 2018 Burak Akguel, Christian Gesse, Fabian Ruhland, Filip Krakowski, Michael Schoettner
 * Heinrich-Heine University
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "SystemManagement.h"

#include <devices/misc/Bios.h>
#include <kernel/interrupts/IntDispatcher.h>
#include <lib/libc/printf.h>
#include <lib/multiboot/Structure.h>
#include <kernel/log/Logger.h>
#include <devices/timer/Pit.h>
#include <kernel/log/StdOutAppender.h>
#include "devices/cpu/Cpu.h"
#include "kernel/memory/Paging.h"
#include "MemLayout.h"

// some external functions are implemented in assembler code
extern "C"{
    #include "lib/libc/string.h"
	// load CR3 with physical address of page directory
	void load_page_directory(uint32_t* pdAddress);
	// functions to set up memory management and paging
    void _init();
    void _fini();
    void init_system(Multiboot::Info *address);
    void fini_system();
    void init_gdt(uint16_t* gdt, uint16_t* gdt_bios, uint16_t* gdt_desc, uint16_t* gdt_bios_desc, uint16_t* gdt_phys_desc);
}

// initialize static members
SystemManagement* SystemManagement::systemManagement = nullptr;
MemoryManager* SystemManagement::kernelMemoryManager = nullptr;
bool SystemManagement::initialized = false;
bool SystemManagement::kernelMode = true;

/**
 * Is called from assembler code before calling the main function, because it sets up
 * everything to get the system run.
 */
void init_system(Multiboot::Info *address) {
    // enable interrupts afterwards
    Cpu::enableInterrupts();

    Multiboot::Structure::init(address);

    // create an instance of the SystemManagement and initialize it
    // (sets up paging and system management)
    SystemManagement &systemManagement = SystemManagement::getInstance();

    systemManagement.init();

    Multiboot::Structure::parse();

    Pit::getInstance().plugin();

    Logger::initialize();

    Logger::setLevel(Multiboot::Structure::getKernelOption("log_level"));

    if(Multiboot::Structure::getKernelOption("gdb").isEmpty()) {
        systemManagement.writeProtectKernelCode();
    }
}

/**
 * Finishes the system and calls global destructors.
 */
void fini_system() {

    _fini();
}

/**
 * Sets up the GDT for the system and a special GDT for BIOS-calls.
 * Only these two GDTs are needed, because memory protection and abstractions is done via paging.
 * The memory where the parameters point to is reserved in assembler code before paging is enabled.
 * Therefore we assume that the given pointers are physical addresses  - this is very important
 * to guarantee correct GDT descriptors using this setup-function.
 *
 * @param gdt Pointer to the GDT of the system
 * @param gdt_bios Pointer to the GDT for BIOS-calls
 * @param gdt_desc Pointer to the descriptor of GDT; this descriptor should contain the virtual address of GDT
 * @param gdt_phys_desc Pointer to the descriptor of GDT; this descriptor should contain the physical address of GDT
 * @param gdt_bios_desc Pointer to the descriptor of BIOS-GDT; this descriptor should contain the physical address of BIOS-GDT
 */
void init_gdt(uint16_t* gdt, uint16_t* gdt_bios, uint16_t* gdt_desc, uint16_t* gdt_bios_desc, uint16_t* gdt_phys_desc) {
	// first set up general GDT for the system
	// first entry has to be null
	SystemManagement::createGDTEntry(gdt, 0, 0, 0, 0, 0);
	// kernel code segment
	SystemManagement::createGDTEntry(gdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xC);
	// kernel data segment
	SystemManagement::createGDTEntry(gdt, 2, 0, 0xFFFFFFFF, 0x92, 0xC);
	// user code segment
	SystemManagement::createGDTEntry(gdt, 3, 0, 0xFFFFFFFF, 0xFA, 0xC);
	// user data segment
	SystemManagement::createGDTEntry(gdt, 4, 0, 0xFFFFFFFF, 0xF2, 0xC);

	// set up descriptor for GDT
	*((uint16_t*)gdt_desc) = 5 * 8;
	// the normal descriptor should contain the virtual address of GDT
	*((uint32_t*)(gdt_desc + 1)) = (uint32_t)gdt + KERNEL_START;

	// set up descriptor for GDT with phys. address - needed for bootstrapping
	*((uint16_t*)gdt_phys_desc) = 5 * 8;
	// this descriptor should contain the physical address of GDT
	*((uint32_t*)(gdt_phys_desc + 1)) = (uint32_t)gdt;

	// now set up GDT for BIOS-calls (notice that no userspace entries are necessary here)
	// first entry has to be null
	SystemManagement::createGDTEntry(gdt_bios, 0, 0, 0, 0, 0);
	// kernel code segment
	SystemManagement::createGDTEntry(gdt_bios, 1, 0, 0xFFFFFFFF, 0x9A, 0xC);
	// kernel data segment
	SystemManagement::createGDTEntry(gdt_bios, 2, 0, 0xFFFFFFFF, 0x92, 0xC);
	// prepared BIOS-call segment (contains 16-bit code etc...)
	SystemManagement::createGDTEntry(gdt_bios, 3, 0x4000, 0xFFFFFFFF, 0x9A, 0x8);


	// set up descriptor for BIOS-GDT
	*((uint16_t*)gdt_bios_desc) = 4 * 8;
	// the descriptor should contain physical address of BIOS-GDT because paging is not enabled during BIOS-calls
	*((uint32_t*)(gdt_bios_desc + 1)) = (uint32_t)gdt_bios;
}

/**
 * Creates an entry into a given GDT (Global Descriptor Table).
 * Memory for the GDT must be allocated before.
 */
void SystemManagement::createGDTEntry(uint16_t* gdt, uint16_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
	// each GDT-entry consists of 4 16-bit unsigned integers
	// calculate index into 16bit-array that represents GDT
	uint16_t idx = 4 * num;

	// first 16-bit value: [Limit 0:15]
	gdt[idx] 		= (uint16_t) (limit & 0xFFFF);
	// second 16-bit value: [Base 0:15]
	gdt[idx + 1] 	= (uint16_t) (base & 0xFFFF);
	// third 16-bit value: [Access Byte][Base 16:23]
	gdt[idx + 2]	= (uint16_t) ((base >> 16) & 0xFF) | (access << 8);
	// fourth 16-bit value: [Base 24:31][Flags][Limit 16:19]
	gdt[idx + 3]	= (uint16_t) ((limit >> 16) & 0x0F) | ((flags << 4) & 0xF0) | ((base >> 16) & 0xFF00);
	// end of GDT-entry
}

void SystemManagement::plugin() {
    IntDispatcher::getInstance().assign(IntDispatcher::PAGEFAULT, *this);
}


void SystemManagement::trigger(InterruptFrame &frame) {
#if DEBUG_PM
    printf("[PAGINGMANAGER] Pagefault occured\n");
    printf("[PAGINGMANAGER] Address %x\n", faultedAddress);
    printf("[PAGINGMANAGER] Flags %x\n", faultFlags);
    printf("[PAGINGMANAGER] Floored 4kb aligned address %x\n", (faultedAddress & 0xFFFFF000));
#endif
    // check if pagefault was caused by illegal page access
    if ((faultFlags & 0x00000001) > 0) {
    	Cpu::throwException(Cpu::Exception::ILLEGEAL_PAGE_ACCESS);
    }

    // Map the faulted Page
    map(faultedAddress, PAGE_PRESENT | PAGE_READ_WRITE);
    // TODO: Check other Faults
}

void SystemManagement::init() {
    // Init Paging Area Manager -> Manages the virtual addresses of all page tables
    // and directories
    pagingAreaManager = new PagingAreaManager();
    // create a Base Page Directory (used to map the kernel into every process)
    basePageDirectory = new PageDirectory();

    // Physical Page Frame Allocator is initialized to be possible to allocate
    // physical memory (page frames)
    calcTotalPhysicalMemory();
    pageFrameAllocator = new PageFrameAllocator();
    pageFrameAllocator->init(0, totalPhysMemory, false);

#if DEBUG_PM
    printf("[PAGINGMANAGER] 4KB paging is activated \n");
#endif

    // to be able to map new pages, a bootstrap address space is created.
    // It uses only the basePageDirectory with mapping for kernel space
    currentAddressSpace = new VirtualAddressSpace(basePageDirectory, nullptr);

    // register Paging Manager to handle Page Faults
    this->plugin();

    // init io-memory afterwards, because pagefault will occur setting up the first list header
    // Init the manager for virtual IO Memory
    ioMemManager = new IOMemoryManager();

    // now create the first address space with memory managers for kernel and user space
    VirtualAddressSpace *addressSpace = new VirtualAddressSpace(basePageDirectory);
    VirtualAddressSpace *tmp = currentAddressSpace;
    switchAddressSpace(addressSpace);
    // we can delete the bootstrap address space
    delete tmp;
    // add first address space to list with all address spaces
    addressSpaces = new Util::ArrayList<VirtualAddressSpace*>;
    addressSpaces->add(currentAddressSpace);

    // Initialize global objects afterwards, because now missing pages can be mapped
    _init();

    // the memory management system is fully initialized now
    initialized = true;
}

void SystemManagement::map(uint32_t virtAddress, uint16_t flags) {
	// allocate a physical page frame where the page should be mapped
    uint32_t physAddress = (uint32_t) pageFrameAllocator->alloc(PAGESIZE);
    // map the page into the directory
    currentAddressSpace->getPageDirectory()->map(physAddress, virtAddress, flags);

#if DEBUG_PM
    printf("[PAGINGMANAGER] Map virtual address %x to phys address %x\n", (virtAddress & 0xFFFFF000), physAddress);
#endif
}

void SystemManagement::map(uint32_t virtAddress, uint16_t flags, uint32_t physAddress) {
#if DEBUG_PM
    printf("[PAGINGMANAGER] Map virtual address %x to phys address %x\n", (virtAddress & 0xFFFFF000), physAddress);
#endif
    // map the page into the directory
    currentAddressSpace->getPageDirectory()->map(physAddress, virtAddress, flags);
}

/**
 * Range map function to map a range of virtual addresses into the current Page 
 * Directory .
 */
void SystemManagement::map(uint32_t virtStartAddress, uint32_t virtEndAddress, uint16_t flags){
    // get 4kb-aligned start and end address
    uint32_t alignedStartAddress = virtStartAddress & 0xFFFFF000;
    uint32_t alignedEndAddress = virtEndAddress & 0xFFFFF000;
    alignedEndAddress += (virtEndAddress % PAGESIZE == 0) ? 0 : PAGESIZE;
    // map all pages
    for(uint32_t i = alignedStartAddress; i < alignedEndAddress; i += PAGESIZE){
        map(i, flags);
    }
}

uint32_t SystemManagement::createPageTable(PageDirectory *dir, uint32_t idx){
    // get some virtual memory for the table
    void *virtAddress = pagingAreaManager->alloc(PAGESIZE);
    // get physical memory for the table
    void *physAddress = getPhysicalAddress(virtAddress);
    // there must be no mapping from virtual to physical address be done here,
    // because the page is zeroed out after allocation by the PagingAreaManager

#if DEBUG_PM
    printf("[PAGINGMANAGER] Create new page table for index %d\n", idx);
#endif

    // create the table in the page directory
    dir->createTable(idx, (uint32_t) physAddress, (uint32_t) virtAddress);
    return 0;
}

uint32_t SystemManagement::unmap(uint32_t virtAddress){
	// request the pagedirectory to unmap the page
    uint32_t physAddress = currentAddressSpace->getPageDirectory()->unmap(virtAddress);
    if(!physAddress){
#if DEBUG_PM
        printf("[PAGINGMAMNAGER] WARN: Page was not present\n");
#endif
        return 0;
    }

    pageFrameAllocator->free((void*)(physAddress));

#if DEBUG_PM
    printf("[PAGINGMANAGER] Unmap page with virtual address %x\n", virtAddress);
#endif
    // invalidate entry in TLB
    asm volatile("push %%edx;"
        "movl %0,%%edx;"
        "invlpg (%%edx);" 
        "pop %%edx;"  : : "r"(virtAddress));

    return physAddress;
}

uint32_t SystemManagement::unmap(uint32_t virtStartAddress, uint32_t virtEndAddress) {
    // remark: if given addresses are not aligned on pages, we do not want to unmap 
    // data that could be on the same page before startVirtAddress or behind endVirtAddress

    // get aligned start and end address of the area to be freed
    uint32_t startVAddr = virtStartAddress & 0xFFFFF000;
    startVAddr += ((virtStartAddress % PAGESIZE != 0) ? PAGESIZE : 0);
    // calc start address of the last page we want to unmap
    uint32_t endVAddr = virtEndAddress & 0xFFFFF000;
    endVAddr -= (((virtEndAddress + 1) % PAGESIZE != 0) ? PAGESIZE : 0);

    // check if an unmap is possible (the start and end address have to contain
    // at least one complete page)
    if(endVAddr < virtStartAddress) {
        return 0;
    }
    // amount of pages to be unmapped
    uint32_t pageCnt = (endVAddr - startVAddr) / PAGESIZE + 1;

#if DEBUG_PM
    printf("[PAGINGMANAGER] Unmap range [%x, %x] #page %d\n", startVAddr, endVAddr, pageCnt);
#endif

    // loop through the pages and unmap them
    uint32_t ret = 0;
    uint8_t cnt = 0;
    uint32_t i;
    for(i=0; i < pageCnt; i++) {
        ret = unmap(startVAddr + i*PAGESIZE);

        if(!ret) {
            cnt++;
        } else {
            cnt = 0;
        }
        // if there were three pages after each other already unmapped, we break here
        // this is sort of a workaround because by merging large free memory blocks in memory management
        // it might happen that some parts of the memory are already unmapped
        if(cnt == 3) {
            break;
        }
    }


    return ret;
}

void* SystemManagement::mapIO(uint32_t physAddress, uint32_t size) {
    // get amount of needed pages
    uint32_t pageCnt = size / PAGESIZE;
    pageCnt += (size % PAGESIZE == 0) ? 0 : 1;

    // allocate 4kb-aligned virtual IO-memory
    void *virtStartAddress = ioMemManager->alloc(size);

    // Check for nullpointer
    if(virtStartAddress == nullptr) {
        Cpu::throwException(Cpu::Exception::OUT_OF_MEMORY);
    }

    // map the allocated virtual IO memory to physical addresses
    for(uint32_t i = 0; i < pageCnt; i++) {
        // since the virtual memory is one block, we can update the virtual address this way
        uint32_t virtAddress = (uint32_t) virtStartAddress + i * PAGESIZE;

        // if the virtual address is already mapped, we have to unmap it
        // this can happen because the headers of the free list are mapped
        // to arbitrary physical addresses, but the IO Memory should be mapped
        // to given physical addresses
        SystemManagement::getInstance().unmap(virtAddress);
        // map the page to given physical address

        map(virtAddress, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_NO_CACHING , physAddress + i * PAGESIZE);
    }

    return virtStartAddress;
}

void* SystemManagement::mapIO(uint32_t size) {
    // get amount of needed pages
    uint32_t pageCnt = size / PAGESIZE;
    pageCnt += (size % PAGESIZE == 0) ? 0 : 1;

    // allocate block of physical memory
    void *physStartAddress = pageFrameAllocator->alloc(size);

    // allocate 4kb-aligned virtual IO-memory
	void *virtStartAddress = ioMemManager->alloc(size);

	// check for nullpointer
    if(virtStartAddress ==  nullptr){
        Cpu::throwException(Cpu::Exception::OUT_OF_MEMORY);
    }

    // map the allocated virtual IO memory to physical addresses
    for(uint32_t i = 0; i < pageCnt; i++) {
        // since the virtual memory is one block, we can update the virtual address this way
        uint32_t virtAddress = (uint32_t) virtStartAddress + i * PAGESIZE;

        // if the virtual address is already mapped, we have to unmap it
        // this can happen because the headers of the free list are mapped
        // to arbitrary physical addresses, but the IO Memory should be mapped
        // to given physical addresses
        SystemManagement::getInstance().unmap(virtAddress);
        // map the page to given physical address

        map(virtAddress, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_NO_CACHING, (uint32_t) physStartAddress + i * PAGESIZE);
    }

    return virtStartAddress;
}

void SystemManagement::freeIO(void *ptr) {
    ioMemManager->free(ptr);
}

void* SystemManagement::getPhysicalAddress(void *virtAddress) {
    return currentAddressSpace->getPageDirectory()->getPhysicalAddress(virtAddress);
}

/**
 * Checks if the system management is fully initialized.
 */
bool SystemManagement::isInitialized() {
    return initialized;
}

bool SystemManagement::isKernelMode() {
	return kernelMode;
}

uint32_t SystemManagement::getFaultingAddress() {
    return faultedAddress;
}

void SystemManagement::calcTotalPhysicalMemory() {

    Util::Array<Multiboot::MemoryMapEntry> memoryMap = Multiboot::Structure::getMemoryMap();
    Multiboot::MemoryMapEntry &maxEntry = memoryMap[0];
    for (const auto &entry : memoryMap) {
        if (entry.type != Multiboot::MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        if (entry.length > maxEntry.length) {
            maxEntry = entry;
        }
    }

    if (maxEntry.type != Multiboot::MULTIBOOT_MEMORY_AVAILABLE) {
        Cpu::throwException(Cpu::Exception::ILLEGAL_STATE, "No usable memory found!");
    }

    totalPhysMemory = static_cast<uint32_t>(maxEntry.length);

    // if there is more than 3,75GB memory apply a cap
    if(totalPhysMemory > PHYS_MEM_CAP) {
        totalPhysMemory = PHYS_MEM_CAP;
    }

    // We need at least 10MB physical memory to run properly
    if(totalPhysMemory < 10 * 1024 * 1024){
        printf("[MEMORYMANAGEMENT] Kernel Panic: not enough RAM!");
        Cpu::halt();
    }

    printf("[SYSTEMMANAGEMENT] Total Physical Memory: %d MB!", totalPhysMemory/(1024*1024));
}

VirtualAddressSpace* SystemManagement::createAddressSpace() {
	VirtualAddressSpace *addressSpace = new VirtualAddressSpace(basePageDirectory);
	// add to the list of address spaces
	addressSpaces->add(addressSpace);

	return addressSpace;
}

void SystemManagement::switchAddressSpace(VirtualAddressSpace *addressSpace) {
	// set current address space
	this->currentAddressSpace = addressSpace;
	// load cr3-register with phys. address of Page Directory
	load_page_directory(addressSpace->getPageDirectory()->getPageDirectoryPhysicalAddress());

	if(!this->currentAddressSpace->isInitialized()) {
	    this->currentAddressSpace->init();
	}
}

void SystemManagement::removeAddressSpace(VirtualAddressSpace *addressSpace){
	// the current active address space cannot be removed
	if(currentAddressSpace == addressSpace){
		return;
	}
	// remove from list
	addressSpaces->remove(addressSpace);
	// call destructor
	delete addressSpace;
}

void* SystemManagement::allocPageTable() {
	return pagingAreaManager->alloc(PAGESIZE);
}

void SystemManagement::freePageTable(void *virtTableAddress) {
    void *physAddress = getPhysicalAddress(virtTableAddress);
	// free virtual memory
	pagingAreaManager->free(virtTableAddress);
	// free physical memory
	pageFrameAllocator->free(physAddress);
}

void SystemManagement::setFaultParams(uint32_t faultAddress, uint32_t flags) {
	faultedAddress = faultAddress;
	faultFlags = flags;
}

SystemManagement& SystemManagement::getInstance() noexcept {
	if(systemManagement == nullptr) {
		// create a static memory manager for the kernel heap
        static FreeListMemoryManager heapMemoryManager;
        heapMemoryManager.init(PHYS2VIRT(Multiboot::Structure::physReservedMemoryEnd), VIRT_KERNEL_HEAP_END, true);
        // set the kernel heap memory manager to this manager
		kernelMemoryManager = &heapMemoryManager;
        // use the new memory manager to alloc memory for the instance of SystemManegement
		systemManagement = new SystemManagement();
	}

	return *systemManagement;
}


void SystemManagement::writeProtectKernelCode() {
    basePageDirectory->writeProtectKernelCode();
}

void *SystemManagement::realloc(void *ptr, uint32_t size, uint32_t alignment) {
    if(!SystemManagement::isKernelMode()){
        return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->realloc(ptr, size, alignment);
    } else {
        return SystemManagement::getKernelHeapManager()->realloc(ptr, size, alignment);
    }
}

void* operator new ( uint32_t size ) {
	if(!SystemManagement::isKernelMode()){
		return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->alloc(size);
	} else {
		return SystemManagement::getKernelHeapManager()->alloc(size);
	}
}

void* operator new[]( uint32_t count ) {
	if(!SystemManagement::isKernelMode()){
		return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->alloc(count);
	} else {
		return SystemManagement::getKernelHeapManager()->alloc(count);
	}
}

void operator delete ( void* ptr )  {
	if(!SystemManagement::isKernelMode()){
		return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->free(ptr);
	} else {
		return SystemManagement::getKernelHeapManager()->free(ptr);
	}
}

void operator delete[] ( void* ptr ) {
	if(!SystemManagement::isKernelMode()){
		return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->free(ptr);
	} else {
		return SystemManagement::getKernelHeapManager()->free(ptr);
	}
}

// Placement new
void *operator new(size_t, void *p) { return p; }
void *operator new[](size_t, void *p) { return p; }
void  operator delete  (void *, void *) { };
void  operator delete[](void *, void *) { };

void* operator new(size_t size, uint32_t alignment) {
    if(!SystemManagement::isKernelMode()){
        return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->alloc(size, alignment);
    } else {
        return SystemManagement::getKernelHeapManager()->alloc(size, alignment);
    }
}

void* operator new[](size_t size, uint32_t alignment) {
    if(!SystemManagement::isKernelMode()){
        return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->alloc(size, alignment);
    } else {
        return SystemManagement::getKernelHeapManager()->alloc(size, alignment);
    }
}

void operator delete(void *ptr, uint32_t alignment) {
    if(!SystemManagement::isKernelMode()){
        return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->free(ptr, alignment);
    } else {
        return SystemManagement::getKernelHeapManager()->free(ptr);
    }
}

void operator delete[](void *ptr, uint32_t alignment) {
    if(!SystemManagement::isKernelMode()){
        return SystemManagement::getInstance().getCurrentUserSpaceHeapManager()->free(ptr, alignment);
    } else {
        return SystemManagement::getKernelHeapManager()->free(ptr);
    }
}
