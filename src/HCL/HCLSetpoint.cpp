#include "HCLSetpoint.h"
#include <cstdio>
#include <cstdlib>

namespace HCL {

uint16_t Setpoint::parseTime(const char* timeStr) {
    if (!timeStr || timeStr[0] == '\0') {
        return 0xFFFF;
    }
    
    int hours = 0, minutes = 0;
    if (sscanf(timeStr, "%d:%d", &hours, &minutes) != 2) {
        return 0xFFFF;
    }
    
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
