#include "HCLMaster.h"
#include <algorithm>
#include <cmath>

namespace HCL {

Master::Master() {
    // Initialize with default setpoints (all invalid)
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        _setpoints[i] = Setpoint();
        _setpoints[i].timeMinutes = 0xFFFF; // Mark as invalid
        _setpointsSummer[i] = Setpoint();
        _setpointsSummer[i].timeMinutes = 0xFFFF; // Mark as invalid
    }

    _hasSummerSetpoints = false;
    _isSummer = false;

    _curveType = CurveType::FixedTime;
    _manualKelvin = 4000;
    _appliedKelvin = 4000;
    _slewRateKelvinPerMinute = 0;
    _lastSlewUpdateMs = 0;
    _sunriseMinutes = 390; // 06:30
    _sunsetMinutes = 1170; // 19:30
    _sunTimesValid = false;
    _sunriseOffsetMin = 0;
    _sunsetOffsetMin = 0;
    _latitudeDeg = 50.0f;
    _longitudeDeg = 8.0f;
    _timezoneOffsetMin = 60;
    _astroMinKelvin = 2400;
    _astroMaxKelvin = 5000;
    _astroMinBrightness = 10;
    _astroMaxBrightness = 80;
}

void Master::setSunTimes(uint16_t sunriseMinutes, uint16_t sunsetMinutes) {
    if (sunriseMinutes >= 1440 || sunsetMinutes >= 1440) {
        return;
    }

    _sunriseMinutes = sunriseMinutes;
    _sunsetMinutes = sunsetMinutes;
    _sunTimesValid = true;
}

bool Master::setSetpoint(uint8_t index, const Setpoint& setpoint) {
    if (index >= MAX_SETPOINTS) {
        return false;
    }
    
    _setpoints[index] = setpoint;
    return true;
}

const Setpoint* Master::getSetpoint(uint8_t index) const {
    if (index >= MAX_SETPOINTS) {
        return nullptr;
    }
    
    return &_setpoints[index];
}

uint8_t Master::getValidSetpointCount() const {
    return countValidInArray(_setpoints);
}

void Master::sortSetpoints() {
    // Simple bubble sort (good enough for 10 elements)
    for (uint8_t i = 0; i < MAX_SETPOINTS - 1; i++) {
        for (uint8_t j = 0; j < MAX_SETPOINTS - i - 1; j++) {
            if (_setpoints[j].timeMinutes > _setpoints[j + 1].timeMinutes) {
                Setpoint temp = _setpoints[j];
                _setpoints[j] = _setpoints[j + 1];
                _setpoints[j + 1] = temp;
            }
        }
    }
}

bool Master::setSummerSetpoint(uint8_t index, const Setpoint& setpoint) {
    if (index >= MAX_SETPOINTS) {
        return false;
    }
    _setpointsSummer[index] = setpoint;
    if (setpoint.kelvin != 0xFFFF && setpoint.timeMinutes < 1440) {
        _hasSummerSetpoints = true;
    }
    return true;
}

void Master::sortSummerSetpoints() {
    for (uint8_t i = 0; i < MAX_SETPOINTS - 1; i++) {
        for (uint8_t j = 0; j < MAX_SETPOINTS - i - 1; j++) {
            if (_setpointsSummer[j].timeMinutes > _setpointsSummer[j + 1].timeMinutes) {
                Setpoint temp = _setpointsSummer[j];
                _setpointsSummer[j] = _setpointsSummer[j + 1];
                _setpointsSummer[j + 1] = temp;
            }
        }
    }
}

bool Master::findInterpolationPoints(const Setpoint* arr, uint16_t currentTime, uint8_t& prevIndex, uint8_t& nextIndex) const {
    uint8_t validCount = countValidInArray(arr);
    if (validCount < 2) {
        return false;
    }
    
    // Find first and last valid setpoints
    uint8_t firstValid = 0xFF;
    uint8_t lastValid = 0xFF;
    
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        if (arr[i].timeMinutes < 1440) {
            if (firstValid == 0xFF) {
                firstValid = i;
            }
            lastValid = i;
        }
    }
    
    // Find the two setpoints to interpolate between
    prevIndex = lastValid;  // Default to last point (wrap around)
    nextIndex = firstValid;
    
    for (uint8_t i = firstValid; i < MAX_SETPOINTS; i++) {
        if (arr[i].timeMinutes >= 1440) {
            continue; // Skip invalid setpoints
        }
        
        if (arr[i].timeMinutes <= currentTime) {
            prevIndex = i;
        } else {
            nextIndex = i;
            break;
        }
    }
    
    // Handle wraparound: if we're after the last setpoint, next is first
    if (currentTime > arr[lastValid].timeMinutes) {
        prevIndex = lastValid;
        nextIndex = firstValid;
    }
    
    return true;
}

InterpolatedValue Master::calculateFixedTimeValue(uint16_t currentTimeMinutes) const {
    InterpolatedValue result;
    
    const Setpoint* arr = (_isSummer && _hasSummerSetpoints) ? _setpointsSummer : _setpoints;

    uint8_t prevIdx, nextIdx;
    if (!findInterpolationPoints(arr, currentTimeMinutes, prevIdx, nextIdx)) {
        // Not enough setpoints - return default
        return result;
    }
    
    const Setpoint& prev = arr[prevIdx];
    const Setpoint& next = arr[nextIdx];
    
    // Calculate time difference and interpolation factor
    int32_t timeDiff;
    int32_t timeFromPrev;
    
    if (next.timeMinutes > prev.timeMinutes) {
        // Normal case: no wraparound
        timeDiff = next.timeMinutes - prev.timeMinutes;
        timeFromPrev = currentTimeMinutes - prev.timeMinutes;
    } else {
        // Wraparound case: next is after midnight
        timeDiff = (1440 - prev.timeMinutes) + next.timeMinutes;
        if (currentTimeMinutes >= prev.timeMinutes) {
            timeFromPrev = currentTimeMinutes - prev.timeMinutes;
        } else {
            timeFromPrev = (1440 - prev.timeMinutes) + currentTimeMinutes;
        }
    }
    
    // Calculate interpolation factor (0.0 to 1.0)
    float t = 0.0f;
    if (timeDiff > 0) {
        t = (float)timeFromPrev / (float)timeDiff;
        t = constrain(t, 0.0f, 1.0f);
    }
    
    // Linear interpolation
    result.kelvin = (uint16_t)lerp(prev.kelvin, next.kelvin, t);
    result.brightness = (uint8_t)lerp(prev.brightness, next.brightness, t);
    
    // Clamp values to valid ranges
    result.kelvin = constrain(result.kelvin, 2000, 6500);
    result.brightness = constrain(result.brightness, 0, 100);
    
    return result;
}

void Master::getSetpointRanges(const Setpoint* arr, uint16_t& minKelvin, uint16_t& maxKelvin, uint8_t& minBrightness, uint8_t& maxBrightness) const {
    minKelvin = 2700;
    maxKelvin = 6500;
    minBrightness = 0;
    maxBrightness = 100;

    bool found = false;
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        if (arr[i].timeMinutes >= 1440) {
            continue;
        }

        if (!found) {
            minKelvin = maxKelvin = arr[i].kelvin;
            minBrightness = maxBrightness = arr[i].brightness;
            found = true;
            continue;
        }

        minKelvin = min(minKelvin, arr[i].kelvin);
        maxKelvin = max(maxKelvin, arr[i].kelvin);
        minBrightness = min(minBrightness, arr[i].brightness);
        maxBrightness = max(maxBrightness, arr[i].brightness);
    }
}

uint8_t Master::countValidInArray(const Setpoint* arr) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        if (arr[i].timeMinutes < 1440) {
            count++;
        }
    }
    return count;
}

InterpolatedValue Master::calculateSunPositionValue(uint16_t currentTimeMinutes) const {
    InterpolatedValue result = calculateFixedTimeValue(currentTimeMinutes);

    uint16_t minKelvin;
    uint16_t maxKelvin;
    uint8_t minBrightness;
    uint8_t maxBrightness;
    const Setpoint* arr = (_isSummer && _hasSummerSetpoints) ? _setpointsSummer : _setpoints;
    getSetpointRanges(arr, minKelvin, maxKelvin, minBrightness, maxBrightness);

    int32_t sunrise = static_cast<int32_t>(_sunriseMinutes) + _sunriseOffsetMin;
    int32_t sunset = static_cast<int32_t>(_sunsetMinutes) + _sunsetOffsetMin;
    if (sunrise < 0) sunrise = 0;
    if (sunrise > 1439) sunrise = 1439;
    if (sunset < 0) sunset = 0;
    if (sunset > 1439) sunset = 1439;

    if (sunset <= sunrise) {
        result.kelvin = minKelvin;
        result.brightness = minBrightness;
        return result;
    }

    if (currentTimeMinutes < sunrise || currentTimeMinutes > sunset) {
        result.kelvin = minKelvin;
        result.brightness = minBrightness;
        return result;
    }

    uint16_t midpoint = static_cast<uint16_t>(sunrise + ((sunset - sunrise) / 2));
    uint16_t kelvinRange = maxKelvin - minKelvin;
    uint8_t brightnessRange = maxBrightness - minBrightness;

    if (currentTimeMinutes <= midpoint) {
        uint16_t riseDuration = max<uint16_t>(1, midpoint - sunrise);
        uint16_t elapsed = currentTimeMinutes - sunrise;
        result.kelvin = minKelvin + static_cast<uint16_t>((static_cast<uint32_t>(kelvinRange) * elapsed) / riseDuration);
        result.brightness = minBrightness + static_cast<uint8_t>((static_cast<uint32_t>(brightnessRange) * elapsed) / riseDuration);
    } else {
        uint16_t fallDuration = max<uint16_t>(1, sunset - midpoint);
        uint16_t elapsed = currentTimeMinutes - midpoint;
        result.kelvin = maxKelvin - static_cast<uint16_t>((static_cast<uint32_t>(kelvinRange) * elapsed) / fallDuration);
        result.brightness = maxBrightness - static_cast<uint8_t>((static_cast<uint32_t>(brightnessRange) * elapsed) / fallDuration);
    }

    result.kelvin = constrain(result.kelvin, 2000, 6500);
    result.brightness = constrain(result.brightness, 0, 100);
    return result;
}

InterpolatedValue Master::calculateManualValue(uint16_t currentTimeMinutes) const {
    InterpolatedValue result = calculateFixedTimeValue(currentTimeMinutes);
    result.kelvin = constrain(_manualKelvin, 2000, 6500);
    return result;
}

InterpolatedValue Master::calculateAstronomicalValue(uint16_t currentTimeMinutes, int16_t dayOfYear) const {
    if (dayOfYear < 1 || dayOfYear > 366) {
        dayOfYear = 1;
    }

    InterpolatedValue result;

    constexpr float pi = 3.14159265358979323846f;
    const float fractionalHour = static_cast<float>(currentTimeMinutes) / 60.0f;
    const float gamma = (2.0f * pi / 365.0f) *
        (static_cast<float>(dayOfYear) - 1.0f + ((fractionalHour - 12.0f) / 24.0f));

    const float eqTime = 229.18f * (
        0.000075f
        + 0.001868f * cosf(gamma)
        - 0.032077f * sinf(gamma)
        - 0.014615f * cosf(2.0f * gamma)
        - 0.040849f * sinf(2.0f * gamma));

    const float decl =
        0.006918f
        - 0.399912f * cosf(gamma)
        + 0.070257f * sinf(gamma)
        - 0.006758f * cosf(2.0f * gamma)
        + 0.000907f * sinf(2.0f * gamma)
        - 0.002697f * cosf(3.0f * gamma)
        + 0.001480f * sinf(3.0f * gamma);

    const float timeOffset = eqTime + (4.0f * _longitudeDeg) - static_cast<float>(_timezoneOffsetMin);
    float trueSolarTime = static_cast<float>(currentTimeMinutes) + timeOffset;
    while (trueSolarTime < 0.0f) {
        trueSolarTime += 1440.0f;
    }
    while (trueSolarTime >= 1440.0f) {
        trueSolarTime -= 1440.0f;
    }

    const float hourAngleDeg = (trueSolarTime / 4.0f) - 180.0f;
    const float hourAngle = hourAngleDeg * (pi / 180.0f);
    const float latRad = _latitudeDeg * (pi / 180.0f);

    float cosZenith =
        sinf(latRad) * sinf(decl)
        + cosf(latRad) * cosf(decl) * cosf(hourAngle);
    cosZenith = constrain(cosZenith, -1.0f, 1.0f);

    const float zenith = acosf(cosZenith);
    const float elevationDeg = 90.0f - (zenith * (180.0f / pi));

    // Map solar elevation to a smooth day profile: -6°..60° => 0..1
    float t = (elevationDeg + 6.0f) / 66.0f;
    t = constrain(t, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);

    const uint16_t kelvinRange = _astroMaxKelvin - _astroMinKelvin;
    const uint8_t brightnessRange = _astroMaxBrightness - _astroMinBrightness;

    result.kelvin = _astroMinKelvin + static_cast<uint16_t>(static_cast<float>(kelvinRange) * smooth);
    result.brightness = _astroMinBrightness + static_cast<uint8_t>(static_cast<float>(brightnessRange) * smooth);

    result.kelvin = constrain(result.kelvin, 2000, 6500);
    result.brightness = constrain(result.brightness, 0, 100);
    return result;
}

void Master::applySlew(uint16_t targetKelvin, uint32_t currentTimeMs) {
    if (_appliedKelvin < 2000 || _appliedKelvin > 6500) {
        _appliedKelvin = constrain(targetKelvin, 2000, 6500);
        _lastSlewUpdateMs = currentTimeMs;
        return;
    }

    // First slew update after boot/re-init:
    // start exactly at the current curve target, not at constructor default.
    if (_lastSlewUpdateMs == 0) {
        _appliedKelvin = constrain(targetKelvin, 2000, 6500);
        _lastSlewUpdateMs = currentTimeMs;
        return;
    }

    if (_slewRateKelvinPerMinute == 0) {
        _appliedKelvin = constrain(targetKelvin, 2000, 6500);
        _lastSlewUpdateMs = currentTimeMs;
        return;
    }

    uint32_t deltaMs = currentTimeMs - _lastSlewUpdateMs;
    _lastSlewUpdateMs = currentTimeMs;
    if (deltaMs == 0) {
        return;
    }

    uint32_t maxStep = (static_cast<uint32_t>(_slewRateKelvinPerMinute) * deltaMs) / 60000UL;
    if (maxStep == 0) {
        maxStep = 1;
    }

    if (_appliedKelvin < targetKelvin) {
        uint32_t nextKelvin = static_cast<uint32_t>(_appliedKelvin) + maxStep;
        _appliedKelvin = static_cast<uint16_t>(min<uint32_t>(targetKelvin, nextKelvin));
    } else if (_appliedKelvin > targetKelvin) {
        uint32_t currentKelvin = _appliedKelvin;
        uint32_t nextKelvin = (currentKelvin > maxStep) ? (currentKelvin - maxStep) : 0;
        _appliedKelvin = static_cast<uint16_t>(max<uint32_t>(targetKelvin, nextKelvin));
    }

    _appliedKelvin = constrain(_appliedKelvin, 2000, 6500);
}

InterpolatedValue Master::calculateValue(uint16_t currentTimeMinutes, uint32_t currentTimeMs, int16_t dayOfYear) {
    InterpolatedValue target;

    switch (_curveType) {
        case CurveType::SunPosition:
            target = calculateSunPositionValue(currentTimeMinutes);
            break;
        case CurveType::Astronomical:
            target = calculateAstronomicalValue(currentTimeMinutes, dayOfYear);
            break;
        case CurveType::Manual:
            target = calculateManualValue(currentTimeMinutes);
            break;
        case CurveType::FixedTime:
        default:
            target = calculateFixedTimeValue(currentTimeMinutes);
            break;
    }

    applySlew(target.kelvin, currentTimeMs);
    target.kelvin = _appliedKelvin;
    return applyAdaptiveBrightness(target, currentTimeMinutes, currentTimeMs);
}

void Master::setAmbientLux(float lux) {
    if (lux < 0.0f) lux = 0.0f;
    _luxFilterBuffer[_luxFilterIndex] = lux;
    _luxFilterIndex = (_luxFilterIndex + 1) % 3;
    _ambientLux = lux;
    _lastLuxReceiveMs = millis();
}

bool Master::isSensorValid(uint32_t nowMs) const {
    if (_adaptiveConfig.sensorTimeoutMinutes == 0) return true;
    if (_lastLuxReceiveMs == 0) return false;
    uint32_t timeoutMs = static_cast<uint32_t>(_adaptiveConfig.sensorTimeoutMinutes) * 60000UL;
    return (nowMs - _lastLuxReceiveMs) < timeoutMs;
}

float Master::getFilteredLux() const {
    float sum = 0.0f;
    for (uint8_t i = 0; i < 3; i++) {
        sum += _luxFilterBuffer[i];
    }
    return sum / 3.0f;
}

bool Master::isAdaptiveCurrentlyActive(uint16_t currentTimeMinutes, uint32_t nowMs) const {
    if (_adaptiveConfig.mode == AdaptiveMode::Disabled) return false;
    if (!isSensorValid(nowMs)) return false;

    switch (_adaptiveConfig.activeMode) {
        case AdaptiveActiveMode::Always:
            return true;
        case AdaptiveActiveMode::DayOnly: {
            bool isDay = _adaptiveConfig.dayNightPolarity ? !_isDaytime : _isDaytime;
            return isDay;
        }
        case AdaptiveActiveMode::TimeRange: {
            uint16_t s = _adaptiveConfig.activeStartMinutes;
            uint16_t e = _adaptiveConfig.activeEndMinutes;
            if (s <= e) {
                return currentTimeMinutes >= s && currentTimeMinutes < e;
            } else {
                // Mitternachts-Wrap
                return currentTimeMinutes >= s || currentTimeMinutes < e;
            }
        }
        default:
            return false;
    }
}

InterpolatedValue Master::applyAdaptiveBrightness(InterpolatedValue val, uint16_t currentTimeMinutes, uint32_t nowMs) {
    if (!isAdaptiveCurrentlyActive(currentTimeMinutes, nowMs)) {
        _lastSentBrightness = val.brightness;
        return val;
    }

    float filteredLux = getFilteredLux();
    uint8_t output = val.brightness;

    if (_adaptiveConfig.mode == AdaptiveMode::OpenLoop) {
        float maxLux = static_cast<float>(_adaptiveConfig.maxLux);
        if (maxLux < 1.0f) maxLux = 1.0f;
        float ambientNorm = filteredLux / maxLux;
        if (ambientNorm > 1.0f) ambientNorm = 1.0f;
        float raw = static_cast<float>(val.brightness) * (1.0f - ambientNorm * (_adaptiveConfig.strength / 100.0f));
        output = static_cast<uint8_t>(constrain(static_cast<int>(raw + 0.5f), _adaptiveConfig.minBrightness, 100));

    } else if (_adaptiveConfig.mode == AdaptiveMode::ClosedLoop) {
        float maxLux = static_cast<float>(_adaptiveConfig.maxLux);
        if (maxLux < 1.0f) maxLux = 1.0f;
        float targetLux = (static_cast<float>(val.brightness) / 100.0f) * maxLux;

        float error = filteredLux - targetLux;
        if (error < 0.0f) error = -error;

        if (error < static_cast<float>(_adaptiveConfig.deadbandLux)) {
            // Totband: kein Update
            if (_lastSentBrightness == 255) {
                _lastSentBrightness = val.brightness;
            }
            return {val.kelvin, _lastSentBrightness};
        }

        float errorNorm = (targetLux - filteredLux) / maxLux;
        float raw = static_cast<float>(val.brightness) + _adaptiveConfig.kp * errorNorm * 100.0f;
        uint8_t ceiling = _adaptiveConfig.ceilToHCL ? val.brightness : 100;
        output = static_cast<uint8_t>(constrain(static_cast<int>(raw + 0.5f), _adaptiveConfig.minBrightness, ceiling));
    }

    // Mindestschrittgröße
    if (_lastSentBrightness != 255) {
        uint8_t diff = (output > _lastSentBrightness) ? (output - _lastSentBrightness) : (_lastSentBrightness - output);
        if (diff < _adaptiveConfig.minChangePercent) {
            return {val.kelvin, _lastSentBrightness};
        }
    }

    _lastSentBrightness = output;
    return {val.kelvin, output};
}

} // namespace HCL
