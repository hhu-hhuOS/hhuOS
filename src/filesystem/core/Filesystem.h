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

#ifndef __Filesystem_include__
#define __Filesystem_include__

#include <lib/util/async/Spinlock.h>
#include <lib/util/data/HashMap.h>
#include "Driver.h"

namespace Filesystem {

/**
 * The filesystem. It works by maintaining a list of mountVirtualDriver-points. Every request will be handled by picking the right
 * mountVirtualDriver-point and and passing the request over to the corresponding Driver.
 */
class Filesystem {

public:
    /**
     * Constructor.
     */
    Filesystem() = default;

    /**
     * Copy constructor.
     */
    Filesystem(const Filesystem &copy) = delete;

    /**
     * Assignment operator.
     */
     Filesystem& operator=(const Filesystem &other) = delete;

    /**
     * Destructor.
     */
    ~Filesystem() = default;

    /**
     * Processes the '.' and '..' entries of a path.
     *
     * @param path The path
     * @return The processed path
     */
    static Util::Memory::String getCanonicalPath(const Util::Memory::String &path);

    /**
     * Mounts a device at a specified location.
     *
     * @param devicePath The device-file (in /dev/storage) or the direct device name (e.g. "hdd0p1")
     * @param targetPath The mountVirtualDriver-path
     * @param fsType The filesystem-type
     *
     * @return Return code
     */
    bool mountVirtualDriver(const Util::Memory::String &targetPath, Driver &driver);

    /**
     * Unmounts a device from a specified location.
     *
     * @param path The mountVirtualDriver-path
     *
     * @return Return code.
     */
    bool unmount(const Util::Memory::String &path);

    /**
     * Get a node at a specified path.
     * CAUTION: May return nullptr, if the file does not exist.
     *          Always check the return value!
     *
     * @param path The path
     *
     * @return The node (or nullptr on failure)
     */
    Util::File::Node* getNode(const Util::Memory::String &path);

    /**
     * Create a file at a specified path.
     * The parent-directory of the new folder must exist beforehand.
     *
     * @param path The path
     *
     * @return true on success
     */
    bool createFile(const Util::Memory::String &path);

    /**
     * Create a directory at a specified path.
     * The parent-directory of the new folder must exist beforehand.
     *
     * @param path The path
     *
     * @return true on success
     */
    bool createDirectory(const Util::Memory::String &path);

    /**
     * Delete an existing file or directory at a given path.
     * The file must be a regular file or an empty folder (a leaf in the filesystem tree).
     *
     * @param path The path
     *
     * @return true on success
     */
    bool deleteFile(const Util::Memory::String &path);
    
private:

    Util::Data::HashMap<Util::Memory::String, Driver*> mountPoints;
    Util::Async::Spinlock lock;

    /**
     * Get the driver, that is mounted at a specified path.
     * CAUTION: May return nullptr, if the file does not exist.
     *          Always check the return value!
     *
     * @param path The path. After successful execution, the part up to the mountVirtualDriver point will truncated,
     *             so that the path can be used for the returned driver.
     *
     * @return The driver (or nullptr on failure)
     */
    Driver* getMountedDriver(Util::Memory::String &path);
    
};

}

#endif