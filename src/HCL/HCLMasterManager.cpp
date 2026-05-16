#include "HCLMasterManager.h"

namespace HCL {

// Global instance
MasterManager masterManager;

MasterManager::MasterManager()
    : _enabled(false)
    , _applyBlocked(false)
    , _updateIntervalMs(60000)  // Default: 60 seconds
    , _fadeDurationSec(5)       // Default: 5 seconds
    , _lastUpdateMs(0)
    , _lastTimeMinutes(0xFFFF)
    , _lastDayOfYear(-1)
{
    // Initialize current values to defaults
    for (uint8_t i = 0; i < MAX_MASTERS; i++) {
        _currentValues[i] = InterpolatedValue(4000, 100);
        _masterApplyBlocked[i] = false;
    }
}

void MasterManager::setup() {
    _lastUpdateMs = millis();
    _lastTimeMinutes = 0xFFFF;  // Force initial update
    _lastDayOfYear = -1;
}

void MasterManager::loop(uint16_t currentTimeMinutes, int16_t dayOfYear) {
    if (!_enabled) {
        return;
    }
    
    uint32_t now = millis();
    
    // Check if it's time to update
    if ((now - _lastUpdateMs) >= _updateIntervalMs || _lastTimeMinutes != currentTimeMinutes || _lastDayOfYear != dayOfYear) {
        updateCurrentValues(currentTimeMinutes, dayOfYear);
        _lastUpdateMs = now;
        _lastTimeMinutes = currentTimeMinutes;
        _lastDayOfYear = dayOfYear;
    }
}

Master* MasterManager::getMaster(uint8_t masterNum) {
    if (masterNum < 1 || masterNum > MAX_MASTERS) {
        return nullptr;
    }
    
    return &_masters[masterNum - 1];
}

InterpolatedValue MasterManager::getCurrentValue(uint8_t masterNum) const {
    if (masterNum < 1 || masterNum > MAX_MASTERS) {
        return InterpolatedValue();  // Return default
    }
    
    return _currentValues[masterNum - 1];
}

void MasterManager::setMasterApplyBlocked(uint8_t masterNum, bool blocked)
{
    if (masterNum < 1 || masterNum > MAX_MASTERS)
    {
        return;
    }

    _masterApplyBlocked[masterNum - 1] = blocked;
}

bool MasterManager::isMasterApplyBlocked(uint8_t masterNum) const
{
    if (masterNum < 1 || masterNum > MAX_MASTERS)
    {
        return false;
    }

    return _masterApplyBlocked[masterNum - 1];
}

void MasterManager::forceUpdate() {
    _lastUpdateMs = 0;  // Force next loop() call to update
}

uint32_t MasterManager::getTimeUntilNextUpdate() const {
    uint32_t now = millis();
    uint32_t elapsed = now - _lastUpdateMs;
    
    if (elapsed >= _updateIntervalMs) {
        return 0;
    }
    
    return _updateIntervalMs - elapsed;
}

void MasterManager::updateCurrentValues(uint16_t currentTimeMinutes, int16_t dayOfYear) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < MAX_MASTERS; i++) {
        if (_masters[i].isValid()) {
            _currentValues[i] = _masters[i].calculateValue(currentTimeMinutes, now, dayOfYear);
            
            #ifdef DEBUG_HCL
            Serial.printf("[HCL] Master %d: %dK, %d%%\n", 
                i + 1, _currentValues[i].kelvin, _currentValues[i].brightness);
            #endif
        }
    }
}

void MasterManager::setMasterAmbientLux(uint8_t masterNum, float lux) {
    if (masterNum < 1 || masterNum > MAX_MASTERS) return;
    _masters[masterNum - 1].setAmbientLux(lux);
    forceUpdate();
}

void MasterManager::setMasterDaytime(uint8_t masterNum, bool isDaytime) {
    if (masterNum < 1 || masterNum > MAX_MASTERS) return;
    _masters[masterNum - 1].setDaytime(isDaytime);
}

bool MasterManager::isMasterAdaptiveActive(uint8_t masterNum) const {
    if (masterNum < 1 || masterNum > MAX_MASTERS) return false;
    uint32_t now = millis();
    // Aktuelle Uhrzeit nicht vorhanden → konservativ aus _lastTimeMinutes lesen
    return _masters[masterNum - 1].isAdaptiveCurrentlyActive(_lastTimeMinutes, now);
}

} // namespace HCL
