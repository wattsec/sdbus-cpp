/**
 * (C) 2016 - 2021 KISTLER INSTRUMENTE AG, Winterthur, Switzerland
 * (C) 2016 - 2024 Stanislav Angelovic <stanislav.angelovic@protonmail.com>
 *
 * @file AdaptorInterfaces.h
 *
 * Created on: Nov 8, 2016
 * Project: sdbus-c++
 * Description: High-level D-Bus IPC C++ library based on sd-bus
 *
 * This file is part of sdbus-c++.
 *
 * sdbus-c++ is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * sdbus-c++ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sdbus-c++. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDBUS_CXX_ADAPTORINTERFACES_H_
#define SDBUS_CXX_ADAPTORINTERFACES_H_

#include <sdbus-c++/IObject.h>
#include <cassert>
#include <string>
#include <memory>

// Forward declarations
namespace sdbus {
    class IConnection;
}

namespace sdbus {

    /********************************************//**
     * @class ObjectHolder
     *
     * ObjectHolder is a helper that simply owns and provides
     * access to an object to other classes in the inheritance
     * hierarchy of an object based on generated interface classes.
     *
     ***********************************************/
    class ObjectHolder
    {
    protected:
        ObjectHolder(std::unique_ptr<IObject>&& object)
            : object_(std::move(object))
        {
        }

        const IObject& getObject() const
        {
            assert(object_ != nullptr);
            return *object_;
        }

        IObject& getObject()
        {
            assert(object_ != nullptr);
            return *object_;
        }

    private:
        std::unique_ptr<IObject> object_;
    };

    /********************************************//**
     * @class AdaptorInterfaces
     *
     * AdaptorInterfaces is a helper template class that joins all interface classes of a remote
     * D-Bus object generated by sdbus-c++-xml2cpp to be used on the server (the adaptor) side,
     * including some auxiliary classes. AdaptorInterfaces is the class that native-like object
     * implementation classes written by users should inherit from and implement all pure virtual
     * methods. So the _Interfaces template parameter is a list of sdbus-c++-xml2cpp-generated
     * adaptor-side interface classes representing interfaces (with methods, signals and properties)
     * of the D-Bus object.
     *
     * In the final adaptor class inherited from AdaptorInterfaces, it is necessary to finish
     * adaptor registration in class constructor (finishRegistration();`), and, conversely,
     * unregister the adaptor in class destructor (`unregister();`).
     *
     ***********************************************/
    template <typename... _Interfaces>
    class AdaptorInterfaces
        : protected ObjectHolder
        , public _Interfaces...
    {
    public:
        /*!
         * @brief Creates object instance
         *
         * @param[in] connection D-Bus connection where the object will publish itself
         * @param[in] objectPath Path of the D-Bus object
         *
         * For more information, consult @ref createObject(sdbus::IConnection&,std::string)
         */
        AdaptorInterfaces(IConnection& connection, std::string objectPath)
            : ObjectHolder(createObject(connection, std::move(objectPath)))
            , _Interfaces(getObject())...
        {
        }

        /*!
         * @brief Finishes adaptor API registration and publishes the adaptor on the bus
         *
         * This function must be called in the constructor of the final adaptor class that implements AdaptorInterfaces.
         *
         * For more information, see underlying @ref IObject::finishRegistration()
         */
        void registerAdaptor()
        {
            getObject().finishRegistration();
        }

        /*!
         * @brief Unregisters adaptors's API and removes it from the bus
         *
         * This function must be called in the destructor of the final adaptor class that implements AdaptorInterfaces.
         *
         * For more information, see underlying @ref IObject::unregister()
         */
        void unregisterAdaptor()
        {
            getObject().unregister();
        }

        /*!
         * @brief Returns object path of the underlying DBus object
         */
        const std::string& getObjectPath() const
        {
            return getObject().getObjectPath();
        }

    protected:
        using base_type = AdaptorInterfaces;

        AdaptorInterfaces(const AdaptorInterfaces&) = delete;
        AdaptorInterfaces& operator=(const AdaptorInterfaces&) = delete;
        AdaptorInterfaces(AdaptorInterfaces&&) = default;
        AdaptorInterfaces& operator=(AdaptorInterfaces&&) = default;
        ~AdaptorInterfaces() = default;
    };

}

#endif /* SDBUS_CXX_ADAPTORINTERFACES_H_ */
