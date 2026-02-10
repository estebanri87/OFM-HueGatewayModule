#include "HCLMaster.h"
#include <algorithm>

namespace HCL {

Master::Master() {
    // Initialize with default setpoints (all invalid)
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        _setpoints[i] = Setpoint();
        _setpoints[i].timeMinutes = 0xFFFF; // Mark as invalid
    }
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
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        if (_setpoints[i].timeMinutes < 1440) {
            count++;
        }
    }
    return count;
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

bool Master::findInterpolationPoints(uint16_t currentTime, uint8_t& prevIndex, uint8_t& nextIndex) const {
    uint8_t validCount = getValidSetpointCount();
    if (validCount < 2) {
        return false;
    }
    
    // Find first and last valid setpoints
    uint8_t firstValid = 0xFF;
    uint8_t lastValid = 0xFF;
    
    for (uint8_t i = 0; i < MAX_SETPOINTS; i++) {
        if (_setpoints[i].timeMinutes < 1440) {
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
        if (_setpoints[i].timeMinutes >= 1440) {
            continue; // Skip invalid setpoints
        }
        
        if (_setpoints[i].timeMinutes <= currentTime) {
            prevIndex = i;
        } else {
            nextIndex = i;
            break;
        }
    }
    
    // Handle wraparound: if we're after the last setpoint, next is first
    if (currentTime > _setpoints[lastValid].timeMinutes) {
        prevIndex = lastValid;
        nextIndex = firstValid;
    }
    
    return true;
}

InterpolatedValue Master::calculateValue(uint16_t currentTimeMinutes) const {
    InterpolatedValue result;
    
    uint8_t prevIdx, nextIdx;
    if (!findInterpolationPoints(currentTimeMinutes, prevIdx, nextIdx)) {
        // Not enough setpoints - return default
        return result;
    }
    
    const Setpoint& prev = _setpoints[prevIdx];
    const Setpoint& next = _setpoints[nextIdx];
    
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

} // namespace HCL
