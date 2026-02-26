#include "HCLSetpoint.h"
#include <cstdio>
#include <cstdlib>

namespace HCL {

uint16_t Setpoint::parseTime(const char* timeStr) {
    if (!timeStr || timeStr[0] == '\0') {
        return 0xFFFF;
    }

    const char h0 = timeStr[0];
    const char h1 = timeStr[1];
    const char sep = timeStr[2];
    const char m0 = timeStr[3];
    const char m1 = timeStr[4];

    if (h0 < '0' || h0 > '9' ||
        h1 < '0' || h1 > '9' ||
        sep != ':' ||
        m0 < '0' || m0 > '9' ||
        m1 < '0' || m1 > '9') {
        return 0xFFFF;
    }

    const int hours = (h0 - '0') * 10 + (h1 - '0');
    const int minutes = (m0 - '0') * 10 + (m1 - '0');

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        return 0xFFFF;
    }
    
    return hours * 60 + minutes;
}

void Setpoint::formatTime(uint16_t minutes, char* buffer) {
    if (minutes >= 1440) {
        sprintf(buffer, "--:--");
        return;
    }
    
    uint8_t hours = minutes / 60;
    uint8_t mins = minutes % 60;
    sprintf(buffer, "%02d:%02d", hours, mins);
}

} // namespace HCL
