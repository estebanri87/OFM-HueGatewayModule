#pragma once

#include "HCLSetpoint.h"
#include <Arduino.h>

namespace HCL {

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
    
    /**
     * @brief Calculate interpolated value for current time
     * @param currentTimeMinutes Current time in minutes since midnight
     * @return Interpolated color temperature and brightness
     */
    InterpolatedValue calculateValue(uint16_t currentTimeMinutes) const;
    
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
};

} // namespace HCL
