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

#ifndef HHUOS_TABLEMEMORYMANAGER_H
#define HHUOS_TABLEMEMORYMANAGER_H

#include "BitmapMemoryManager.h"
#include <lib/util/stream/PrintWriter.h>
#include <lib/util/async/Atomic.h>
#include <kernel/log/Logger.h>

namespace Kernel {

class TableMemoryManager {

public:
    /**
     * Constructor.
     */
    TableMemoryManager(BitmapMemoryManager &bitmapMemoryManager, uint32_t startAddress, uint32_t endAddress, uint32_t blockSize = 4096);

    /**
     * Copy constructor.
     */
    TableMemoryManager(const TableMemoryManager &copy) = delete;

    /**
     * Assignment operator.
     */
    TableMemoryManager& operator=(const TableMemoryManager &other) = delete;

    /**
     * Destructor.
     */
    virtual ~TableMemoryManager() = default;

    void reserveMemory(uint32_t reservedStart, uint32_t reservedEnd, uint16_t useCount = 0);

    void* alloc();

    void* alloc(void *address);

    void free(void *pointer);

    void logTables();

private:

    void printAllocationTable(uint32_t referenceTableArrayIndex, uint32_t referenceTableIndex);

    struct ReferenceTableEntry {

    private:
        /**
         * 0-1:    unused
         * 2:      lock
         * 3:      installed
         * 4-31:   address
         */
        uint32_t value;

    public:

        Util::Async::Atomic<uint32_t> getAtomicWrapper() {
            return Util::Async::Atomic<uint32_t>(value);
        }

        void setAddress(uint32_t address) {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            uint32_t oldValue;
            uint32_t exchangeValue;

            do {
                oldValue = valueWrapper.get();
                exchangeValue = (oldValue & 0x0000000f) | (address & 0xfffffff0);
            } while (valueWrapper.getAndSet(exchangeValue) != oldValue);
        }

        bool setAndCompareAddress(uint32_t oldAddress, uint32_t newAddress) {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            uint32_t oldValue = valueWrapper.get();
            uint32_t exchangeValue = (oldValue & 0x0000000f) | (newAddress & 0xfffffff0);

            return valueWrapper.compareAndSet(oldValue, exchangeValue);
        }

        void setInstalled(bool installed) {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            if (installed) {
                valueWrapper.bitSet(3);
            } else {
                valueWrapper.bitReset(3);
            }
        }

        void acquireLock() {
            while (!tryAcquireLock());
        }

        bool tryAcquireLock() {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            return !valueWrapper.bitTestAndSet(2);
        }

        void releaseLock() {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            valueWrapper.bitReset(2);
        }

        [[nodiscard]] uint32_t getAddress() {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            return valueWrapper.get() & 0xfffffff0;
        }

        [[nodiscard]] bool isInstalled() {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            return valueWrapper.bitTest(3);
        }

        [[nodiscard]] bool isLocked() {
            auto valueWrapper = Util::Async::Atomic<uint32_t>(value);
            return valueWrapper.bitTest(2);
        }
    };

    struct AllocationTableEntry {

    private:
        /**
         * 0:       reserved
         * 1-15:    useCount
         */
        uint16_t value;

    public:

        void setReserved(bool reserved) {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            if (reserved) {
                valueWrapper.bitSet(0);
            } else {
                valueWrapper.bitReset(0);
            }
        }

        void setUseCount(uint16_t useCount) {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            uint16_t oldValue;
            uint16_t exchangeValue;

            do {
                oldValue = valueWrapper.get();
                exchangeValue = (oldValue & 0x0001) | (useCount << 1);
            } while (valueWrapper.getAndSet(exchangeValue) != oldValue);
        }

        void incrementUseCount() {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            uint16_t oldValue = valueWrapper.fetchAndAdd(0b0000000000000010);
            if (static_cast<uint16_t>(oldValue + 2) <= oldValue) {
                Util::Exception::throwException(Util::Exception::Error::PAGING_ERROR, "Page frame has been mapped too often!");
            }
        }

        void decrementUseCount() {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            uint16_t oldValue = valueWrapper.fetchAndSub(0b0000000000000010);
            if (static_cast<uint16_t>(oldValue - 2) >= oldValue) {
                Util::Exception::throwException(Util::Exception::Error::PAGING_ERROR, "Page frame underflow!");
            }
        }

        [[nodiscard]] uint16_t getUseCount() {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            return valueWrapper.get() >> 1;
        }

        [[nodiscard]] bool isReserved() {
            auto valueWrapper = Util::Async::Atomic<uint16_t>(value);
            return valueWrapper.bitTest(0);
        }
    };

    struct TableIndex {
        uint32_t referenceTableArrayIndex;
        uint32_t referenceTableIndex;
        uint32_t allocationTableIndex;
    };

    [[nodiscard]] TableIndex calculateIndex(uint32_t address) const;

    [[nodiscard]] uint32_t calculateAddress(const TableIndex &index) const;

private:

    BitmapMemoryManager &bitmapMemoryManager;
    uint32_t startAddress;
    uint32_t endAddress;
    uint32_t blockSize;

    uint32_t referenceTableSizeInBlocks;
    uint32_t allocationTableEntriesPerBlock;
    uint32_t managedMemoryPerAllocationTable;
    uint32_t referenceTableEntriesPerBlock;
    uint32_t allocationTableCount;

    ReferenceTableEntry **referenceTableArray;

    static Logger log;
    static const constexpr uint32_t MIN_BITMAP_BLOCK_SIZE = 16;
};

}

#endif
