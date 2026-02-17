#pragma once

#include "HCLSetpoint.h"
#include <Arduino.h>

namespace HCL {

enum class CurveType : uint8_t {
    FixedTime = 0,
    SunPosition = 1,
    Manual = 2
};

/**
 * @brief Manages a single HCL Master with up to 10 setpoints
 * 
 * Provides linear interpolation between setpoints based on current time.
 * Handles wraparound at midnight (23:59 -> 00:00).
 */
class Master {
public:
    static constexpr uint8_t MAX_SETPOINTS = 10;
    
    /**
     * @brief Constructor
     */
    Master();
    
    /**
     * @brief Set a setpoint at a specific index
     * @param index Setpoint index (0-9)
     * @param setpoint The setpoint to set
     * @return true if successful, false if index out of range
     */
    bool setSetpoint(uint8_t index, const Setpoint& setpoint);
    
    /**
     * @brief Get a setpoint at a specific index
     * @param index Setpoint index (0-9)
     * @return Pointer to setpoint or nullptr if index out of range
     */
    const Setpoint* getSetpoint(uint8_t index) const;

    void setCurveType(CurveType curveType) { _curveType = curveType; }
    CurveType getCurveType() const { return _curveType; }

    void setManualKelvin(uint16_t kelvin) { _manualKelvin = constrain(kelvin, 2000, 6500); }
    uint16_t getManualKelvin() const { return _manualKelvin; }

    void setSunTimes(uint16_t sunriseMinutes, uint16_t sunsetMinutes);
    void clearSunTimes() { _sunTimesValid = false; }
    bool hasSunTimes() const { return _sunTimesValid; }

    void setSunOffsets(int16_t sunriseOffsetMin, int16_t sunsetOffsetMin) {
        _sunriseOffsetMin = sunriseOffsetMin;
        _sunsetOffsetMin = sunsetOffsetMin;
    }

    void setSlewRateKelvinPerMinute(uint16_t kelvinPerMinute) { _slewRateKelvinPerMinute = kelvinPerMinute; }
    uint16_t getSlewRateKelvinPerMinute() const { return _slewRateKelvinPerMinute; }
    uint16_t getAppliedKelvin() const { return _appliedKelvin; }
    
    /**
     * @brief Calculate interpolated value for current time
     * @param currentTimeMinutes Current time in minutes since midnight
     * @return Interpolated color temperature and brightness
     */
    InterpolatedValue calculateValue(uint16_t currentTimeMinutes, uint32_t currentTimeMs);
    
    /**
     * @brief Get number of valid setpoints
     * @return Number of setpoints with valid time
     */
    uint8_t getValidSetpointCount() const;
    
    /**
     * @brief Sort setpoints by time (ascending)
     */
    void sortSetpoints();
    
    /**
     * @brief Check if master has at least 2 valid setpoints
     */
    bool isValid() const {
        return getValidSetpointCount() >= 2;
    }
    
private:
    Setpoint _setpoints[MAX_SETPOINTS];
    CurveType _curveType;
    uint16_t _manualKelvin;
    uint16_t _appliedKelvin;
    uint16_t _slewRateKelvinPerMinute;
    uint32_t _lastSlewUpdateMs;
    uint16_t _sunriseMinutes;
    uint16_t _sunsetMinutes;
    bool _sunTimesValid;
    int16_t _sunriseOffsetMin;
    int16_t _sunsetOffsetMin;
    
    /**
     * @brief Linear interpolation between two values
     */
    static inline float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
    
    /**
     * @brief Find the two setpoints to interpolate between
     * @param currentTime Current time in minutes
     * @param prevIndex Output: index of previous setpoint
     * @param nextIndex Output: index of next setpoint
     * @return true if found, false if not enough setpoints
     */
    bool findInterpolationPoints(uint16_t currentTime, uint8_t& prevIndex, uint8_t& nextIndex) const;
    InterpolatedValue calculateFixedTimeValue(uint16_t currentTimeMinutes) const;
    InterpolatedValue calculateSunPositionValue(uint16_t currentTimeMinutes) const;
    InterpolatedValue calculateManualValue(uint16_t currentTimeMinutes) const;
    void applySlew(uint16_t targetKelvin, uint32_t currentTimeMs);
    void getSetpointRanges(uint16_t& minKelvin, uint16_t& maxKelvin, uint8_t& minBrightness, uint8_t& maxBrightness) const;
};

} // namespace HCL
