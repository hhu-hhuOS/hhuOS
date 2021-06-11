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

#include "kernel/memory/MemLayout.h"
#include "IOMemoryManager.h"
#include "kernel/core/Management.h"
#include "kernel/memory/Paging.h"

namespace Kernel {

IOMemoryManager::IOMemoryManager() : BitmapMemoryManager(VIRT_IO_START, VIRT_IO_END, PAGESIZE, false) {}

void *IOMemoryManager::alloc() {

    lock.acquire();

    void *ret = BitmapMemoryManager::alloc();

    // TODO: Get rid of old IOMemoryManager
    /*if (ret != nullptr) {
        uint32_t pageCount = (size / PAGESIZE) + ((size % PAGESIZE == 0) ? 0 : 1);
        ioMemoryMap.put(ret, pageCount);
    }*/

    lock.release();

    return ret;
}

void IOMemoryManager::free(void *ptr) {

    auto virtualAddress = (uint32_t) ptr;

    if (virtualAddress < getStartAddress() || virtualAddress >= getEndAddress()) {
        return;
    }

    Management &systemManagement = Management::getInstance();

    uint32_t pageCount = ioMemoryMap.get(ptr);

    lock.acquire();

    for (uint32_t i = 0; i < pageCount; i++) {
        BitmapMemoryManager::free((void *) (virtualAddress + i * PAGESIZE));
        systemManagement.unmap(virtualAddress + i * PAGESIZE);
    }

    ioMemoryMap.remove(ptr);

    lock.release();
}

}