#pragma once

#include "HueGatewayDevice.h"
#include <knx.h>

/**
 * @brief Hue switch/button channel (Taster/Schalter).
 *
 * Supports up to 4 buttons per physical switch and an optional rotary encoder.
 * Each button can be assigned one of 6 KNX functions:
 *   0 = Schalten      — 1 KO (DPT 1.001): short press toggles/ON, long press OFF
 *   1 = Dimmen        — 2 KOs: KO+0 DPT 1.001 toggle, KO+1 DPT 3.007 start/stop
 *   2 = Jalousie      — 2 KOs: KO+0 DPT 1.008 step, KO+1 DPT 1.007 move/stop
 *   3 = Medien        — 2 KOs: KO+0 DPT 1.001 play/pause, KO+1 DPT 3.007 volume
 *   4 = Szene         — 1 KO (DPT 18.001): recall scene number
 *   5 = Zwei Objekte  — 2 KOs (DPT 1.001): short=Obj A, long=Obj B
 *
 * KO slot layout (per button, 2 slots each):
 *   Button 1: koBase+0, koBase+1
 *   Button 2: koBase+2, koBase+3
 *   Button 3: koBase+4, koBase+5
 *   Button 4: koBase+6, koBase+7
 *   Rotary:   koBase+8, koBase+9
 *   Reserved: koBase+10, koBase+11
 *
 * Device type discriminator: 2
 */
class HueGatewayButton : public HueGatewayDevice
{
public:
    static constexpr uint8_t MAX_BUTTONS = 4;

    /** Per-button KNX function type */
    enum class ButtonFunction : uint8_t
    {
        Schalten     = 0,
        Dimmen       = 1,
        Jalousie     = 2,
        Medien       = 3,
        Szene        = 4,
        ZweiObjekte  = 5,
    };

    /** Rotary encoder KNX function type */
    enum class RotaryFunction : uint8_t
    {
        Dimmen         = 0,
        Wertgeber      = 1,
        Lautstaerke    = 2,
        Farbtemperatur = 3,
        Lamelle        = 4,
    };

    HueGatewayButton(const String& resourceId, const String& name, uint8_t buttonCount)
        : _resourceId(resourceId)
        , _name(name)
        , _buttonCount(buttonCount > MAX_BUTTONS ? MAX_BUTTONS : buttonCount)
        , _reachable(false)
        , _initialized(false)
        , _hasRotary(false)
        , _rotaryStepPercent(5)
        , _rotaryValue(127)
    {
        for (uint8_t i = 0; i < MAX_BUTTONS; i++)
        {
            _koBtn[i][0]      = 0;
            _koBtn[i][1]      = 0;
            _btnFunction[i]   = ButtonFunction::Schalten;
            _btnInvert[i]     = false;
            _btnSceneNr[i]    = 1;
            _btnState[i]      = false;   // current toggle state
            _btnLongActive[i] = false;   // long press in progress
            _serviceRid[i]    = "";
        }
        _rotaryKo[0]       = 0;
        _rotaryKo[1]       = 0;
        _rotaryFunction    = RotaryFunction::Dimmen;
        _rotaryServiceRid  = "";
    }

    uint8_t deviceType() const override { return 2; }
    String getResourceId() const override { return _resourceId; }
    String getName() const override { return _name; }
    bool isReachable() const override { return _reachable; }
    uint8_t getButtonCount() const { return _buttonCount; }

    /**
     * @brief Initializes KO mapping (2 KO slots per button, 2 for rotary).
     * koBase is the first KO of this channel's block.
     */
    void begin(uint16_t koBase)
    {
        for (uint8_t i = 0; i < MAX_BUTTONS; i++)
        {
            _koBtn[i][0] = koBase + i * 2;
            _koBtn[i][1] = koBase + i * 2 + 1;
        }
        _rotaryKo[0] = koBase + 8;
        _rotaryKo[1] = koBase + 9;
        _initialized = true;
    }

    void setButtonFunction(uint8_t idx, ButtonFunction fn)
    {
        if (idx < MAX_BUTTONS) _btnFunction[idx] = fn;
    }
    void setButtonInvert(uint8_t idx, bool invert)
    {
        if (idx < MAX_BUTTONS) _btnInvert[idx] = invert;
    }
    void setButtonSceneNr(uint8_t idx, uint8_t sceneNr)
    {
        if (idx < MAX_BUTTONS) _btnSceneNr[idx] = sceneNr;
    }
    void setHasRotary(bool hasRotary) { _hasRotary = hasRotary; }
    void setRotaryFunction(RotaryFunction fn) { _rotaryFunction = fn; }
    void setRotaryStepPercent(uint8_t pct)
    {
        _rotaryStepPercent = (pct < 1) ? 1 : (pct > 25 ? 25 : pct);
    }

    void updateReachable(bool reachable) { _reachable = reachable; }

    // ---- Service RID management ----

    /**
     * @brief Stores the service RID for button index 0-3.
     * This is the "button" service RID from the Hue device's services list.
     */
    void setButtonServiceRid(uint8_t idx, const String& rid)
    {
        if (idx < MAX_BUTTONS) _serviceRid[idx] = rid;
    }
    void setRotaryServiceRid(const String& rid) { _rotaryServiceRid = rid; }

    /**
     * @brief Returns the button index (0-based) whose service RID matches the given rid,
     *        or -1 if not matched. outIsRotary is set true if the rotary matches.
     */
    int matchServiceRid(const String& rid, bool& outIsRotary) const
    {
        outIsRotary = false;
        if (_rotaryServiceRid.length() > 0
            && _rotaryServiceRid.equalsIgnoreCase(rid))
        {
            outIsRotary = true;
            return 0;
        }
        for (uint8_t i = 0; i < MAX_BUTTONS; i++)
        {
            if (_serviceRid[i].length() > 0
                && _serviceRid[i].equalsIgnoreCase(rid))
                return i;
        }
        return -1;
    }

    bool matchesEventRid(const String& rid) const override
    {
        bool dummy;
        return matchServiceRid(rid, dummy) >= 0;
    }

    // ---- Event handling ----

    /**
     * @brief Handles a button SSE event for the button with the given Hue control_id.
     * @param hueControlId  Hue metadata.control_id (1-based from API, mapped to 0-based index)
     * @param eventType     "initial_press", "repeat", "short_release", "long_release"
     */
    void handleButtonEvent(int hueControlId, const String& eventType)
    {
        if (!_initialized) return;
        // Hue control_id is 1-based; map to 0-based
        const int idx = hueControlId - 1;
        if (idx < 0 || idx >= (int)_buttonCount) return;

        if (eventType == "initial_press")
        {
            _btnLongActive[idx] = false;
            // No KNX send yet — wait for repeat or release
        }
        else if (eventType == "repeat")
        {
            if (!_btnLongActive[idx])
            {
                _btnLongActive[idx] = true;
                sendLongPressStart(static_cast<uint8_t>(idx));
            }
        }
        else if (eventType == "short_release")
        {
            if (!_btnLongActive[idx])
                sendShortPress(static_cast<uint8_t>(idx));
            // else: long press was active, stop will come via long_release
        }
        else if (eventType == "long_release")
        {
            if (_btnLongActive[idx])
                sendLongPressStop(static_cast<uint8_t>(idx));
            _btnLongActive[idx] = false;
        }
    }

    /**
     * @brief Handles a relative_rotary SSE event.
     * @param clockwise   true = clock_wise
     * @param steps       step count from Hue API
     */
    void handleRotaryEvent(bool clockwise, int steps)
    {
        if (!_initialized || !_hasRotary || _rotaryKo[0] == 0) return;

        const bool invert = false;  // Rotary has no invert option
        const bool up = clockwise ^ invert;

        // Scale steps: each Hue step ≈ _rotaryStepPercent / 100 * 254
        const int delta = (_rotaryStepPercent * 254 * steps) / 100;

        using RF = RotaryFunction;
        switch (_rotaryFunction)
        {
            case RF::Dimmen:
            case RF::Wertgeber:
            case RF::Lautstaerke:
            case RF::Lamelle:
            {
                int newVal = (int)_rotaryValue + (up ? delta : -delta);
                if (newVal < 0)   newVal = 0;
                if (newVal > 254) newVal = 254;
                _rotaryValue = static_cast<uint8_t>(newVal);
                knx.getGroupObject(_rotaryKo[0]).value(_rotaryValue, Dpt(5, 1));
                Serial.printf("[HueGatewayButton] %s - Rotary %s -> KNX DPT5 %u\n",
                              _name.c_str(), up ? "+" : "-", _rotaryValue);
                break;
            }
            case RF::Farbtemperatur:
            {
                // DPT 7.600 (2-byte unsigned, Kelvin 2000-6536)
                int newVal = (int)_rotaryValue16 + (up ? steps * (int)_rotaryStepPercent * 50 / 5
                                                        : -(steps * (int)_rotaryStepPercent * 50 / 5));
                if (newVal < 2000) newVal = 2000;
                if (newVal > 6536) newVal = 6536;
                _rotaryValue16 = static_cast<uint16_t>(newVal);
                knx.getGroupObject(_rotaryKo[0]).value(_rotaryValue16, Dpt(7, 600));
                Serial.printf("[HueGatewayButton] %s - Rotary CT %s -> KNX DPT7.600 %u K\n",
                              _name.c_str(), up ? "+" : "-", _rotaryValue16);
                break;
            }
        }
    }

private:
    String _resourceId;
    String _name;
    uint8_t _buttonCount;
    bool _reachable;
    bool _initialized;

    // Per-button state
    uint16_t _koBtn[MAX_BUTTONS][2];     // [btn][0=primary, 1=secondary]
    ButtonFunction _btnFunction[MAX_BUTTONS];
    bool _btnInvert[MAX_BUTTONS];
    uint8_t _btnSceneNr[MAX_BUTTONS];
    bool _btnState[MAX_BUTTONS];         // toggle state for Schalten/Medien
    bool _btnLongActive[MAX_BUTTONS];    // long press state machine
    String _serviceRid[MAX_BUTTONS];     // button service RIDs

    // Rotary state
    bool _hasRotary;
    RotaryFunction _rotaryFunction;
    uint8_t _rotaryStepPercent;
    uint16_t _rotaryKo[2];
    String _rotaryServiceRid;
    uint8_t _rotaryValue   = 127;        // current absolute value (DPT 5.001)
    uint16_t _rotaryValue16 = 4000;      // current absolute value (DPT 7.600, Kelvin)

    // ---- Action dispatch ----

    void sendShortPress(uint8_t idx)
    {
        using BF = ButtonFunction;
        const uint16_t ko0 = _koBtn[idx][0];
        const bool inv = _btnInvert[idx];

        switch (_btnFunction[idx])
        {
            case BF::Schalten:
                // Toggle: first press ON, invert flips
                _btnState[idx] = !_btnState[idx];
                if (ko0) knx.getGroupObject(ko0).value(static_cast<bool>(_btnState[idx] ^ inv), Dpt(1, 1));
                Serial.printf("[HueGatewayButton] %s btn%u Schalten -> %d\n",
                              _name.c_str(), idx+1, (_btnState[idx] ^ inv) ? 1 : 0);
                break;

            case BF::Dimmen:
                // Short press toggles on/off
                _btnState[idx] = !_btnState[idx];
                if (ko0) knx.getGroupObject(ko0).value(_btnState[idx], Dpt(1, 1));
                Serial.printf("[HueGatewayButton] %s btn%u Dimmen short -> toggle %d\n",
                              _name.c_str(), idx+1, _btnState[idx] ? 1 : 0);
                break;

            case BF::Jalousie:
                // Short press = step (DPT 1.008: 0=up, 1=down)
                if (ko0) knx.getGroupObject(ko0).value(inv ? false : true, Dpt(1, 8));
                Serial.printf("[HueGatewayButton] %s btn%u Jalousie step\n", _name.c_str(), idx+1);
                break;

            case BF::Medien:
                // Short press = play/pause toggle
                _btnState[idx] = !_btnState[idx];
                if (ko0) knx.getGroupObject(ko0).value(_btnState[idx], Dpt(1, 1));
                Serial.printf("[HueGatewayButton] %s btn%u Medien play/pause -> %d\n",
                              _name.c_str(), idx+1, _btnState[idx] ? 1 : 0);
                break;

            case BF::Szene:
                // Recall scene (DPT 18.001: activate bit 7 set, scene number 0-based)
                if (ko0)
                {
                    const uint8_t sceneVal = 0x80 | ((_btnSceneNr[idx] - 1) & 0x3F);
                    knx.getGroupObject(ko0).value(sceneVal, Dpt(18, 1));
                }
                Serial.printf("[HueGatewayButton] %s btn%u Szene %u\n",
                              _name.c_str(), idx+1, _btnSceneNr[idx]);
                break;

            case BF::ZweiObjekte:
                // Short press = Object A (true)
                if (ko0) knx.getGroupObject(ko0).value(true, Dpt(1, 1));
                Serial.printf("[HueGatewayButton] %s btn%u ZweiObjekte -> A\n", _name.c_str(), idx+1);
                break;
        }
    }

    void sendLongPressStart(uint8_t idx)
    {
        using BF = ButtonFunction;
        const uint16_t ko1 = _koBtn[idx][1];
        const bool inv = _btnInvert[idx];

        switch (_btnFunction[idx])
        {
            case BF::Schalten:
                // Long press = force OFF
                if (_koBtn[idx][0])
                    knx.getGroupObject(_koBtn[idx][0]).value(inv ? true : false, Dpt(1, 1));
                _btnState[idx] = !inv;
                Serial.printf("[HueGatewayButton] %s btn%u Schalten long -> OFF\n", _name.c_str(), idx+1);
                break;

            case BF::Dimmen:
                // Long press start = DPT 3.007 dimming start (bit3=direction, bits0-2=speed code 5)
                // Direction: brighter (inv=false) = 0x1D, darker (inv=true) = 0x0D
                if (ko1)
                {
                    const uint8_t dimmVal = inv ? 0x0D : 0x1D;  // speed code 5, direction
                    knx.getGroupObject(ko1).value(dimmVal, Dpt(3, 7));
                }
                Serial.printf("[HueGatewayButton] %s btn%u Dimmen start -> %s\n",
                              _name.c_str(), idx+1, inv ? "darker" : "brighter");
                break;

            case BF::Jalousie:
                // Long press = move (DPT 1.007: 0=up, 1=down)
                if (ko1) knx.getGroupObject(ko1).value(inv ? false : true, Dpt(1, 7));
                Serial.printf("[HueGatewayButton] %s btn%u Jalousie move %s\n",
                              _name.c_str(), idx+1, inv ? "up" : "down");
                break;

            case BF::Medien:
                // Long press = volume start (DPT 3.007: increase = 0x1D)
                if (ko1)
                {
                    const uint8_t volVal = inv ? 0x0D : 0x1D;
                    knx.getGroupObject(ko1).value(volVal, Dpt(3, 7));
                }
                Serial.printf("[HueGatewayButton] %s btn%u Medien volume %s\n",
                              _name.c_str(), idx+1, inv ? "down" : "up");
                break;

            case BF::ZweiObjekte:
                // Long press = Object B
                if (_koBtn[idx][1])
                    knx.getGroupObject(_koBtn[idx][1]).value(true, Dpt(1, 1));
                Serial.printf("[HueGatewayButton] %s btn%u ZweiObjekte -> B\n", _name.c_str(), idx+1);
                break;

            default:
                break;
        }
    }

    void sendLongPressStop(uint8_t idx)
    {
        using BF = ButtonFunction;
        const uint16_t ko1 = _koBtn[idx][1];

        switch (_btnFunction[idx])
        {
            case BF::Dimmen:
                // DPT 3.007 stop = 0x00
                if (ko1) knx.getGroupObject(ko1).value((uint8_t)0x00, Dpt(3, 7));
                Serial.printf("[HueGatewayButton] %s btn%u Dimmen stop\n", _name.c_str(), idx+1);
                break;

            case BF::Jalousie:
                // Stop = DPT 1.007 value 0 (stop)
                if (ko1) knx.getGroupObject(ko1).value(false, Dpt(1, 7));
                Serial.printf("[HueGatewayButton] %s btn%u Jalousie stop\n", _name.c_str(), idx+1);
                break;

            case BF::Medien:
                // Volume stop = DPT 3.007 stop
                if (ko1) knx.getGroupObject(ko1).value((uint8_t)0x00, Dpt(3, 7));
                Serial.printf("[HueGatewayButton] %s btn%u Medien volume stop\n", _name.c_str(), idx+1);
                break;

            default:
                break;
        }
    }
};
