/*
 * Copyright (C) 2018-2021 Heinrich-Heine-Universitaet Duesseldorf,
 * Institute of Computer Science, Department Operating Systems
 * Burak Akguel, Christian Gesse, Fabian Ruhland, Filip Krakowski, Michael Schoettner
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

#include <lib/util/memory/Address.h>
#include <asm_interface.h>
#include <device/port/serial/SerialPort.h>
#include <device/cpu/Cpu.h>
#include "Management.h"

#include "kernel/interrupt/InterruptDispatcher.h"
#include "kernel/multiboot/Structure.h"
#include "kernel/memory/Paging.h"
#include "kernel/memory/MemLayout.h"
#include "System.h"

namespace Kernel {

// initialize static members
TaskStateSegment Management::taskStateSegment{};
Management *Management::systemManagement = nullptr;
HeapMemoryManager *Management::kernelMemoryManager = nullptr;
bool Management::initialized = false;
bool Management::kernelMode = true;

/**
 * Is called from assembler code before calling the main function, because it sets up
 * everything to get the system run.
 */
void Management::initializeSystem(Multiboot::Info *multibootInfoAddress) {
    Multiboot::Structure::init(multibootInfoAddress);

    // create an instance of the SystemManagement and initialize it (sets up paging and system management)
    auto &management = Management::getInstance();
    management.init();
    Device::Cpu::enableInterrupts();

    // Parse multiboot structure
    Multiboot::Structure::parse();

    // Protect kernel code
    management.writeProtectKernelCode();
}

/**
 * Sets up the GDT for the system and a special GDT for BIOS-calls.
 * Only these two GDTs are needed, because memory protection and abstractions is done via paging.
 * The memory where the parameters point to is reserved in assembler code before paging is enabled.
 * Therefore we assume that the given pointers are physical addresses  - this is very important
 * to guarantee correct GDT descriptors using this setup-function.
 *
 * @param systemGdt Pointer to the GDT of the system
 * @param biosGdt Pointer to the GDT for BIOS-calls
 * @param systemGdtDescriptor Pointer to the descriptor of GDT; this descriptor should contain the virtual address of GDT
 * @param biosGdtDescriptor Pointer to the descriptor of BIOS-GDT; this descriptor should contain the physical address of BIOS-GDT
 * @param PhysicalGdtDescriptor Pointer to the descriptor of GDT; this descriptor should contain the physical address of GDT
 */
void Management::initializeGlobalDescriptorTables(uint16_t *systemGdt, uint16_t *biosGdt, uint16_t *systemGdtDescriptor, uint16_t *biosGdtDescriptor, uint16_t *PhysicalGdtDescriptor) {
    // Set first 6 GDT entries to 0
    Util::Memory::Address<uint32_t>(systemGdt).setRange(0, 48);

    // Set first 4 bios GDT entries to 0
    Util::Memory::Address<uint32_t>(biosGdt).setRange(0, 32);

    // first set up general GDT for the system
    // first entry has to be null
    Management::createGDTEntry(systemGdt, 0, 0, 0, 0, 0);
    // kernel code segment
    Management::createGDTEntry(systemGdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xC);
    // kernel data segment
    Management::createGDTEntry(systemGdt, 2, 0, 0xFFFFFFFF, 0x92, 0xC);
    // user code segment
    Management::createGDTEntry(systemGdt, 3, 0, 0xFFFFFFFF, 0xFA, 0xC);
    // user data segment
    Management::createGDTEntry(systemGdt, 4, 0, 0xFFFFFFFF, 0xF2, 0xC);
    // tss segment
    Management::createGDTEntry(systemGdt, 5, reinterpret_cast<uint32_t>(&Management::getTaskStateSegment()), sizeof(Kernel::TaskStateSegment), 0x89, 0x4);

    // set up descriptor for GDT
    *((uint16_t *) systemGdtDescriptor) = 6 * 8;
    // the normal descriptor should contain the virtual address of GDT
    *((uint32_t *) (systemGdtDescriptor + 1)) = (uint32_t) systemGdt + KERNEL_START;

    // set up descriptor for GDT with phys. address - needed for bootstrapping
    *((uint16_t *) PhysicalGdtDescriptor) = 6 * 8;
    // this descriptor should contain the physical address of GDT
    *((uint32_t *) (PhysicalGdtDescriptor + 1)) = (uint32_t) systemGdt;

    // now set up GDT for BIOS-calls (notice that no userspace entries are necessary here)
    // first entry has to be null
    Management::createGDTEntry(biosGdt, 0, 0, 0, 0, 0);
    // kernel code segment
    Management::createGDTEntry(biosGdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xC);
    // kernel data segment
    Management::createGDTEntry(biosGdt, 2, 0, 0xFFFFFFFF, 0x92, 0xC);
    // prepared BIOS-call segment (contains 16-bit code etc...)
    Management::createGDTEntry(biosGdt, 3, 0x4000, 0xFFFFFFFF, 0x9A, 0x8);


    // set up descriptor for BIOS-GDT
    *((uint16_t *) biosGdtDescriptor) = 4 * 8;
    // the descriptor should contain physical address of BIOS-GDT because paging is not enabled during BIOS-calls
    *((uint32_t *) (biosGdtDescriptor + 1)) = (uint32_t) biosGdt;
}

/**
 * Creates an entry into a given GDT (Global Descriptor Table).
 * Memory for the GDT must be allocated before.
 */
void Management::createGDTEntry(uint16_t *gdt, uint16_t num, uint32_t base, uint32_t limit, uint8_t access,
                                      uint8_t flags) {
    // each GDT-entry consists of 4 16-bit unsigned integers
    // calculate index into 16bit-array that represents GDT
    uint16_t idx = 4 * num;

    // first 16-bit value: [Limit 0:15]
    gdt[idx] = (uint16_t) (limit & 0xFFFF);
    // second 16-bit value: [Base 0:15]
    gdt[idx + 1] = (uint16_t) (base & 0xFFFF);
    // third 16-bit value: [Access Byte][Base 16:23]
    gdt[idx + 2] = (uint16_t) ((base >> 16) & 0xFF) | (access << 8);
    // fourth 16-bit value: [Base 24:31][Flags][Limit 16:19]
    gdt[idx + 3] = (uint16_t) ((limit >> 16) & 0x0F) | ((flags << 4) & 0xF0) | ((base >> 16) & 0xFF00);
    // end of GDT-entry
}

void Management::plugin() {
    InterruptDispatcher::getInstance().assign(InterruptDispatcher::PAGEFAULT, *this);
}


void Management::trigger(InterruptFrame &frame) {

    // GdbServer::memError = true;

    // Get page fault address and flags
    uint32_t faultAddress = 0;
    // the faulted linear address is loaded in the cr2 register
    asm ("mov %%cr2, %0" : "=r" (faultAddress));

    // There should be no access to the first page (address 0)
    if (faultAddress == 0) {
        frame.interrupt = (uint32_t) Util::Exception::NULL_POINTER;
        System::panic(&frame);
    }

    // check if pagefault was caused by illegal page access
    if ((frame.error & 0x00000001u) > 0) {
        Util::Exception::throwException(Util::Exception::ILLEGAL_PAGE_ACCESS);
    }

    // Map the faulted Page
    map(faultAddress, PAGE_PRESENT | PAGE_READ_WRITE);
    // TODO: Check other Faults
}

void Management::init() {
    // Physical Page Frame Allocator is initialized to be possible to allocate
    // physical memory (page frames)
    calcTotalPhysicalMemory();
    pageFrameAllocator = new PageFrameAllocator(0, totalPhysMemory - 1);

    // to be able to map new pages, a bootstrap address space is created.
    // It uses only the basePageDirectory with mapping for kernel space
    currentAddressSpace = new VirtualAddressSpace(nullptr);

    // Init Paging Area Manager -> Manages the virtual addresses of all page tables
    // and directories
    pagingAreaManager = new PagingAreaManager();
    // create a Base Page Directory (used to map the kernel into every process)
    basePageDirectory = currentAddressSpace->getPageDirectory();

    // register Paging Manager to handle Page Faults
    this->plugin();

    // init io-memory afterwards, because pagefault will occur setting up the first list header
    // Init the manager for virtual IO Memory
    ioMemManager = new IOMemoryManager();

    currentAddressSpace->init();
    switchAddressSpace(currentAddressSpace);

    // add first address space to list with all address spaces
    addressSpaces = new Util::Data::ArrayList<VirtualAddressSpace*>;
    addressSpaces->add(currentAddressSpace);

    // Initialize global objects afterwards, because now missing pages can be mapped
    _init();

    // the memory management system is fully initialized now
    initialized = true;
}

void Management::map(uint32_t virtAddress, uint16_t flags) {
    // allocate a physical page frame where the page should be mapped
    uint32_t physAddress = (uint32_t) pageFrameAllocator->alloc();
    // map the page into the directory
    currentAddressSpace->getPageDirectory()->map(physAddress, virtAddress, flags);
}

void Management::map(uint32_t virtAddress, uint16_t flags, uint32_t physAddress) {
    // map the page into the directory
    currentAddressSpace->getPageDirectory()->map(physAddress, virtAddress, flags);
}

/**
 * Range map function to map a range of virtual addresses into the current Page 
 * Directory .
 */
void Management::map(uint32_t virtStartAddress, uint32_t virtEndAddress, uint16_t flags) {
    // get 4KB-aligned start and end address
    uint32_t alignedStartAddress = virtStartAddress & 0xFFFFF000;
    uint32_t alignedEndAddress = virtEndAddress & 0xFFFFF000;
    alignedEndAddress += (virtEndAddress % PAGESIZE == 0) ? 0 : PAGESIZE;
    // map all pages
    for (uint32_t i = alignedStartAddress; i < alignedEndAddress; i += PAGESIZE) {
        map(i, flags);
    }
}

uint32_t Management::createPageTable(PageDirectory *dir, uint32_t idx) {
    // get some virtual memory for the table
    void *virtAddress = pagingAreaManager->alloc();
    // get physical memory for the table
    void *physAddress = getPhysicalAddress(virtAddress);
    // there must be no mapping from virtual to physical address be done here,
    // because the page is zeroed out after allocation by the PagingAreaManager

    // create the table in the page directory
    dir->createTable(idx, (uint32_t) physAddress, (uint32_t) virtAddress);
    return 0;
}

uint32_t Management::unmap(uint32_t virtAddress) {
    // request the pagedirectory to unmap the page
    uint32_t physAddress = currentAddressSpace->getPageDirectory()->unmap(virtAddress);
    if (!physAddress) {
        return 0;
    }

    pageFrameAllocator->free((void *) (physAddress));

    // invalidate entry in TLB
    asm volatile("push %%edx;"
                 "movl %0,%%edx;"
                 "invlpg (%%edx);"
                 "pop %%edx;"  : : "r"(virtAddress));

    return physAddress;
}

uint32_t Management::unmap(uint32_t virtStartAddress, uint32_t virtEndAddress) {
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
    if (endVAddr < virtStartAddress) {
        return 0;
    }
    // amount of pages to be unmapped
    uint32_t pageCnt = (endVAddr - startVAddr) / PAGESIZE + 1;

    // loop through the pages and unmap them
    uint32_t ret = 0;
    uint8_t cnt = 0;
    uint32_t i;
    for (i = 0; i < pageCnt; i++) {
        ret = unmap(startVAddr + i * PAGESIZE);

        if (!ret) {
            cnt++;
        } else {
            cnt = 0;
        }
        // if there were three pages after each other already unmapped, we break here
        // this is sort of a workaround because by merging large free memory blocks in memory management
        // it might happen that some parts of the memory are already unmapped
        if (cnt == 3) {
            break;
        }
    }


    return ret;
}

void *Management::mapIO(uint32_t physAddress, uint32_t size) {
    // TODO: Get rid of old IOMemoryManager
    // get amount of needed pages
    uint32_t pageCnt = size / PAGESIZE;
    pageCnt += (size % PAGESIZE == 0) ? 0 : 1;

    // allocate 4KB-aligned virtual IO-memory
    void *virtStartAddress = ioMemManager->alloc();

    // Check for nullpointer
    if (virtStartAddress == nullptr) {
        Util::Exception::throwException(Util::Exception::OUT_OF_MEMORY);
    }

    // map the allocated virtual IO memory to physical addresses
    for (uint32_t i = 0; i < pageCnt; i++) {
        // since the virtual memory is one block, we can update the virtual address this way
        uint32_t virtAddress = (uint32_t) virtStartAddress + i * PAGESIZE;

        // if the virtual address is already mapped, we have to unmap it
        // this can happen because the headers of the free list are mapped
        // to arbitrary physical addresses, but the IO Memory should be mapped
        // to given physical addresses
        Management::getInstance().unmap(virtAddress);
        // map the page to given physical address

        map(virtAddress, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_NO_CACHING, physAddress + i * PAGESIZE);
    }

    return virtStartAddress;
}

void *Management::mapIO(uint32_t size) {
    // get amount of needed pages
    uint32_t pageCnt = size / PAGESIZE;
    pageCnt += (size % PAGESIZE == 0) ? 0 : 1;

    // allocate block of physical memory
    void *physStartAddress = pageFrameAllocator->alloc();

    // allocate 4KB-aligned virtual IO-memory
    void *virtStartAddress = ioMemManager->alloc();

    // check for nullpointer
    if (virtStartAddress == nullptr) {
        Util::Exception::throwException(Util::Exception::OUT_OF_MEMORY);
    }

    // map the allocated virtual IO memory to physical addresses
    for (uint32_t i = 0; i < pageCnt; i++) {
        // since the virtual memory is one block, we can update the virtual address this way
        uint32_t virtAddress = (uint32_t) virtStartAddress + i * PAGESIZE;

        // if the virtual address is already mapped, we have to unmap it
        // this can happen because the headers of the free list are mapped
        // to arbitrary physical addresses, but the IO Memory should be mapped
        // to given physical addresses
        Management::getInstance().unmap(virtAddress);
        // map the page to given physical address

        map(virtAddress, PAGE_PRESENT | PAGE_READ_WRITE | PAGE_NO_CACHING, (uint32_t) physStartAddress + i * PAGESIZE);
    }

    return virtStartAddress;
}

void Management::freeIO(void *ptr) {
    ioMemManager->free(ptr);
}

void *Management::getPhysicalAddress(void *virtAddress) {
    return currentAddressSpace->getPageDirectory()->getPhysicalAddress(virtAddress);
}

/**
 * Checks if the system management is fully initialized.
 */
bool Management::isInitialized() {
    return initialized;
}

bool Management::isKernelMode() {
    return kernelMode;
}

void Management::calcTotalPhysicalMemory() {

    Util::Data::Array<Multiboot::MemoryMapEntry> memoryMap = Multiboot::Structure::getMemoryMap();
    Multiboot::MemoryMapEntry &maxEntry = memoryMap[0];
    for (const auto &entry : memoryMap) {
        if (entry.type != Multiboot::MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        if (entry.address + entry.length > maxEntry.address + maxEntry.length) {
            maxEntry = entry;
        }
    }

    if (maxEntry.type != Multiboot::MULTIBOOT_MEMORY_AVAILABLE) {
        Util::Exception::throwException(Util::Exception::ILLEGAL_STATE, "No usable memory found!");
    }

    totalPhysMemory = static_cast<uint32_t>(maxEntry.address + maxEntry.length);

    // if there is more than 3,75GB memory apply a cap
    /*if (totalPhysMemory > PHYS_MEM_CAP) {
        totalPhysMemory = PHYS_MEM_CAP;
    }*/

    // We need at least 10MB physical memory to run properly
    if (totalPhysMemory < 10 * 1024 * 1024) {
        Device::Cpu::halt();
    }
}

VirtualAddressSpace *Management::createAddressSpace(uint32_t managerOffset, const Util::Memory::String &managerType) {
    auto addressSpace = new VirtualAddressSpace(basePageDirectory, managerOffset, managerType);
    // add to the list of address spaces
    addressSpaces->add(addressSpace);

    return addressSpace;
}

void Management::switchAddressSpace(VirtualAddressSpace *addressSpace) {
    // set current address space
    this->currentAddressSpace = addressSpace;
    // load cr3-register with phys. address of Page Directory
    load_page_directory(addressSpace->getPageDirectory()->getPageDirectoryPhysicalAddress());

    if (!this->currentAddressSpace->isInitialized()) {
        this->currentAddressSpace->init();
    }
}

void Management::removeAddressSpace(VirtualAddressSpace *addressSpace) {
    // the current active address space cannot be removed
    if (currentAddressSpace == addressSpace) {
        return;
    }
    // remove from list
    addressSpaces->remove(addressSpace);
    // call destructor
    delete addressSpace;
}

void *Management::allocPageTable() {
    return pagingAreaManager->alloc();
}

void Management::freePageTable(void *virtTableAddress) {
    void *physAddress = getPhysicalAddress(virtTableAddress);
    // free virtual memory
    pagingAreaManager->free(virtTableAddress);
    // free physical memory
    pageFrameAllocator->free(physAddress);
}

Management &Management::getInstance() noexcept {
    if (systemManagement == nullptr) {
        // create a static memory manager for the kernel heap
        for (uint32_t i = 0; Multiboot::Structure::blockMap[i].blockCount != 0; i++) {
            const auto &block = Multiboot::Structure::blockMap[i];

            if (block.type == Multiboot::Structure::HEAP_RESERVED) {
                static FreeListMemoryManager heapMemoryManager;
                heapMemoryManager.initialize(block.virtualStartAddress, VIRT_KERNEL_HEAP_END);
                // set the kernel heap memory manager to this manager
                kernelMemoryManager = &heapMemoryManager;
                // use the new memory manager to alloc memory for the instance of SystemManegement
                systemManagement = new Management();

                break;
            }
        }
    }

    return *systemManagement;
}


void Management::writeProtectKernelCode() {
    basePageDirectory->writeProtectKernelCode();
}

}