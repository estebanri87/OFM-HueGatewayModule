#pragma once

#include "HueGatewayDevice.h"
#include <knx.h>

/**
 * @brief Hue switch/button channel (Taster/Schalter).
 *
 * Represents a Hue button resource (/clip/v2/resource/button).
 * Button events are translated to KNX 1-bit telegrams (DPST-1-1).
 * Up to 4 buttons per physical switch are supported.
 * KO slots: 0=btn1, 3=btn2, 9=btn3, 10=btn4.
 *
 * Device type discriminator: 2
 */
class HueGatewayButton : public HueGatewayDevice
{
public:
    static constexpr uint8_t MAX_BUTTONS = 4;

    HueGatewayButton(const String& resourceId, const String& name, uint8_t buttonCount)
        : _resourceId(resourceId)
        , _name(name)
        , _buttonCount(buttonCount > MAX_BUTTONS ? MAX_BUTTONS : buttonCount)
        , _reachable(false)
        , _initialized(false)
    {
        for (uint8_t i = 0; i < MAX_BUTTONS; i++)
            _koButtons[i] = 0;
    }

    uint8_t deviceType() const override { return 2; }

    String getResourceId() const override { return _resourceId; }
    String getName() const override { return _name; }
    bool isReachable() const override { return _reachable; }
    uint8_t getButtonCount() const { return _buttonCount; }

    /**
     * @brief Initializes KO mapping for up to 4 buttons.
     * Pass 0 for KO numbers of unused buttons.
     */
    void begin(uint16_t koBtn1, uint16_t koBtn2, uint16_t koBtn3, uint16_t koBtn4)
    {
        _koButtons[0] = koBtn1;
        _koButtons[1] = koBtn2;
        _koButtons[2] = koBtn3;
        _koButtons[3] = koBtn4;
        _initialized = true;
    }

    void updateReachable(bool reachable) { _reachable = reachable; }

    /**
     * @brief Fires a KNX telegram for the given button index (0-based).
     * @param buttonIndex 0-3
     * @param value true for press, false for release
     */
    void triggerButton(uint8_t buttonIndex, bool value)
    {
        if (!_initialized || buttonIndex >= MAX_BUTTONS)
            return;
        const uint16_t ko = _koButtons[buttonIndex];
        if (ko == 0 || buttonIndex >= _buttonCount)
            return;
        knx.getGroupObject(ko).value(value, Dpt(1, 1));
        Serial.printf("[HueGatewayButton] %s - Button %u -> KNX value=%d\n",
                      _name.c_str(), buttonIndex + 1, value ? 1 : 0);
    }

private:
    String _resourceId;
    String _name;
    uint8_t _buttonCount;
    bool _reachable;
    bool _initialized;
    uint16_t _koButtons[MAX_BUTTONS];
};
