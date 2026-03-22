#include "HueGatewayContact.h"
#include <knx.h>

void HueGatewayContact::sendStatusToKnx()
{
    if (!_initialized || _koContactStatus == 0)
        return;

    // DPST-1-9: DPT 1.009 — 0=geschlossen (contact), 1=geöffnet (no_contact)
    knx.getGroupObject(_koContactStatus).value(_contactOpen, Dpt(1, 9));

    Serial.printf("[HueGatewayContact] %s - Status -> KNX: ContactOpen=%d\n",
                  _name.c_str(), _contactOpen ? 1 : 0);
}

void HueGatewayContact::sendOptionalKosToKnx()
{
    if (!_initialized) return;

    // KO3: Sabotage (DPST-1-1)
    if (_optTamper && _koTamper > 0)
        knx.getGroupObject(_koTamper).value(_tampered, Dpt(1, 1));

    // KO4: Batterie (DPST-5-1)
    if (_optBattery && _koBattery > 0 && _batteryPercent != 255)
        knx.getGroupObject(_koBattery).value(_batteryPercent, Dpt(5, 1));

    // KO9: Erreichbar (DPST-1-2)
    if (_optReachable && _koReachable > 0)
        knx.getGroupObject(_koReachable).value(_reachable, Dpt(1, 2));
}
