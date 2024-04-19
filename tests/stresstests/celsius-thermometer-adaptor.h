
/*
 * This file was automatically generated by sdbus-c++-xml2cpp; DO NOT EDIT!
 */

#ifndef __sdbuscpp__celsius_thermometer_adaptor_h__adaptor__H__
#define __sdbuscpp__celsius_thermometer_adaptor_h__adaptor__H__

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <tuple>

namespace org {
namespace sdbuscpp {
namespace stresstests {
namespace celsius {

class thermometer_adaptor
{
public:
    static constexpr const char* INTERFACE_NAME = "org.sdbuscpp.stresstests.celsius.thermometer";

protected:
    thermometer_adaptor(sdbus::IObject& object)
        : m_object(object)
    {
    }

    thermometer_adaptor(const thermometer_adaptor&) = delete;
    thermometer_adaptor& operator=(const thermometer_adaptor&) = delete;
    thermometer_adaptor(thermometer_adaptor&&) = delete;
    thermometer_adaptor& operator=(thermometer_adaptor&&) = delete;

    ~thermometer_adaptor() = default;

    void registerAdaptor()
    {
        m_object.addVTable(sdbus::registerMethod("getCurrentTemperature").withOutputParamNames("result").implementedAs([this](){ return this->getCurrentTemperature(); })).forInterface(INTERFACE_NAME);
    }

private:
    virtual uint32_t getCurrentTemperature() = 0;

private:
    sdbus::IObject& m_object;
};

}}}} // namespaces

#endif
