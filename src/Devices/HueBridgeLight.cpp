#include "HueBridgeLight.h"
#include "../HueBridgeClient.h"
#include <knx.h>

HueBridgeLight::HueBridgeLight(const String& lightId, const String& name, HueBridgeClient* client)
    : _lightId(lightId)
    , _name(name)
    , _client(client)
    , _koSwitch(0)
    , _koBrightness(0)
    , _koDimming(0)
    , _koStatusSwitch(0)
    , _koStatusBrightness(0)
    , _on(false)
    , _brightness(0)
    , _reachable(true)
    , _initialized(false)
    , _lastUpdate(0)
{
}

HueBridgeLight::~HueBridgeLight()
{
}

void HueBridgeLight::begin(uint16_t koSwitch, uint16_t koBrightness, uint16_t koDimming,
                     uint16_t koStatusSwitch, uint16_t koStatusBrightness)
{
    _koSwitch = koSwitch;
    _koBrightness = koBrightness;
    _koDimming = koDimming;
    _koStatusSwitch = koStatusSwitch;
    _koStatusBrightness = koStatusBrightness;
    _koStatus = koStatusSwitch;  // Backward compatibility
    _initialized = true;
    
    Serial.printf("[HueBridgeLight] %s initialized - KO Switch:%d Brightness:%d Dimming:%d StatusSwitch:%d StatusBrightness:%d\n",
                  _name.c_str(), _koSwitch, _koBrightness, _koDimming, _koStatusSwitch, _koStatusBrightness);
}

void HueBridgeLight::processKnxSwitch(bool value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueBridgeLight] %s - KNX Switch: %d\n", _name.c_str(), value);
    
    _on = value;
    sendToHue();
}

void HueBridgeLight::processKnxBrightness(uint8_t value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueBridgeLight] %s - KNX Brightness: %d%% (DPT 5.001)\n", _name.c_str(), value);
    
    // KNX DPT 5.001: 0-100% → Hue 0-254
    _brightness = (uint8_t)((value / 100.0f) * 254.0f);
    
    // Bei Brightness > 0 automatisch einschalten
    if (_brightness > 0 && !_on)
    {
        _on = true;
        Serial.printf("[HueBridgeLight] %s - Auto-on due to brightness > 0\n", _name.c_str());
    }
    // Bei Brightness = 0 ausschalten
    else if (_brightness == 0 && _on)
    {
        _on = false;
        Serial.printf("[HueBridgeLight] %s - Auto-off due to brightness = 0\n", _name.c_str());
    }
    
    sendToHue();
}

void HueBridgeLight::processKnxDimming(uint8_t control)
{
    if (!_initialized || !_client)
        return;
    
    // DPT 3.007: 4-Bit Dimm-Steuerung
    // Bit 3: 0=dunkler, 1=heller
    // Bit 0-2: Anzahl Schritte (0=Stop, 1-7=Schritte)
    
    uint8_t steps = control & 0x07;  // Bits 0-2
    bool brighter = (control & 0x08) != 0;  // Bit 3
    
    // Stop-Telegramm ignorieren (0 Schritte)
    if (steps == 0)
    {
        Serial.printf("[HueBridgeLight] %s - KNX Dimming STOP\n", _name.c_str());
        return;
    }
    
    // Berechne Helligkeitsänderung
    // Pro Schritt ca. 10% (254/25 ≈ 10)
    int16_t brightnessChange = steps * 10;  // 10 Einheiten pro Schritt
    
    if (!brighter)
        brightnessChange = -brightnessChange;
    
    // Neue Helligkeit berechnen (mit Clamping)
    int16_t newBrightness = _brightness + brightnessChange;
    if (newBrightness < 0) newBrightness = 0;
    if (newBrightness > 254) newBrightness = 254;
    
    _brightness = (uint8_t)newBrightness;
    
    Serial.printf("[HueBridgeLight] %s - KNX Dimming: %s %d steps -> Brightness: %d\n",
                  _name.c_str(), brighter ? "BRIGHTER" : "DARKER", steps, _brightness);
    
    // Bei Brightness > 0 automatisch einschalten
    if (_brightness > 0 && !_on)
    {
        _on = true;
        Serial.printf("[HueBridgeLight] %s - Auto-on due to dimming to > 0\n", _name.c_str());
    }
    // Bei Brightness = 0 ausschalten
    else if (_brightness == 0 && _on)
    {
        _on = false;
        Serial.printf("[HueBridgeLight] %s - Auto-off due to dimming to 0\n", _name.c_str());
    }
    
    sendToHue();
}

void HueBridgeLight::updateFromHue(bool on, uint8_t brightness)
{
    bool changed = false;
    
    if (_on != on)
    {
        _on = on;
        changed = true;
    }
    
    if (_brightness != brightness)
    {
        _brightness = brightness;
        changed = true;
    }
    
    if (changed)
    {
        _lastUpdate = millis();
        Serial.printf("[HueBridgeLight] %s - Updated from Hue: On:%d Bri:%d\n",
                      _name.c_str(), _on, _brightness);
        
        sendStatusToKnx();
    }
}

void HueBridgeLight::sendStatusToKnx()
{
    if (!_initialized)
        return;
    
    // Status an separate Status-KOs schreiben
    // Hue 0-254 → KNX 0-100%
    uint8_t brightnessPercent = (uint8_t)((_brightness / 254.0f) * 100.0f);
    
    // KO Status Switch: DPT 1.001 (Bool) - Ein/Aus Feedback
    knx.getGroupObject(_koStatusSwitch).value(_on, Dpt(1, 1));
    
    // KO Status Brightness: DPT 5.001 (Percentage 0-100%) - Helligkeits-Feedback
    knx.getGroupObject(_koStatusBrightness).value(brightnessPercent, Dpt(5, 1));
    
    Serial.printf("[HueBridgeLight] %s - Sent to KNX: On:%d Bri:%d%% (Hue:%d)\n",
                  _name.c_str(), _on, brightnessPercent, _brightness);
}

// ===== Private Methods =====

void HueBridgeLight::sendToHue()
{
    if (!_client || !_client->isInitialized())
    {
        Serial.printf("[HueBridgeLight] %s - ERROR: Client not ready\n", _name.c_str());
        return;
    }
    
    bool success = _client->setLightState(_lightId, _on, _brightness);
    
    if (success)
    {
        _lastUpdate = millis();
        Serial.printf("[HueBridgeLight] %s - Sent to Hue: On:%d Bri:%d\n",
                      _name.c_str(), _on, _brightness);
    }
    else
    {
        Serial.printf("[HueBridgeLight] %s - ERROR: Failed to send to Hue\n", _name.c_str());
    }
}

uint8_t HueBridgeLight::knxToHueBrightness(uint8_t knxValue)
{
    // KNX: 0-255
    // Hue: 0-254 (1-254 für dimmbares Licht, 0 = aus)
    
    if (knxValue == 0)
        return 0;
    
    // Linear mapping: 1-255 → 1-254
    return (uint8_t)((knxValue / 255.0f) * 254.0f);
}

uint8_t HueBridgeLight::hueToKnxBrightness(uint8_t hueValue)
{
    // Hue: 0-254
    // KNX: 0-255
    
    if (hueValue == 0)
        return 0;
    
    // Linear mapping: 0-254 → 0-255
    return (uint8_t)((hueValue / 254.0f) * 255.0f);
}

