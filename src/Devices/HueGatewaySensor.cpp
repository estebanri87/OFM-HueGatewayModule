#include "HueGatewaySensor.h"
#include <knx.h>

void HueGatewaySensor::sendStatusToKnx()
{
    if (!_initialized || _koMotionStatus == 0)
        return;

    // DPST-1-8: DPT 1.008 — 0=keine Bewegung, 1=Bewegung erkannt
    knx.getGroupObject(_koMotionStatus).value(_motion, Dpt(1, 8));

    Serial.printf("[HueGatewaySensor] %s - Status -> KNX: Motion=%d\n",
                  _name.c_str(), _motion ? 1 : 0);
}

void HueGatewaySensor::sendOptionalKosToKnx()
{
    if (!_initialized) return;

    if (_optReachable && _koReachable > 0)
        knx.getGroupObject(_koReachable).value(_reachable, Dpt(1, 2));

    if (_optTemp && _koTemp > 0 && !isnan(_temperature))
        knx.getGroupObject(_koTemp).value(_temperature, Dpt(9, 1));

    if (_optLux && _koLux > 0 && _lightLevelLux > 0.0f)
        knx.getGroupObject(_koLux).value(_lightLevelLux, Dpt(9, 4));

    if (_optBattery && _koBattery > 0 && _batteryPercent != 255)
        knx.getGroupObject(_koBattery).value(_batteryPercent, Dpt(5, 1));
}
