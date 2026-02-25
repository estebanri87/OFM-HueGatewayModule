#include "HueGatewayLight.h"
#include "../HueGatewayClient.h"
#include "../HCL/HCLMasterManager.h"
#include <knx.h>
#include <math.h>

namespace
{
float dimmingStepCodeToPercent(uint8_t stepCode)
{
    switch (stepCode)
    {
        case 1: return 100.0f;
        case 2: return 50.0f;
        case 3: return 25.0f;
        case 4: return 12.5f;
        case 5: return 6.25f;
        case 6: return 3.125f;
        case 7: return 1.5625f;
        default: return 0.0f;
    }
}
}

HueGatewayLight::HueGatewayLight(const String& lightId, const String& name, HueGatewayClient* client)
    : _lightId(lightId)
    , _name(name)
    , _client(client)
    , _koSwitch(0)
    , _koBrightness(0)
    , _koDimming(0)
    , _koStatusSwitch(0)
    , _koStatusBrightness(0)
    , _koStatusColorTemp(0)
    , _koStatusColorRGB(0)
    , _on(false)
    , _brightness(0)
    , _reachable(true)
    , _currentRed(255)
    , _currentGreen(255)
    , _currentBlue(255)
    , _lightType(0)
    , _minBrightnessPercent(0)
    , _minBrightnessHue(0)
    , _isGroupedTarget(false)
    , _hclMasterNum(0)
    , _fadingActive(false)
    , _currentKelvin(4000)
    , _lastHCLUpdate(0)
    , _lastHCLBrightness(0)
    , _initialized(false)
    , _lastUpdate(0)
    , _lastRelativeDimCmdMs(0)
    , _relativeDimCooldownUntilMs(0)
    , _relativeDimErrorStreak(0)
{
}

HueGatewayLight::~HueGatewayLight()
{
}

void HueGatewayLight::begin(uint16_t koSwitch, uint16_t koBrightness, uint16_t koDimming,
                            uint16_t koStatusSwitch, uint16_t koStatusBrightness,
                            uint16_t koStatusColorTemp, uint16_t koStatusColorRGB)
{
    _koSwitch = koSwitch;
    _koBrightness = koBrightness;
    _koDimming = koDimming;
    _koStatusSwitch = koStatusSwitch;
    _koStatusBrightness = koStatusBrightness;
    _koStatusColorTemp = koStatusColorTemp;
    _koStatusColorRGB = koStatusColorRGB;
    _koStatus = koStatusSwitch;  // Backward compatibility for older code paths.
    _initialized = true;
    
    Serial.printf("[HueGatewayLight] %s initialized - KO Switch:%d Brightness:%d Dimming:%d StatusSwitch:%d StatusBrightness:%d StatusColorTemp:%d StatusRGB:%d\n",
                  _name.c_str(), _koSwitch, _koBrightness, _koDimming, _koStatusSwitch, _koStatusBrightness, _koStatusColorTemp, _koStatusColorRGB);
}

void HueGatewayLight::processKnxSwitch(bool value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueGatewayLight] %s - KNX Switch: %d\n", _name.c_str(), value);
    
    // Stop ongoing fade immediately when switching off.
    if (!value && _fadingActive) {
        _fadingActive = false;
        Serial.printf("[HueGatewayLight] %s - Fade aborted by switch off\n", _name.c_str());
    }
    
    _on = value;

    if (_on && _brightness == 0 && _minBrightnessHue > 0)
    {
        _brightness = _minBrightnessHue;
    }
    
    // On switch-on with HCL assignment, apply the current interpolated HCL target.
    if (value
        && _hclMasterNum > 0
        && _hclMasterNum <= 4
        && !HCL::masterManager.isApplyBlocked()
        && !HCL::masterManager.isMasterApplyBlocked(_hclMasterNum)) {
        HCL::InterpolatedValue hclValue = HCL::masterManager.getCurrentValue(_hclMasterNum);
        
        // Convert brightness percent (0-100) to Hue scale (0-254).
        _brightness = (uint8_t)((hclValue.brightness * 254) / 100);
        
        // Prime loop state for continuous HCL updates.
        _lastHCLUpdate = millis();
        _lastHCLBrightness = hclValue.brightness;
        
        Serial.printf("[HueGatewayLight] %s - Applying HCL Master %d values: %dK, %d%% (%d Hue)\n",
                     _name.c_str(), _hclMasterNum, hclValue.kelvin, hclValue.brightness, _brightness);
        
        // UX preference: fast switch-on, slow switch-off.
        uint8_t fadeDuration = value ? 0 : HCL::masterManager.getFadeDuration();
        if (_lightType >= 2)
        {
            sendToHueWithColorTemp(hclValue.kelvin, fadeDuration);
            return;
        }

        sendToHue();
        return;
    }
    
    sendToHue();
}

void HueGatewayLight::processKnxBrightness(uint8_t value)
{
    if (!_initialized || !_client)
        return;
    
    Serial.printf("[HueGatewayLight] %s - KNX Brightness: %d%% (DPT 5.001)\n", _name.c_str(), value);
    
    // KNX DPT 5.001: 0-100% -> Hue 0-254.
    _brightness = (uint8_t)((value / 100.0f) * 254.0f);

    if (_brightness > 0 && _brightness < _minBrightnessHue)
    {
        _brightness = _minBrightnessHue;
    }
    
    // Auto-turn on when brightness is set above zero.
    if (_brightness > 0 && !_on)
    {
        _on = true;
        Serial.printf("[HueGatewayLight] %s - Auto-on due to brightness > 0\n", _name.c_str());
    }
    // Auto-turn off when brightness reaches zero.
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
    
    // DPT 3.007: 4-bit dimming control.
    // Bit 3: 0=darker, 1=brighter
    // Bit 0-2: number of steps (0=stop, 1-7=steps)
    
    uint8_t steps = control & 0x07;  // Bits 0-2
    bool brighter = (control & 0x08) != 0;  // Bit 3
    unsigned long nowMs = millis();

    // Debounce very fast telegram bursts to reduce API pressure.
    if ((nowMs - _lastRelativeDimCmdMs) < 100UL)
    {
        return;
    }
    _lastRelativeDimCmdMs = nowMs;
    
    // Ignore stop telegram (0 steps).
    if (steps == 0)
    {
        Serial.printf("[HueGatewayLight] %s - KNX Dimming STOP\n", _name.c_str());

        bool stopOk = _isGroupedTarget
            ? _client->stopGroupedLightDimming(_lightId)
            : _client->stopLightDimming(_lightId);

        if (!stopOk)
        {
            _relativeDimErrorStreak = min<uint8_t>(static_cast<uint8_t>(_relativeDimErrorStreak + 1), static_cast<uint8_t>(10));
            if (_relativeDimErrorStreak >= 3)
            {
                _relativeDimCooldownUntilMs = nowMs + 5000UL;
                Serial.printf("[HueGatewayLight] %s - Relative dimming disabled for 5s after stop errors\n", _name.c_str());
            }
        }
        else
        {
            _relativeDimErrorStreak = 0;
            _relativeDimCooldownUntilMs = 0;
        }
        return;
    }

    // Use relative delta API when healthy, otherwise fallback to stable legacy path.
    if (_relativeDimCooldownUntilMs == 0 || nowMs >= _relativeDimCooldownUntilMs)
    {
        bool deltaOk = _isGroupedTarget
            ? _client->setGroupedLightDimmingDelta(_lightId, brighter, steps)
            : _client->setLightDimmingDelta(_lightId, brighter, steps);

        if (deltaOk)
        {
            _relativeDimErrorStreak = 0;
            _relativeDimCooldownUntilMs = 0;
            applyRelativeDimmingCache(brighter, steps);
            sendStatusToKnx();
            return;
        }

        _relativeDimErrorStreak = min<uint8_t>(static_cast<uint8_t>(_relativeDimErrorStreak + 1), static_cast<uint8_t>(10));
        if (_relativeDimErrorStreak >= 3)
        {
            _relativeDimCooldownUntilMs = nowMs + 5000UL;
            Serial.printf("[HueGatewayLight] %s - Relative dimming disabled for 5s, falling back to legacy\n", _name.c_str());
        }
    }

    applyRelativeDimmingLegacy(brighter, steps);
}

void HueGatewayLight::applyRelativeDimmingLegacy(bool brighter, uint8_t steps)
{
    applyRelativeDimmingCache(brighter, steps);
    sendToHue();
}

void HueGatewayLight::applyRelativeDimmingCache(bool brighter, uint8_t steps)
{
    float deltaPercent = dimmingStepCodeToPercent(steps);
    int16_t brightnessChange = static_cast<int16_t>(roundf((deltaPercent / 100.0f) * 254.0f));
    if (brightnessChange < 1)
        brightnessChange = 1;

    if (!brighter)
        brightnessChange = -brightnessChange;

    int16_t newBrightness = static_cast<int16_t>(_brightness) + brightnessChange;
    if (newBrightness < 0)
        newBrightness = 0;
    if (newBrightness > 254)
        newBrightness = 254;

    _brightness = static_cast<uint8_t>(newBrightness);

    if (_brightness > 0 && _brightness < _minBrightnessHue)
    {
        _brightness = _minBrightnessHue;
    }

    Serial.printf("[HueGatewayLight] %s - KNX Dimming: %s stepCode=%u (%.3f%%) -> Cached Brightness: %u\n",
                  _name.c_str(),
                  brighter ? "BRIGHTER" : "DARKER",
                  static_cast<unsigned>(steps),
                  deltaPercent,
                  static_cast<unsigned>(_brightness));

    if (_brightness > 0 && !_on)
    {
        _on = true;
    }
    else if (_brightness == 0 && _on)
    {
        _on = false;
    }
}

void HueGatewayLight::processKnxColorTemp(uint16_t kelvin)
{
    if (!_initialized || !_client)
        return;

    if (_lightType < 2)
    {
        Serial.printf("[HueGatewayLight] %s - Ignoring ColorTemp for non-CT light type %u\n", _name.c_str(), _lightType);
        return;
    }

    if (kelvin < 2000) kelvin = 2000;
    if (kelvin > 6500) kelvin = 6500;

    uint16_t mirek = kelvinToMirek(kelvin);
    bool ok = _isGroupedTarget
        ? _client->setGroupedLightColorTemperature(_lightId, mirek)
        : _client->setLightColorTemperature(_lightId, mirek);

    if (ok)
    {
        _currentKelvin = kelvin;
        _lastUpdate = millis();
        sendStatusToKnx();
        Serial.printf("[HueGatewayLight] %s - KNX ColorTemp: %uK (%u mirek)\n", _name.c_str(), kelvin, mirek);
    }
    else
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Failed to set ColorTemp\n", _name.c_str());
    }
}

void HueGatewayLight::processKnxColorRGB(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!_initialized || !_client)
        return;

    if (_lightType < 3)
    {
        Serial.printf("[HueGatewayLight] %s - Ignoring RGB for non-RGB light type %u\n", _name.c_str(), _lightType);
        return;
    }

    float x = 0.0f;
    float y = 0.0f;
    rgbToXy(red, green, blue, x, y);

    bool ok = _isGroupedTarget
        ? _client->setGroupedLightColor(_lightId, x, y)
        : _client->setLightColor(_lightId, x, y);

    if (ok)
    {
        _currentRed = red;
        _currentGreen = green;
        _currentBlue = blue;
        _lastUpdate = millis();
        sendStatusToKnx();
        Serial.printf("[HueGatewayLight] %s - KNX RGB: (%u,%u,%u) -> XY:(%.3f,%.3f)\n",
                      _name.c_str(), red, green, blue, x, y);
    }
    else
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Failed to set RGB\n", _name.c_str());
    }
}

void HueGatewayLight::updateFromHue(bool on, uint8_t brightness, uint16_t colorTempKelvin, uint8_t red, uint8_t green, uint8_t blue)
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

    if (_lightType >= 2 && colorTempKelvin >= 2000 && colorTempKelvin <= 6500 && _currentKelvin != colorTempKelvin)
    {
        _currentKelvin = colorTempKelvin;
        changed = true;
    }

    if (_lightType >= 3 && (_currentRed != red || _currentGreen != green || _currentBlue != blue))
    {
        _currentRed = red;
        _currentGreen = green;
        _currentBlue = blue;
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
    
    // Publish status to dedicated feedback KOs.
    // Hue 0-254 -> KNX 0-100%.
    // Report 0% while switched off, even if Hue internally keeps the last dim level.
    uint8_t statusBrightnessHue = _on ? _brightness : 0;
    uint8_t brightnessPercent = (uint8_t)((statusBrightnessHue / 254.0f) * 100.0f);
    
    // KO Status Switch: DPT 1.001 (bool) - On/Off feedback.
    knx.getGroupObject(_koStatusSwitch).value(_on, Dpt(1, 1));
    
    // KO Status Brightness: DPT 5.001 (0-100%) - brightness feedback.
    knx.getGroupObject(_koStatusBrightness).value(brightnessPercent, Dpt(5, 1));

    if (_lightType >= 2 && _koStatusColorTemp > 0)
    {
        knx.getGroupObject(_koStatusColorTemp).value(_currentKelvin, Dpt(7, 600));
    }

    if (_lightType >= 3 && _koStatusColorRGB > 0)
    {
        GroupObject& rgbKo = knx.getGroupObject(_koStatusColorRGB);
        uint8_t* rgb = rgbKo.valueRef();
        rgb[0] = _currentRed;
        rgb[1] = _currentGreen;
        rgb[2] = _currentBlue;
        rgbKo.objectWritten();
    }
    
    Serial.printf("[HueGatewayLight] %s - Sent to KNX: On:%d Bri:%d%% (Hue:%d)\n",
                  _name.c_str(), _on, brightnessPercent, statusBrightnessHue);
}

// ===== Private Methods =====

void HueGatewayLight::sendToHue()
{
    if (!_client || !_client->isInitialized())
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Client not ready\n", _name.c_str());
        return;
    }
    
    bool success = _isGroupedTarget
        ? _client->setGroupedLightState(_lightId, _on, _brightness)
        : _client->setLightState(_lightId, _on, _brightness);
    
    if (success)
    {
        _lastUpdate = millis();
        sendStatusToKnx();
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
    // Hue: 0-254 (1-254 for dimmable light, 0 = off)
    
    if (knxValue == 0)
        return 0;
    
    // Linear mapping: 1-255 -> 1-254
    return (uint8_t)((knxValue / 255.0f) * 254.0f);
}

uint8_t HueGatewayLight::hueToKnxBrightness(uint8_t hueValue)
{
    // Hue: 0-254
    // KNX: 0-255
    
    if (hueValue == 0)
        return 0;
    
    // Linear mapping: 0-254 -> 0-255
    return (uint8_t)((hueValue / 254.0f) * 255.0f);
}

void HueGatewayLight::setMinBrightness(uint8_t minBrightness)
{
    if (minBrightness > 100)
    {
        minBrightness = 100;
    }

    _minBrightnessPercent = minBrightness;
    _minBrightnessHue = static_cast<uint8_t>((static_cast<uint16_t>(_minBrightnessPercent) * 254U) / 100U);
}

uint16_t HueGatewayLight::kelvinToMirek(uint16_t kelvin)
{
    // Kelvin -> mirek (micro reciprocal kelvin)
    // mirek = 1.000.000 / Kelvin
    // Hue range: 153-500 mirek (equivalent to 6500K-2000K)
    
    if (kelvin < 2000) kelvin = 2000;
    if (kelvin > 6500) kelvin = 6500;
    
    uint16_t mirek = 1000000 / kelvin;
    
    // Clamp to Hue's valid range
    if (mirek < 153) mirek = 153;  // 6500K
    if (mirek > 500) mirek = 500;  // 2000K
    
    return mirek;
}

void HueGatewayLight::rgbToXy(uint8_t red, uint8_t green, uint8_t blue, float& x, float& y)
{
    float r = red / 255.0f;
    float g = green / 255.0f;
    float b = blue / 255.0f;

    r = (r > 0.04045f) ? powf((r + 0.055f) / 1.055f, 2.4f) : (r / 12.92f);
    g = (g > 0.04045f) ? powf((g + 0.055f) / 1.055f, 2.4f) : (g / 12.92f);
    b = (b > 0.04045f) ? powf((b + 0.055f) / 1.055f, 2.4f) : (b / 12.92f);

    float X = r * 0.664511f + g * 0.154324f + b * 0.162028f;
    float Y = r * 0.283881f + g * 0.668433f + b * 0.047685f;
    float Z = r * 0.000088f + g * 0.072310f + b * 0.986039f;

    float sum = X + Y + Z;
    if (sum <= 0.000001f)
    {
        x = 0.3127f;
        y = 0.3290f;
        return;
    }

    x = X / sum;
    y = Y / sum;
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
    
    bool success = _isGroupedTarget
        ? _client->setGroupedLightStateWithColorTemp(_lightId, _on, _brightness, mirek, fadeDuration)
        : _client->setLightStateWithColorTemp(_lightId, _on, _brightness, mirek, fadeDuration);

    if (!success && _isGroupedTarget)
    {
        Serial.printf("[HueGatewayLight] %s - grouped CT update failed, fallback to state-only\n", _name.c_str());
        success = _client->setGroupedLightState(_lightId, _on, _brightness);
    }

    if (success)
    {
        _lastUpdate = millis();
        sendStatusToKnx();
    }
    else
    {
        Serial.printf("[HueGatewayLight] %s - ERROR: Failed to send color-temp state to Hue\n", _name.c_str());
    }
}

void HueGatewayLight::loop()
{
    // Only run HCL loop when initialized, switched on, and assigned to a valid master.
    if (!_initialized
        || !_on
        || _hclMasterNum == 0
        || _hclMasterNum > 4
        || HCL::masterManager.isApplyBlocked()
        || HCL::masterManager.isMasterApplyBlocked(_hclMasterNum))
        return;
    
    // Read update interval from HCL manager (seconds).
    uint32_t updateIntervalSec = HCL::masterManager.getUpdateInterval();
    unsigned long updateIntervalMs = updateIntervalSec * 1000UL;
    
    // Enforce update interval.
    unsigned long now = millis();
    if (updateIntervalMs > 0 && (now - _lastHCLUpdate) < updateIntervalMs)
        return;
    
    // Fetch current interpolated HCL target values.
    HCL::InterpolatedValue hclValue = HCL::masterManager.getCurrentValue(_hclMasterNum);
    
    // Update when at least a small effective step changed.
    // Keep thresholds low so slow night/day curves continue to fade as configured.
    const int previousKelvin = static_cast<int>(_currentKelvin);
    const int previousBrightness = static_cast<int>(_lastHCLBrightness);
    bool kelvinChanged = abs(static_cast<int>(hclValue.kelvin) - previousKelvin) >= 1;
    bool brightnessChanged = abs(static_cast<int>(hclValue.brightness) - previousBrightness) >= 1;
    
    if (!kelvinChanged && !brightnessChanged)
        return;
    
    // Apply changed values and publish update.
    _lastHCLUpdate = now;
    _lastHCLBrightness = hclValue.brightness;
    
    // Convert brightness percent (0-100) to Hue scale (0-254).
    _brightness = (uint8_t)((hclValue.brightness * 254) / 100);
    
    Serial.printf("[HueGatewayLight] %s - HCL Update: %dK → %dK, %d%% → %d%%\n",
                 _name.c_str(), previousKelvin, hclValue.kelvin,
                 previousBrightness, hclValue.brightness);
    
    // Read transition duration from HCL manager.
    uint8_t fadeDuration = HCL::masterManager.getFadeDuration();
    
    if (_lightType >= 2)
    {
        sendToHueWithColorTemp(hclValue.kelvin, fadeDuration);
    }
    else
    {
        sendToHue();
    }
}
