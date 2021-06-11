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
#include "PagingAreaManager.h"
#include "kernel/memory/Paging.h"

namespace Kernel {

PagingAreaManager::PagingAreaManager() : BitmapMemoryManager(VIRT_PAGE_MEM_START, VIRT_PAGE_MEM_END, PAGESIZE, true) {
    // We use already 256 Page Tables for Kernel mappings and one Page Directory as the Kernel´s PD
    setRange(0, 8 * 32 + 2);
    refillPool();
}

void PagingAreaManager::onError() {
    Util::Exception::throwException(Util::Exception::OUT_OF_PAGE_MEMORY);
}

void *PagingAreaManager::alloc() {
    return BitmapMemoryManager::alloc();
}

void PagingAreaManager::refillPool() {

}

}