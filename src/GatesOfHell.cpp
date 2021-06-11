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

#include <lib/util/stream/TerminalOutputStream.h>
#include <lib/util/stream/BufferedOutputStream.h>
#include <device/bios/Bios.h>
#include <device/graphic/VesaBiosExtensions.h>
#include <kernel/multiboot/MultibootLinearFrameBufferProvider.h>
#include <device/graphic/LinearFrameBufferTerminalProvider.h>
#include <device/graphic/ColorGraphicsArrayProvider.h>
#include <lib/util/reflection/InstanceFactory.h>
#include <kernel/core/System.h>
#include <kernel/multiboot/Structure.h>
#include <kernel/multiboot/MultibootTerminalProvider.h>
#include <device/hid/Keyboard.h>
#include <lib/util/stream/PipedInputStream.h>
#include <lib/util/stream/InputStreamReader.h>
#include <lib/util/file/tar/Archive.h>
#include <filesystem/tar/ArchiveDriver.h>
#include <lib/util/file/File.h>
#include <lib/util/stream/BufferedReader.h>
#include <kernel/service/FilesystemService.h>
#include <filesystem/memory/MemoryDriver.h>
#include <lib/util/stream/FileInputStream.h>
#include <kernel/memory/manager/TableMemoryManager.h>
#include <kernel/core/Management.h>
#include <device/time/Pit.h>
#include "GatesOfHell.h"
#include "BuildConfig.h"

Kernel::Logger GatesOfHell::log = Kernel::Logger::get("GatesOfHell");

void GatesOfHell::enter() {
    Device::Pit::getInstance().plugin();

    const auto logLevel = Kernel::Multiboot::Structure::hasKernelOption("log_level") ? Kernel::Multiboot::Structure::getKernelOption("log_level") : "info";
    Kernel::Logger::setLevel(logLevel);
    enableSerialLogging();

    log.info("Welcome to hhuOS");
    log.info("%u MiB of physical memory detected", Kernel::Management::getInstance().getTotalPhysicalMemory() / 1024 / 1024);

    if (Device::Bios::isAvailable()) {
        log.info("BIOS detected");
        Device::Bios::init();
    }

    if (Device::Graphic::VesaBiosExtensions::isAvailable()) {
        log.info("VESA graphics detected");
        Util::Reflection::InstanceFactory::registerPrototype(new Device::Graphic::VesaBiosExtensions(true));
    }

    if (Device::Graphic::ColorGraphicsArrayProvider::isAvailable()) {
        log.info("CGA graphics detected");
        Util::Reflection::InstanceFactory::registerPrototype(new Device::Graphic::ColorGraphicsArrayProvider(true));
    }

    Device::Graphic::LinearFrameBufferProvider *lfbProvider = nullptr;
    Device::Graphic::TerminalProvider *terminalProvider;

    if (Kernel::Multiboot::Structure::hasKernelOption("lfb_provider")) {
        log.info("LFB provider set to [%s] -> Starting initialization");
        auto providerName = Kernel::Multiboot::Structure::getKernelOption("lfb_provider");
        lfbProvider = reinterpret_cast<Device::Graphic::LinearFrameBufferProvider*>(Util::Reflection::InstanceFactory::createInstance(providerName));
    } else if (Kernel::Multiboot::MultibootLinearFrameBufferProvider::isAvailable()) {
        log.info("LFB provider is not set -> Initializing LFB provider with multiboot values");
        lfbProvider = new Kernel::Multiboot::MultibootLinearFrameBufferProvider();
    }

    if (Kernel::Multiboot::Structure::hasKernelOption("terminal_provider")) {
        auto providerName = Kernel::Multiboot::Structure::getKernelOption("terminal_provider");
        log.info("Terminal provider set to [%s] -> Starting initialization", static_cast<const char*>(providerName));
        terminalProvider = reinterpret_cast<Device::Graphic::TerminalProvider*>(Util::Reflection::InstanceFactory::createInstance(providerName));
    } else if (lfbProvider != nullptr) {
        log.info("Terminal provider is not set -> Initializing terminal provider with LFB");
        terminalProvider = new Device::Graphic::LinearFrameBufferTerminalProvider(*lfbProvider);
    }  else if (Kernel::Multiboot::MultibootTerminalProvider::isAvailable()) {
        log.info("Terminal provider is not set and LFB is not available -> Initializing terminal with multiboot values");
        terminalProvider = new Kernel::Multiboot::MultibootTerminalProvider();
    } else {
        Util::Exception::throwException(Util::Exception::ILLEGAL_STATE, "Unable to find a suitable graphics driver for this machine!");
    }

    log.info("Initializing filesystem");
    Kernel::System::registerService(Kernel::FilesystemService::SERVICE_NAME, *new Kernel::FilesystemService());
    auto &filesystem = Kernel::System::getService<Kernel::FilesystemService>()->getFilesystem();

    log.info("Mounting root filesystem");
    auto *memoryDriver = new Filesystem::Memory::MemoryDriver();
    filesystem.mountVirtualDriver("/", *memoryDriver);

    if (Kernel::Multiboot::Structure::isModuleLoaded("initrd")) {
        log.info("Initial ramdisk detected -> Mounting [%s]", "/initrd");
        auto module = Kernel::Multiboot::Structure::getModule("initrd");
        auto *tarArchive = new Util::File::Tar::Archive(module.start);
        auto *tarDriver = new Filesystem::Tar::ArchiveDriver(*tarArchive);

        filesystem.createDirectory("/initrd");
        filesystem.mountVirtualDriver("/initrd", *tarDriver);
    }

    log.info("Initializing graphical terminal");
    auto resolution = terminalProvider->searchMode(100, 37, 24);
    auto &terminal = terminalProvider->initializeTerminal(resolution);
    auto terminalStream = Util::Stream::TerminalOutputStream(terminal);
    auto bufferedStream = Util::Stream::BufferedOutputStream(terminalStream, resolution.columns);
    auto writer = Util::Stream::PrintWriter(bufferedStream, true);

    log.info("Initializing keyboard");
    auto keyboardInputStream = Util::Stream::PipedInputStream();
    auto reader = Util::Stream::InputStreamReader(keyboardInputStream);
    auto keyboard = Device::Keyboard(keyboardInputStream);
    keyboard.plugin();

    auto bannerFile = Util::File::File("/initrd/banner.txt");
    if (!bannerFile.exists()) {
        printDefaultBanner(writer);
    } else {
        auto *bannerData = new uint8_t[bannerFile.getLength()];
        auto bannerStream = Util::Stream::FileInputStream(bannerFile);
        auto bannerReader = Util::Stream::InputStreamReader(bannerStream);
        auto bufferedReader = Util::Stream::BufferedReader(bannerReader);

        printBannerLine(writer, bufferedReader);
        writer << "# Welcome to hhuOS!" << Util::Stream::PrintWriter::endl;
        printBannerLine(writer, bufferedReader);
        writer << "# Version      : " << BuildConfig::getVersion() << Util::Stream::PrintWriter::endl;
        printBannerLine(writer, bufferedReader);
        writer << "# Build Date   : " << BuildConfig::getBuildDate() << Util::Stream::PrintWriter::endl;
        printBannerLine(writer, bufferedReader);
        writer << "# Git Branch   : " << BuildConfig::getGitBranch() << Util::Stream::PrintWriter::endl;
        printBannerLine(writer, bufferedReader);
        writer << "# Git Commit   : " << BuildConfig::getGitRevision() << Util::Stream::PrintWriter::endl << Util::Stream::PrintWriter::endl;

        delete[] bannerData;
    }

    auto totalMemory = Kernel::Management::getInstance().getTotalPhysicalMemory();
    auto tableMemoryManager = Kernel::TableMemoryManager(*Kernel::Management::getInstance().getPagingAreaManager(), 0, totalMemory - 1,4096);
    tableMemoryManager.reserveMemory(0, 4095, 1);
    tableMemoryManager.reserveMemory(8192, 10000, 1);
    tableMemoryManager.reserveMemory(7.9 * 1024 * 1024, 8.1 * 1024 * 1024, 1);

    void *test1 = tableMemoryManager.alloc();
    void *test2 = tableMemoryManager.alloc();
    void *test3 = tableMemoryManager.alloc();
    void *test4 = tableMemoryManager.alloc();
    void *test5 = tableMemoryManager.alloc();
    void *test6 = tableMemoryManager.alloc();
    void *test7 = tableMemoryManager.alloc();
    void *test8 = tableMemoryManager.alloc();

    tableMemoryManager.alloc(test1);
    tableMemoryManager.alloc(test4);
    tableMemoryManager.alloc(test7);

    tableMemoryManager.free(test4);

    tableMemoryManager.logTables();

    writer << "> " << Util::Stream::PrintWriter::flush;

    while(true) {
        char input = reader.read();
        writer << input;

        if (input == '\n') {
            writer << "> ";
        }

        writer << Util::Stream::PrintWriter::flush;
    }
}

void GatesOfHell::printDefaultBanner(Util::Stream::PrintWriter &writer) {
    writer << "Welcome to hhuOS!" << Util::Stream::PrintWriter::endl
           << "Version: " << BuildConfig::getVersion() << " (" << BuildConfig::getGitBranch() << ")" << Util::Stream::PrintWriter::endl
           << "Git revision: " << BuildConfig::getGitRevision() << Util::Stream::PrintWriter::endl
           << "Build date: " << BuildConfig::getBuildDate() << Util::Stream::PrintWriter::endl << Util::Stream::PrintWriter::endl;
}

void GatesOfHell::printBannerLine(Util::Stream::PrintWriter &writer, Util::Stream::Reader &reader) {
    char c = reader.read();
    while (c != '\n') {
        writer << c;
        c = reader.read();
    }
}

void GatesOfHell::enableSerialLogging() {
    if (!Kernel::Multiboot::Structure::hasKernelOption("log_com_port")) {
        return;
    }

    Device::SerialPort::ComPort port;
    const auto portName = Kernel::Multiboot::Structure::getKernelOption("log_com_port").toLowerCase();
    if (portName == "com1") {
        port = Device::SerialPort::COM1;
    } else if (portName == "com2") {
        port = Device::SerialPort::COM2;
    } else if (portName == "com3") {
        port = Device::SerialPort::COM3;
    } else if (portName == "com4") {
        port = Device::SerialPort::COM4;
    } else {
        return;
    }

    if (!Device::SerialPort::checkPort(port)) {
        return;
    }

    auto *comPort = new Device::SerialPort(port);
    auto *comStream = new Device::SerialOutputStream(*comPort);
    Kernel::Logger::addOutputStream(*comStream);
}
