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

#include "TableMemoryManager.h"

namespace Kernel {

Logger TableMemoryManager::log = Logger::get("TableMemoryManager");

TableMemoryManager::TableMemoryManager(BitmapMemoryManager &bitmapMemoryManager, uint32_t startAddress, uint32_t endAddress, uint32_t blockSize) :
        bitmapMemoryManager(bitmapMemoryManager), startAddress(startAddress), endAddress(endAddress), blockSize(blockSize) {
    uint32_t memorySize = endAddress - startAddress + 1;
    uint32_t blockCount = memorySize / blockSize;
    if (memorySize % blockCount != 0) {
        blockCount++;
    }

    if (bitmapMemoryManager.getBlockSize() < MIN_BITMAP_BLOCK_SIZE) {
        Util::Exception::throwException(Util::Exception::INVALID_ARGUMENT, "Bitmap block size is too small!");
    }

    allocationTableCount = (blockCount * sizeof(AllocationTableEntry)) / bitmapMemoryManager.getBlockSize();
    if ((blockCount * sizeof(AllocationTableEntry)) % bitmapMemoryManager.getBlockSize() != 0) {
        allocationTableCount++;
    }

    referenceTableSizeInBlocks = allocationTableCount / (bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry));
    if (allocationTableCount % (bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry)) != 0) {
        referenceTableSizeInBlocks++;
    }

    allocationTableEntriesPerBlock = bitmapMemoryManager.getBlockSize() / sizeof(AllocationTableEntry);
    referenceTableEntriesPerBlock = bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry);
    managedMemoryPerAllocationTable = allocationTableEntriesPerBlock * blockSize;

    log.debug("startAddress: [%u]", startAddress);
    log.debug("endAddress: [%u]", endAddress);
    log.debug("blockSize: [%u]", blockSize);
    log.debug("memorySize: [%u]", memorySize);
    log.debug("blockCount: [%u]", blockCount);
    log.debug("bitmapBlockSize: [%u]", bitmapMemoryManager.getBlockSize());
    log.debug("allocationTableCount: [%u]", allocationTableCount);
    log.debug("allocationTableEntriesPerBlock: [%u]", allocationTableEntriesPerBlock);
    log.debug("managedMemoryPerAllocationTable: [%u]", managedMemoryPerAllocationTable);
    log.debug("referenceTableEntriesPerBlock: [%u]", referenceTableEntriesPerBlock);
    log.debug("referenceTableSizeInBlocks: [%u]", referenceTableSizeInBlocks);

    referenceTableArray = new ReferenceTableEntry*[referenceTableSizeInBlocks];
    for (uint32_t i = 0; i < referenceTableSizeInBlocks; i++) {
        referenceTableArray[i] = static_cast<ReferenceTableEntry*>(bitmapMemoryManager.alloc());
        for (uint32_t j = 0; j < bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry); j++) {
            bool installed = (i * (bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry)) + j) < allocationTableCount;
            auto &entry = referenceTableArray[i][j];
            entry.setAddress(0);
            entry.setInstalled(installed);
        }
    }
}

void TableMemoryManager::logTables() {
    for (uint32_t i = 0; i < referenceTableSizeInBlocks; i++) {
        for (uint32_t j = 0; j < bitmapMemoryManager.getBlockSize() / sizeof(ReferenceTableEntry); j++) {
            auto entry = referenceTableArray[i][j];
            log.debug("Index: [%04u], Allocation Table Address: [%08x], Installed: [%B], Locked: [%B]", i * j + j, entry.getAddress(), entry.isInstalled(), entry.isLocked());
            if (entry.getAddress() > 0) {
                printAllocationTable(i, j);
            }
        }
    }
}

void TableMemoryManager::printAllocationTable(uint32_t referenceTableArrayIndex, uint32_t referenceTableIndex) {
    auto referenceTableEntry = referenceTableArray[referenceTableArrayIndex][referenceTableIndex];
    auto *allocationTable = reinterpret_cast<AllocationTableEntry*>(referenceTableEntry.getAddress());
    for (uint32_t i = 0; i < allocationTableEntriesPerBlock; i++) {
        auto entry = allocationTable[i];
        uint32_t address = referenceTableArrayIndex * referenceTableEntriesPerBlock * managedMemoryPerAllocationTable + referenceTableIndex * managedMemoryPerAllocationTable + i * blockSize;
        log.debug("Index: [%04u], Address: [%08x], Used: [%04u], Reserved: [%B]", i, address, entry.getUseCount(), entry.isReserved());
    }
}

void TableMemoryManager::reserveMemory(uint32_t reservedStart, uint32_t reservedEnd, uint16_t useCount) {
    auto startIndex = calculateIndex(reservedStart);
    auto endIndex = calculateIndex(reservedEnd);

    for (uint32_t i = startIndex.referenceTableArrayIndex; i <= endIndex.referenceTableArrayIndex; i++) {
        auto *referenceTable = reinterpret_cast<ReferenceTableEntry*>(referenceTableArray[i]);
        uint32_t referenceTableStartIndex = i == startIndex.referenceTableArrayIndex ? startIndex.referenceTableIndex : 0;
        uint32_t referenceTableEndIndex = i == endIndex.referenceTableArrayIndex ? endIndex.referenceTableIndex : referenceTableEntriesPerBlock;

        for (uint32_t j = referenceTableStartIndex; j <= referenceTableEndIndex; j++) {
            auto &referenceTableEntry = referenceTable[j];
            referenceTableEntry.acquireLock();

            uint32_t address = referenceTableEntry.getAddress();
            if (address == 0) {
                void *block = bitmapMemoryManager.alloc();
                referenceTableEntry.setAddress(reinterpret_cast<uint32_t>(block));
            }

            auto *allocationTable = reinterpret_cast<AllocationTableEntry*>(referenceTableEntry.getAddress());
            uint32_t allocationTableStartIndex = j == referenceTableStartIndex ? startIndex.allocationTableIndex : 0;
            uint32_t allocationTableEndIndex = j == referenceTableEndIndex ? endIndex.allocationTableIndex : allocationTableEntriesPerBlock;

            for (uint32_t k = allocationTableStartIndex; k <= allocationTableEndIndex; k++) {
                auto &allocationTableEntry = allocationTable[k];
                allocationTableEntry.setReserved(true);
                allocationTableEntry.setUseCount(useCount);
            }

            referenceTableEntry.releaseLock();
        }
    }
}

TableMemoryManager::TableIndex TableMemoryManager::calculateIndex(uint32_t address) const {
    uint32_t referenceTableAddress = (address / blockSize) * blockSize;
    uint32_t referenceTableIndex = referenceTableAddress / managedMemoryPerAllocationTable;

    uint32_t allocationTableAddress = address % managedMemoryPerAllocationTable;
    uint32_t allocationTableIndex = allocationTableAddress / blockSize;

    return { referenceTableIndex / referenceTableEntriesPerBlock, referenceTableIndex % referenceTableEntriesPerBlock, allocationTableIndex };
}

uint32_t TableMemoryManager::calculateAddress(const TableMemoryManager::TableIndex &index) const {
    return index.referenceTableArrayIndex * referenceTableEntriesPerBlock * managedMemoryPerAllocationTable
           + index.referenceTableIndex * managedMemoryPerAllocationTable
           + index.allocationTableIndex * blockSize;
}

void *TableMemoryManager::alloc() {
    auto startIndex = calculateIndex(startAddress);
    auto endIndex = calculateIndex(endAddress);

    for (uint32_t i = startIndex.referenceTableArrayIndex; i <= endIndex.referenceTableArrayIndex; i++) {
        auto *referenceTable = reinterpret_cast<ReferenceTableEntry*>(referenceTableArray[i]);
        uint32_t referenceTableStartIndex = i == startIndex.referenceTableArrayIndex ? startIndex.referenceTableIndex : 0;
        uint32_t referenceTableEndIndex = i == endIndex.referenceTableArrayIndex ? endIndex.referenceTableIndex : referenceTableEntriesPerBlock;

        for (uint32_t j = referenceTableStartIndex; j <= referenceTableEndIndex; j++) {
            auto &referenceTableEntry = referenceTable[j];
            if (!referenceTableEntry.tryAcquireLock()) {
                continue;
            }

            uint32_t address = referenceTableEntry.getAddress();
            if (address == 0) {
                void *block = bitmapMemoryManager.alloc();
                referenceTableEntry.setAddress(reinterpret_cast<uint32_t>(block));
            }

            auto *allocationTable = reinterpret_cast<AllocationTableEntry*>(referenceTableEntry.getAddress());
            uint32_t allocationTableStartIndex = j == referenceTableStartIndex ? startIndex.allocationTableIndex : 0;
            uint32_t allocationTableEndIndex = j == referenceTableEndIndex ? endIndex.allocationTableIndex : allocationTableEntriesPerBlock;

            for (uint32_t k = allocationTableStartIndex; k <= allocationTableEndIndex; k++) {
                auto &allocationTableEntry = allocationTable[k];
                if (allocationTableEntry.isReserved() || allocationTableEntry.getUseCount() > 0) {
                    continue;
                }

                allocationTableEntry.incrementUseCount();
                referenceTableEntry.releaseLock();
                const TableIndex index = {i, j, k};
                return reinterpret_cast<void*>(calculateAddress(index));
            }

            referenceTableEntry.releaseLock();
        }
    }

    Util::Exception::throwException(Util::Exception::ILLEGAL_STATE, "TableMemoryManager: Allocation failed!");
}

void *TableMemoryManager::alloc(void *address) {
    const auto index = calculateIndex(reinterpret_cast<uint32_t>(address));

    auto *referenceTable = reinterpret_cast<ReferenceTableEntry*>(referenceTableArray[index.referenceTableArrayIndex]);
    auto &referenceTableEntry = referenceTable[index.referenceTableIndex];
    auto *allocationTable = reinterpret_cast<AllocationTableEntry*>(referenceTableEntry.getAddress());
    auto &allocationTableEntry = allocationTable[index.allocationTableIndex];

    allocationTableEntry.incrementUseCount();
    return reinterpret_cast<void*>(calculateAddress(index));
}

void TableMemoryManager::free(void *pointer) {
    const auto index = calculateIndex(reinterpret_cast<uint32_t>(pointer));

    auto *referenceTable = reinterpret_cast<ReferenceTableEntry*>(referenceTableArray[index.referenceTableArrayIndex]);
    auto &referenceTableEntry = referenceTable[index.referenceTableIndex];
    auto *allocationTable = reinterpret_cast<AllocationTableEntry*>(referenceTableEntry.getAddress());
    auto &allocationTableEntry = allocationTable[index.allocationTableIndex];

    allocationTableEntry.decrementUseCount();
}

}