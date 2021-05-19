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

#ifndef __Kernel_include__
#define __Kernel_include__

#include "Service.h"
#include "lib/util/async/Spinlock.h"

#include <cstdint>
#include "lib/util/memory/String.h"
#include "kernel/thread/ThreadState.h"
#include "lib/util/data/HashMap.h"

namespace Kernel {

/**
 * Provides access to registered KernelServices.
 *
 * @author Filip Krakowski
 */
class System {

public:

    System() = delete;

    /**
     * Returns an already registered service.
     *
     * @tparam T The service's type
     * @return The service
     */
    template<class T>
    static T *getService() {

        if (!isServiceRegistered(T::SERVICE_NAME)) {

            Util::Exception::throwException(Util::Exception::INVALID_ARGUMENT, "Invalid service!");
        }

        return (T *) serviceMap.get(T::SERVICE_NAME);
    }

    /**
	 * Registers an instance of a kernel service under a given ID.
	 *
	 * @param serviceId The unique service id.
	 * @param kernelService Instance of the KernelService
	 */
    static void registerService(const Util::Memory::String &serviceId, Service &kernelService);

    /**
     * Indicates whether a particular service has already been registered.
     *
     * @param serviceId The service's id
     * @return true, if the service has already been registered, false else
     */
    static bool isServiceRegistered(const Util::Memory::String &serviceId);

    /**
     * Triggers a kernel panic printing relevant information inside a bluescreen.
     *
     * @param frame The interrupt frame
     */
    static void panic(InterruptFrame *frame);

private:

    static Util::Data::HashMap<Util::Memory::String, Service *> serviceMap;

    static Util::Async::Spinlock serviceLock;
};

}

#endif
