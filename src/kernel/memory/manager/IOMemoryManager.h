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

#ifndef __IOMEMMANAGER_H__
#define __IOMEMMANAGER_H__

#include <cstdint>
#include "lib/util/data/HashMap.h"
#include "lib/util/async/Spinlock.h"
#include "BitmapMemoryManager.h"

namespace Kernel {

/**
 * Memory manager, that manages IO-Space for HW-Buffers, DMA etc.
 * Maps a given physical address (for example LFB) into the virtual IO-space
 * as defined in MemLayout.h or allocates an 4KB-aligned physical buffer and
 * maps it into virtual IO-space.
 * It is based on the BitmapMemoryManager.
 *
 * @author Burak Akguel, Christian Gesse, Fabian Ruhland, Filip Krakowski, Michael Schoettner
 * @date 2018
 */
class IOMemoryManager : public BitmapMemoryManager {

public:
    /**
     * Constructor.
     */
    IOMemoryManager();

    /**
     * Copy constructor.
     */
    IOMemoryManager(const IOMemoryManager &copy) = delete;

    /**
     * Assignment operator.
     */
    IOMemoryManager& operator=(const IOMemoryManager &other) = delete;

    /**
     * Destructor.
     */
    ~IOMemoryManager() override = default;

    /**
     * Overriding function from MemoryManager.
     */
    void *alloc() override;

    /**
     * Overriding function from MemoryManager.
     */
    void free(void *ptr) override;

private:
    Util::Data::HashMap<void*, uint32_t> ioMemoryMap;
    Util::Async::Spinlock lock;

};

}

#endif
