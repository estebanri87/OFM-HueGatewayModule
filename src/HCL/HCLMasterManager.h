#pragma once

#include "HCLMaster.h"
#include <Arduino.h>

namespace HCL {

/**
 * @brief Manages up to 8 HCL Masters and provides continuous updates
 * 
 * This class:
 * - Manages up to 8 HCL Master instances
 * - Calculates current values based on time
 * - Provides continuous fade updates for lights
 * - Handles immediate switch-on with correct HCL values
 */
class MasterManager {
public:
    static constexpr uint8_t MAX_MASTERS = 8;
    
    /**
     * @brief Constructor
     */
    MasterManager();
    
    /**
     * @brief Initialize the manager
     */
    void setup();
    
    /**
     * @brief Update loop - call regularly (e.g. every second)
     * @param currentTimeMinutes Current time in minutes since midnight
     */
    void loop(uint16_t currentTimeMinutes, int16_t dayOfYear = -1);
    
    /**
    * @brief Get a HCL Master by index (1-8)
    * @param masterNum Master number (1-8)
     * @return Pointer to master or nullptr if invalid
     */
    Master* getMaster(uint8_t masterNum);
    
    /**
     * @brief Get current interpolated value for a master
    * @param masterNum Master number (1-8)
     * @return Current interpolated value
     */
    InterpolatedValue getCurrentValue(uint8_t masterNum) const;
    
    /**
     * @brief Check if HCL is enabled globally
     */
    bool isEnabled() const { return _enabled; }
    
    /**
     * @brief Enable or disable HCL
     */
    void setEnabled(bool enabled) { _enabled = enabled; }

    /**
     * @brief Block or allow applying HCL values to lights.
     *
     * When blocked, the manager still keeps calculating current values
     * but channel light loops can skip applying them.
     */
    void setApplyBlocked(bool blocked) { _applyBlocked = blocked; }

    /**
     * @brief Returns whether applying HCL values is currently blocked.
     */
    bool isApplyBlocked() const { return _applyBlocked; }

    /**
     * @brief Block or allow applying HCL values for a specific master.
    * @param masterNum Master number (1-8)
     * @param blocked True to block HCL apply for this master
     */
    void setMasterApplyBlocked(uint8_t masterNum, bool blocked);

    /**
     * @brief Returns whether applying HCL values is blocked for a specific master.
    * @param masterNum Master number (1-8)
     */
    bool isMasterApplyBlocked(uint8_t masterNum) const;
    
    /**
     * @brief Set update interval in seconds
     */
    void setUpdateInterval(uint16_t seconds) { _updateIntervalMs = seconds * 1000; }
    
    /**
     * @brief Get update interval in seconds
     */
    uint16_t getUpdateInterval() const { return _updateIntervalMs / 1000; }
    
    /**
     * @brief Set fade duration in seconds
     */
    void setFadeDuration(uint8_t seconds) { _fadeDurationSec = seconds; }
    
    /**
     * @brief Get fade duration in seconds
     */
    uint8_t getFadeDuration() const { return _fadeDurationSec; }
    
    /**
     * @brief Force immediate recalculation of all values
     */
    void forceUpdate();
    
    /**
     * @brief Get time until next update in milliseconds
     */
    uint32_t getTimeUntilNextUpdate() const;

    // --- Adaptive Helligkeit ---
    void setMasterAmbientLux(uint8_t masterNum, float lux);
    void setMasterDaytime(uint8_t masterNum, bool isDaytime);
    bool isMasterAdaptiveActive(uint8_t masterNum) const;
    
private:
    Master _masters[MAX_MASTERS];
    InterpolatedValue _currentValues[MAX_MASTERS];
    bool _enabled;
    bool _applyBlocked;
    bool _masterApplyBlocked[MAX_MASTERS];
    uint16_t _updateIntervalMs;  // Update interval in milliseconds
    uint8_t _fadeDurationSec;    // Fade duration in seconds
    uint32_t _lastUpdateMs;      // Last update timestamp
    uint16_t _lastTimeMinutes;   // Last calculated time
    int16_t _lastDayOfYear;      // Last day-of-year used for calculation
    
    /**
     * @brief Update all current values
     */
    void updateCurrentValues(uint16_t currentTimeMinutes, int16_t dayOfYear);
};

// Global instance
extern MasterManager masterManager;

} // namespace HCL
