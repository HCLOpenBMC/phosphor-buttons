#include "button_handler.hpp"

#include "settings.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/State/Chassis/server.hpp>
#include <xyz/openbmc_project/State/Host/server.hpp>

#define SWITCH_POS_PATH "/etc/swPos.data"
#define KEY_FRONTPANEL_UART_POS "uart_sel_pos"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#define MULTI_HOST_ENABLED 0

namespace phosphor
{
namespace button
{

namespace sdbusRule = sdbusplus::bus::match::rules;
using namespace sdbusplus::xyz::openbmc_project::State::server;
using namespace phosphor::logging;
using sdbusplus::exception::SdBusError;

constexpr auto propertyIface = "org.freedesktop.DBus.Properties";
constexpr auto chassisIface = "xyz.openbmc_project.State.Chassis";
constexpr auto chassisSystemIface = "xyz.openbmc_project.State.Chassis-system0";
constexpr auto hostIface = "xyz.openbmc_project.State.Host";
constexpr auto powerButtonIface = "xyz.openbmc_project.Chassis.Buttons.Power";
constexpr auto idButtonIface = "xyz.openbmc_project.Chassis.Buttons.ID";
constexpr auto resetButtonIface = "xyz.openbmc_project.Chassis.Buttons.Reset";
constexpr auto selectorButtonIface =
    "xyz.openbmc_project.Chassis.Buttons.Selector";
constexpr auto mapperIface = "xyz.openbmc_project.ObjectMapper";
constexpr auto ledGroupIface = "xyz.openbmc_project.Led.Group";

constexpr auto mapperObjPath = "/xyz/openbmc_project/object_mapper";
constexpr auto mapperService = "xyz.openbmc_project.ObjectMapper";
constexpr auto ledGroupBasePath = "/xyz/openbmc_project/led/groups/";

int host;
int poistion;

Handler::Handler(sdbusplus::bus::bus& bus) : bus(bus)
{
    try
    {
        if (!getService(POWER_DBUS_OBJECT_NAME, powerButtonIface).empty())
        {
            log<level::INFO>("Starting power button handler");
            powerButtonReleased = std::make_unique<sdbusplus::bus::match_t>(
                bus,
                sdbusRule::type::signal() + sdbusRule::member("Released") +
                    sdbusRule::path(POWER_DBUS_OBJECT_NAME) +
                    sdbusRule::interface(powerButtonIface),
                std::bind(std::mem_fn(&Handler::powerPressed), this,
                          std::placeholders::_1));

            powerButtonLongPressReleased =
                std::make_unique<sdbusplus::bus::match_t>(
                    bus,
                    sdbusRule::type::signal() +
                        sdbusRule::member("PressedLong") +
                        sdbusRule::path(POWER_DBUS_OBJECT_NAME) +
                        sdbusRule::interface(powerButtonIface),
                    std::bind(std::mem_fn(&Handler::longPowerPressed), this,
                              std::placeholders::_1));
        }
    }
    catch (SdBusError& e)
    {
        // The button wasn't implemented
    }

    try
    {
        if (!getService(ID_DBUS_OBJECT_NAME, idButtonIface).empty())
        {
            log<level::INFO>("Registering ID button handler");
            idButtonReleased = std::make_unique<sdbusplus::bus::match_t>(
                bus,
                sdbusRule::type::signal() + sdbusRule::member("Released") +
                    sdbusRule::path(ID_DBUS_OBJECT_NAME) +
                    sdbusRule::interface(idButtonIface),
                std::bind(std::mem_fn(&Handler::idPressed), this,
                          std::placeholders::_1));
        }
    }
    catch (SdBusError& e)
    {
        // The button wasn't implemented
    }

    try
    {
        if (!getService(RESET_DBUS_OBJECT_NAME, resetButtonIface).empty())
        {
            log<level::INFO>("Registering reset button handler");
            resetButtonReleased = std::make_unique<sdbusplus::bus::match_t>(
                bus,
                sdbusRule::type::signal() + sdbusRule::member("Released") +
                    sdbusRule::path(RESET_DBUS_OBJECT_NAME) +
                    sdbusRule::interface(resetButtonIface),
                std::bind(std::mem_fn(&Handler::resetPressed), this,
                          std::placeholders::_1));
        }
    }
    catch (SdBusError& e)
    {
        // The button wasn't implemented
    }

    try
    {
        if (!getService(SELECTOR_DBUS_OBJECT_NAME, selectorButtonIface).empty())
        {
            log<level::INFO>("Registering selector button handler");
            selectorButtonReleased = std::make_unique<sdbusplus::bus::match_t>(
                bus,
                sdbusRule::type::signal() + sdbusRule::member("Released") +
                    sdbusRule::path(SELECTOR_DBUS_OBJECT_NAME) +
                    sdbusRule::interface(selectorButtonIface),
                std::bind(std::mem_fn(&Handler::selectorPressed), this,
                          std::placeholders::_1));
        }
    }
    catch (SdBusError& e)
    {
        // The button wasn't implemented
    }
}

std::string Handler::getService(const std::string& path,
                                const std::string& interface) const
{
    auto method = bus.new_method_call(mapperService, mapperObjPath, mapperIface,
                                      "GetObject");
    method.append(path, std::vector{interface});
    auto result = bus.call(method);

    std::map<std::string, std::vector<std::string>> objectData;
    result.read(objectData);

    return objectData.begin()->first;
}

bool Handler::poweredOn() const
{
    auto service = getService(CHASSIS_STATE_OBJECT_NAME, chassisIface);
    auto method = bus.new_method_call(
        service.c_str(), CHASSIS_STATE_OBJECT_NAME, propertyIface, "Get");
    method.append(chassisIface, "CurrentPowerState");
    auto result = bus.call(method);

    std::variant<std::string> state;
    result.read(state);

    return Chassis::PowerState::On ==
           Chassis::convertPowerStateFromString(std::get<std::string>(state));
}

#define SERVER1 1
#define BMC 0

void Handler::powerPressed(sdbusplus::message::message& msg)
{
    auto transition = Host::Transition::On;

    try
    {
        if (poweredOn())
        {
            transition = Host::Transition::Off;
        }

        log<level::INFO>("Handling power button press");

        std::variant<std::string> state = convertForMessage(transition);

        auto service = getService(HOST_STATE_OBJECT_NAME, hostIface);
        auto method = bus.new_method_call(
            service.c_str(), HOST_STATE_OBJECT_NAME, propertyIface, "Set");
        method.append(hostIface, "RequestedHostTransition", state);

        bus.call(method);
    }
    catch (SdBusError& e)
    {
        log<level::ERR>("Failed power state change on a power button press",
                        entry("ERROR=%s", e.what()));
    }
}

void Handler::longPowerPressed(sdbusplus::message::message& msg)
{
    try
    {
        if (!poweredOn())
        {
            log<level::INFO>(
                "Power is off so ignoring long power button press");
            return;
        }

        log<level::INFO>("Handling long power button press");
#if MULTI_HOST_ENABLED
        if (poistion == BMC)
        {
            std::variant<std::string> state =
                convertForMessage(chassis_system0::Transition::On);
            // chassis system reset or sled cycle
            auto service =
                getService(CHASSISSYSTEM_STATE_OBJECT_NAME, chassisSystemIface);
            auto method = bus.new_method_call(service.c_str(),
                                              CHASSISSYSTEM_STATE_OBJECT_NAME,
                                              propertyIface, "Set");
            method.append(chassisSystemIface, "RequestedPowerTransition",
                          state);

            bus.call(method);
        }
        else
        {
            std::variant<std::string> state =
                convertForMessage(Chassis::Transition::On);

            auto service =
                getService(CHASSIS_STATE_OBJECT_NAME + std::to_string(host),
                           chassisIface + std::to_string(host));
            auto method = bus.new_method_call(service.c_str(),
                                              CHASSIS_STATE_OBJECT_NAME +
                                                  std::to_string(host),
                                              propertyIface, "Set");

            method.append(chassisIface, "RequestedPowerTransition", state);

            bus.call(method);
        }

#endif
        std::variant<std::string> state =
            convertForMessage(Chassis::Transition::On);
        auto Chassisstr = CHASSIS_STATE_OBJECT_NAME; // TODO

        auto service = getService(CHASSIS_STATE_OBJECT_NAME + std::to_string(0),
                                  chassisIface + std::to_string(0));
        auto method = bus.new_method_call(
            service.c_str(), CHASSIS_STATE_OBJECT_NAME, propertyIface, "Set");
        method.append(chassisIface, "RequestedPowerTransition", state);

        bus.call(method);
    }
    catch (SdBusError& e)
    {
        log<level::ERR>("Failed powering off on long power button press",
                        entry("ERROR=%s", e.what()));
    }
}

void Handler::resetPressed(sdbusplus::message::message& msg)
{
    try
    {
        if (!poweredOn())
        {
            log<level::INFO>("Power is off so ignoring reset button press");
            return;
        }

        log<level::INFO>("Handling reset button press");

        std::variant<std::string> state =
            convertForMessage(Host::Transition::Reboot);

        auto service = getService(HOST_STATE_OBJECT_NAME, hostIface);
        auto method = bus.new_method_call(
            service.c_str(), HOST_STATE_OBJECT_NAME, propertyIface, "Set");

        method.append(hostIface, "RequestedHostTransition", state);

        bus.call(method);
    }
    catch (SdBusError& e)
    {
        log<level::ERR>("Failed power state change on a reset button press",
                        entry("ERROR=%s", e.what()));
    }
}

nlohmann::json appData __attribute__((init_priority(101)));

int16_t getSwPpos(char* pos)
{
    /* Get App data stored in json file */
    std::ifstream file(SWITCH_POS_PATH);
    if (file)
    {
        file >> appData;
        file.close();
    }
    else
    {
        std::cout << "Error in read file" << SWITCH_POS_PATH << "\n";
        std::cout.flush();
        return -1;
    }
    std::string str = appData[KEY_FRONTPANEL_UART_POS].get<std::string>();

    *pos++ = 0; // byte 1: Set selector not supported
    *pos++ = 0; // byte 2: Only ASCII supported

    int len = str.length();
    *pos++ = len;
    memcpy(pos, str.data(), len);

    std::cout << "Read Data : " << pos << "\n";
    std::cout.flush();

    return 0;
}

int16_t setSwPpos(char* pos)
{
    std::stringstream ss;

    ss << (char)pos[0];

    std::cout << "Write Data : " << ss.str() << "\n";
    std::cout.flush();
    appData[KEY_FRONTPANEL_UART_POS] = ss.str();

    std::ofstream file(SWITCH_POS_PATH);
    file << appData;
    file.close();

    return 0;
}
#if 0

void Handler::longResetPressed(sdbusplus::message::message& msg)
{
    try
    {
        if (!poweredOn())
        {
            log<level::INFO>("Power is off so ignoring reset button press");
            return;
        } 

        log<level::INFO>("Handling long reset button press");

        std::variant<std::string> state =
            convertForMessage(Chassis::Transition::Reboot);

        auto service = getService(CHASSIS_STATE_OBJECT_NAME, chassisIface);
        auto method = bus.new_method_call(
            service.c_str(), CHASSIS_STATE_OBJECT_NAME, propertyIface, "Set");

        method.append(chassisIface, "RequestedHostTransition", state);

        bus.call(method);
    }
    catch (SdBusError& e)
    {
        log<level::ERR>("Failed power state change on a long reset button press",
                        entry("ERROR=%s", e.what()));
    }
}
#endif

void Handler::idPressed(sdbusplus::message::message& msg)
{
    std::string groupPath{ledGroupBasePath};
    groupPath += ID_LED_GROUP;

    auto service = getService(groupPath, ledGroupIface);

    if (service.empty())
    {
        log<level::INFO>("No identify LED group found during ID button press",
                         entry("GROUP=%s", groupPath.c_str()));
        return;
    }

    try
    {
        auto method = bus.new_method_call(service.c_str(), groupPath.c_str(),
                                          propertyIface, "Get");
        method.append(ledGroupIface, "Asserted");
        auto result = bus.call(method);

        std::variant<bool> state;
        result.read(state);

        state = !std::get<bool>(state);

        log<level::INFO>("Changing ID LED group state on ID LED press",
                         entry("GROUP=%s", groupPath.c_str()),
                         entry("STATE=%d", std::get<bool>(state)));

        method = bus.new_method_call(service.c_str(), groupPath.c_str(),
                                     propertyIface, "Set");

        method.append(ledGroupIface, "Asserted", state);
        result = bus.call(method);
    }
    catch (SdBusError& e)
    {
        log<level::ERR>("Error toggling ID LED group on ID button press",
                        entry("ERROR=%s", e.what()));
    }
}

void Handler::selectorPressed(sdbusplus::message::message& msg)
{

    char locstr[10];

    host = (host >= BMC) ? SERVER1 : (host + 1);
    sprintf(locstr, "%u", host);
    setSwPpos(locstr);
}

} // namespace button
} // namespace phosphor
