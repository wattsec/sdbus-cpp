/**
 * (C) 2016 - 2021 KISTLER INSTRUMENTE AG, Winterthur, Switzerland
 * (C) 2016 - 2024 Stanislav Angelovic <stanislav.angelovic@protonmail.com>
 *
 * @file Proxy.cpp
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

#include "Proxy.h"
#include "IConnection.h"
#include "MessageUtils.h"
#include "Utils.h"
#include "sdbus-c++/Message.h"
#include "sdbus-c++/IConnection.h"
#include "sdbus-c++/Error.h"
#include "ScopeGuard.h"
#include SDBUS_HEADER
#include <cassert>
#include <chrono>
#include <utility>

namespace sdbus::internal {

Proxy::Proxy(sdbus::internal::IConnection& connection, std::string destination, std::string objectPath)
    : connection_(&connection, [](sdbus::internal::IConnection *){ /* Intentionally left empty */ })
    , destination_(std::move(destination))
    , objectPath_(std::move(objectPath))
{
    SDBUS_CHECK_SERVICE_NAME(destination_);
    SDBUS_CHECK_OBJECT_PATH(objectPath_);

    // The connection is not ours only, it is owned and managed by the user and we just reference
    // it here, so we expect the client to manage the event loop upon this connection themselves.
}

Proxy::Proxy( std::unique_ptr<sdbus::internal::IConnection>&& connection
            , std::string destination
            , std::string objectPath )
    : connection_(std::move(connection))
    , destination_(std::move(destination))
    , objectPath_(std::move(objectPath))
{
    SDBUS_CHECK_SERVICE_NAME(destination_);
    SDBUS_CHECK_OBJECT_PATH(objectPath_);

    // The connection is ours only, i.e. it's us who has to manage the event loop upon this connection,
    // in order that we get and process signals, async call replies, and other messages from D-Bus.
    connection_->enterEventLoopAsync();
}

Proxy::Proxy( std::unique_ptr<sdbus::internal::IConnection>&& connection
            , std::string destination
            , std::string objectPath
            , dont_run_event_loop_thread_t )
    : connection_(std::move(connection))
    , destination_(std::move(destination))
    , objectPath_(std::move(objectPath))
{
    SDBUS_CHECK_SERVICE_NAME(destination_);
    SDBUS_CHECK_OBJECT_PATH(objectPath_);

    // Even though the connection is ours only, we don't start an event loop thread.
    // This proxy is meant to be created, used for simple synchronous D-Bus call(s) and then dismissed.
}

MethodCall Proxy::createMethodCall(const std::string& interfaceName, const std::string& methodName)
{
    return connection_->createMethodCall(destination_, objectPath_, interfaceName, methodName);
}

MethodReply Proxy::callMethod(const MethodCall& message, uint64_t timeout)
{
    SDBUS_THROW_ERROR_IF(!message.isValid(), "Invalid method call message provided", EINVAL);

    return connection_->callMethod(message, timeout);
}

PendingAsyncCall Proxy::callMethod(const MethodCall& message, async_reply_handler asyncReplyCallback, uint64_t timeout)
{
    SDBUS_THROW_ERROR_IF(!message.isValid(), "Invalid async method call message provided", EINVAL);

    auto callback = (void*)&Proxy::sdbus_async_reply_handler;
    auto callData = std::make_shared<AsyncCalls::CallData>(AsyncCalls::CallData{*this, std::move(asyncReplyCallback), {}, AsyncCalls::CallData::State::RUNNING});
    auto weakData = std::weak_ptr<AsyncCalls::CallData>{callData};

    callData->slot = connection_->callMethod(message, callback, callData.get(), timeout);

    pendingAsyncCalls_.addCall(std::move(callData));

    // TODO: Instead of PendingAsyncCall consider using Slot implementation for simplicity and consistency
    return {weakData};
}

std::future<MethodReply> Proxy::callMethod(const MethodCall& message, with_future_t)
{
    return Proxy::callMethod(message, {}, with_future);
}

std::future<MethodReply> Proxy::callMethod(const MethodCall& message, uint64_t timeout, with_future_t)
{
    auto promise = std::make_shared<std::promise<MethodReply>>();
    auto future = promise->get_future();

    async_reply_handler asyncReplyCallback = [promise = std::move(promise)](MethodReply& reply, const Error* error) noexcept
    {
        if (error == nullptr)
            promise->set_value(reply);
        else
            promise->set_exception(std::make_exception_ptr(*error));
    };

    (void)Proxy::callMethod(message, std::move(asyncReplyCallback), timeout);

    return future;
}

void Proxy::registerSignalHandler( const std::string& interfaceName
                                 , const std::string& signalName
                                 , signal_handler signalHandler )
{
    SDBUS_CHECK_INTERFACE_NAME(interfaceName);
    SDBUS_CHECK_MEMBER_NAME(signalName);
    SDBUS_THROW_ERROR_IF(!signalHandler, "Invalid signal handler provided", EINVAL);

    auto& interface = interfaces_[interfaceName];

    auto signalData = std::make_unique<InterfaceData::SignalData>(*this, std::move(signalHandler), nullptr);
    auto insertionResult = interface.signals_.emplace(signalName, std::move(signalData));

    auto inserted = insertionResult.second;
    SDBUS_THROW_ERROR_IF(!inserted, "Failed to register signal handler: handler already exists", EINVAL);
}

void Proxy::unregisterSignalHandler( const std::string& interfaceName
                                   , const std::string& signalName )
{
    auto it = interfaces_.find(interfaceName);

    if (it != interfaces_.end())
        it->second.signals_.erase(signalName);
}

void Proxy::finishRegistration()
{
    registerSignalHandlers(*connection_);
}

void Proxy::registerSignalHandlers(sdbus::internal::IConnection& connection)
{
    for (auto& interfaceItem : interfaces_)
    {
        const auto& interfaceName = interfaceItem.first;
        auto& signalsOnInterface = interfaceItem.second.signals_;

        for (auto& signalItem : signalsOnInterface)
        {
            const auto& signalName = signalItem.first;
            auto* signalData = signalItem.second.get();
            signalData->slot = connection.registerSignalHandler( destination_
                                                               , objectPath_
                                                               , interfaceName
                                                               , signalName
                                                               , &Proxy::sdbus_signal_handler
                                                               , signalData);
        }
    }
}

void Proxy::unregister()
{
    pendingAsyncCalls_.clear();
    interfaces_.clear();
}

sdbus::IConnection& Proxy::getConnection() const
{
    return *connection_;
}

const std::string& Proxy::getObjectPath() const
{
    return objectPath_;
}

Message Proxy::getCurrentlyProcessedMessage() const
{
    return connection_->getCurrentlyProcessedMessage();
}

int Proxy::sdbus_async_reply_handler(sd_bus_message *sdbusMessage, void *userData, sd_bus_error *retError)
{
    auto* asyncCallData = static_cast<AsyncCalls::CallData*>(userData);
    assert(asyncCallData != nullptr);
    assert(asyncCallData->callback);
    auto& proxy = asyncCallData->proxy;
    auto state = asyncCallData->state;

    // We are removing the CallData item at the complete scope exit, after the callback has been invoked.
    // We can't do it earlier (before callback invocation for example), because CallBack data (slot release)
    // is the synchronization point between callback invocation and Proxy::unregister.
    SCOPE_EXIT
    {
        // Remove call meta-data if it's a real async call (a sync call done in terms of async has STATE_NOT_ASYNC)
        if (state != AsyncCalls::CallData::State::NOT_ASYNC)
            proxy.pendingAsyncCalls_.removeCall(asyncCallData);
    };

    auto message = Message::Factory::create<MethodReply>(sdbusMessage, &proxy.connection_->getSdBusInterface());

    auto ok = invokeHandlerAndCatchErrors([&]
    {
        const auto* error = sd_bus_message_get_error(sdbusMessage);
        if (error == nullptr)
        {
            asyncCallData->callback(message, nullptr);
        }
        else
        {
            Error exception(error->name, error->message);
            asyncCallData->callback(message, &exception);
        }
    }, retError);

    return ok ? 0 : -1;
}

int Proxy::sdbus_signal_handler(sd_bus_message *sdbusMessage, void *userData, sd_bus_error *retError)
{
    auto* signalData = static_cast<InterfaceData::SignalData*>(userData);
    assert(signalData != nullptr);
    assert(signalData->callback);

    auto message = Message::Factory::create<Signal>(sdbusMessage, &signalData->proxy.connection_->getSdBusInterface());

    auto ok = invokeHandlerAndCatchErrors([&](){ signalData->callback(message); }, retError);

    return ok ? 0 : -1;
}

}

namespace sdbus {

PendingAsyncCall::PendingAsyncCall(std::weak_ptr<void> callData)
    : callData_(std::move(callData))
{
}

void PendingAsyncCall::cancel()
{
    if (auto ptr = callData_.lock(); ptr != nullptr)
    {
        auto* callData = static_cast<internal::Proxy::AsyncCalls::CallData*>(ptr.get());
        callData->proxy.pendingAsyncCalls_.removeCall(callData);

        // At this point, the callData item is being deleted, leading to the release of the
        // sd-bus slot pointer. This release locks the global sd-bus mutex. If the async
        // callback is currently being processed, the sd-bus mutex is locked by the event
        // loop thread, thus access to the callData item is synchronized and thread-safe.
    }
}

bool PendingAsyncCall::isPending() const
{
    return !callData_.expired();
}

}

namespace sdbus {

std::unique_ptr<sdbus::IProxy> createProxy( IConnection& connection
                                          , std::string destination
                                          , std::string objectPath )
{
    auto* sdbusConnection = dynamic_cast<sdbus::internal::IConnection*>(&connection);
    SDBUS_THROW_ERROR_IF(!sdbusConnection, "Connection is not a real sdbus-c++ connection", EINVAL);

    return std::make_unique<sdbus::internal::Proxy>( *sdbusConnection
                                                   , std::move(destination)
                                                   , std::move(objectPath) );
}

std::unique_ptr<sdbus::IProxy> createProxy( std::unique_ptr<IConnection>&& connection
                                          , std::string destination
                                          , std::string objectPath )
{
    auto* sdbusConnection = dynamic_cast<sdbus::internal::IConnection*>(connection.get());
    SDBUS_THROW_ERROR_IF(!sdbusConnection, "Connection is not a real sdbus-c++ connection", EINVAL);

    connection.release();

    return std::make_unique<sdbus::internal::Proxy>( std::unique_ptr<sdbus::internal::IConnection>(sdbusConnection)
                                                   , std::move(destination)
                                                   , std::move(objectPath) );
}

std::unique_ptr<sdbus::IProxy> createProxy( std::unique_ptr<IConnection>&& connection
                                          , std::string destination
                                          , std::string objectPath
                                          , dont_run_event_loop_thread_t )
{
    auto* sdbusConnection = dynamic_cast<sdbus::internal::IConnection*>(connection.get());
    SDBUS_THROW_ERROR_IF(!sdbusConnection, "Connection is not a real sdbus-c++ connection", EINVAL);

    connection.release();

    return std::make_unique<sdbus::internal::Proxy>( std::unique_ptr<sdbus::internal::IConnection>(sdbusConnection)
                                                   , std::move(destination)
                                                   , std::move(objectPath)
                                                   , dont_run_event_loop_thread );
}

std::unique_ptr<sdbus::IProxy> createProxy( std::string destination
                                          , std::string objectPath )
{
    auto connection = sdbus::createConnection();

    auto sdbusConnection = std::unique_ptr<sdbus::internal::IConnection>(dynamic_cast<sdbus::internal::IConnection*>(connection.release()));
    assert(sdbusConnection != nullptr);

    return std::make_unique<sdbus::internal::Proxy>( std::move(sdbusConnection)
                                                   , std::move(destination)
                                                   , std::move(objectPath) );
}

std::unique_ptr<sdbus::IProxy> createProxy( std::string destination
                                          , std::string objectPath
                                          , dont_run_event_loop_thread_t )
{
    auto connection = sdbus::createConnection();

    auto sdbusConnection = std::unique_ptr<sdbus::internal::IConnection>(dynamic_cast<sdbus::internal::IConnection*>(connection.release()));
    assert(sdbusConnection != nullptr);

    return std::make_unique<sdbus::internal::Proxy>( std::move(sdbusConnection)
                                                   , std::move(destination)
                                                   , std::move(objectPath)
                                                   , dont_run_event_loop_thread );
}

}
