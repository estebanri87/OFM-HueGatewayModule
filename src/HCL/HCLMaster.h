#pragma once

#include "HCLSetpoint.h"
#include <Arduino.h>

namespace HCL {

enum class CurveType : uint8_t {
    FixedTime = 0,
    SunPosition = 1,
    Manual = 2,
    Astronomical = 3
};

enum class AdaptiveMode : uint8_t {
    Disabled = 0,
    OpenLoop = 1,   // Proportionale Kompensation: helle Umgebung → weniger Licht
    ClosedLoop = 2  // P-Regler: Umgebungshelligkeit auf Sollwert regeln
};

enum class AdaptiveActiveMode : uint8_t {
    Always = 0,    // Immer aktiv
    DayOnly = 1,   // Nur wenn Tag/Nacht-KO = Tag
    TimeRange = 2  // Nur innerhalb definierter Uhrzeit
};

struct AdaptiveConfig {
    AdaptiveMode mode = AdaptiveMode::Disabled;
    AdaptiveActiveMode activeMode = AdaptiveActiveMode::Always;
    uint8_t strength = 80;             // OpenLoop: Kompensationsstärke 0-100%
    uint16_t maxLux = 1000;            // Skalierungsmaximum in Lux
    uint8_t minBrightness = 5;         // Mindestausgabe 0-100%
    float kp = 1.0f;                   // ClosedLoop: Proportionalfaktor
    uint16_t activeStartMinutes = 360; // TimeRange: Start (Minuten seit Mitternacht)
    uint16_t activeEndMinutes = 1320;  // TimeRange: Ende (Minuten seit Mitternacht)
    bool ceilToHCL = true;             // Ausgabe auf HCL-Wert gedeckelt
    bool dayNightPolarity = false;     // false: KO-Wert 1=Tag; true: KO-Wert 1=Nacht
    uint16_t deadbandLux = 50;         // ClosedLoop: Totband in Lux
    uint8_t sensorTimeoutMinutes = 5;  // Failsafe: Rückfall nach X Minuten (0=deaktiviert)
    uint8_t minChangePercent = 2;      // Mindestschrittgröße in %
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

    void setLocation(float latitudeDeg, float longitudeDeg) {
        _latitudeDeg = constrain(latitudeDeg, -90.0f, 90.0f);
        _longitudeDeg = constrain(longitudeDeg, -180.0f, 180.0f);
    }
    void setTimezoneOffsetMinutes(int16_t timezoneOffsetMin) {
        _timezoneOffsetMin = constrain(timezoneOffsetMin, static_cast<int16_t>(-720), static_cast<int16_t>(840));
    }
    void setAstronomicalProfile(uint16_t minKelvin, uint16_t maxKelvin, uint8_t minBrightness, uint8_t maxBrightness) {
        _astroMinKelvin = constrain(minKelvin, 2000, 6500);
        _astroMaxKelvin = constrain(maxKelvin, 2000, 6500);
        if (_astroMaxKelvin < _astroMinKelvin) {
            const uint16_t temp = _astroMinKelvin;
            _astroMinKelvin = _astroMaxKelvin;
            _astroMaxKelvin = temp;
        }

        _astroMinBrightness = constrain(minBrightness, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
        _astroMaxBrightness = constrain(maxBrightness, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
        if (_astroMaxBrightness < _astroMinBrightness) {
            const uint8_t temp = _astroMinBrightness;
            _astroMinBrightness = _astroMaxBrightness;
            _astroMaxBrightness = temp;
        }
    }

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

    // --- Adaptive Helligkeit ---
    void setAdaptiveConfig(const AdaptiveConfig& config) { _adaptiveConfig = config; }
    const AdaptiveConfig& getAdaptiveConfig() const { return _adaptiveConfig; }
    void setAmbientLux(float lux);
    void setDaytime(bool isDaytime) { _isDaytime = isDaytime; }
    bool isAdaptiveCurrentlyActive(uint16_t currentTimeMinutes, uint32_t nowMs) const;
    
    /**
     * @brief Calculate interpolated value for current time
     * @param currentTimeMinutes Current time in minutes since midnight
     * @return Interpolated color temperature and brightness
     */
    InterpolatedValue calculateValue(uint16_t currentTimeMinutes, uint32_t currentTimeMs, int16_t dayOfYear = -1);
    
    /**
     * @brief Get number of valid setpoints
     * @return Number of setpoints with valid time
     */
    uint8_t getValidSetpointCount() const;
    
    /**
     * @brief Sort setpoints by time (ascending)
     */
    void sortSetpoints();

    bool setSummerSetpoint(uint8_t index, const Setpoint& setpoint);
    void sortSummerSetpoints();
    void setIsSummer(bool isSummer) { _isSummer = isSummer; }
    
    /**
     * @brief Check whether the configured curve can produce valid output
     */
    bool isValid() const {
        switch (_curveType) {
            case CurveType::Manual:
            case CurveType::Astronomical:
                return true;
            case CurveType::SunPosition:
            case CurveType::FixedTime:
            default:
                return getValidSetpointCount() >= 2;
        }
    }
    
private:
    Setpoint _setpoints[MAX_SETPOINTS];
    Setpoint _setpointsSummer[MAX_SETPOINTS];
    bool _hasSummerSetpoints;
    bool _isSummer;
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
    float _latitudeDeg;
    float _longitudeDeg;
    int16_t _timezoneOffsetMin;
    uint16_t _astroMinKelvin;
    uint16_t _astroMaxKelvin;
    uint8_t _astroMinBrightness;
    uint8_t _astroMaxBrightness;

    // --- Adaptive Helligkeit ---
    AdaptiveConfig _adaptiveConfig;
    float _ambientLux = 0.0f;
    bool _isDaytime = true;
    uint32_t _lastLuxReceiveMs = 0;
    uint8_t _lastSentBrightness = 255; // 255 = noch kein Wert gesendet
    float _luxFilterBuffer[3] = {0.0f, 0.0f, 0.0f};
    uint8_t _luxFilterIndex = 0;
    
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
    static uint8_t countValidInArray(const Setpoint* arr);
    bool findInterpolationPoints(const Setpoint* arr, uint16_t currentTime, uint8_t& prevIndex, uint8_t& nextIndex) const;
    InterpolatedValue calculateFixedTimeValue(uint16_t currentTimeMinutes) const;
    InterpolatedValue calculateSunPositionValue(uint16_t currentTimeMinutes) const;
    InterpolatedValue calculateManualValue(uint16_t currentTimeMinutes) const;
    InterpolatedValue calculateAstronomicalValue(uint16_t currentTimeMinutes, int16_t dayOfYear) const;
    void applySlew(uint16_t targetKelvin, uint32_t currentTimeMs);
    void getSetpointRanges(const Setpoint* arr, uint16_t& minKelvin, uint16_t& maxKelvin, uint8_t& minBrightness, uint8_t& maxBrightness) const;

    // --- Adaptive Helligkeit (privat) ---
    bool isSensorValid(uint32_t nowMs) const;
    float getFilteredLux() const;
    InterpolatedValue applyAdaptiveBrightness(InterpolatedValue val, uint16_t currentTimeMinutes, uint32_t nowMs);
};

} // namespace HCL
