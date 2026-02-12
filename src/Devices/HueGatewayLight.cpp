#include "HueGatewayLight.h"
#include "../HueGatewayClient.h"
#include "../HCL/HCLMasterManager.h"
#include <knx.h>

HueGatewayLight::HueGatewayLight(const String& lightId, const String& name, HueGatewayClient* client)
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
    , _hclMasterNum(0)
    , _fadingActive(false)
    , _currentKelvin(4000)
    , _lastHCLUpdate(0)
    , _lastHCLBrightness(0)
    , _initialized(false)
    , _lastUpdate(0)
{
}

HueGatewayLight::~HueGatewayLight()
{
}

void HueGatewayLight::begin(uint16_t koSwitch, uint16_t koBrightness, uint16_t koDimming,
                     uint16_t koStatusSwitch, uint16_t koStatusBrightness)
{
    _koSwitch = koSwitch;
    _koBrightness = koBrightness;
    _koDimming = koDimming;
    _koStatusSwitch = koStatusSwitch;
    _koStatusBrightness = koStatusBrightness;
    _koStatus = koStatusSwitch;  // Backward compatibility
    _initialized = true;
    
    Serial.printf("[HueGatewayLight] %s initialized - KO Switch:%d Brightness:%d Dimming:%d StatusSwitch:%d StatusBrightness:%d\n",
                  _name.c_str(), _koSwitch, _koBrightness, _koDimming, _koStatusSwitch, _koStatusBrightness);
}

void HueGatewayLight::processKnxSwitch(bool value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueGatewayLight] %s - KNX Switch: %d\n", _name.c_str(), value);
    
    // Beim Ausschalten: Fade sofort abbrechen
    if (!value && _fadingActive) {
        _fadingActive = false;
        Serial.printf("[HueGatewayLight] %s - Fade aborted by switch off\n", _name.c_str());
    }
    
    _on = value;
    
    // Beim Einschalten mit HCL: Aktuelle HCL-Werte anwenden
    if (value && _hclMasterNum > 0 && _hclMasterNum <= 4) {
        HCL::InterpolatedValue hclValue = HCL::masterManager.getCurrentValue(_hclMasterNum);
        
        // Helligkeit von % (0-100) zu Hue (0-254) konvertieren
        _brightness = (uint8_t)((hclValue.brightness * 254) / 100);
        
        // Timer und letzte Werte aktualisieren für kontinuierliches Update
        _lastHCLUpdate = millis();
        _lastHCLBrightness = hclValue.brightness;
        
        Serial.printf("[HueGatewayLight] %s - Applying HCL Master %d values: %dK, %d%% (%d Hue)\n",
                     _name.c_str(), _hclMasterNum, hclValue.kelvin, hclValue.brightness, _brightness);
        
        // Mit Farbtemperatur senden (Fade-Dauer aus HCL Manager)
        uint8_t fadeDuration = HCL::masterManager.getFadeDuration();
        sendToHueWithColorTemp(hclValue.kelvin, fadeDuration);
        return;  // Frühzeitiger Exit, da sendToHueWithColorTemp bereits sendet
    }
    
    sendToHue();
}

void HueGatewayLight::processKnxBrightness(uint8_t value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueGatewayLight] %s - KNX Brightness: %d%% (DPT 5.001)\n", _name.c_str(), value);
    
    // KNX DPT 5.001: 0-100% → Hue 0-254
    _brightness = (uint8_t)((value / 100.0f) * 254.0f);
    
    // Bei Brightness > 0 automatisch einschalten
    if (_brightness > 0 && !_on)
    {
        _on = true;
        Serial.printf("[HueGatewayLight] %s - Auto-on due to brightness > 0\n", _name.c_str());
    }
    // Bei Brightness = 0 ausschalten
    else if (_brightness == 0 && _on)
    {
        _on = false;
        Serial.printf("[HueGatewayLight] %s - Auto-off due to brightness = 0\n", _name.c_str());
    }
    
    sendToHue();
}

void HueGatewayLight::processKnxDimming(uint8_t control)
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
        Serial.printf("[HueGatewayLight] %s - KNX Dimming STOP\n", _name.c_str());
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
    
    Serial.printf("[HueGatewayLight] %s - KNX Dimming: %s %d steps -> Brightness: %d\n",
                  _name.c_str(), brighter ? "BRIGHTER" : "DARKER", steps, _brightness);
    
    // Bei Brightness > 0 automatisch einschalten
    if (_brightness > 0 && !_on)
    {
        _on = true;
        Serial.printf("[HueGatewayLight] %s - Auto-on due to dimming to > 0\n", _name.c_str());
    }
    // Bei Brightness = 0 ausschalten
    else if (_brightness == 0 && _on)
    {
        _on = false;
        Serial.printf("[HueGatewayLight] %s - Auto-off due to dimming to 0\n", _name.c_str());
    }
    
    sendToHue();
}

void HueGatewayLight::updateFromHue(bool on, uint8_t brightness)
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
        Serial.printf("[HueGatewayLight] %s - Updated from Hue: On:%d Bri:%d\n",
                      _name.c_str(), _on, _brightness);
        
        sendStatusToKnx();
    }
}

void HueGatewayLight::sendStatusToKnx()
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
    
    Serial.printf("[HueGatewayLight] %s - Sent to KNX: On:%d Bri:%d%% (Hue:%d)\n",
                  _name.c_str(), _on, brightnessPercent, _brightness);
}

// ===== Private Methods =====

void HueGatewayLight::sendToHue()
{
    if (!_client || !_client->isInitialized())
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Client not ready\n", _name.c_str());
        return;
    }
    
    bool success = _client->setLightState(_lightId, _on, _brightness);
    
    if (success)
    {
        _lastUpdate = millis();
        Serial.printf("[HueGatewayLight] %s - Sent to Hue: On:%d Bri:%d\n",
                      _name.c_str(), _on, _brightness);
    }
    else
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Failed to send to Hue\n", _name.c_str());
    }
}

uint8_t HueGatewayLight::knxToHueBrightness(uint8_t knxValue)
{
    // KNX: 0-255
    // Hue: 0-254 (1-254 für dimmbares Licht, 0 = aus)
    
    if (knxValue == 0)
        return 0;
    
    // Linear mapping: 1-255 → 1-254
    return (uint8_t)((knxValue / 255.0f) * 254.0f);
}

uint8_t HueGatewayLight::hueToKnxBrightness(uint8_t hueValue)
{
    // Hue: 0-254
    // KNX: 0-255
    
    if (hueValue == 0)
        return 0;
    
    // Linear mapping: 0-254 → 0-255
    return (uint8_t)((hueValue / 254.0f) * 255.0f);
}

uint16_t HueGatewayLight::kelvinToMirek(uint16_t kelvin)
{
    // Kelvin → mirek (Micro Reciprocal Kelvin)
    // mirek = 1.000.000 / Kelvin
    // Hue Range: 153-500 mirek (entspricht 6500K-2000K)
    
    if (kelvin < 2000) kelvin = 2000;
    if (kelvin > 6500) kelvin = 6500;
    
    uint16_t mirek = 1000000 / kelvin;
    
    // Clamp to Hue's valid range
    if (mirek < 153) mirek = 153;  // 6500K
    if (mirek > 500) mirek = 500;  // 2000K
    
    return mirek;
}

void HueGatewayLight::sendToHueWithColorTemp(uint16_t kelvin, uint8_t fadeDuration)
{
    if (!_initialized || !_client)
        return;
    
    uint16_t mirek = kelvinToMirek(kelvin);
    _currentKelvin = kelvin;
    
    Serial.printf("[HueGatewayLight] %s - Sending to Hue: %s, %d%% (%d/254), %dK (%d mirek), fade:%ds\n",
                  _name.c_str(), 
                  _on ? "ON" : "OFF",
                  (_brightness * 100) / 254,
                  _brightness,
                  kelvin,
                  mirek,
                  fadeDuration);
    
    _client->setLightStateWithColorTemp(_lightId, _on, _brightness, mirek, fadeDuration);
    
    // Status-KOs aktualisieren
    sendStatusToKnx();
}

void HueGatewayLight::loop()
{
    // Nur wenn initialisiert, eingeschaltet und einem HCL-Master zugeordnet
    if (!_initialized || !_on || _hclMasterNum == 0 || _hclMasterNum > 4)
        return;
    
    // Update-Intervall aus HCL-Manager abrufen (in Minuten)
    uint8_t updateIntervalMin = HCL::masterManager.getUpdateInterval();
    unsigned long updateIntervalMs = updateIntervalMin * 60000UL; // Minuten zu Millisekunden
    
    // Prüfen, ob genug Zeit vergangen ist
    unsigned long now = millis();
    if (updateIntervalMs > 0 && (now - _lastHCLUpdate) < updateIntervalMs)
        return;
    
    // Aktuelle HCL-Werte abrufen
    HCL::InterpolatedValue hclValue = HCL::masterManager.getCurrentValue(_hclMasterNum);
    
    // Prüfen, ob sich die Werte signifikant geändert haben
    // Kelvin-Toleranz: ±10K, Brightness-Toleranz: ±2%
    bool kelvinChanged = abs((int)hclValue.kelvin - (int)_currentKelvin) > 10;
    bool brightnessChanged = abs((int)hclValue.brightness - (int)_lastHCLBrightness) > 2;
    
    if (!kelvinChanged && !brightnessChanged)
        return;
    
    // Werte haben sich geändert - anwenden
    _lastHCLUpdate = now;
    _lastHCLBrightness = hclValue.brightness;
    
    // Helligkeit von % (0-100) zu Hue (0-254) konvertieren
    _brightness = (uint8_t)((hclValue.brightness * 254) / 100);
    
    Serial.printf("[HueGatewayLight] %s - HCL Update: %dK → %dK, %d%% → %d%%\n",
                 _name.c_str(), _currentKelvin, hclValue.kelvin, 
                 (_currentKelvin * 100) / 254, hclValue.brightness);
    
    // Fade-Dauer aus HCL-Manager abrufen
    uint8_t fadeDuration = HCL::masterManager.getFadeDuration();
    
    // Sende Update mit Farbtemperatur
    sendToHueWithColorTemp(hclValue.kelvin, fadeDuration);
}
