#include "HueGatewayModule.h"
#include "versions.h"
#include "HueGatewayDiscovery.h"
#include "HueGatewayAuth.h"
#include "HueGatewayClient.h"
#include "Devices/HueGatewayLight.h"
#include "Devices/HueGatewaySensor.h"
#include "Devices/HueGatewayButton.h"
#include "Devices/HueGatewayContact.h"
#include "Devices/HueGatewayPlug.h"
#include "OpenKNX/Led/RGB.h"
#include <ETH.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstring>
#include <memory>
#include <new>

#if __has_include("NetworkModule.h")
#include "NetworkModule.h"
#define HUEGATEWAY_HAS_OPENKNX_NETWORK 1
#endif

#if __has_include("LightManagerModule.h")
#include "LightManagerModule.h"
#define HUEGATEWAY_HAS_LIGHTMANAGER 1
#endif

namespace
{
#ifndef OPENKNX_HUE_EVENTSTREAM_POLICY_DEFAULT
#define OPENKNX_HUE_EVENTSTREAM_POLICY_DEFAULT 1
#endif

static constexpr unsigned long kFastTrackFirstDelayMs = 200UL;
static constexpr unsigned long kFastTrackSecondDelayMs = 300UL;
static constexpr unsigned long kFastTrackCooldownMs = 800UL;
static constexpr uint8_t kFastTrackChecksPerCommand = 2;
static constexpr unsigned long kBridgePingBackoffMinMs = 60000UL;
static constexpr unsigned long kBridgePingBackoffMaxMs = 120000UL;
static constexpr unsigned long kBridgeDiscoveryRetryMs = 1000UL;
static constexpr unsigned long kBridgeNoIpSkipLogThrottleMs = 10000UL;
static constexpr unsigned long kBridgeHealthForEventstreamMs = 15000UL;
static constexpr unsigned long kBridgeHealthRecentSkipPingMs = 60000UL;
static constexpr unsigned long kEventStreamRetryBaseMs = 1000UL;
static constexpr unsigned long kBootEventStreamWarmupMs = 15000UL;
static constexpr unsigned long kDeviceSetupRetryBaseMs = 20000UL;
static constexpr unsigned long kDeviceSetupRetryMaxMs = 60000UL;
static constexpr uint8_t kEmptyLightEscalationThreshold = 3;
static constexpr unsigned long kEmptyLightQuickRetryMs = 5000UL;
static constexpr unsigned long kConnectedEnterHysteresisMs = 6000UL;
static constexpr unsigned long kConnectedExitHysteresisMs = 12000UL;
static constexpr unsigned long kSetupCircuitOpenMs = 180000UL;
static constexpr uint8_t kSetupCircuitTripThreshold = 4;
static constexpr unsigned long kLoopBudgetMaxMs = 20UL;
static constexpr unsigned long kLoopBudgetLogThrottleMs = 15000UL;
static constexpr unsigned long kPollingSnapshotCacheMs = 25000UL;
static constexpr unsigned long kPollingSnapshotHardMaxAgeMs = 90000UL;
static constexpr uint32_t kSetupMinInternalFreeBytes = 42000U;
static constexpr uint32_t kSetupMinInternalLargestBlockBytes = 28000U;
static constexpr int kWebScanMaxLights = 192;
static constexpr int kWebScanMaxTargets = 192;
static constexpr unsigned long kWebScanCacheStaleMs = 15000UL;
static constexpr unsigned long kWebScanTimeoutMs = 180000UL;
static constexpr size_t kDiagLogCapacity = 150;
static constexpr size_t kDiagLogMessageMaxLen = 220;
static constexpr size_t kDiagDefaultDepth = 150;
static bool sEventStreamEnabled = (OPENKNX_HUE_EVENTSTREAM_POLICY_DEFAULT != 0);

static unsigned long sBridgeNextPingAllowedMs = 0UL;
static unsigned long sBridgePingBackoffMs = kBridgePingBackoffMinMs;
static unsigned long sBridgeNextDiscoveryTryMs = 0UL;
static unsigned long sBridgeNoIpSkipLastLogMs = 0UL;

static bool hasSetupTlsHeadroom()
{
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return freeInternal >= kSetupMinInternalFreeBytes && largestInternal >= kSetupMinInternalLargestBlockBytes;
}

static void logSetupTlsHeadroom(const char* phase)
{
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("[HueGatewayModule] HEAP %s intFree=%u intLargest=%u (need free>=%u largest>=%u)\n",
                  phase,
                  static_cast<unsigned>(freeInternal),
                  static_cast<unsigned>(largestInternal),
                  static_cast<unsigned>(kSetupMinInternalFreeBytes),
                  static_cast<unsigned>(kSetupMinInternalLargestBlockBytes));
}

static String readFixedTimeParam(const uint8_t* rawTime)
{
    if (rawTime == nullptr || rawTime[0] == '\0')
    {
        return String("");
    }

    char buffer[6] = {0, 0, 0, 0, 0, 0};
    memcpy(buffer, rawTime, 5);
    return String(buffer);
}

struct HclFixedInterpolationDebug
{
    bool valid = false;
    bool wrapsMidnight = false;
    uint16_t currentMinutes = 0;
    uint16_t prevTime = 0;
    uint16_t nextTime = 0;
    uint16_t prevKelvin = 0;
    uint16_t nextKelvin = 0;
    uint8_t prevBrightness = 0;
    uint8_t nextBrightness = 0;
    uint16_t spanMinutes = 0;
    uint16_t elapsedMinutes = 0;
    uint16_t targetKelvin = 0;
    uint8_t targetBrightness = 0;
    float factor = 0.0f;
};

static String formatMinutesToClock(uint16_t minutes)
{
    char timeBuffer[6] = {0};
    HCL::Setpoint::formatTime(minutes % 1440, timeBuffer);
    return String(timeBuffer);
}

static bool isDateInSummerRange(uint8_t month, uint8_t day,
                                uint8_t startMonth, uint8_t startDay,
                                uint8_t endMonth, uint8_t endDay)
{
    if (startMonth == endMonth && startDay == endDay)
    {
        Serial.println("[HCL] Saison-Datum: Start == End, verwende Winter");
        return false;
    }

    // Encode as day-of-year approximation using month*32+day for comparison
    const uint16_t cur   = static_cast<uint16_t>(month)      * 32u + day;
    const uint16_t start = static_cast<uint16_t>(startMonth) * 32u + startDay;
    const uint16_t end   = static_cast<uint16_t>(endMonth)   * 32u + endDay;

    if (end > start)
    {
        // Normal case: summer within one calendar year (e.g. Apr – Oct)
        return cur >= start && cur <= end;
    }
    else
    {
        // Wrap-around: summer spans year boundary (e.g. Nov – Mar, southern hemisphere)
        return cur >= start || cur <= end;
    }
}

static bool buildFixedInterpolationDebug(const HCL::Master* master, uint16_t currentMinutes, HclFixedInterpolationDebug& debug)
{
    debug = HclFixedInterpolationDebug();
    if (master == nullptr)
    {
        return false;
    }

    uint8_t firstValid = 0xFF;
    uint8_t lastValid = 0xFF;
    uint8_t validCount = 0;

    for (uint8_t i = 0; i < HCL::Master::MAX_SETPOINTS; i++)
    {
        const HCL::Setpoint* setpoint = master->getSetpoint(i);
        if (setpoint == nullptr || setpoint->timeMinutes >= 1440)
        {
            continue;
        }

        if (firstValid == 0xFF)
        {
            firstValid = i;
        }
        lastValid = i;
        validCount++;
    }

    if (validCount < 2 || firstValid == 0xFF || lastValid == 0xFF)
    {
        return false;
    }

    uint8_t prevIndex = lastValid;
    uint8_t nextIndex = firstValid;

    for (uint8_t i = firstValid; i < HCL::Master::MAX_SETPOINTS; i++)
    {
        const HCL::Setpoint* setpoint = master->getSetpoint(i);
        if (setpoint == nullptr || setpoint->timeMinutes >= 1440)
        {
            continue;
        }

        if (setpoint->timeMinutes <= currentMinutes)
        {
            prevIndex = i;
        }
        else
        {
            nextIndex = i;
            break;
        }
    }

    const HCL::Setpoint* lastSetpoint = master->getSetpoint(lastValid);
    if (lastSetpoint != nullptr && currentMinutes > lastSetpoint->timeMinutes)
    {
        prevIndex = lastValid;
        nextIndex = firstValid;
    }

    const HCL::Setpoint* prev = master->getSetpoint(prevIndex);
    const HCL::Setpoint* next = master->getSetpoint(nextIndex);
    if (prev == nullptr || next == nullptr)
    {
        return false;
    }

    int32_t span = 0;
    int32_t elapsed = 0;
    const bool wraps = (next->timeMinutes <= prev->timeMinutes);
    if (!wraps)
    {
        span = static_cast<int32_t>(next->timeMinutes) - static_cast<int32_t>(prev->timeMinutes);
        elapsed = static_cast<int32_t>(currentMinutes) - static_cast<int32_t>(prev->timeMinutes);
    }
    else
    {
        span = (1440 - static_cast<int32_t>(prev->timeMinutes)) + static_cast<int32_t>(next->timeMinutes);
        if (currentMinutes >= prev->timeMinutes)
        {
            elapsed = static_cast<int32_t>(currentMinutes) - static_cast<int32_t>(prev->timeMinutes);
        }
        else
        {
            elapsed = (1440 - static_cast<int32_t>(prev->timeMinutes)) + static_cast<int32_t>(currentMinutes);
        }
    }

    float factor = 0.0f;
    if (span > 0)
    {
        factor = static_cast<float>(elapsed) / static_cast<float>(span);
        factor = constrain(factor, 0.0f, 1.0f);
    }

    debug.valid = true;
    debug.wrapsMidnight = wraps;
    debug.currentMinutes = currentMinutes;
    debug.prevTime = prev->timeMinutes;
    debug.nextTime = next->timeMinutes;
    debug.prevKelvin = prev->kelvin;
    debug.nextKelvin = next->kelvin;
    debug.prevBrightness = prev->brightness;
    debug.nextBrightness = next->brightness;
    debug.spanMinutes = static_cast<uint16_t>((span < 0) ? 0 : span);
    debug.elapsedMinutes = static_cast<uint16_t>((elapsed < 0) ? 0 : elapsed);
    debug.factor = factor;
    const float targetKelvinFloat = static_cast<float>(prev->kelvin) +
        (static_cast<float>(next->kelvin) - static_cast<float>(prev->kelvin)) * factor;
    const float targetBrightnessFloat = static_cast<float>(prev->brightness) +
        (static_cast<float>(next->brightness) - static_cast<float>(prev->brightness)) * factor;
    debug.targetKelvin = static_cast<uint16_t>(constrain(static_cast<int32_t>(targetKelvinFloat), static_cast<int32_t>(2000), static_cast<int32_t>(6500)));
    debug.targetBrightness = static_cast<uint8_t>(constrain(static_cast<int32_t>(targetBrightnessFloat), static_cast<int32_t>(0), static_cast<int32_t>(100)));
    return true;
}

static String buildSetpointListDebug(const HCL::Master* master)
{
    if (master == nullptr)
    {
        return String("n/a");
    }

    String details;
    details.reserve(448);
    bool first = true;
    for (uint8_t i = 0; i < HCL::Master::MAX_SETPOINTS; i++)
    {
        const HCL::Setpoint* setpoint = master->getSetpoint(i);
        if (setpoint == nullptr || setpoint->timeMinutes >= 1440)
        {
            continue;
        }

        if (!first)
        {
            details += " | ";
        }

        details += String(i + 1);
        details += ": ";
        details += formatMinutesToClock(setpoint->timeMinutes);
        details += " ";
        details += String(setpoint->kelvin);
        details += "K/";
        details += String(setpoint->brightness);
        details += "%";
        first = false;
    }

    if (first)
    {
        return String("(keine gültigen Setpoints)");
    }

    return details;
}

}

static bool isUsableIp(const IPAddress& ip)
{
    return (ip[0] != 0) || (ip[1] != 0) || (ip[2] != 0) || (ip[3] != 0);
}

static String getDeviceIpString()
{
    const IPAddress wifiIp = WiFi.localIP();
    if (isUsableIp(wifiIp))
    {
        return wifiIp.toString();
    }

    const IPAddress ethIp = ETH.localIP();
    if (isUsableIp(ethIp))
    {
        return ethIp.toString();
    }

    return String("n/a");
}

static String getRequestLocalIpString(httpd_req_t* req)
{
    if (req != nullptr)
    {
        const int socketFd = httpd_req_to_sockfd(req);
        if (socketFd >= 0)
        {
            struct sockaddr_storage localAddr;
            socklen_t localAddrLen = sizeof(localAddr);
            memset(&localAddr, 0, sizeof(localAddr));

            if (getsockname(socketFd, reinterpret_cast<struct sockaddr*>(&localAddr), &localAddrLen) == 0)
            {
                if (localAddr.ss_family == AF_INET)
                {
                    const struct sockaddr_in* localAddrV4 = reinterpret_cast<const struct sockaddr_in*>(&localAddr);
                    char ipBuffer[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &(localAddrV4->sin_addr), ipBuffer, sizeof(ipBuffer)) != nullptr)
                    {
                        return String(ipBuffer);
                    }
                }
            }
        }
    }

    return getDeviceIpString();
}

void HueGatewayModule::appendDiagnosticLog(const char* level, const char* category, const String& message)
{
    if (_diagLogRing.empty())
    {
        return;
    }

    String normalized = message;
    normalized.replace('\n', ' ');
    normalized.replace('\r', ' ');
    if (normalized.length() > kDiagLogMessageMaxLen)
    {
        normalized = normalized.substring(0, kDiagLogMessageMaxLen);
        normalized += "...";
    }

    DiagnosticLogEntry& slot = _diagLogRing[_diagLogRingHead];
    slot.uptimeMs = millis();
    slot.level = (level != nullptr) ? String(level) : String("INFO");
    slot.category = (category != nullptr) ? String(category) : String("GEN");
    slot.message = normalized;

    _diagLogRingHead = (_diagLogRingHead + 1) % _diagLogRing.size();
    if (_diagLogRingCount < _diagLogRing.size())
    {
        _diagLogRingCount++;
    }
    else
    {
        _diagLogDropped++;
    }
}

static String maskIpForDiagnose(const String& ip, bool includeNetworkDetails)
{
    if (includeNetworkDetails)
    {
        return ip.length() > 0 ? ip : String("-");
    }

    if (ip.length() == 0)
    {
        return "-";
    }

    const int firstDot = ip.indexOf('.');
    if (firstDot < 0)
    {
        return "masked";
    }
    const int secondDot = ip.indexOf('.', firstDot + 1);
    if (secondDot < 0)
    {
        return "masked";
    }

    return ip.substring(0, secondDot) + ".x.x";
}

String HueGatewayModule::buildDiagnosticReport(size_t requestedDepth, const String& testerNote, bool includeNetworkDetails)
{
    const size_t available = _diagLogRingCount;
    const size_t effectiveDepth = min(requestedDepth, available);
    const unsigned long uptimeMs = millis();
    const String maskedDeviceIp = maskIpForDiagnose(getDeviceIpString(), includeNetworkDetails);
    const String maskedBridgeIp = maskIpForDiagnose(_bridgeIP, includeNetworkDetails);

    String report;
    report.reserve(50000);

    report += "=== OpenKNX Hue Diagnose V1 ===\n";
    report += "GeneratedAtMs=" + String(uptimeMs) + "\n";
    report += "ModuleVersion=" + String(version().c_str()) + "\n";
    report += "NetworkDetailsIncluded=" + String(includeNetworkDetails ? "1" : "0") + "\n";
    report += "DeviceIp=" + maskedDeviceIp + "\n";
    report += "BridgeIp=" + maskedBridgeIp + "\n\n";

    report += "[RuntimeStatus]\n";
    report += "Initialized=" + String(_initialized ? "1" : "0") + "\n";
    report += "BridgeStatus=" + String(static_cast<int>(_bridgeStatus)) + "\n";
    report += "AuthPending=" + String(_authPending ? "1" : "0") + "\n";
    report += "ActiveLights=" + String(_lightCount) + "\n";
    report += "DeviceSetupNeedsRetry=" + String(_deviceSetupNeedsRetry ? "1" : "0") + "\n";
    report += "ConsecutiveEmptyLightFetches=" + String(_consecutiveEmptyLightFetches) + "\n";
    report += "LastValidLightFetchMs=" + String(_lastValidLightFetchMs) + "\n";
    report += "SetupBackoffMs=" + String(_deviceSetupRetryBackoffMs) + "\n";
    report += "EventStreamRetryBackoffMs=" + String(_eventStreamRetryBackoffMs) + "\n";
    report += "EventStreamPauseUntilMs=" + String(_eventStreamPauseUntilMs) + "\n";
    report += "WebScanInProgress=" + String(_webScanInProgress ? "1" : "0") + "\n";
    report += "WebScanLastLightCount=" + String(_lastWebScanLightCount) + "\n";
    report += "WebScanLastDurationMs=" + String(_lastWebScanDurationMs) + "\n\n";

    report += "[Resource]\n";
    report += "FreeHeap=" + String(ESP.getFreeHeap()) + "\n";
    report += "MinFreeHeap=" + String(ESP.getMinFreeHeap()) + "\n";
    report += "InternalFree=" + String(static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) + "\n";
    report += "InternalLargest=" + String(static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) + "\n\n";

    report += "[Counters]\n";
    report += "SetupRuns=" + String(_diagCounterSetupRuns) + "\n";
    report += "SetupIncomplete=" + String(_diagCounterSetupIncomplete) + "\n";
    report += "UnresolvedTargets=" + String(_diagCounterUnresolvedTargets) + "\n";
    report += "KoCommands=" + String(_diagCounterKoCommands) + "\n";
    report += "KoBlockedSyncDir=" + String(_diagCounterKoBlockedSyncDir) + "\n";
    report += "KoBlockedChannelMissing=" + String(_diagCounterKoBlockedChannelMissing) + "\n";
    report += "WebScanRuns=" + String(_diagCounterWebScanRuns) + "\n";
    report += "WebScanTimeouts=" + String(_diagCounterWebScanTimeouts) + "\n";
    report += "EmptyLightFetches=" + String(_diagCounterEmptyLightFetches) + "\n";
    report += "DiagLogDropped=" + String(_diagLogDropped) + "\n\n";

    report += "[HttpEventStats]\n";
    if (_client != nullptr)
    {
        const HueGatewayClient::DiagnosticsStats& stats = _client->getDiagnosticsStats();
        report += "HttpGetCount=" + String(stats.httpGetCount) + "\n";
        report += "HttpPutCount=" + String(stats.httpPutCount) + "\n";
        report += "HttpGetErrors=" + String(stats.httpGetErrorCount) + "\n";
        report += "HttpPutErrors=" + String(stats.httpPutErrorCount) + "\n";
        report += "HttpTimeouts=" + String(stats.httpTimeoutCount) + "\n";
        report += "LastHttpMethod=" + (stats.lastHttpMethod.length() > 0 ? stats.lastHttpMethod : String("-")) + "\n";
        report += "LastHttpEndpoint=" + (stats.lastHttpEndpoint.length() > 0 ? stats.lastHttpEndpoint : String("-")) + "\n";
        report += "LastHttpStatus=" + String(stats.lastHttpStatusCode) + "\n";
        report += "EventConnectOk=" + String(stats.eventConnectOk) + "\n";
        report += "EventConnectFail=" + String(stats.eventConnectFail) + "\n";
        report += "EventStopCount=" + String(stats.eventStopCount) + "\n";
        report += "EventDisconnectCount=" + String(stats.eventDisconnectCount) + "\n";
        report += "EventHandshakeTimeouts=" + String(stats.eventHandshakeTimeoutCount) + "\n";
        report += "EventHttpErrors=" + String(stats.eventHttpErrorCount) + "\n";
    }
    else
    {
        report += "Client=not-initialized\n";
    }
    report += "\n";

    report += "[LastSetupSnapshot]\n";
    report += "EnabledChannels=" + String(_diagLastEnabledChannels) + "\n";
    report += "BridgeLightCount=" + String(_diagLastBridgeLightCount) + "\n";
    report += "RoomTargetCount=" + String(_diagLastRoomTargetCount) + "\n";
    report += "ZoneTargetCount=" + String(_diagLastZoneTargetCount) + "\n";
    report += "HasUnresolvedGroupTarget=" + String(_diagLastHasUnresolvedGroupTarget ? "1" : "0") + "\n\n";

    report += "[ChannelSnapshot]\n";
    uint8_t configuredChannels = ParamHUE_HUEChannelCount;
    if (configuredChannels > MAX_LIGHTS)
    {
        configuredChannels = MAX_LIGHTS;
    }

    for (uint8_t ch = 0; ch < configuredChannels; ch++)
    {
        uint8_t _channelIndex = ch;
        const bool channelDisabled = ParamHUE_CHDisabled;
        String targetRid = "";
        #ifdef ParamHUE_CHTargetRIDStr
        {
            std::string t = ParamHUE_CHTargetRIDStr;
            targetRid = String(t.c_str());
            targetRid.trim();
        }
        #endif

        std::string legacyStd = ParamHUE_CHLightUUIDStr;
        String legacyUuid(legacyStd.c_str());
        legacyUuid.trim();

        uint8_t targetType = 0;
        #ifdef ParamHUE_CHTargetType
        targetType = ParamHUE_CHTargetType;
        #endif

        uint8_t devType = 0;
        #ifdef ParamHUE_CHDeviceType
        devType = ParamHUE_CHDeviceType;
        #endif

        uint8_t syncDir = ParamHUE_CHSyncDir;
        if (syncDir > 2)
        {
            syncDir = 2;
        }
        uint8_t pollIntervalSec = ParamHUE_CHPollInterval;

        String mappedName = "-";
        String mappedId = "-";
        String grouped = "-";
        String lastWriteMs = "-";
        uint8_t hclMaster = 0;
        bool hclLocked = false;
        if (_devices[ch] != nullptr)
        {
            mappedName = _devices[ch]->getName();
            mappedId = _devices[ch]->getResourceId();
            grouped = _devices[ch]->isGroupedTarget() ? "1" : "0";
            lastWriteMs = String(_devices[ch]->getLastHueWriteSuccessMs());
            hclMaster = _devices[ch]->getHCLMaster();
            hclLocked = _hclChannelLockActive[ch];
        }

        report += "ch=" + String(ch + 1)
            + " disabled=" + String(channelDisabled ? "1" : "0")
            + " devType=" + String(devType)
            + " sync=" + String(syncDir)
            + " poll=" + String(pollIntervalSec)
            + " targetType=" + String(targetType)
            + " targetRid=" + (targetRid.length() > 0 ? targetRid : String("-"))
            + " legacyUuid=" + (legacyUuid.length() > 0 ? legacyUuid : String("-"))
            + " mappedName=" + mappedName
            + " mappedId=" + mappedId
            + " grouped=" + grouped
            + " hclMaster=" + String(hclMaster)
            + " hclLocked=" + String(hclLocked ? "1" : "0")
            + " lastHueWriteMs=" + lastWriteMs
                + " lastKoMs=" + String(_diagLastKoCommandMs[ch])
                + " lastKoType=" + String(_diagLastKoType[ch])
                + " lastKoValue=" + (_diagLastKoValue[ch].length() > 0 ? _diagLastKoValue[ch] : String("-"))
                + " koBlockReason=" + (_diagLastKoBlockReason[ch].length() > 0 ? _diagLastKoBlockReason[ch] : String("-"))
                + " traceMs=" + String(_diagLastWriteTraceMs[ch])
                + " traceDurMs=" + String(_diagLastWriteTraceDurationMs[ch])
                + " traceResult=" + (_diagLastWriteTraceResult[ch].length() > 0 ? _diagLastWriteTraceResult[ch] : String("-"))
                + " traceHttpStatus=" + String(_diagLastWriteTraceHttpStatus[ch])
                + " traceHttpMethod=" + (_diagLastWriteTraceMethod[ch].length() > 0 ? _diagLastWriteTraceMethod[ch] : String("-"))
                + " traceHttpEndpoint=" + (_diagLastWriteTraceEndpoint[ch].length() > 0 ? _diagLastWriteTraceEndpoint[ch] : String("-"))
            + "\n";
    }
    report += "\n";

    report += "[SceneConfig]\n";
    for (uint8_t ch = 0; ch < configuredChannels; ch++)
    {
        uint8_t _channelIndex = ch;
        bool sceneEnabled = ParamHUE_CHSceneEnabled;
        bool sceneStore   = ParamHUE_CHSceneStoreActive;

        report += "ch=" + String(ch + 1)
            + " sceneEnabled=" + String(sceneEnabled ? "1" : "0")
            + " sceneStore=" + String(sceneStore ? "1" : "0")
            + "\n";

        if (!sceneEnabled) continue;

        static const char* kActionNames[] = {"Aus", "Ein", "Helligkeit", "CT", "Helli+CT", "RGB", "Helli+RGB", "HueSzene"};
        for (uint8_t s = 0; s < 8; s++)
        {
            const uint16_t base     = 89 + static_cast<uint16_t>(s) * 9;
            const uint8_t  sceneNum = knx.paramByte(HUE_ParamCalcIndex(base + 0));
            if (sceneNum == 0) continue;  // Slot nicht konfiguriert

            const uint8_t  action = knx.paramByte(HUE_ParamCalcIndex(base + 1));
            const uint8_t  bri    = knx.paramByte(HUE_ParamCalcIndex(base + 3));
            const uint16_t ct     = knx.paramWord(HUE_ParamCalcIndex(base + 4));
            const uint8_t  red    = knx.paramByte(HUE_ParamCalcIndex(base + 6));
            const uint8_t  green  = knx.paramByte(HUE_ParamCalcIndex(base + 7));
            const uint8_t  blue   = knx.paramByte(HUE_ParamCalcIndex(base + 8));

            const char* actionName = (action <= 7) ? kActionNames[action] : "?";
            report += "  slot=" + String(s + 1)
                + " sceneNr=" + String(sceneNum)
                + " action=" + String(action) + "(" + String(actionName) + ")"
                + " bri=" + String(bri) + "%"
                + " ct=" + String(ct) + "K"
                + " rgb=(" + String(red) + "," + String(green) + "," + String(blue) + ")";

            if (sceneStore)
            {
                const SceneStoreData& sd = _sceneStore[ch][s];
                if (sd.valid == SCENE_STORE_VALID)
                {
                    report += " stored={on=" + String(sd.onOff)
                        + " bri=" + String(sd.brightness) + "%"
                        + " ct=" + String(sd.colorTemp) + "K"
                        + " rgb=(" + String(sd.red) + "," + String(sd.green) + "," + String(sd.blue) + ")}";
                }
                else
                {
                    report += " stored=none";
                }
            }
            report += "\n";
        }
    }
    report += "\n";

    report += "[HCLLock]\n";
    report += "GlobalLock=" + String(_hclLockActive ? "1" : "0") + "\n";
    report += "GlobalPolicy=" + String(hclFallbackPolicyToText(static_cast<HclLockFallbackPolicy>(_hclFallbackPolicy))) + "\n";
    report += "GlobalFallbackMode=" + String(hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclLockFallbackMode))) + "\n";
    report += "GlobalFallbackDurationMs=" + String(_hclFallbackDurationMs) + "\n";
    report += "GlobalFallbackReleaseMin=" + String(_hclFallbackReleaseMinuteOfDay) + "\n";
    report += "GlobalActivatedMs=" + String(_hclLockActivatedMs) + "\n";
    report += "GlobalAutoReleaseMs=" + String(_hclLockAutoReleaseMs) + "\n";
    report += "HclApplyBlocked=" + String(HCL::masterManager.isApplyBlocked() ? "1" : "0") + "\n";
    for (uint8_t m = 0; m < HCL::MasterManager::MAX_MASTERS; m++)
    {
        report += "M" + String(m + 1) + "Lock=" + String(_hclManagerLockActive[m] ? "1" : "0")
            + " policy=" + String(hclFallbackPolicyToText(static_cast<HclLockFallbackPolicy>(_hclManagerFallbackPolicy[m])))
            + " mode=" + String(hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclManagerLockFallbackMode[m])))
            + " activatedMs=" + String(_hclManagerLockActivatedMs[m])
            + " autoReleaseMs=" + String(_hclManagerLockAutoReleaseMs[m])
            + "\n";
    }
    report += "ChannelLockSummary=";
    {
        bool any = false;
        for (uint8_t ch = 0; ch < configuredChannels; ch++)
        {
            if (_hclChannelLockActive[ch])
            {
                if (any) report += ",";
                report += String(ch + 1);
                any = true;
            }
        }
        if (!any) report += "none";
    }
    report += "\n";
    for (uint8_t ch = 0; ch < configuredChannels; ch++)
    {
        if (_devices[ch] == nullptr || _devices[ch]->getHCLMaster() == 0) continue;
        report += "ch=" + String(ch + 1)
            + " hclMaster=" + String(_devices[ch]->getHCLMaster())
            + " locked=" + String(_hclChannelLockActive[ch] ? "1" : "0")
            + " mode=" + String(hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclChannelLockFallbackMode[ch])))
            + " activatedMs=" + String(_hclChannelLockActivatedMs[ch])
            + " autoReleaseMs=" + String(_hclChannelLockAutoReleaseMs[ch])
            + "\n";
    }
    report += "\n";

    report += "[LastHueScanText]\n";
    if (_lastWebScanText.length() > 0)
    {
        report += _lastWebScanText;
        if (!_lastWebScanText.endsWith("\n"))
        {
            report += "\n";
        }
    }
    else
    {
        report += "(none)\n";
    }
    report += "\n";

    report += "[RingLog]\n";
    report += "RingCapacity=" + String(static_cast<unsigned long>(_diagLogRing.size())) + "\n";
    report += "RequestedDepth=" + String(static_cast<unsigned long>(requestedDepth)) + "\n";
    report += "EffectiveDepth=" + String(static_cast<unsigned long>(effectiveDepth)) + "\n";

    if (effectiveDepth > 0 && !_diagLogRing.empty())
    {
        const size_t ringSize = _diagLogRing.size();
        size_t first = (_diagLogRingHead + ringSize - effectiveDepth) % ringSize;
        for (size_t i = 0; i < effectiveDepth; i++)
        {
            const DiagnosticLogEntry& entry = _diagLogRing[(first + i) % ringSize];
            report += String(entry.uptimeMs);
            report += "|";
            report += entry.level;
            report += "|";
            report += entry.category;
            report += "|";
            report += entry.message;
            report += "\n";
        }
    }
    else
    {
        report += "(no entries)\n";
    }
    report += "\n";

    report += "[TesterNote]\n";
    report += testerNote.length() > 0 ? testerNote : String("-");
    report += "\n";

    return report;
}

HueGatewayModule::HueGatewayModule()
    : _initialized(false)
    , _lastConnectionCheckMs(0)
    , _bootStartMs(0)
    , _lastRefreshTickMs(0)
    , _lastDeviceSetupRetryMs(0)
    , _deviceSetupRetryBackoffMs(kDeviceSetupRetryBaseMs)
    , _lastEventStreamRetryMs(0)
    , _eventStreamRetryBackoffMs(kEventStreamRetryBaseMs)
    , _eventStreamPauseUntilMs(0)
    , _eventStreamFailureCount(0)
    , _client(nullptr)
    , _lightCount(0)
    , _pollBackoffUntilMs(0)
    , _pollBackoffMs(1000)
    , _pollFailureCount(0)
    , _lastBridgeHealthOkMs(0)
    , _lastChannelSyncOkMs(0)
    , _pollCursor(0)
    , _bridgeStatus(BridgeStatus::DISCONNECTED)
    , _ledBlinkTime(0)
    , _bridgeIP("")
    , _authPending(false)
    , _authStartTime(0)
    , _authLastTry(0)
    , _authWindowMs(30000)
    , _lastReconnectTryMs(0)
    , _reconnectBackoffMs(10000)
    , _setupCircuitOpenUntilMs(0)
    , _setupCircuitTrips(0)
    , _lastLoopBudgetLogMs(0)
    , _manualPairingRequired(false)
    , _pairingTriggerLastState(false)
    , _lastPairingTriggerMs(0)
    , _devicesInitialized(false)
    , _deviceSetupNeedsRetry(false)
    , _consecutiveEmptyLightFetches(0)
    , _diagCounterEmptyLightFetches(0)
    , _lastValidLightFetchMs(0)
    , _biAutoDeletePending(false)
    , _biAutoDeleteTriggerMs(0)
    , _webScanRequested(false)
    , _webScanInProgress(false)
    , _networkConnectedLast(false)
    , _mdnsStarted(false)
    , _webScanStartedMs(0)
    , _lastWebScanDurationMs(0)
    , _lastWebScanMs(0)
    , _lastWebScanLightCount(-1)
    , _lastWebScanAccessoryCount(0)
    , _hclLockActive(false)
    , _hclLockFallbackMode(static_cast<uint8_t>(HueGatewayModule::HclLockFallbackMode::None))
    , _hclFallbackPolicy(static_cast<uint8_t>(HueGatewayModule::HclLockFallbackPolicy::Legacy))
    , _hclFallbackDurationMs(0)
    , _hclFallbackReleaseMinuteOfDay(0xFFFF)
    , _hclLockActivatedMs(0)
    , _hclLockAutoReleaseMs(0)
    , _hclLockActivationDayOfYear(-1)
    , _hclLockActivationMinuteOfDay(-1)
    , _diagLogRingHead(0)
    , _diagLogRingCount(0)
    , _diagLogDropped(0)
    , _diagCounterSetupRuns(0)
    , _diagCounterSetupIncomplete(0)
    , _diagCounterUnresolvedTargets(0)
    , _diagCounterKoCommands(0)
    , _diagCounterKoBlockedSyncDir(0)
    , _diagCounterKoBlockedChannelMissing(0)
    , _diagCounterWebScanRuns(0)
    , _diagCounterWebScanTimeouts(0)
    , _diagLastEnabledChannels(0)
    , _diagLastBridgeLightCount(0)
    , _diagLastRoomTargetCount(0)
    , _diagLastZoneTargetCount(0)
    , _diagLastHasUnresolvedGroupTarget(false)
{
    // Light-Array initialisieren
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        _devices[i] = nullptr;
        _channelLastPollMs[i] = 0;
        _channelFastTrackNextMs[i] = 0;
        _channelFastTrackCooldownUntilMs[i] = 0;
        _channelFastTrackRemaining[i] = 0;
        _hclChannelLockActive[i] = false;
        _hclChannelLockFallbackMode[i] = static_cast<uint8_t>(HclLockFallbackMode::None);
        _hclChannelLockActivatedMs[i] = 0;
        _hclChannelLockAutoReleaseMs[i] = 0;
        _hclChannelLockActivationDayOfYear[i] = -1;
        _hclChannelLockActivationMinuteOfDay[i] = -1;
        _diagLastKoCommandMs[i] = 0;
        _diagLastKoType[i] = 0xFF;
        _diagLastKoValue[i] = "";
        _diagLastKoBlockReason[i] = "-";
        _diagLastWriteTraceMs[i] = 0;
        _diagLastWriteTraceDurationMs[i] = 0;
        _diagLastWriteTraceHttpStatus[i] = 0;
        _diagLastWriteTraceResult[i] = "-";
        _diagLastWriteTraceMethod[i] = "-";
        _diagLastWriteTraceEndpoint[i] = "-";
        for (int s = 0; s < SCENE_SLOTS; s++)
            _sceneStore[i][s] = { SCENE_STORE_EMPTY, 0, 0, 0, 0, 0, 0 };
    }

    for (uint8_t i = 0; i < HCL::MasterManager::MAX_MASTERS; i++)
    {
        _hclManagerLockActive[i] = false;
        _hclManagerLockFallbackMode[i] = static_cast<uint8_t>(HclLockFallbackMode::None);
        _hclManagerFallbackPolicy[i] = static_cast<uint8_t>(HclLockFallbackPolicy::Legacy);
        _hclManagerFallbackDurationMs[i] = 0;
        _hclManagerFallbackReleaseMinuteOfDay[i] = 0xFFFF;
        _hclManagerLockActivatedMs[i] = 0;
        _hclManagerLockAutoReleaseMs[i] = 0;
        _hclManagerLockActivationDayOfYear[i] = -1;
        _hclManagerLockActivationMinuteOfDay[i] = -1;
        _hclLastPublishedKelvin[i] = 0;
        _hclLastPublishedBrightness[i] = 0;
        _hclMasterValuesPublished[i] = false;
        _hclLastPublishMs[i] = 0;
    }

    _lastWebScanError = "";
    _lastWebScanHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte laden</title></head><body><h1>🔍 Hue-Geräte laden</h1><p>Noch kein Scan durchgeführt.</p></body></html>";
    _lastWebScanText = "Noch kein Scan durchgeführt.\n";
}

HueGatewayModule::~HueGatewayModule()
{
    resetDevices();

    // Cleanup Client
    if (_client)
    {
        delete _client;
        _client = nullptr;
    }
}

void HueGatewayModule::setup()
{
    Serial.println("[HueGatewayModule] Setup started");
    Serial.println("[HueGatewayModule] Initializing...");
    _bootStartMs = millis();

    if (_diagLogRing.empty())
    {
        _diagLogRing.resize(kDiagLogCapacity);
    }

    appendDiagnosticLog("INFO", "BOOT", "Setup started");

    static bool extmemConfigured = false;
    if (!extmemConfigured && psramFound())
    {
        heap_caps_malloc_extmem_enable(0);
        extmemConfigured = true;
        Serial.println("[HueGatewayModule] PSRAM malloc routing enabled (internal RAM reserved for TLS)");
    }
    
    setupBridge();
    setupDevices();
    setupHCL();
    setupWebUI();
    setupMDNS();
    _networkConnectedLast = hasNetworkConnectivity();
    
    _initialized = true;
    Serial.println("[HueGatewayModule] Setup complete");
    appendDiagnosticLog("INFO", "BOOT", "Setup complete");

    // Defense-in-depth: hardware watchdog resets device if loop() stalls >30s
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(NULL);
}

void HueGatewayModule::loop()
{
    static bool fallbackActiveLogged = false;

    if (!_initialized)
        return;

    esp_task_wdt_reset();

    // Update Info-LED pattern
    updateInfoLED();

    // Non-blocking authentication polling
    pollAuthentication();
    
    struct tm timeinfo;
    const bool hasTime = getLocalTime(&timeinfo, 0);
    evaluateHclChannelLockFallback(hasTime ? &timeinfo : nullptr, hasTime);
    
    // Call loop on all active device channels
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (_devices[i] != nullptr)
        {
            _devices[i]->loop();

            // Sync HCL channel lock status if the device changed it internally
            // (e.g. auto-release on switch-off or lock-enable on scene recall).
            if (_devices[i]->getHCLMaster() > 0)
            {
                const bool deviceLock = _devices[i]->isHCLChannelLocked();
                if (deviceLock != _hclChannelLockActive[i])
                {
                    _hclChannelLockActive[i] = deviceLock;
                    publishHclChannelLockStatus(static_cast<uint8_t>(i));
                }
            }
        }
    }
    
    unsigned long now = millis();
    const unsigned long loopStartMs = now;
    auto hasLoopBudget = [&](const char* taskName) -> bool
    {
        const unsigned long elapsedMs = millis() - loopStartMs;
        if (elapsedMs <= kLoopBudgetMaxMs)
        {
            return true;
        }

        if ((now - _lastLoopBudgetLogMs) >= kLoopBudgetLogThrottleMs || now < _lastLoopBudgetLogMs)
        {
            Serial.printf("[HueGatewayModule] Loop budget exceeded (%lu ms), defer '%s'\n",
                          static_cast<unsigned long>(elapsedMs),
                          taskName ? taskName : "task");
            _lastLoopBudgetLogMs = now;
        }
        return false;
    };
    
    // Re-check bridge connectivity every 5 seconds.
    if (now - _lastConnectionCheckMs > 5000)
    {
        _lastConnectionCheckMs = now;
        refreshNetworkServices();
        checkConnection();
    }

    if (_client && _client->isInitialized() && !_authPending)
    {
        uint8_t configuredChannels = ParamHUE_HUEChannelCount;
        if (configuredChannels > MAX_LIGHTS)
        {
            configuredChannels = MAX_LIGHTS;
        }

        const bool suppressEventStreamRetryForSetup = (configuredChannels > 0) && _deviceSetupNeedsRetry;

        if (!sEventStreamEnabled)
        {
            if (_client->isEventStreamConnected())
            {
                _client->stopEventStream();
            }
            _eventStreamRetryBackoffMs = kEventStreamRetryBaseMs;
            _eventStreamPauseUntilMs = 0;
            _eventStreamFailureCount = 0;
        }
        else
        {
            _client->checkEventStreamConnect();

            if (!_client->isEventStreamConnected() && !_client->isEventStreamConnectPending())
            {
                if (suppressEventStreamRetryForSetup)
                {
                    static unsigned long sLastSetupRetrySuppressLogMs = 0;
                    if ((now - sLastSetupRetrySuppressLogMs) >= 10000UL || now < sLastSetupRetrySuppressLogMs)
                    {
                        Serial.println("[HueGatewayModule] EventStream retry suppressed until device setup succeeds");
                        sLastSetupRetrySuppressLogMs = now;
                    }
                }
                else
                {
                    if (hasLoopBudget("eventstream-retry"))
                    {
                        const bool bridgeHealthyForEventstream = (_lastBridgeHealthOkMs != 0)
                            && ((now - _lastBridgeHealthOkMs) <= kBridgeHealthForEventstreamMs);

                        if (_eventStreamPauseUntilMs != 0 && now >= _eventStreamPauseUntilMs)
                        {
                            _eventStreamPauseUntilMs = 0;
                            _eventStreamRetryBackoffMs = kEventStreamRetryBaseMs;
                            _eventStreamFailureCount = 0;
                            Serial.println("[HueGatewayModule] EventStream cooldown elapsed, retrying");
                        }

                        if (_eventStreamPauseUntilMs == 0
                            && bridgeHealthyForEventstream
                            && (now >= HueGatewayLight::globalHclWriteNextAllowedMs())
                            && (now - _lastEventStreamRetryMs >= _eventStreamRetryBackoffMs))
                        {
                            _lastEventStreamRetryMs = now;
                            Serial.printf("[HueGatewayModule] EventStream retry at %lu ms\n", static_cast<unsigned long>(now));
                            if (!_client->startEventStream())
                            {
                                _eventStreamFailureCount = min<uint8_t>(static_cast<uint8_t>(_eventStreamFailureCount + 1), static_cast<uint8_t>(10));
                                _eventStreamRetryBackoffMs = min<unsigned long>(_eventStreamRetryBackoffMs * 2UL, 120000UL);

                                if (_eventStreamFailureCount >= 6)
                                {
                                    _eventStreamPauseUntilMs = now + 600000UL;
                                    _eventStreamFailureCount = 0;
                                    Serial.println("[HueGatewayModule] EventStream disabled for 10 minutes (TLS/memory pressure), polling remains active");
                                }
                                else
                                {
                                    Serial.printf("[HueGatewayModule] EventStream retry failed, next retry in %lu s\n",
                                                  static_cast<unsigned long>(_eventStreamRetryBackoffMs / 1000UL));
                                }
                            }
                            else
                            {
                                _eventStreamFailureCount = 0;
                            }
                        }
                    }
                }
            }
            else
            {
                _eventStreamRetryBackoffMs = kEventStreamRetryBaseMs;
                _eventStreamPauseUntilMs = 0;
                _eventStreamFailureCount = 0;

                if (fallbackActiveLogged)
                {
                    Serial.println("[HueGatewayModule] EventStream active, polling fallback suspended");
                    fallbackActiveLogged = false;
                }
            }

            // Drain pending EventStream updates (common for both connected and retry paths).
            {
                static HueGatewayEventLightUpdate updates[MAX_LIGHTS];
                static HueGatewayEventSensorUpdate sensorUpdates[MAX_LIGHTS];
                int sensorCount = 0;
                int updateCount = _client->pollEventStreamFull(updates, MAX_LIGHTS, sensorUpdates, MAX_LIGHTS, sensorCount);
                if (updateCount > 0)
                {
                    _lastChannelSyncOkMs = now;
                    Serial.printf("[HueGatewayModule] EventStream light updates received: %d\n", updateCount);
                    applyEventStreamUpdates(updates, updateCount);
                }
                if (sensorCount > 0)
                {
                    _lastChannelSyncOkMs = now;
                    Serial.printf("[HueGatewayModule] EventStream sensor updates received: %d\n", sensorCount);
                    applyEventStreamDeviceUpdates(sensorUpdates, sensorCount);
                }
            }
        }
    }

    // --- Auto-delete behavior_instances when SSE reports a new one ---
    if (_client && _devicesInitialized && _client->hasBehaviorInstanceEvent())
    {
        _client->clearBehaviorInstanceEvent();
        if (!_biAutoDeletePending)
        {
            _biAutoDeletePending = true;
            _biAutoDeleteTriggerMs = now;
            Serial.println("[HueGatewayModule] behavior_instance SSE event -> delete pending (2s debounce)");
        }
        else
        {
            // Reset debounce timer on subsequent events
            _biAutoDeleteTriggerMs = now;
        }
    }
    if (_biAutoDeletePending && (now - _biAutoDeleteTriggerMs >= 2000UL))
    {
        _biAutoDeletePending = false;
        processBehaviorInstanceAutoDelete();
    }

    if (!_authPending && !_manualPairingRequired && (_bridgeStatus == BridgeStatus::CONNECTION_LOST || _bridgeStatus == BridgeStatus::BRIDGE_UNREACHABLE || !_client || !_client->isInitialized()))
    {
        if (_bridgeIP.isEmpty())
        {
            if (now >= sBridgeNextDiscoveryTryMs)
            {
                _bridgeIP = getBridgeIP();
                sBridgeNextDiscoveryTryMs = now + kBridgeDiscoveryRetryMs;
            }
        }

        bool hasStoredKey = _auth.loadStoredAppKey();
        if (!hasStoredKey)
        {
            _manualPairingRequired = true;
            _authPending = false;
            updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
            Serial.println("[HueGatewayModule] No stored App-Key during reconnect. Waiting for manual pairing trigger (KO).\n");
            return;
        }
        _manualPairingRequired = false;

        if (!_bridgeIP.isEmpty() && (now - _lastReconnectTryMs >= _reconnectBackoffMs))
        {
            _lastReconnectTryMs = now;
            Serial.printf("[HueGatewayModule] Reconnect attempt (backoff=%lus)\n", static_cast<unsigned long>(_reconnectBackoffMs / 1000UL));

            updateStatus(BridgeStatus::CONNECTING);

            // Soft reconnect: if client is still alive and we have a valid light mapping,
            // try a lightweight bridge ping and re-run setupDevices() without destroying
            // the existing mapping. This preserves KNX<->Hue connectivity during transient
            // Bridge API issues (e.g., /clip/v2/resource/light returning empty).
            if (_client && _client->isInitialized() && _lightCount > 0 && _client->pingBridgeApiV2())
            {
                Serial.println("[HueGatewayModule] Soft reconnect: bridge reachable, retrying setupDevices with existing mapping");
                appendDiagnosticLog("INFO", "CONN", "soft reconnect: bridge ping ok, retrying setupDevices");
                setupDevices();
                if (_lightCount > 0)
                {
                    updateStatus(BridgeStatus::CONNECTED);
                    _reconnectBackoffMs = 10000;
                    Serial.println("[HueGatewayModule] Soft reconnect successful, mapping preserved");
                }
                else
                {
                    _reconnectBackoffMs = min<unsigned long>(_reconnectBackoffMs * 2UL, 120000UL);
                    updateStatus(BridgeStatus::CONNECTION_LOST);
                    Serial.println("[HueGatewayModule] Soft reconnect failed: mapping lost after setupDevices");
                }
            }
            else if (initClientWithAppKey())
            {
                uint8_t configuredChannels = ParamHUE_HUEChannelCount;
                if (configuredChannels > MAX_LIGHTS)
                {
                    configuredChannels = MAX_LIGHTS;
                }

                const bool requiresDeviceSetup = configuredChannels > 0;
                bool bridgeReachable = true;
                if (!requiresDeviceSetup)
                {
                    bridgeReachable = _client && _client->pingBridgeApiV2();
                }
                if (bridgeReachable)
                {
                    if (requiresDeviceSetup)
                    {
                        setupDevices();
                    }

                    const bool setupHealthy = (!requiresDeviceSetup) || (_lightCount > 0);
                    if (setupHealthy)
                    {
                        updateStatus(BridgeStatus::CONNECTED);
                        _reconnectBackoffMs = 10000;
                        Serial.println("[HueGatewayModule] Reconnect successful");
                    }
                    else
                    {
                        _reconnectBackoffMs = min<unsigned long>(_reconnectBackoffMs * 2UL, 120000UL);
                        updateStatus(BridgeStatus::CONNECTION_LOST);
                        Serial.println("[HueGatewayModule] Reconnect not accepted: setupDevices mapped 0 lights");
                    }
                }
                else
                {
                    _reconnectBackoffMs = min<unsigned long>(_reconnectBackoffMs * 2UL, 120000UL);
                    updateStatus(BridgeStatus::CONNECTION_LOST);
                    Serial.println("[HueGatewayModule] Reconnect failed: bridge ping unsuccessful");
                }
            }
            else
            {
                _reconnectBackoffMs = min<unsigned long>(_reconnectBackoffMs * 2UL, 120000UL);
                updateStatus(BridgeStatus::CONNECTION_LOST);
                Serial.println("[HueGatewayModule] Reconnect failed");
            }
        }
    }

    if (_client && _client->isInitialized() && !_authPending)
    {
        uint8_t configuredChannels = ParamHUE_HUEChannelCount;
        if (configuredChannels > MAX_LIGHTS)
        {
            configuredChannels = MAX_LIGHTS;
        }

        if (configuredChannels > 0 && _deviceSetupNeedsRetry)
        {
            if (hasLoopBudget("setupDevices-retry")
                && (now - _lastDeviceSetupRetryMs) >= _deviceSetupRetryBackoffMs)
            {
                _lastDeviceSetupRetryMs = now;
                Serial.printf("[HueGatewayModule] Device map requires retry (%u channels configured), retrying setupDevices()\n",
                              static_cast<unsigned>(configuredChannels));
                setupDevices();
            }
        }
    }

    // Hue->KNX refresh in 1-second ticks (channel-specific poll interval applies in refreshLightStatus).
    if (now - _lastRefreshTickMs >= 1000)
    {
        _lastRefreshTickMs = now;
        if (!(_client && (_client->isEventStreamConnected() || _client->isEventStreamConnectPending()))
            && (now >= HueGatewayLight::globalHclWriteNextAllowedMs()))
        {
            if (!fallbackActiveLogged)
            {
                Serial.println("[HueGatewayModule] Polling fallback active (EventStream disconnected)");
                fallbackActiveLogged = true;
            }
            if (hasLoopBudget("refreshLightStatus"))
            {
                refreshLightStatus();
            }
        }
    }

    if (_webScanRequested && !_webScanInProgress && _client && _initialized && _client->isInitialized())
    {
        _webScanRequested = false;
        _webScanInProgress = true;
        _webScanStartedMs = millis();
        _lastWebScanError = "";
        updateWebScanCache();
        _lastWebScanDurationMs = (_webScanStartedMs == 0) ? 0 : (millis() - _webScanStartedMs);
        _webScanStartedMs = 0;
        _lastWebScanMs = millis();
        _webScanInProgress = false;
    }
}

const std::string HueGatewayModule::name()
{
    return "HueGatewayModule";
}

const std::string HueGatewayModule::version()
{
    std::string v = MODULE_HueGatewayModule_Version;
    auto pos = v.find('+');
    if (pos != std::string::npos)
        v = v.substr(0, pos);
    return v;
}

HueGatewayLight* HueGatewayModule::lightAt(int ch) const
{
    if (ch < 0 || ch >= MAX_CHANNELS)
        return nullptr;
    HueGatewayDevice* d = _devices[ch];
    if (d == nullptr || d->deviceType() != 0)
        return nullptr;
    return static_cast<HueGatewayLight*>(d);
}

HueGatewayPlug* HueGatewayModule::plugAt(int ch) const
{
    if (ch < 0 || ch >= MAX_CHANNELS)
        return nullptr;
    HueGatewayDevice* d = _devices[ch];
    if (d == nullptr || d->deviceType() != 4)
        return nullptr;
    return static_cast<HueGatewayPlug*>(d);
}

void HueGatewayModule::processInputKo(GroupObject& ko)
{
    if (!_initialized)
        return;
        
    uint16_t koNumber = ko.asap();
    
    if (koNumber == HUE_KoHUEPairingTrigger)
    {
        const bool trigger = ko.value(Dpt(1, 1));
        const unsigned long nowMs = millis();
        const bool debounceElapsed = (_lastPairingTriggerMs == 0)
                                     || (nowMs < _lastPairingTriggerMs)
                                     || ((nowMs - _lastPairingTriggerMs) >= 1500UL);

        Serial.printf("[HueGatewayModule] Pairing KO received (value=%u, last=%u, debounce=%u)\n",
                      trigger ? 1 : 0,
                      _pairingTriggerLastState ? 1 : 0,
                      debounceElapsed ? 1 : 0);

        if (trigger && (!_pairingTriggerLastState || debounceElapsed))
        {
            Serial.println("[HueGatewayModule] Pairing triggered via ETS KO");
            _lastPairingTriggerMs = nowMs;
            startPairing();
        }
        _pairingTriggerLastState = trigger;
        return;
    }

    #ifdef HUE_KoHUEHCLLock
    if (koNumber == HUE_KoHUEHCLLock)
    {
        bool lockRequest = ko.value(Dpt(1, 1));
        setHclLock(lockRequest, "KO");
        return;
    }
    #endif

    #ifdef HUE_KoHUEHCLReleaseTrigger
    if (koNumber == HUE_KoHUEHCLReleaseTrigger)
    {
        if (ko.value(Dpt(1, 1)))
        {
            setHclLock(false, "KO release trigger");
            for (uint8_t manager = 1; manager <= HCL::MasterManager::MAX_MASTERS; manager++)
            {
                setHclManagerLock(manager, false, "KO release trigger");
            }
            for (uint8_t channel = 0; channel < MAX_LIGHTS; channel++)
            {
                setHclChannelLock(channel, false, "KO release trigger");
            }
        }
        return;
    }
    #endif

    {
        struct HclMasterKoEntry { uint16_t ko; uint8_t master; };
        static const HclMasterKoEntry hclMasterKos[] = {
#ifdef HUE_KoHUEHCLM1Lock
            { HUE_KoHUEHCLM1Lock, 1 },
#endif
#ifdef HUE_KoHUEHCLM2Lock
            { HUE_KoHUEHCLM2Lock, 2 },
#endif
#ifdef HUE_KoHUEHCLM3Lock
            { HUE_KoHUEHCLM3Lock, 3 },
#endif
#ifdef HUE_KoHUEHCLM4Lock
            { HUE_KoHUEHCLM4Lock, 4 },
#endif
#ifdef HUE_KoHUEHCLM5Lock
            { HUE_KoHUEHCLM5Lock, 5 },
#endif
#ifdef HUE_KoHUEHCLM6Lock
            { HUE_KoHUEHCLM6Lock, 6 },
#endif
#ifdef HUE_KoHUEHCLM7Lock
            { HUE_KoHUEHCLM7Lock, 7 },
#endif
#ifdef HUE_KoHUEHCLM8Lock
            { HUE_KoHUEHCLM8Lock, 8 },
#endif
            { 0, 0 } // sentinel
        };
        for (size_t i = 0; hclMasterKos[i].master != 0; i++)
        {
            if (koNumber == hclMasterKos[i].ko)
            {
                setHclManagerLock(hclMasterKos[i].master, ko.value(Dpt(1, 1)), "KO");
                return;
            }
        }
    }

    {
        struct HclSummerKoEntry { uint16_t ko; uint8_t master; };
        static const HclSummerKoEntry hclSummerKos[] = {
#ifdef HUE_KoHUEHCLM1SummerActive
            { HUE_KoHUEHCLM1SummerActive, 1 },
#endif
#ifdef HUE_KoHUEHCLM2SummerActive
            { HUE_KoHUEHCLM2SummerActive, 2 },
#endif
#ifdef HUE_KoHUEHCLM3SummerActive
            { HUE_KoHUEHCLM3SummerActive, 3 },
#endif
#ifdef HUE_KoHUEHCLM4SummerActive
            { HUE_KoHUEHCLM4SummerActive, 4 },
#endif
#ifdef HUE_KoHUEHCLM5SummerActive
            { HUE_KoHUEHCLM5SummerActive, 5 },
#endif
#ifdef HUE_KoHUEHCLM6SummerActive
            { HUE_KoHUEHCLM6SummerActive, 6 },
#endif
#ifdef HUE_KoHUEHCLM7SummerActive
            { HUE_KoHUEHCLM7SummerActive, 7 },
#endif
#ifdef HUE_KoHUEHCLM8SummerActive
            { HUE_KoHUEHCLM8SummerActive, 8 },
#endif
            { 0, 0 } // sentinel
        };
        for (size_t i = 0; hclSummerKos[i].master != 0; i++)
        {
            if (koNumber == hclSummerKos[i].ko)
            {
                HCL::Master* master = HCL::masterManager.getMaster(hclSummerKos[i].master);
                if (master)
                    master->setIsSummer(ko.value(Dpt(1, 1)));
                return;
            }
        }
    }

    // === Adaptive Helligkeit: Lux-Eingang (DPT 9.004) ===
    {
        struct HclLuxKoEntry { uint16_t ko; uint8_t master; };
        static const HclLuxKoEntry hclLuxKos[] = {
#ifdef HUE_KoHUEHCLM1AmbientLux
            { HUE_KoHUEHCLM1AmbientLux, 1 },
#endif
#ifdef HUE_KoHUEHCLM2AmbientLux
            { HUE_KoHUEHCLM2AmbientLux, 2 },
#endif
#ifdef HUE_KoHUEHCLM3AmbientLux
            { HUE_KoHUEHCLM3AmbientLux, 3 },
#endif
#ifdef HUE_KoHUEHCLM4AmbientLux
            { HUE_KoHUEHCLM4AmbientLux, 4 },
#endif
#ifdef HUE_KoHUEHCLM5AmbientLux
            { HUE_KoHUEHCLM5AmbientLux, 5 },
#endif
#ifdef HUE_KoHUEHCLM6AmbientLux
            { HUE_KoHUEHCLM6AmbientLux, 6 },
#endif
#ifdef HUE_KoHUEHCLM7AmbientLux
            { HUE_KoHUEHCLM7AmbientLux, 7 },
#endif
#ifdef HUE_KoHUEHCLM8AmbientLux
            { HUE_KoHUEHCLM8AmbientLux, 8 },
#endif
            { 0, 0 }
        };
        for (size_t i = 0; hclLuxKos[i].master != 0; i++)
        {
            if (koNumber == hclLuxKos[i].ko)
            {
                const float lux = ko.value(Dpt(9, 4));
                HCL::masterManager.setMasterAmbientLux(hclLuxKos[i].master, lux);
                // Status-KO "Adaptive aktiv" nach Lux-Update senden
                {
                    const uint8_t mn = hclLuxKos[i].master;
                    const bool active = HCL::masterManager.isMasterAdaptiveActive(mn);
                    switch (mn) {
#ifdef HUE_KoHUEHCLM1AdaptiveActive
                        case 1: knx.getGroupObject(HUE_KoHUEHCLM1AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM2AdaptiveActive
                        case 2: knx.getGroupObject(HUE_KoHUEHCLM2AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM3AdaptiveActive
                        case 3: knx.getGroupObject(HUE_KoHUEHCLM3AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM4AdaptiveActive
                        case 4: knx.getGroupObject(HUE_KoHUEHCLM4AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM5AdaptiveActive
                        case 5: knx.getGroupObject(HUE_KoHUEHCLM5AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM6AdaptiveActive
                        case 6: knx.getGroupObject(HUE_KoHUEHCLM6AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM7AdaptiveActive
                        case 7: knx.getGroupObject(HUE_KoHUEHCLM7AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
#ifdef HUE_KoHUEHCLM8AdaptiveActive
                        case 8: knx.getGroupObject(HUE_KoHUEHCLM8AdaptiveActive).value(active, Dpt(1, 11)); break;
#endif
                        default: break;
                    }
                }
                return;
            }
        }
    }

    // === Adaptive Helligkeit: Tag/Nacht-Eingang (DPT 1.001) ===
    {
        struct HclDayNightKoEntry { uint16_t ko; uint8_t master; };
        static const HclDayNightKoEntry hclDayNightKos[] = {
#ifdef HUE_KoHUEHCLM1DayNight
            { HUE_KoHUEHCLM1DayNight, 1 },
#endif
#ifdef HUE_KoHUEHCLM2DayNight
            { HUE_KoHUEHCLM2DayNight, 2 },
#endif
#ifdef HUE_KoHUEHCLM3DayNight
            { HUE_KoHUEHCLM3DayNight, 3 },
#endif
#ifdef HUE_KoHUEHCLM4DayNight
            { HUE_KoHUEHCLM4DayNight, 4 },
#endif
#ifdef HUE_KoHUEHCLM5DayNight
            { HUE_KoHUEHCLM5DayNight, 5 },
#endif
#ifdef HUE_KoHUEHCLM6DayNight
            { HUE_KoHUEHCLM6DayNight, 6 },
#endif
#ifdef HUE_KoHUEHCLM7DayNight
            { HUE_KoHUEHCLM7DayNight, 7 },
#endif
#ifdef HUE_KoHUEHCLM8DayNight
            { HUE_KoHUEHCLM8DayNight, 8 },
#endif
            { 0, 0 }
        };
        for (size_t i = 0; hclDayNightKos[i].master != 0; i++)
        {
            if (koNumber == hclDayNightKos[i].ko)
            {
                const bool isDaytime = ko.value(Dpt(1, 1));
                HCL::masterManager.setMasterDaytime(hclDayNightKos[i].master, isDaytime);
                return;
            }
        }
    }

    if (koNumber < HUE_KoBlockOffset)
    {
        return;
    }

    int32_t channel = HUE_KoCalcChannel(koNumber);
    if (channel < 0 || channel >= MAX_LIGHTS)
    {
        static uint32_t lastOutOfRangeLogMs = 0;
        uint32_t nowMs = millis();
        if ((nowMs - lastOutOfRangeLogMs) >= 10000 || nowMs < lastOutOfRangeLogMs)
        {
            Serial.printf("[HueGatewayModule] KO %d outside Hue channel range\n", koNumber);
            lastOutOfRangeLogMs = nowMs;
        }
        return;
    }

    uint8_t koType = static_cast<uint8_t>((koNumber - HUE_KoBlockOffset) % HUE_KoBlockSize);
    uint32_t nowMs = millis();

    if (koType == 9)
    {
        setHclChannelLock(static_cast<uint8_t>(channel), ko.value(Dpt(1, 1)), "KO");
        return;
    }

    uint8_t _channelIndex = static_cast<uint8_t>(channel);
    uint8_t syncDir = ParamHUE_CHSyncDir;
    if (syncDir > 2)
    {
        syncDir = 2;
    }
    bool isCommandKo = (koType == 0 || koType == 1 || koType == 2 || koType == 5 || koType == 7 || koType == 11);

    if (isCommandKo)
    {
        _diagCounterKoCommands++;
        _diagLastKoCommandMs[channel] = nowMs;
        _diagLastKoType[channel] = koType;
        _diagLastKoBlockReason[channel] = "ok";
    }

    if (isCommandKo && !(syncDir == 0 || syncDir == 2))
    {
        _diagCounterKoBlockedSyncDir++;
        _diagLastKoBlockReason[channel] = "syncdir";
        Serial.printf("[HueGatewayModule] Channel %d SyncDir=%u blocks KNX->Hue command (KO type %u)\n",
                      channel + 1,
                      static_cast<unsigned>(syncDir),
                      static_cast<unsigned>(koType));
        appendDiagnosticLog("WARN", "KNX", String("ch=") + String(channel + 1)
            + " blocked=syncdir"
            + " sync=" + String(static_cast<unsigned>(syncDir))
            + " koType=" + String(static_cast<unsigned>(koType)));
        return;
    }

    if (_devices[channel] == nullptr)
    {
        static uint32_t lastRecoverTryMs = 0;
        if (_client && _client->isInitialized() && ((nowMs - lastRecoverTryMs) >= 5000 || nowMs < lastRecoverTryMs))
        {
            lastRecoverTryMs = nowMs;
            Serial.println("[HueGatewayModule] Channel map missing, retrying setupDevices()...");
            setupDevices();
        }

        if (_devices[channel] == nullptr)
        {
            if (isCommandKo)
            {
                _diagCounterKoBlockedChannelMissing++;
                _diagLastKoBlockReason[channel] = "channel-missing";
            }

            static uint32_t lastNotConfiguredLogMs = 0;
            if ((nowMs - lastNotConfiguredLogMs) >= 10000 || nowMs < lastNotConfiguredLogMs)
            {
                Serial.printf("[HueGatewayModule] Channel %d not configured\n", channel + 1);
                lastNotConfiguredLogMs = nowMs;
            }

            if (isCommandKo)
            {
                appendDiagnosticLog("WARN", "KNX", String("ch=") + String(channel + 1)
                    + " blocked=channel-missing"
                    + " koType=" + String(static_cast<unsigned>(koType)));
            }
            return;
        }
    }

    unsigned long commandStartMs = nowMs;
    unsigned long beforeHueWriteMs = (_devices[channel] != nullptr) ? _devices[channel]->getLastHueWriteSuccessMs() : 0;
    String commandValue = "-";
    HueGatewayClient::DiagnosticsStats statsBefore;
    if (_client != nullptr)
    {
        statsBefore = _client->getDiagnosticsStats();
    }

    switch (koType)
    {
        case 0:
        {
            bool value = ko.value(Dpt(1, 1));
            commandValue = String(value ? "1" : "0");
            if (auto* light = lightAt(channel))
                light->processKnxSwitch(value);
            else if (_devices[channel] != nullptr)
                _devices[channel]->processKoInput(0, ko);
            break;
        }
        case 1:
        {
            uint8_t value = ko.value(Dpt(5, 1));
            commandValue = String(static_cast<unsigned>(value));
            if (auto* light = lightAt(channel)) light->processKnxBrightness(value);
            break;
        }
        case 2:
        {
            uint8_t controlBit = ko.value(Dpt(3, 7, 0));
            uint8_t stepCode = ko.value(Dpt(3, 7, 1));
            uint8_t value = static_cast<uint8_t>(((controlBit & 0x01) << 3) | (stepCode & 0x07));
            commandValue = String(static_cast<unsigned>(value));
            if (auto* light = lightAt(channel)) light->processKnxDimming(value);
            break;
        }
        case 3:
        case 4:
            break;
        case 5:
        {
            uint16_t kelvin = ko.value(Dpt(7, 600));
            commandValue = String(static_cast<unsigned>(kelvin));
            if (auto* light = lightAt(channel)) light->processKnxColorTemp(kelvin);
            break;
        }
        case 6:
            break;
        case 7:
        {
            uint8_t* rgb = ko.valueRef();
            commandValue = String(static_cast<unsigned>(rgb[0])) + "," + String(static_cast<unsigned>(rgb[1])) + "," + String(static_cast<unsigned>(rgb[2]));
            if (auto* light = lightAt(channel)) light->processKnxColorRGB(rgb[0], rgb[1], rgb[2]);
            break;
        }
        case 8:
            // Rotary KO: incoming value from bus (actuator status / GroupValueResponse)
            // syncs the button's internal rotary position tracking.
            if (_devices[channel] != nullptr && _devices[channel]->deviceType() == 2)
            {
                static_cast<HueGatewayButton*>(_devices[channel])->handleRotaryStatusKo(ko);
                Serial.printf("[HueGatewayModule] Rotary KO sync ch%d\n", channel + 1);
            }
            break;
        case 10:
            break;
        case 11:
        {
            // Check SceneEnabled flag (byte 88, bit 7 = ETS BitOffset 0).
            bool sceneEnabled = ParamHUE_CHSceneEnabled;
            if (!sceneEnabled)
                break;

            bool storeActive = ParamHUE_CHSceneStoreActive;

            // Read KO value: DPT 18.001 if storeActive, DPT 17.001 otherwise
            uint8_t raw;
            bool isSave = false;
            uint8_t sceneNumber;
            if (storeActive)
            {
                raw = static_cast<uint8_t>(ko.value(Dpt(18, 1)));
                isSave = (raw & 0x80) != 0;
                sceneNumber = raw & 0x3F;
            }
            else
            {
                raw = static_cast<uint8_t>(ko.value(Dpt(17, 1)));
                sceneNumber = raw & 0x3F;
            }

            if (isSave)
            {
                // --- Scene Store (DPT 18.001, Bit 7 = 1) ---

                // Find which ETS slot has this scene number
                int8_t storeSlot = -1;
                for (uint8_t s = 0; s < 8; s++)
                {
                    const uint16_t base = 89 + static_cast<uint16_t>(s) * 9;
                    const uint8_t paramNumber = knx.paramByte(HUE_ParamCalcIndex(base + 0));
                    if (paramNumber != 0 && paramNumber == static_cast<uint8_t>(sceneNumber + 1))
                    {
                        storeSlot = static_cast<int8_t>(s);
                        break;
                    }
                }
                if (storeSlot < 0)
                    break;  // No matching slot configured

                SceneStoreData& sd = _sceneStore[channel][storeSlot];
                if (auto* light = lightAt(channel))
                {
                    sd.onOff = light->isOn() ? 1 : 0;
                    // getBrightness() returns Hue range 0-254, convert to 0-100%.
                    sd.brightness = static_cast<uint8_t>((static_cast<uint16_t>(light->getBrightness()) * 100U + 127U) / 254U);
                    sd.colorTemp = light->getColorTempKelvin();
                    sd.red = light->getRed();
                    sd.green = light->getGreen();
                    sd.blue = light->getBlue();
                    sd.valid = SCENE_STORE_VALID;
                }
                else if (auto* plug = plugAt(channel))
                {
                    sd.onOff = plug->isOn() ? 1 : 0;
                    sd.brightness = 0;
                    sd.colorTemp = 0;
                    sd.red = 0;
                    sd.green = 0;
                    sd.blue = 0;
                    sd.valid = SCENE_STORE_VALID;
                }
                openknx.flash.save();
                commandValue = String("store:") + String(static_cast<unsigned>(sceneNumber));
                break;
            }

            // --- Scene Recall (DPT 18.001, Bit 7 = 0) ---

            // Scan ETS slots 1-8
            for (uint8_t s = 0; s < 8; s++)
            {
                const uint16_t base = 89 + static_cast<uint16_t>(s) * 9;
                const uint8_t paramNumber = knx.paramByte(HUE_ParamCalcIndex(base + 0));
                if (paramNumber == 0)
                    continue;  // nicht aktiv
                if (paramNumber != static_cast<uint8_t>(sceneNumber + 1))
                    continue;  // wrong scene number

                // Check if this slot has a stored scene (overrides ETS preset)
                bool storeActive = ParamHUE_CHSceneStoreActive;
                if (storeActive)
                {
                    const SceneStoreData& sd = _sceneStore[channel][s];
                    if (sd.valid == SCENE_STORE_VALID)
                    {
                        bool onOff = sd.onOff != 0;
                        if (auto* light = lightAt(channel))
                            light->processKnxSceneRecall(onOff, true, sd.brightness, true, sd.colorTemp, true, sd.red, sd.green, sd.blue);
                        else if (auto* plug = plugAt(channel))
                            plug->processKnxSceneRecall(onOff);
                        commandValue = String("store-recall:") + String(static_cast<unsigned>(sceneNumber));
                        break;
                    }
                }

                const uint8_t action = knx.paramByte(HUE_ParamCalcIndex(base + 1));
                commandValue = String(static_cast<unsigned>(sceneNumber));

                if (action == 7)
                {
                    // Hue-Scene recall: look up RID from module-level params.
                    const uint8_t hueSceneRef = knx.paramByte(HUE_ParamCalcIndex(base + 2));
                    String        rid;
                    switch (hueSceneRef)
                    {
                        case 1: rid = String(ParamHUE_HUEHueScene1RIDStr.c_str()); break;
                        case 2: rid = String(ParamHUE_HUEHueScene2RIDStr.c_str()); break;
                        case 3: rid = String(ParamHUE_HUEHueScene3RIDStr.c_str()); break;
                        case 4: rid = String(ParamHUE_HUEHueScene4RIDStr.c_str()); break;
                        case 5: rid = String(ParamHUE_HUEHueScene5RIDStr.c_str()); break;
                        case 6: rid = String(ParamHUE_HUEHueScene6RIDStr.c_str()); break;
                        case 7: rid = String(ParamHUE_HUEHueScene7RIDStr.c_str()); break;
                        case 8: rid = String(ParamHUE_HUEHueScene8RIDStr.c_str()); break;
                        default: break;
                    }
                    if (rid.length() > 0 && _client != nullptr)
                    {
                        _client->recallHueScene(String(rid));
                        if (_devices[channel] != nullptr && _devices[channel]->getHCLMaster() > 0)
                            setHclChannelLock(static_cast<uint8_t>(channel), true, "Hue scene recall");
                    }
                }
                else
                {
                    // ETS-Preset action
                    bool onOff      = (action >= 1);  // 0=Aus, else Ein
                    bool applyBri   = (action == 2 || action == 4 || action == 6);
                    bool applyCT    = (action == 3 || action == 4);
                    bool applyColor = (action == 5 || action == 6);

                    const uint8_t  bri   = knx.paramByte(HUE_ParamCalcIndex(base + 3));
                    const uint16_t ct    = knx.paramWord(HUE_ParamCalcIndex(base + 4));
                    const uint8_t  red   = knx.paramByte(HUE_ParamCalcIndex(base + 6));
                    const uint8_t  green = knx.paramByte(HUE_ParamCalcIndex(base + 7));
                    const uint8_t  blue  = knx.paramByte(HUE_ParamCalcIndex(base + 8));

                    if (auto* light = lightAt(channel))
                        light->processKnxSceneRecall(onOff, applyBri, bri, applyCT, ct, applyColor, red, green, blue);
                    else if (auto* plug = plugAt(channel))
                        plug->processKnxSceneRecall(onOff);
                }
                break;  // First matching slot wins.
            }
            break;
        }
        default:
            break;
    }

    if (isCommandKo)
    {
        const unsigned long finishedMs = millis();
        const unsigned long durationMs = (finishedMs >= commandStartMs) ? (finishedMs - commandStartMs) : 0;
        _diagLastKoValue[channel] = commandValue;
        _diagLastWriteTraceMs[channel] = finishedMs;
        _diagLastWriteTraceDurationMs[channel] = durationMs;

        int httpStatus = 0;
        String httpMethod = "-";
        String httpEndpoint = "-";
        String result = "no-http";
        uint32_t putDelta = 0;

        if (_client != nullptr)
        {
            const HueGatewayClient::DiagnosticsStats& statsAfter = _client->getDiagnosticsStats();
            if (statsAfter.httpPutCount >= statsBefore.httpPutCount)
            {
                putDelta = statsAfter.httpPutCount - statsBefore.httpPutCount;
            }
            else
            {
                putDelta = (0xFFFFFFFFu - statsBefore.httpPutCount) + statsAfter.httpPutCount + 1u;
            }

            httpStatus = statsAfter.lastHttpStatusCode;
            if (statsAfter.lastHttpMethod.length() > 0)
            {
                httpMethod = statsAfter.lastHttpMethod;
            }
            if (statsAfter.lastHttpEndpoint.length() > 0)
            {
                httpEndpoint = statsAfter.lastHttpEndpoint;
            }
        }

        const unsigned long afterHueWriteMs = (_devices[channel] != nullptr) ? _devices[channel]->getLastHueWriteSuccessMs() : 0;
        const bool writeSuccessByTimestamp = (afterHueWriteMs > 0) && (afterHueWriteMs != beforeHueWriteMs) && (afterHueWriteMs >= commandStartMs);
        const bool writeSuccessByStatus = (putDelta > 0) && (httpStatus >= 200) && (httpStatus < 300);
        if (writeSuccessByTimestamp || writeSuccessByStatus)
        {
            result = "ok";
        }
        else if (putDelta > 0)
        {
            result = "http-fail";
        }

        _diagLastWriteTraceHttpStatus[channel] = httpStatus;
        _diagLastWriteTraceMethod[channel] = httpMethod;
        _diagLastWriteTraceEndpoint[channel] = httpEndpoint;
        _diagLastWriteTraceResult[channel] = result;

        String fadeInfo = "";
        if (koType == 0 && _devices[channel] != nullptr)
        {
            bool switchVal = (commandValue == "1");
            uint8_t fadeSec = switchVal ? _devices[channel]->getSwitchOnTransitionSec() : _devices[channel]->getSwitchOffTransitionSec();
            fadeInfo = " fadeSec=" + String(static_cast<unsigned>(fadeSec));
        }

        appendDiagnosticLog(writeSuccessByTimestamp || writeSuccessByStatus ? "INFO" : "WARN",
            "KNXWRITE",
            String("ch=") + String(channel + 1)
            + " koType=" + String(static_cast<unsigned>(koType))
            + " value=" + commandValue
            + fadeInfo
            + " result=" + result
            + " status=" + String(httpStatus)
            + " method=" + httpMethod
            + " endpoint=" + httpEndpoint
            + " durMs=" + String(durationMs));
    }

    if (isCommandKo && _devices[channel] != nullptr)
    {
        if (syncDir == 1 || syncDir == 2)
        {
            if (_channelFastTrackCooldownUntilMs[channel] == 0 || nowMs >= _channelFastTrackCooldownUntilMs[channel])
            {
                _channelFastTrackRemaining[channel] = kFastTrackChecksPerCommand;
                _channelFastTrackNextMs[channel] = nowMs + kFastTrackFirstDelayMs;
                _channelFastTrackCooldownUntilMs[channel] = nowMs + kFastTrackCooldownMs;
            }
        }
    }
}

uint16_t HueGatewayModule::flashSize()
{
    // Version (1 byte) + per-channel per-slot scene store data (8 bytes each, 8 slots)
    return 1 + MAX_CHANNELS * SCENE_SLOTS * 8;
}

void HueGatewayModule::writeFlash()
{
    openknx.flash.writeByte(SCENE_STORE_VERSION);
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
    {
        for (int s = 0; s < SCENE_SLOTS; s++)
        {
            const SceneStoreData& sd = _sceneStore[ch][s];
            openknx.flash.writeByte(sd.valid);
            openknx.flash.writeByte(sd.onOff);
            openknx.flash.writeByte(sd.brightness);
            openknx.flash.writeByte(static_cast<uint8_t>(sd.colorTemp >> 8));
            openknx.flash.writeByte(static_cast<uint8_t>(sd.colorTemp & 0xFF));
            openknx.flash.writeByte(sd.red);
            openknx.flash.writeByte(sd.green);
            openknx.flash.writeByte(sd.blue);
        }
    }
}

void HueGatewayModule::readFlash(const uint8_t* data, const uint16_t size)
{
    if (size == 0)
        return;  // First boot, no data

    uint8_t version = openknx.flash.readByte();
    if (version != SCENE_STORE_VERSION)
        return;

    uint16_t maxSlots = static_cast<uint16_t>((size - 1) / 8);
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
    {
        for (int s = 0; s < SCENE_SLOTS; s++)
        {
            uint16_t slotIdx = static_cast<uint16_t>(ch) * SCENE_SLOTS + s;
            if (slotIdx >= maxSlots)
                return;
            SceneStoreData& sd = _sceneStore[ch][s];
            sd.valid      = openknx.flash.readByte();
            sd.onOff      = openknx.flash.readByte();
            sd.brightness = openknx.flash.readByte();
            uint8_t ctHi  = openknx.flash.readByte();
            uint8_t ctLo  = openknx.flash.readByte();
            sd.colorTemp  = (static_cast<uint16_t>(ctHi) << 8) | ctLo;
            sd.red        = openknx.flash.readByte();
            sd.green      = openknx.flash.readByte();
            sd.blue       = openknx.flash.readByte();
        }
    }
}

bool HueGatewayModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (cmd == "hue")
    {
        openknx.console.printHelpLine("hue scan", "Scan for Hue Bridge and list all lights");
        openknx.console.printHelpLine("hue pair", "Start pairing workflow (press bridge button)");
        openknx.console.printHelpLine("hue status", "Show current module status");
        openknx.console.printHelpLine("hue hcl", "Show HCL runtime and advanced master settings");
        openknx.console.printHelpLine("hue retry", "Force setupDevices retry (reset backoff/circuit)");
        return true;
    }

    if (cmd == "hue pair")
    {
        if (startPairing())
        {
            Serial.println("[HueGatewayModule] Pairing command accepted. Press Hue Bridge button now.");
        }
        else
        {
            Serial.println("[HueGatewayModule] Pairing command rejected. Check network and bridge configuration.");
        }
        return true;
    }

    if (cmd == "hue retry")
    {
        _deviceSetupNeedsRetry = true;
        _deviceSetupRetryBackoffMs = 0;
        _setupCircuitOpenUntilMs = 0;
        _setupCircuitTrips = 0;
        _consecutiveEmptyLightFetches = 0;
        _lastDeviceSetupRetryMs = 0;
        Serial.println("[HueGatewayModule] Manual setup retry triggered via console");
        appendDiagnosticLog("INFO", "CLI", "manual setup retry triggered");
        return true;
    }
    
    if (cmd == "hue scan")
    {
        Serial.println("=================================");
        Serial.println("Hue Bridge Scan");
        Serial.println("=================================");
        
        // Bridge finden
        HueGatewayDiscovery discovery;
        String bridgeIP;
        
        if (!discovery.findBridge(bridgeIP))
        {
            Serial.println("ERROR: No Hue Bridge found!");
            Serial.println("Check:");
            Serial.println("  - Bridge is powered on");
            Serial.println("  - Bridge is in same network");
            Serial.println("  - mDNS is working");
            return true;
        }
        
        Serial.printf("\nBridge found: %s\n\n", bridgeIP.c_str());
        
        // Check whether authentication is available (non-blocking).
        HueGatewayAuth auth;
        if (!auth.loadStoredAppKey())
        {
            Serial.println("No stored App-Key. Trying single non-blocking pairing request...");
            if (!auth.requestAppKeyOnce(bridgeIP.c_str()))
            {
                Serial.println("Authentication pending: Press button on Hue Bridge and run 'hue scan' again.");
                return true;
            }
        }
        
        // Fetch available lights from the bridge.
        HueGatewayClient client;
        if (!client.begin(bridgeIP, auth.getAppKey()))
        {
            Serial.println("ERROR: Client init failed!");
            return true;
        }
        
        HueGatewayLightState* lights = new (std::nothrow) HueGatewayLightState[kWebScanMaxLights];
        HueGatewayTargetInfo* rooms = new (std::nothrow) HueGatewayTargetInfo[kWebScanMaxTargets];
        HueGatewayTargetInfo* zones = new (std::nothrow) HueGatewayTargetInfo[kWebScanMaxTargets];

        if (lights == nullptr || rooms == nullptr || zones == nullptr)
        {
            delete[] lights;
            delete[] rooms;
            delete[] zones;
            Serial.println("ERROR: Nicht genug RAM fuer Hue-Scan-Puffer");
            return true;
        }

        int count = client.getLights(lights, kWebScanMaxLights);
        int roomCount = client.getRoomTargets(rooms, kWebScanMaxTargets);
        int zoneCount = client.getZoneTargets(zones, kWebScanMaxTargets);
        
        Serial.println("Found Lights:");
        Serial.println("---------------------------------");
        
        for (int i = 0; i < count; i++)
        {
            Serial.printf("%2d: %-25s %s\n", i, lights[i].name.c_str(), lights[i].id.c_str());
            Serial.printf("    Status: %s, Brightness: %d/254, Room: %s, Zone: %s\n",
                          lights[i].on ? "ON " : "OFF", lights[i].brightness,
                          lights[i].room.c_str(), lights[i].zone.c_str());
        }
        
        Serial.println("---------------------------------");
        Serial.printf("Total: %d lights\n\n", count);

        if (roomCount > 0)
        {
            Serial.println("Rooms (for TargetType=Room):");
            Serial.println("---------------------------------");
            for (int i = 0; i < roomCount; i++)
            {
                Serial.printf("%2d: %-25s roomRID=%s grouped=%s\n",
                              i,
                              rooms[i].name.c_str(),
                              rooms[i].id.c_str(),
                              rooms[i].groupedLightId.c_str());
            }
            Serial.println("---------------------------------");
        }

        if (zoneCount > 0)
        {
            Serial.println("Zones (for TargetType=Zone):");
            Serial.println("---------------------------------");
            for (int i = 0; i < zoneCount; i++)
            {
                Serial.printf("%2d: %-25s zoneRID=%s grouped=%s\n",
                              i,
                              zones[i].name.c_str(),
                              zones[i].id.c_str(),
                              zones[i].groupedLightId.c_str());
            }
            Serial.println("---------------------------------");
        }

        Serial.println("To use in ETS (new parameters):");
        Serial.println("1. Set 'Zieltyp' = Licht/Raum/Zone");
        Serial.println("2. Fill 'Hue Ziel-ID' with matching target ID or name");
        Serial.println("3. Set channel to active");
        Serial.println("Legacy fallback still works: 'Hue Lampen-ID (UUID)' or prefixes room:/zone:");
        Serial.println("=================================");

        delete[] lights;
        delete[] rooms;
        delete[] zones;
        
        return true;
    }
    
    if (cmd == "hue status")
    {
        Serial.println("=================================");
        Serial.println("Hue Bridge Module Status");
        Serial.println("=================================");
        Serial.printf("Initialized: %s\n", _initialized ? "Yes" : "No");
        Serial.printf("Client ready: %s\n", (_client && _client->isInitialized()) ? "Yes" : "No");
        Serial.printf("Configured lights: %d\n", _lightCount);
        
        if (_client && _client->isInitialized())
        {
            Serial.printf("Bridge IP: %s\n", _client->getBridgeIP().c_str());
        }
        
        Serial.println("\nConfigured Devices:");
        Serial.println("---------------------------------");
        
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            if (_devices[i] == nullptr)
                continue;
            Serial.printf("%2d: %-25s %s\n", i, 
                          _devices[i]->getName().c_str(),
                          _devices[i]->getResourceId().c_str());
            Serial.printf("    Status: %s, Brightness: %d\n",
                          _devices[i]->isOn() ? "ON " : "OFF",
                          _devices[i]->getBrightness());
        }

        Serial.println("\nHCL Status:");
        Serial.println("---------------------------------");

        #ifdef ParamHUE_HUEHCLEnable
        const bool hclEnabled = (ParamHUE_HUEHCLEnable != 0);
        Serial.printf("Enabled: %s\n", hclEnabled ? "Yes" : "No");
        #else
        const bool hclEnabled = false;
        Serial.println("Enabled: No");
        #endif

        if (hclEnabled)
        {
            #ifdef ParamHUE_HUEHCLMasterCount
            const uint8_t masterCount = ParamHUE_HUEHCLMasterCount;
            #else
            const uint8_t masterCount = 0;
            #endif

            #ifdef ParamHUE_HUEHCLUpdateInterval
            Serial.printf("Update interval: %us\n", static_cast<unsigned>(ParamHUE_HUEHCLUpdateInterval));
            #endif

            #ifdef ParamHUE_HUEHCLFadeDuration
            Serial.printf("Fade duration: %us\n", static_cast<unsigned>(ParamHUE_HUEHCLFadeDuration));
            #endif

            for (uint8_t masterNumber = 1; masterNumber <= HCL::MasterManager::MAX_MASTERS; masterNumber++)
            {
                if (masterNumber > masterCount)
                {
                    break;
                }

                HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
                if (!master)
                {
                    continue;
                }

                HCL::InterpolatedValue current = HCL::masterManager.getCurrentValue(masterNumber);
                Serial.printf("M%u: current=%uK/%u%% setpoints=%u\n",
                              static_cast<unsigned>(masterNumber),
                              static_cast<unsigned>(current.kelvin),
                              static_cast<unsigned>(current.brightness),
                              static_cast<unsigned>(master->getValidSetpointCount()));

                uint8_t curveTypeValue = 0;
                uint16_t slewRate = 0;
                uint16_t manualKelvin = 4000;
                String sunrise = "";
                String sunset = "";
                int16_t sunriseOffset = 0;
                int16_t sunsetOffset = 0;

                switch (masterNumber)
                {
                    case 1:
                        #ifdef ParamHUE_HCLM1CurveType
                        curveTypeValue = ParamHUE_HCLM1CurveType;
                        slewRate = ParamHUE_HCLM1SlewRate;
                        manualKelvin = ParamHUE_HCLM1ManualKelvin;
                        sunrise = readFixedTimeParam(ParamHUE_HCLM1Sunrise);
                        sunset = readFixedTimeParam(ParamHUE_HCLM1Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                        #endif
                        break;
                    case 2:
                        #ifdef ParamHUE_HCLM2CurveType
                        curveTypeValue = ParamHUE_HCLM2CurveType;
                        slewRate = ParamHUE_HCLM2SlewRate;
                        manualKelvin = ParamHUE_HCLM2ManualKelvin;
                        sunrise = readFixedTimeParam(ParamHUE_HCLM2Sunrise);
                        sunset = readFixedTimeParam(ParamHUE_HCLM2Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                        #endif
                        break;
                    case 3:
                        #ifdef ParamHUE_HCLM3CurveType
                        curveTypeValue = ParamHUE_HCLM3CurveType;
                        slewRate = ParamHUE_HCLM3SlewRate;
                        manualKelvin = ParamHUE_HCLM3ManualKelvin;
                        sunrise = readFixedTimeParam(ParamHUE_HCLM3Sunrise);
                        sunset = readFixedTimeParam(ParamHUE_HCLM3Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                        #endif
                        break;
                    case 4:
                        #ifdef ParamHUE_HCLM4CurveType
                        curveTypeValue = ParamHUE_HCLM4CurveType;
                        slewRate = ParamHUE_HCLM4SlewRate;
                        manualKelvin = ParamHUE_HCLM4ManualKelvin;
                        sunrise = readFixedTimeParam(ParamHUE_HCLM4Sunrise);
                        sunset = readFixedTimeParam(ParamHUE_HCLM4Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                        #endif
                        break;
                    case 5:
                        #ifdef ParamHUE_HCLM5CurveType
                        curveTypeValue = ParamHUE_HCLM5CurveType;
                        slewRate = ParamHUE_HCLM5SlewRate;
                        manualKelvin = ParamHUE_HCLM5ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM5Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM5Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM5SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM5SunsetOffset);
                        #endif
                        break;
                    case 6:
                        #ifdef ParamHUE_HCLM6CurveType
                        curveTypeValue = ParamHUE_HCLM6CurveType;
                        slewRate = ParamHUE_HCLM6SlewRate;
                        manualKelvin = ParamHUE_HCLM6ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM6Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM6Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM6SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM6SunsetOffset);
                        #endif
                        break;
                    case 7:
                        #ifdef ParamHUE_HCLM7CurveType
                        curveTypeValue = ParamHUE_HCLM7CurveType;
                        slewRate = ParamHUE_HCLM7SlewRate;
                        manualKelvin = ParamHUE_HCLM7ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM7Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM7Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM7SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM7SunsetOffset);
                        #endif
                        break;
                    case 8:
                        #ifdef ParamHUE_HCLM8CurveType
                        curveTypeValue = ParamHUE_HCLM8CurveType;
                        slewRate = ParamHUE_HCLM8SlewRate;
                        manualKelvin = ParamHUE_HCLM8ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM8Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM8Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM8SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM8SunsetOffset);
                        #endif
                        break;
                    default:
                        break;
                }

                const char* curveText = "FixedTime";
                if (curveTypeValue == 1)
                {
                    curveText = "SunWindow";
                }
                else if (curveTypeValue == 2)
                {
                    curveText = "Manual";
                }
                else if (curveTypeValue == 3)
                {
                    curveText = "Astronomical";
                }

                Serial.printf("    curve=%s slew=%uK/min manual=%uK sun=%s/%s offset=%d/%d applied=%uK\n",
                              curveText,
                              static_cast<unsigned>(slewRate),
                              static_cast<unsigned>(manualKelvin),
                              sunrise.c_str(),
                              sunset.c_str(),
                              static_cast<int>(sunriseOffset),
                              static_cast<int>(sunsetOffset),
                              static_cast<unsigned>(master->getAppliedKelvin()));
            }
        }
        
        Serial.println("=================================");
        
        return true;
    }

    if (cmd == "hue hcl")
    {
        Serial.println("=================================");
        Serial.println("Hue HCL Diagnostics");
        Serial.println("=================================");

        #ifdef ParamHUE_HUEHCLEnable
        const bool hclEnabled = (ParamHUE_HUEHCLEnable != 0);
        Serial.printf("Enabled: %s\n", hclEnabled ? "Yes" : "No");
        #else
        const bool hclEnabled = false;
        Serial.println("Enabled: No");
        #endif

        if (!hclEnabled)
        {
            Serial.println("HCL is disabled in ETS.");
            Serial.println("=================================");
            return true;
        }

        #ifdef ParamHUE_HUEHCLMasterCount
        const uint8_t masterCount = ParamHUE_HUEHCLMasterCount;
        Serial.printf("Master count: %u\n", static_cast<unsigned>(masterCount));
        #else
        const uint8_t masterCount = 0;
        Serial.println("Master count: 0");
        #endif

        #ifdef ParamHUE_HUEHCLUpdateInterval
        Serial.printf("Update interval: %us\n", static_cast<unsigned>(ParamHUE_HUEHCLUpdateInterval));
        #endif

        #ifdef ParamHUE_HUEHCLFadeDuration
        Serial.printf("Fade duration: %us\n", static_cast<unsigned>(ParamHUE_HUEHCLFadeDuration));
        #endif

        for (uint8_t masterNumber = 1; masterNumber <= HCL::MasterManager::MAX_MASTERS; masterNumber++)
        {
            if (masterNumber > masterCount)
            {
                break;
            }

            HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
            if (!master)
            {
                continue;
            }

            HCL::InterpolatedValue current = HCL::masterManager.getCurrentValue(masterNumber);
            Serial.printf("M%u: current=%uK/%u%% setpoints=%u applied=%uK\n",
                          static_cast<unsigned>(masterNumber),
                          static_cast<unsigned>(current.kelvin),
                          static_cast<unsigned>(current.brightness),
                          static_cast<unsigned>(master->getValidSetpointCount()),
                          static_cast<unsigned>(master->getAppliedKelvin()));

            uint8_t curveTypeValue = 0;
            uint16_t slewRate = 0;
            uint16_t manualKelvin = 4000;
            String sunrise = "";
            String sunset = "";
            int16_t sunriseOffset = 0;
            int16_t sunsetOffset = 0;

            switch (masterNumber)
            {
                case 1:
                    #ifdef ParamHUE_HCLM1CurveType
                    curveTypeValue = ParamHUE_HCLM1CurveType;
                    slewRate = ParamHUE_HCLM1SlewRate;
                    manualKelvin = ParamHUE_HCLM1ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM1Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM1Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                    #endif
                    break;
                case 2:
                    #ifdef ParamHUE_HCLM2CurveType
                    curveTypeValue = ParamHUE_HCLM2CurveType;
                    slewRate = ParamHUE_HCLM2SlewRate;
                    manualKelvin = ParamHUE_HCLM2ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM2Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM2Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                    #endif
                    break;
                case 3:
                    #ifdef ParamHUE_HCLM3CurveType
                    curveTypeValue = ParamHUE_HCLM3CurveType;
                    slewRate = ParamHUE_HCLM3SlewRate;
                    manualKelvin = ParamHUE_HCLM3ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM3Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM3Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                    #endif
                    break;
                case 4:
                    #ifdef ParamHUE_HCLM4CurveType
                    curveTypeValue = ParamHUE_HCLM4CurveType;
                    slewRate = ParamHUE_HCLM4SlewRate;
                    manualKelvin = ParamHUE_HCLM4ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM4Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM4Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                    #endif
                    break;
                case 5:
                    #ifdef ParamHUE_HCLM5CurveType
                    curveTypeValue = ParamHUE_HCLM5CurveType;
                    slewRate = ParamHUE_HCLM5SlewRate;
                    manualKelvin = ParamHUE_HCLM5ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM5Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM5Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM5SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM5SunsetOffset);
                    #endif
                    break;
                case 6:
                    #ifdef ParamHUE_HCLM6CurveType
                    curveTypeValue = ParamHUE_HCLM6CurveType;
                    slewRate = ParamHUE_HCLM6SlewRate;
                    manualKelvin = ParamHUE_HCLM6ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM6Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM6Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM6SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM6SunsetOffset);
                    #endif
                    break;
                case 7:
                    #ifdef ParamHUE_HCLM7CurveType
                    curveTypeValue = ParamHUE_HCLM7CurveType;
                    slewRate = ParamHUE_HCLM7SlewRate;
                    manualKelvin = ParamHUE_HCLM7ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM7Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM7Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM7SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM7SunsetOffset);
                    #endif
                    break;
                case 8:
                    #ifdef ParamHUE_HCLM8CurveType
                    curveTypeValue = ParamHUE_HCLM8CurveType;
                    slewRate = ParamHUE_HCLM8SlewRate;
                    manualKelvin = ParamHUE_HCLM8ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM8Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM8Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM8SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM8SunsetOffset);
                    #endif
                    break;
                default:
                    break;
            }

            const char* curveText = "FixedTime";
            if (curveTypeValue == 1)
            {
                curveText = "SunWindow";
            }
            else if (curveTypeValue == 2)
            {
                curveText = "Manual";
            }
            else if (curveTypeValue == 3)
            {
                curveText = "Astronomical";
            }

            Serial.printf("    curve=%s slew=%uK/min manual=%uK sun=%s/%s offset=%d/%d\n",
                          curveText,
                          static_cast<unsigned>(slewRate),
                          static_cast<unsigned>(manualKelvin),
                          sunrise.c_str(),
                          sunset.c_str(),
                          static_cast<int>(sunriseOffset),
                          static_cast<int>(sunsetOffset));
        }

        Serial.println("=================================");
        return true;
    }
    
    return false;  // Not my command.
}

void HueGatewayModule::setupMDNS()
{
    if (_mdnsStarted)
    {
        return;
    }

    // Register mDNS service for convenient local access via openknx-bridge.local.
    if (!MDNS.begin("openknx-bridge"))
    {
        _mdnsStarted = false;
        logErrorP("mDNS start failed!");
        return;
    }
    
    // Register HTTP service.
    MDNS.addService("http", "tcp", 80);
    _mdnsStarted = true;
    
    logInfoP("mDNS started: openknx-bridge.local");
}

void HueGatewayModule::refreshNetworkServices()
{
    const bool networkConnected = hasNetworkConnectivity();
    if (networkConnected == _networkConnectedLast)
    {
        return;
    }

    _networkConnectedLast = networkConnected;

    if (!networkConnected)
    {
        if (_mdnsStarted)
        {
            MDNS.end();
            _mdnsStarted = false;
            logInfoP("mDNS stopped (network disconnected)");
        }
        return;
    }

    logInfoP("Network reconnected, reinitializing mDNS");
    setupMDNS();
}

// ===== Private Methods =====

void HueGatewayModule::setupBridge()
{
    Serial.println("[HueGatewayModule] ===== Commissioning: Bridge setup start =====");
    
    // Check whether network connectivity is available (provided by OFM-Network/WLAN).
    if (!hasNetworkConnectivity())
    {
        Serial.println("[HueGatewayModule] ERROR: Network not connected!");
        Serial.println("[HueGatewayModule] Network must be initialized by OFM-Network or WLAN module first");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }
    
    Serial.printf("[HueGatewayModule] Network OK - Device IP: %s\n", getDeviceIpString().c_str());
    
    // Read bridge IP from ETS parameter.
    _bridgeIP = getBridgeIP();

    if (ParamHUE_HUEBridgeMode != 0 && !_bridgeIP.isEmpty())
    {
        HueGatewayDiscovery discovery;
        if (!discovery.setManualIP(_bridgeIP.c_str(), false))
        {
            Serial.println("[HueGatewayModule] WARNING: Could not persist manual bridge IP");
        }
    }
    
    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }
    
    Serial.printf("[HueGatewayModule] Bridge configured: %s\n", _bridgeIP.c_str());
    updateStatus(BridgeStatus::CONNECTING);

    if (ParamHUE_HUEBridgeMode == 0)
    {
        Serial.println("[HueGatewayModule] Discovery mode: Automatic (mDNS/N-UPnP fallback)");
    }
    else
    {
        Serial.println("[HueGatewayModule] Discovery mode: Manual IP");
    }

    if (ParamHUE_HUEResetAuth)
    {
        Serial.println("[HueGatewayModule] Commissioning flag active: reset stored authentication");
        _auth.clearAppKey();
    }

    uint8_t pairingWindowSec = ParamHUE_HUEPairingWindow;
    if (pairingWindowSec < 5)
    {
        pairingWindowSec = 30;
    }
    _authWindowMs = static_cast<unsigned long>(pairingWindowSec) * 1000UL;
    _reconnectBackoffMs = 10000;
    _lastReconnectTryMs = 0;
    Serial.printf("[HueGatewayModule] Pairing window: %u seconds\n", static_cast<unsigned>(pairingWindowSec));
    
    // Authentication (non-blocking if no stored key)
    if (_auth.loadStoredAppKey())
    {
        Serial.println("[HueGatewayModule] Stored App-Key found, trying direct client init");
        updateStatus(BridgeStatus::CONNECTING);
        if (initClientWithAppKey())
        {
            updateStatus(BridgeStatus::CONNECTED);
            Serial.println("[HueGatewayModule] HueGatewayClient ready");
        }
        else
        {
            Serial.println("[HueGatewayModule] Stored App-Key unusable, bridge currently unreachable or TLS/auth mismatch");
            updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        }
        return;
    }

    Serial.println("[HueGatewayModule] No stored App-Key found. Waiting for manual pairing trigger (KO).\n");
    updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
    _authPending = false;
    _manualPairingRequired = true;
}

void HueGatewayModule::setupDevices()
{
    _diagCounterSetupRuns++;
    appendDiagnosticLog("INFO", "SETUP", "setupDevices start");

    const unsigned long nowMs = millis();
    if (_setupCircuitOpenUntilMs != 0 && nowMs < _setupCircuitOpenUntilMs)
    {
        if (_deviceSetupNeedsRetry)
        {
            Serial.printf("[HueGatewayModule] setupDevices skipped: circuit open for %lu s\n",
                          static_cast<unsigned long>((_setupCircuitOpenUntilMs - nowMs) / 1000UL));
        }
        return;
    }

    Serial.println("[HueGatewayModule] Setting up Devices...");
    _lastDeviceSetupRetryMs = nowMs;
    _deviceSetupNeedsRetry = false;

    auto registerSetupFailure = [&](const char* reason)
    {
        _setupCircuitTrips = min<uint8_t>(static_cast<uint8_t>(_setupCircuitTrips + 1), static_cast<uint8_t>(10));
        if (_setupCircuitTrips >= kSetupCircuitTripThreshold)
        {
            _setupCircuitOpenUntilMs = millis() + kSetupCircuitOpenMs;
            _setupCircuitTrips = 0;
            Serial.printf("[HueGatewayModule] setupDevices circuit opened for %lu s (%s)\n",
                          static_cast<unsigned long>(kSetupCircuitOpenMs / 1000UL),
                          reason ? reason : "failure");
        }
    };

    uint8_t channelCount = ParamHUE_HUEChannelCount;
    if (channelCount > MAX_LIGHTS)
    {
        channelCount = MAX_LIGHTS;
    }
    
    if (!_client || !_client->isInitialized())
    {
        if (channelCount > 0)
        {
            _deviceSetupNeedsRetry = true;
            registerSetupFailure("client-not-ready");
            Serial.printf("[HueGatewayModule] Client not ready, scheduling setup retry for %u configured channels\n",
                          static_cast<unsigned>(channelCount));
        }
        Serial.println("[HueGatewayModule] ERROR: HueGatewayClient not ready");
        return;
    }
    
    // Read number of channels from ETS.
    Serial.printf("[HueGatewayModule] Configured channels: %d\n", channelCount);
    
    if (channelCount == 0)
    {
        _deviceSetupRetryBackoffMs = kDeviceSetupRetryBaseMs;
        Serial.println("[HueGatewayModule] No channels configured");
        return;
    }

    if (!hasSetupTlsHeadroom())
    {
        _deviceSetupNeedsRetry = true;
        _deviceSetupRetryBackoffMs = min<unsigned long>(_deviceSetupRetryBackoffMs * 2UL, kDeviceSetupRetryMaxMs);
        registerSetupFailure("tls-headroom");
        Serial.printf("[HueGatewayModule] setupDevices deferred: low internal TLS headroom, retry in %lu s\n",
                      static_cast<unsigned long>(_deviceSetupRetryBackoffMs / 1000UL));
        logSetupTlsHeadroom("setup-deferred");
        return;
    }

    const unsigned long setupStartMs = millis();
    _client->setEventStreamAutoRestartEnabled(false);
    if (_client->isEventStreamConnected())
    {
        Serial.println("[HueGatewayModule] Setup session: pausing EventStream once");
        _client->stopEventStream();
        _lastEventStreamRetryMs = millis();
        delay(20);
    }

    auto finalizeSetupSession = [&](const char* phase)
    {
        _client->setEventStreamAutoRestartEnabled(true);

        if (sEventStreamEnabled && !_client->isEventStreamConnected())
        {
            if (_client->startEventStream())
            {
                Serial.println("[HueGatewayModule] Setup session: EventStream resumed");
            }
            else
            {
                Serial.println("[HueGatewayModule] Setup session: EventStream resume deferred/failed (retry loop active)");
            }
        }

        Serial.printf("[HueGatewayModule] setupDevices duration=%lu ms (%s)\n",
                      static_cast<unsigned long>(millis() - setupStartMs),
                      phase ? phase : "done");
    };
    
    // Retrieve all lights from the bridge for validation/mapping.
    int lightFetchCapacity = kWebScanMaxLights;
    std::unique_ptr<HueGatewayLightState[]> allLights(new (std::nothrow) HueGatewayLightState[lightFetchCapacity]);
    if (!allLights)
    {
        lightFetchCapacity = MAX_LIGHTS;
        allLights.reset(new (std::nothrow) HueGatewayLightState[lightFetchCapacity]);
    }

    if (!allLights)
    {
        _deviceSetupNeedsRetry = true;
        _diagCounterSetupIncomplete++;
        _deviceSetupRetryBackoffMs = min<unsigned long>(_deviceSetupRetryBackoffMs * 2UL, kDeviceSetupRetryMaxMs);
        registerSetupFailure("alloc-lights-buffer");
        Serial.println("[HueGatewayModule] setupDevices aborted: cannot allocate light fetch buffer");
        appendDiagnosticLog("WARN", "SETUP", "setupDevices incomplete: light fetch buffer alloc failed");
        finalizeSetupSession("alloc-failed");
        return;
    }

    int bridgeLightCount = _client->getLights(allLights.get(), lightFetchCapacity, false);
    if (bridgeLightCount <= 0)
    {
        delay(60);
        bridgeLightCount = _client->getLights(allLights.get(), lightFetchCapacity, false);
    }
    Serial.printf("[HueGatewayModule] Bridge has %d lights\n", bridgeLightCount);

    if (bridgeLightCount <= 0)
    {
        _consecutiveEmptyLightFetches++;
        _diagCounterEmptyLightFetches++;
        _deviceSetupNeedsRetry = true;

        if (_consecutiveEmptyLightFetches < kEmptyLightEscalationThreshold)
        {
            // Transient empty response: retry quickly without tripping the circuit breaker
            _deviceSetupRetryBackoffMs = kEmptyLightQuickRetryMs;
            Serial.printf("[HueGatewayModule] Bridge returned 0 lights (%u/%u before escalation), quick retry in %lu ms\n",
                          static_cast<unsigned>(_consecutiveEmptyLightFetches),
                          static_cast<unsigned>(kEmptyLightEscalationThreshold),
                          static_cast<unsigned long>(kEmptyLightQuickRetryMs));
            appendDiagnosticLog("WARN", "SETUP", String("empty light list (") + String(_consecutiveEmptyLightFetches) + "/" + String(kEmptyLightEscalationThreshold) + ")");
        }
        else
        {
            // Persistent empty response: escalate with full backoff and circuit breaker
            _diagCounterSetupIncomplete++;
            _deviceSetupRetryBackoffMs = min<unsigned long>(_deviceSetupRetryBackoffMs * 2UL, kDeviceSetupRetryMaxMs);
            registerSetupFailure("no-lights");
            Serial.printf("[HueGatewayModule] Bridge returned 0 lights (%u consecutive), escalating with backoff\n",
                          static_cast<unsigned>(_consecutiveEmptyLightFetches));
            appendDiagnosticLog("WARN", "SETUP", "setupDevices incomplete: bridge returned no lights (escalated)");
        }
        finalizeSetupSession("no-lights");
        return;
    }

    _consecutiveEmptyLightFetches = 0;
    _lastValidLightFetchMs = millis();

    bool needRoomTargets = false;
    bool needZoneTargets = false;
    #ifdef ParamHUE_CHTargetType
    for (uint8_t ch = 0; ch < channelCount; ch++)
    {
        uint8_t _channelIndex = ch;
        if (ParamHUE_CHDisabled)
        {
            continue;
        }

        uint8_t targetType = ParamHUE_CHTargetType;
        if (targetType == 1)
        {
            needRoomTargets = true;
        }
        else if (targetType == 2)
        {
            needZoneTargets = true;
        }

        if (needRoomTargets && needZoneTargets)
        {
            break;
        }
    }
    #endif

    std::unique_ptr<HueGatewayTargetInfo[]> roomTargets;
    std::unique_ptr<HueGatewayTargetInfo[]> zoneTargets;
    int roomTargetCapacity = needRoomTargets ? kWebScanMaxTargets : 0;
    int zoneTargetCapacity = needZoneTargets ? kWebScanMaxTargets : 0;

    if (needRoomTargets)
    {
        roomTargets.reset(new (std::nothrow) HueGatewayTargetInfo[roomTargetCapacity]);
        if (!roomTargets)
        {
            roomTargetCapacity = MAX_LIGHTS;
            roomTargets.reset(new (std::nothrow) HueGatewayTargetInfo[roomTargetCapacity]);
        }

        if (!roomTargets)
        {
            roomTargetCapacity = 0;
            appendDiagnosticLog("WARN", "SETUP", "room target buffer alloc failed, fallback to direct resolve");
        }
    }

    if (needZoneTargets)
    {
        zoneTargets.reset(new (std::nothrow) HueGatewayTargetInfo[zoneTargetCapacity]);
        if (!zoneTargets)
        {
            zoneTargetCapacity = MAX_LIGHTS;
            zoneTargets.reset(new (std::nothrow) HueGatewayTargetInfo[zoneTargetCapacity]);
        }

        if (!zoneTargets)
        {
            zoneTargetCapacity = 0;
            appendDiagnosticLog("WARN", "SETUP", "zone target buffer alloc failed, fallback to direct resolve");
        }
    }

    const int roomTargetCount = (needRoomTargets && roomTargets)
        ? _client->getRoomTargets(roomTargets.get(), roomTargetCapacity)
        : 0;
    const int zoneTargetCount = (needZoneTargets && zoneTargets)
        ? _client->getZoneTargets(zoneTargets.get(), zoneTargetCapacity)
        : 0;
    _diagLastBridgeLightCount = bridgeLightCount;
    _diagLastRoomTargetCount = roomTargetCount;
    _diagLastZoneTargetCount = zoneTargetCount;
    Serial.printf("[HueGatewayModule] Prefetched grouped targets: rooms=%d zones=%d (needed room=%u zone=%u)\n",
                  roomTargetCount,
                  zoneTargetCount,
                  needRoomTargets ? 1U : 0U,
                  needZoneTargets ? 1U : 0U);
    
    // Load channels from ETS parameters.
    // Prefer UUID mapping from ETS, fallback to index when UUID is empty.
    uint8_t maxChannels = channelCount;
    if (maxChannels > MAX_LIGHTS)
    {
        maxChannels = MAX_LIGHTS;
    }
    bool mappedChannels[MAX_LIGHTS] = {false};
    int mappedCount = 0;
    bool hasIndexMappedChannel = false;
    bool hasUnresolvedGroupTarget = false;
    for (uint8_t ch = 0; ch < maxChannels; ch++)
    {
        uint8_t _channelIndex = ch;

        if (ParamHUE_CHDisabled)
        {
            Serial.printf("[HueGatewayModule] Channel %d: Disabled\n", ch + 1);
            if (_devices[ch] != nullptr)
            {
                delete _devices[ch];
                _devices[ch] = nullptr;
            }
            _channelLastPollMs[ch] = 0;
            _channelFastTrackNextMs[ch] = 0;
            _channelFastTrackCooldownUntilMs[ch] = 0;
            _channelFastTrackRemaining[ch] = 0;
            continue;
        }

        std::string configuredUuidStd = ParamHUE_CHLightUUIDStr;
        String configuredUuid(configuredUuidStd.c_str());
        configuredUuid.trim();

        String configuredTargetRid;
        #ifdef ParamHUE_CHTargetRIDStr
        {
            std::string configuredTargetStd = ParamHUE_CHTargetRIDStr;
            configuredTargetRid = String(configuredTargetStd.c_str());
            configuredTargetRid.trim();
        }
        #endif

        uint8_t configuredTargetType = 0;
        #ifdef ParamHUE_CHTargetType
        configuredTargetType = ParamHUE_CHTargetType;
        #endif

        bool hasLegacyUuid = configuredUuid.length() > 0
            && !configuredUuid.equalsIgnoreCase("01234567-89ab-cdef-0123-456789abcdef");

        if (configuredTargetRid.length() == 0 && !hasLegacyUuid)
        {
            hasIndexMappedChannel = true;
        }

        // ---- Non-light device types (1-3): bypass bridge resolution ----
        #ifdef ParamHUE_CHDeviceType
        {
            const uint8_t devType = ParamHUE_CHDeviceType;
            Serial.printf("[HueGatewayModule] Channel %d: DeviceType=%u (0=Licht,1=Sensor,2=Taster,3=Kontakt,4=Steckdose)\n",
                          ch + 1, static_cast<unsigned>(devType));
            if (devType >= 1 && devType <= 3)
            {
                const String resourceId = configuredTargetRid.length() > 0
                    ? configuredTargetRid
                    : configuredUuid;
                const uint16_t koBase = static_cast<uint16_t>(HUE_KoBlockOffset + ch * HUE_KoBlockSize);

                if (devType == 1)  // Bewegungsmelder
                {
                    const bool needsRecreate = (_devices[ch] == nullptr)
                        || _devices[ch]->deviceType() != 1
                        || !_devices[ch]->getResourceId().equalsIgnoreCase(resourceId);
                    if (needsRecreate)
                    {
                        delete _devices[ch];
                        _devices[ch] = new (std::nothrow) HueGatewaySensor(resourceId, resourceId);
                        if (!_devices[ch]) continue;
                    }
                    static_cast<HueGatewaySensor*>(_devices[ch])->begin(
                        koBase + 0,
                        #ifdef ParamHUE_CHOptReachable
                        ParamHUE_CHOptReachable != 0, koBase + 3,
                        ParamHUE_CHOptTemperature != 0, koBase + 5,
                        ParamHUE_CHOptLux != 0, koBase + 6,
                        ParamHUE_CHOptBattery != 0, koBase + 4
                        #else
                        false, koBase + 3, false, koBase + 5, false, koBase + 6, false, koBase + 4
                        #endif
                    );
                    // Resolve service RIDs so SSE events are matched correctly
                    {
                        static constexpr int kMaxSvc = 16;
                        HueGatewayServiceRid svc[kMaxSvc];
                        const int n = _client->getDeviceServiceRids(resourceId, svc, kMaxSvc);
                        for (int s = 0; s < n; s++)
                        {
                            if (svc[s].rtype.equalsIgnoreCase("motion"))
                                static_cast<HueGatewaySensor*>(_devices[ch])->setMotionServiceRid(svc[s].rid);
                        }
                    }
                    _channelLastPollMs[ch] = 0;
                    mappedChannels[ch] = true;
                    mappedCount++;
                    Serial.printf("[HueGatewayModule] Channel %d: Bewegungsmelder %s -> KO%d\n",
                                  ch + 1, resourceId.c_str(), koBase + 0);
                    continue;
                }
                else if (devType == 2)  // Taster
                {
                    #ifdef ParamHUE_CHButtonCount
                    const uint8_t btnCount = ParamHUE_CHButtonCount;
                    #else
                    const uint8_t btnCount = 1;
                    #endif
                    const bool needsRecreate = (_devices[ch] == nullptr)
                        || _devices[ch]->deviceType() != 2
                        || !_devices[ch]->getResourceId().equalsIgnoreCase(resourceId);
                    if (needsRecreate)
                    {
                        delete _devices[ch];
                        _devices[ch] = new (std::nothrow) HueGatewayButton(resourceId, resourceId, btnCount);
                        if (!_devices[ch]) continue;
                    }
                    auto* btn = static_cast<HueGatewayButton*>(_devices[ch]);
                    btn->begin(koBase);
                    // Apply per-button KurzTyp/LangTyp settings from ETS parameters
                    #ifdef ParamHUE_CHBtn1KurzTyp
                    {
                        using RF = HueGatewayButton::RotaryFunction;

                        // Read per-button KurzTyp/LangTyp + sub-values from ETS parameters.
                        // Sub-values are union members ÔÇö values for non-active types are unused
                        // (dispatch selects the right field based on kurzTyp/langTyp).
                        #define MAP_BTN_PARAMS(N, IDX)                                              \
                        {                                                                            \
                            auto& cfg = btn->buttonConfig(IDX);                                      \
                            cfg.kurzTyp        = ParamHUE_CHBtn##N##KurzTyp;                         \
                            cfg.langTyp        = ParamHUE_CHBtn##N##LangTyp;                         \
                            cfg.kurzSchaltwert = ParamHUE_CHBtn##N##KurzSchaltwert;                  \
                            cfg.kurzDimUp      = (ParamHUE_CHBtn##N##KurzRichtungDim == 0);          \
                            cfg.kurzDimStep    = ParamHUE_CHBtn##N##KurzDimStep;                     \
                            cfg.kurzSceneNr    = ParamHUE_CHBtn##N##KurzSceneNr;                     \
                            cfg.kurzRichtung   = ParamHUE_CHBtn##N##KurzRichtungJal;                 \
                            cfg.kurzProzent    = ParamHUE_CHBtn##N##KurzProzent;                     \
                            cfg.kurzTemp       = ParamHUE_CHBtn##N##KurzTemp;                        \
                            cfg.kurzByte       = ParamHUE_CHBtn##N##KurzByte;                        \
                            cfg.kurzWord       = ParamHUE_CHBtn##N##KurzWord;                        \
                            cfg.langSchaltwert = ParamHUE_CHBtn##N##LangSchaltwert;                  \
                            cfg.langDimUp      = (ParamHUE_CHBtn##N##LangRichtungDim == 0);          \
                            cfg.langDimStep    = ParamHUE_CHBtn##N##LangDimStep;                     \
                            cfg.langSceneNr    = ParamHUE_CHBtn##N##LangSceneNr;                     \
                            cfg.langRichtung   = ParamHUE_CHBtn##N##LangRichtungJal;                 \
                            cfg.langProzent    = ParamHUE_CHBtn##N##LangProzent;                     \
                            cfg.langTemp       = ParamHUE_CHBtn##N##LangTemp;                        \
                            cfg.langByte       = ParamHUE_CHBtn##N##LangByte;                        \
                            cfg.langWord       = ParamHUE_CHBtn##N##LangWord;                        \
                        }

                        MAP_BTN_PARAMS(1, 0)
                        if (btnCount >= 2) MAP_BTN_PARAMS(2, 1)
                        if (btnCount >= 3) MAP_BTN_PARAMS(3, 2)
                        if (btnCount >= 4) MAP_BTN_PARAMS(4, 3)

                        #undef MAP_BTN_PARAMS

                        // Drehregler
                        btn->setHasRotary(ParamHUE_CHHasRotary != 0);
                        if (ParamHUE_CHHasRotary != 0) {
                            btn->setRotaryFunction(static_cast<RF>(ParamHUE_CHRotaryFunction));
                            btn->setRotaryStepPercent(ParamHUE_CHRotaryStepPercent);
                        }
                    }
                    #endif
                    // Resolve service RIDs so SSE button/rotary events are matched correctly
                    {
                        static constexpr int kMaxSvc = 16;
                        HueGatewayServiceRid svc[kMaxSvc];
                        const int n = _client->getDeviceServiceRids(resourceId, svc, kMaxSvc);
                        Serial.printf("[HueGatewayModule] Channel %d: Taster %s has %d services:\n",
                                      ch + 1, resourceId.c_str(), n);
                        uint8_t btnRidIdx = 0;
                        String btnRidSummary;
                        for (int s = 0; s < n; s++)
                        {
                            Serial.printf("  svc[%d] rtype=%s rid=%s\n",
                                          s, svc[s].rtype.c_str(), svc[s].rid.c_str());
                            if (svc[s].rtype.equalsIgnoreCase("button") && btnRidIdx < HueGatewayButton::MAX_BUTTONS)
                            {
                                btn->setButtonServiceRid(btnRidIdx++, svc[s].rid);
                                if (btnRidSummary.length() > 0) btnRidSummary += ",";
                                btnRidSummary += svc[s].rid.substring(0, 8);
                            }
                            else if (svc[s].rtype.equalsIgnoreCase("relative_rotary"))
                                btn->setRotaryServiceRid(svc[s].rid);
                        }
                        Serial.printf("[HueGatewayModule] Channel %d: mapped %u button service RIDs\n",
                                      ch + 1, static_cast<unsigned>(btnRidIdx));
                        appendDiagnosticLog("INFO", "BTN", String("ch") + String(ch + 1)
                            + " svc=" + String(n) + " btnRids=" + String(btnRidIdx)
                            + " [" + btnRidSummary + "]");
                    }
                    // Native Hue Aktion: behavior_instances deaktivieren oder reaktivieren
                    #ifdef ParamHUE_CHNativeHueAction
                    {
                        const uint8_t nativeAction = ParamHUE_CHNativeHueAction;
                        Serial.printf("[HueGatewayModule] Channel %d: NativeHueAction=%u (0=keep, 1=disable)\n",
                                      ch + 1, static_cast<unsigned>(nativeAction));
                        static constexpr int kMaxInst = 8;
                        String instanceIds[kMaxInst];
                        String biDebugInfo;
                        const int nInst = _client->getBehaviorInstances(resourceId, instanceIds, kMaxInst, &biDebugInfo);
                        Serial.printf("[HueGatewayModule] Channel %d: found %d behavior_instances for device %s\n",
                                      ch + 1, nInst, resourceId.c_str());
                        appendDiagnosticLog("INFO", "BTN", String("ch") + String(ch + 1)
                            + " nativeAction=" + String(nativeAction)
                            + " behaviorInst=" + String(nInst));
                        if (biDebugInfo.length() > 0)
                        {
                            appendDiagnosticLog("INFO", "BI_DBG", String("ch") + String(ch + 1) + " " + biDebugInfo);
                        }

                        if (nInst > 0)
                        {
                            const bool disable = (nativeAction == 1);
                            if (disable)
                            {
                                for (int i = 0; i < nInst; i++)
                                {
                                    Serial.printf("  behavior_instance[%d]=%s -> DELETE\n",
                                                  i, instanceIds[i].c_str());
                                    _client->deleteBehaviorInstance(instanceIds[i]);
                                }
                            }
                            else
                            {
                                Serial.printf("  nativeAction=0 (keep), skipping %d behavior_instances\n", nInst);
                            }
                        }
                    }
                    #endif
                    _channelLastPollMs[ch] = 0;
                    mappedChannels[ch] = true;
                    mappedCount++;
                    Serial.printf("[HueGatewayModule] Channel %d: Taster %s (%u Tasten) setup complete\n",
                                  ch + 1, resourceId.c_str(), static_cast<unsigned>(btnCount));
                    continue;
                }
                else  // devType == 3: Kontaktsensor
                {
                    const bool needsRecreate = (_devices[ch] == nullptr)
                        || _devices[ch]->deviceType() != 3
                        || !_devices[ch]->getResourceId().equalsIgnoreCase(resourceId);
                    if (needsRecreate)
                    {
                        delete _devices[ch];
                        _devices[ch] = new (std::nothrow) HueGatewayContact(resourceId, resourceId);
                        if (!_devices[ch]) continue;
                    }
                    static_cast<HueGatewayContact*>(_devices[ch])->begin(
                        koBase + 0,
                        #ifdef ParamHUE_CHOptTamper
                        ParamHUE_CHOptTamper != 0, koBase + 3,
                        ParamHUE_CHOptBattery != 0, koBase + 4,
                        ParamHUE_CHOptReachable != 0, koBase + 9
                        #else
                        false, koBase + 3, false, koBase + 4, false, koBase + 9
                        #endif
                    );
                    // Resolve service RIDs so SSE events are matched correctly
                    {
                        static constexpr int kMaxSvc = 16;
                        HueGatewayServiceRid svc[kMaxSvc];
                        const int n = _client->getDeviceServiceRids(resourceId, svc, kMaxSvc);
                        for (int s = 0; s < n; s++)
                        {
                            if (svc[s].rtype.equalsIgnoreCase("contact_sensor"))
                                static_cast<HueGatewayContact*>(_devices[ch])->setContactServiceRid(svc[s].rid);
                        }
                    }
                    _channelLastPollMs[ch] = 0;
                    mappedChannels[ch] = true;
                    mappedCount++;
                    Serial.printf("[HueGatewayModule] Channel %d: Kontaktsensor %s -> KO%d\n",
                                  ch + 1, resourceId.c_str(), koBase + 0);
                    continue;
                }
            }
        }
        #endif

        const HueGatewayLightState* selectedLight = nullptr;
        HueGatewayLightState fallbackLight;
        bool targetIsGrouped = false;
        bool targetIsRoom = false;
        bool targetIsZone = false;

        enum class TargetProbeType : uint8_t
        {
            Light = 0,
            Room = 1,
            Zone = 2
        };

        TargetProbeType preferredProbe = TargetProbeType::Light;
        if (configuredTargetType == 1)
        {
            preferredProbe = TargetProbeType::Room;
        }
        else if (configuredTargetType == 2)
        {
            preferredProbe = TargetProbeType::Zone;
        }

        String targetProbe;

        auto tryResolveLight = [&]() -> bool
        {
            for (int i = 0; i < bridgeLightCount; i++)
            {
                if (allLights[i].id.equalsIgnoreCase(targetProbe)
                    || allLights[i].name.equalsIgnoreCase(targetProbe))
                {
                    selectedLight = &allLights[i];
                    targetIsGrouped = false;
                    targetIsRoom = false;
                    targetIsZone = false;
                    return true;
                }
            }
            return false;
        };

        auto tryResolveRoom = [&]() -> bool
        {
            for (int i = 0; i < roomTargetCount; i++)
            {
                if (!(roomTargets[i].id.equalsIgnoreCase(targetProbe)
                      || roomTargets[i].name.equalsIgnoreCase(targetProbe)))
                {
                    continue;
                }

                if (roomTargets[i].groupedLightId.length() == 0)
                {
                    continue;
                }

                fallbackLight.id = roomTargets[i].groupedLightId;
                fallbackLight.name = String("Room: ") + roomTargets[i].name;
                fallbackLight.supportsColorTemp = false;
                fallbackLight.supportsColor = false;
                selectedLight = &fallbackLight;
                targetIsGrouped = true;
                targetIsRoom = true;
                targetIsZone = false;
                return true;
            }

            String groupedRid;
            String groupedName;
            if (roomTargetCount <= 0
                && _client->resolveGroupedLightForRoom(targetProbe, groupedRid, groupedName)
                && groupedRid.length() > 0)
            {
                fallbackLight.id = groupedRid;
                fallbackLight.name = String("Room: ") + groupedName;
                fallbackLight.supportsColorTemp = false;
                fallbackLight.supportsColor = false;
                selectedLight = &fallbackLight;
                targetIsGrouped = true;
                targetIsRoom = true;
                targetIsZone = false;
                return true;
            }
            return false;
        };

        auto tryResolveZone = [&]() -> bool
        {
            for (int i = 0; i < zoneTargetCount; i++)
            {
                if (!(zoneTargets[i].id.equalsIgnoreCase(targetProbe)
                      || zoneTargets[i].name.equalsIgnoreCase(targetProbe)))
                {
                    continue;
                }

                if (zoneTargets[i].groupedLightId.length() == 0)
                {
                    continue;
                }

                fallbackLight.id = zoneTargets[i].groupedLightId;
                fallbackLight.name = String("Zone: ") + zoneTargets[i].name;
                fallbackLight.supportsColorTemp = false;
                fallbackLight.supportsColor = false;
                selectedLight = &fallbackLight;
                targetIsGrouped = true;
                targetIsRoom = false;
                targetIsZone = true;
                return true;
            }

            String groupedRid;
            String groupedName;
            if (zoneTargetCount <= 0
                && _client->resolveGroupedLightForZone(targetProbe, groupedRid, groupedName)
                && groupedRid.length() > 0)
            {
                fallbackLight.id = groupedRid;
                fallbackLight.name = String("Zone: ") + groupedName;
                fallbackLight.supportsColorTemp = false;
                fallbackLight.supportsColor = false;
                selectedLight = &fallbackLight;
                targetIsGrouped = true;
                targetIsRoom = false;
                targetIsZone = true;
                return true;
            }
            return false;
        };

        auto resolveSingleTarget = [&](const String& rawTarget, TargetProbeType baseProbe, bool preferLightFirst) -> bool
        {
            String localTarget = rawTarget;
            localTarget.trim();
            if (localTarget.length() == 0)
            {
                return false;
            }

            TargetProbeType probeOrder = baseProbe;
            String localTargetLower = localTarget;
            localTargetLower.toLowerCase();

            if (localTargetLower.startsWith("room:"))
            {
                probeOrder = TargetProbeType::Room;
                localTarget = localTarget.substring(5);
            }
            else if (localTargetLower.startsWith("zone:"))
            {
                probeOrder = TargetProbeType::Zone;
                localTarget = localTarget.substring(5);
            }

            localTarget.trim();
            if (localTarget.length() == 0)
            {
                return false;
            }

            targetProbe = localTarget;

            if (preferLightFirst)
            {
                return tryResolveLight() || tryResolveRoom() || tryResolveZone();
            }

            if (probeOrder == TargetProbeType::Light)
            {
                return tryResolveLight() || tryResolveRoom() || tryResolveZone();
            }
            if (probeOrder == TargetProbeType::Room)
            {
                return tryResolveRoom() || tryResolveLight() || tryResolveZone();
            }

            return tryResolveZone() || tryResolveLight() || tryResolveRoom();
        };

        bool resolvedByNewTarget = false;
        bool resolvedByLegacyTarget = false;

        if (configuredTargetRid.length() > 0)
        {
            resolvedByNewTarget = resolveSingleTarget(configuredTargetRid, preferredProbe, false);
        }

        if (!resolvedByNewTarget && hasLegacyUuid)
        {
            resolvedByLegacyTarget = resolveSingleTarget(configuredUuid, TargetProbeType::Light, true);
            if (resolvedByLegacyTarget)
            {
                Serial.printf("[HueGatewayModule] Channel %d: fallback to legacy Hue Light-ID '%s'\n",
                              ch + 1,
                              configuredUuid.c_str());
            }
        }

        if (configuredTargetRid.length() > 0 || hasLegacyUuid)
        {
            if (selectedLight == nullptr)
            {
                hasUnresolvedGroupTarget = true;
                String unresolvedTarget = configuredTargetRid.length() > 0 ? configuredTargetRid : configuredUuid;
                Serial.printf("[HueGatewayModule] Channel %d: target '%s' unresolved (type=%u, newTarget=%u, legacy=%u), retry pending\n",
                              ch + 1,
                              unresolvedTarget.c_str(),
                              static_cast<unsigned>(configuredTargetType),
                              configuredTargetRid.length() > 0 ? 1U : 0U,
                              hasLegacyUuid ? 1U : 0U);

                if (_devices[ch] != nullptr)
                {
                    mappedChannels[ch] = true;
                    mappedCount++;
                    Serial.printf("[HueGatewayModule] Channel %d: keeping previous mapping (%s)\n",
                                  ch + 1,
                                  _devices[ch]->getResourceId().c_str());
                }
                continue;
            }
        }
        else if (ch < bridgeLightCount)
        {
            selectedLight = &allLights[ch];
            Serial.printf("[HueGatewayModule] Channel %d: No UUID configured, fallback to bridge index %d (%s)\n",
                          ch + 1, ch, selectedLight->id.c_str());
        }

        if (selectedLight == nullptr || selectedLight->id.isEmpty())
        {
            Serial.printf("[HueGatewayModule] Channel %d: No valid bridge light selected\n", ch + 1);
            if (_devices[ch] != nullptr)
            {
                mappedChannels[ch] = true;
                mappedCount++;
            }
            continue;
        }

        uint16_t koBase = HUE_KoBlockOffset + (ch * HUE_KoBlockSize);
        uint16_t koSwitch = koBase + 0;
        uint16_t koBrightness = koBase + 1;
        uint16_t koDimming = koBase + 2;
        uint16_t koStatusSwitch = koBase + 3;
        uint16_t koStatusBrightness = koBase + 4;
        uint16_t koStatusColorTemp = koBase + 6;
        uint16_t koStatusColorRGB = koBase + 8;
        
        #ifdef ParamHUE_CHDeviceType
        const uint8_t chDevType = ParamHUE_CHDeviceType;
        #else
        const uint8_t chDevType = 0;
        #endif

        const bool needsRecreate = (_devices[ch] == nullptr)
            || !_devices[ch]->getResourceId().equalsIgnoreCase(selectedLight->id)
            || (_devices[ch]->isGroupedTarget() != targetIsGrouped)
            || (_devices[ch]->deviceType() != chDevType);

        if (needsRecreate)
        {
            if (_devices[ch] != nullptr)
            {
                delete _devices[ch];
                _devices[ch] = nullptr;
            }

            if (chDevType == 4)  // Steckdose
            {
                HueGatewayPlug* newPlug = new (std::nothrow) HueGatewayPlug(selectedLight->id, selectedLight->name, _client);
                if (newPlug == nullptr)
                {
                    Serial.printf("[HueGatewayModule] Channel %d: allocation failed (Plug) for %s\n",
                                  ch + 1, selectedLight->id.c_str());
                    continue;
                }
                _devices[ch] = newPlug;
            }
            else
            {
                HueGatewayLight* newLight = new (std::nothrow) HueGatewayLight(selectedLight->id, selectedLight->name, _client);
                if (newLight == nullptr)
                {
                    Serial.printf("[HueGatewayModule] Channel %d: allocation failed for %s\n",
                                  ch + 1, selectedLight->id.c_str());
                    continue;
                }
                _devices[ch] = newLight;
            }
        }

        // ---- Steckdose: eigene Initialisierung und weiter ----
        if (chDevType == 4)
        {
            auto* setupPlug = static_cast<HueGatewayPlug*>(_devices[ch]);
            setupPlug->begin(koSwitch, koStatusSwitch,
                #ifdef ParamHUE_CHOptReachable
                ParamHUE_CHOptReachable != 0, koBase + 9
                #else
                false, koBase + 9
                #endif
            );
            mappedChannels[ch] = true;
            mappedCount++;
            Serial.printf("[HueGatewayModule] Channel %d: Steckdose %s -> KO%d/KO%d\n",
                          ch + 1, selectedLight->id.c_str(), koSwitch, koStatusSwitch);
            continue;
        }

        HueGatewayLight* setupLight = lightAt(ch);
        if (setupLight == nullptr)
        {
            continue;
        }

        setupLight->begin(koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness, koStatusColorTemp, koStatusColorRGB);
        setupLight->setGroupedTarget(targetIsGrouped);

        uint8_t lightType = ParamHUE_CHLightType;
        uint8_t effectiveLightType = lightType;

        if (!targetIsGrouped && effectiveLightType >= 3 && !selectedLight->supportsColor)
        {
            if (selectedLight->supportsColorTemp)
            {
                effectiveLightType = 2;
            }
            else
            {
                effectiveLightType = 1;
            }

            Serial.printf("[HueGatewayModule] Channel %d: ETS type %u downgraded to %u (light has no RGB support)\n",
                          ch + 1,
                          static_cast<unsigned>(lightType),
                          static_cast<unsigned>(effectiveLightType));
        }

        if (!targetIsGrouped && effectiveLightType == 2 && !selectedLight->supportsColorTemp)
        {
            effectiveLightType = 1;
            Serial.printf("[HueGatewayModule] Channel %d: ETS type %u downgraded to %u (light has no CT support)\n",
                          ch + 1,
                          static_cast<unsigned>(lightType),
                          static_cast<unsigned>(effectiveLightType));
        }

        setupLight->setLightType(effectiveLightType);

        uint8_t hclMaster = ParamHUE_CHHCLMaster;
        if (hclMaster > HCL::MasterManager::MAX_MASTERS)
        {
            hclMaster = 0;
        }
        _devices[ch]->setHCLMaster(hclMaster);

#ifdef HUEGATEWAY_HAS_LIGHTMANAGER
        // Register/unregister with LightManagerModule so HCL setpoints are pushed
        // to this light. unregister first ensures clean state when the master changes
        // or setupDevices() runs again.
        openknxLightManagerModule.unregisterOutput(setupLight);
        if (hclMaster > 0)
        {
            openknxLightManagerModule.registerOutput(hclMaster, setupLight);
        }
#endif

        _hclChannelLockFallbackMode[ch] = static_cast<uint8_t>(HclLockFallbackMode::None);
        #ifdef ParamHUE_CHHCLLockFallback
        _hclChannelLockFallbackMode[ch] = ParamHUE_CHHCLLockFallback;
        #endif

        _devices[ch]->setHCLChannelLock(_hclChannelLockActive[ch]);
        publishHclChannelLockStatus(ch);

        uint8_t minBrightness = ParamHUE_CHMinBrightness;
        _devices[ch]->setMinBrightness(minBrightness);
        uint8_t switchOnTransitionSec = 2;
        uint8_t switchOffTransitionSec = 6;
        #ifdef ParamHUE_HUESwitchOnTransitionSec
        switchOnTransitionSec = ParamHUE_HUESwitchOnTransitionSec;
        #endif
        #ifdef ParamHUE_HUESwitchOffTransitionSec
        switchOffTransitionSec = ParamHUE_HUESwitchOffTransitionSec;
        #endif
        _devices[ch]->setSwitchTransitionDurations(switchOnTransitionSec, switchOffTransitionSec);
        _channelLastPollMs[ch] = 0;

        Serial.printf("[HueGatewayModule] Channel %d: %s (%s)%s, Type:%u Sync:%u Poll:%us MinBri:%u%% HCL:%u OnFade:%us OffFade:%us -> KO %d/%d/%d/%d/%d\n",
                      ch + 1,
                      selectedLight->name.c_str(),
                      selectedLight->id.c_str(),
                  targetIsGrouped ? (targetIsRoom ? " [room]" : " [zone]") : "",
                      static_cast<unsigned>(effectiveLightType),
                      static_cast<unsigned>(ParamHUE_CHSyncDir),
                      static_cast<unsigned>(ParamHUE_CHPollInterval),
                      static_cast<unsigned>(minBrightness),
                      static_cast<unsigned>(hclMaster),
                      static_cast<unsigned>(switchOnTransitionSec),
                      static_cast<unsigned>(switchOffTransitionSec),
                      koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);

        mappedChannels[ch] = true;
        mappedCount++;
    }

    for (uint8_t ch = maxChannels; ch < MAX_LIGHTS; ch++)
    {
        if (_devices[ch] != nullptr)
        {
            delete _devices[ch];
            _devices[ch] = nullptr;
        }
        _channelLastPollMs[ch] = 0;
        _channelFastTrackNextMs[ch] = 0;
        _channelFastTrackCooldownUntilMs[ch] = 0;
        _channelFastTrackRemaining[ch] = 0;
    }

    for (uint8_t ch = 0; ch < maxChannels; ch++)
    {
        if (mappedChannels[ch] || _devices[ch] == nullptr)
        {
            continue;
        }

        delete _devices[ch];
        _devices[ch] = nullptr;
        _channelLastPollMs[ch] = 0;
        _channelFastTrackNextMs[ch] = 0;
        _channelFastTrackCooldownUntilMs[ch] = 0;
        _channelFastTrackRemaining[ch] = 0;
    }

    _lightCount = mappedCount;

    uint8_t enabledChannels = countEnabledChannels();
    _diagLastEnabledChannels = enabledChannels;
    _diagLastHasUnresolvedGroupTarget = hasUnresolvedGroupTarget;
    if (enabledChannels > 0 && (_lightCount == 0 || (bridgeLightCount <= 0 && hasIndexMappedChannel) || hasUnresolvedGroupTarget))
    {
        _deviceSetupNeedsRetry = true;
        _diagCounterSetupIncomplete++;
        if (hasUnresolvedGroupTarget)
        {
            _diagCounterUnresolvedTargets++;
        }
        _deviceSetupRetryBackoffMs = min<unsigned long>(_deviceSetupRetryBackoffMs * 2UL, kDeviceSetupRetryMaxMs);
        registerSetupFailure("mapping-incomplete");
        Serial.printf("[HueGatewayModule] setupDevices incomplete (enabled=%u, mapped=%d, bridgeLights=%d, unresolvedGroup=%u), retry scheduled\n",
                      static_cast<unsigned>(enabledChannels),
                      _lightCount,
                      bridgeLightCount,
                      hasUnresolvedGroupTarget ? 1 : 0);
        appendDiagnosticLog("WARN", "MAP", "setupDevices incomplete: mapping unresolved or empty");
    }
    else
    {
        _deviceSetupNeedsRetry = false;
        _deviceSetupRetryBackoffMs = kDeviceSetupRetryBaseMs;
        _setupCircuitTrips = 0;
        _setupCircuitOpenUntilMs = 0;
    }
    
    Serial.printf("[HueGatewayModule] Initialized %d lights\n", _lightCount);
    {
        uint8_t onFade = 2;
        uint8_t offFade = 6;
        uint8_t hclFade = 0;
        #ifdef ParamHUE_HUESwitchOnTransitionSec
        onFade = ParamHUE_HUESwitchOnTransitionSec;
        #endif
        #ifdef ParamHUE_HUESwitchOffTransitionSec
        offFade = ParamHUE_HUESwitchOffTransitionSec;
        #endif
        #ifdef ParamHUE_HUEHCLFadeDuration
        hclFade = ParamHUE_HUEHCLFadeDuration;
        #endif
        appendDiagnosticLog("INFO", "SETUP", String("setupDevices done: mapped=") + String(_lightCount)
            + " onFade=" + String(static_cast<unsigned>(onFade)) + "s"
            + " offFade=" + String(static_cast<unsigned>(offFade)) + "s"
            + " hclFade=" + String(static_cast<unsigned>(hclFade)) + "s");
    }

    _lastChannelSyncOkMs = millis();
    _devicesInitialized = true;
    finalizeSetupSession("ok");
}

void HueGatewayModule::processBehaviorInstanceAutoDelete()
{
    if (!_client || !_devicesInitialized)
        return;

    Serial.println("[HueGatewayModule] processBehaviorInstanceAutoDelete: checking channels...");

    #ifdef ParamHUE_HUEChannelCount
    const uint8_t configuredChannels = min(static_cast<uint8_t>(ParamHUE_HUEChannelCount), static_cast<uint8_t>(MAX_LIGHTS));
    #else
    const uint8_t configuredChannels = MAX_LIGHTS;
    #endif

    int totalDeleted = 0;
    for (uint8_t ch = 0; ch < configuredChannels; ch++)
    {
        if (_devices[ch] == nullptr || _devices[ch]->deviceType() != 2)
            continue;

        #ifdef ParamHUE_CHNativeHueAction
        {
            uint8_t _channelIndex = ch;
            const uint8_t nativeAction = ParamHUE_CHNativeHueAction;
            if (nativeAction != 1)
                continue;

            const String resourceId = _devices[ch]->getResourceId();
            static constexpr int kMaxInst = 8;
            String instanceIds[kMaxInst];
            const int nInst = _client->getBehaviorInstances(resourceId, instanceIds, kMaxInst);
            if (nInst > 0)
            {
                for (int i = 0; i < nInst; i++)
                {
                    Serial.printf("  auto-delete behavior_instance[%d]=%s for ch%d\n",
                                  i, instanceIds[i].c_str(), ch + 1);
                    _client->deleteBehaviorInstance(instanceIds[i]);
                    totalDeleted++;
                    delay(100);
                }
                appendDiagnosticLog("INFO", "BI_DEL", String("ch") + String(ch + 1)
                    + " deleted=" + String(nInst));
            }
        }
        #endif
    }

    Serial.printf("[HueGatewayModule] processBehaviorInstanceAutoDelete: deleted %d instances\n", totalDeleted);
}

void HueGatewayModule::setupHCL()
{
    Serial.println("[HueGatewayModule] Setting up HCL...");
    setHclLock(false, "setup");
    for (uint8_t manager = 1; manager <= HCL::MasterManager::MAX_MASTERS; manager++)
    {
        setHclManagerLock(manager, false, "setup");
    }
    for (uint8_t channel = 0; channel < MAX_LIGHTS; channel++)
    {
        setHclChannelLock(channel, false, "setup");
    }

    Serial.println("[HueGatewayModule] HCL ownership moved to LightManagerModule");
}
void HueGatewayModule::checkConnection()
{
    const unsigned long now = millis();
    static unsigned long sConnectedCandidateSinceMs = 0;
    static unsigned long sDisconnectedCandidateSinceMs = 0;

    auto requestConnectedStatus = [&]()
    {
        sDisconnectedCandidateSinceMs = 0;
        if (_authPending)
        {
            return;
        }

        if (_bridgeStatus == BridgeStatus::CONNECTED)
        {
            sConnectedCandidateSinceMs = 0;
            return;
        }

        if (sConnectedCandidateSinceMs == 0 || now < sConnectedCandidateSinceMs)
        {
            sConnectedCandidateSinceMs = now;
        }

        if ((now - sConnectedCandidateSinceMs) >= kConnectedEnterHysteresisMs)
        {
            updateStatus(BridgeStatus::CONNECTED);
            sConnectedCandidateSinceMs = 0;
        }
    };

    auto requestConnectionLostStatus = [&]()
    {
        sConnectedCandidateSinceMs = 0;
        if (_authPending)
        {
            return;
        }

        if (_bridgeStatus != BridgeStatus::CONNECTED)
        {
            updateStatus(BridgeStatus::CONNECTION_LOST);
            sDisconnectedCandidateSinceMs = 0;
            return;
        }

        if (sDisconnectedCandidateSinceMs == 0 || now < sDisconnectedCandidateSinceMs)
        {
            sDisconnectedCandidateSinceMs = now;
        }

        if ((now - sDisconnectedCandidateSinceMs) >= kConnectedExitHysteresisMs)
        {
            Serial.println("[HueGatewayModule] Connectivity degraded beyond hysteresis window, marking connection lost");
            updateStatus(BridgeStatus::CONNECTION_LOST);
            sDisconnectedCandidateSinceMs = 0;
        }
    };

    uint8_t configuredChannels = ParamHUE_HUEChannelCount;
    if (configuredChannels > MAX_LIGHTS)
    {
        configuredChannels = MAX_LIGHTS;
    }
    const bool blockConnectedStatusUntilSetup = (configuredChannels > 0)
        && (_deviceSetupNeedsRetry || _lightCount <= 0);

    if (_client && _client->isInitialized() && _client->isEventStreamConnected())
    {
        _lastBridgeHealthOkMs = now;
        if (!blockConnectedStatusUntilSetup)
        {
            requestConnectedStatus();
        }
        return;
    }

    if (_bridgeIP.isEmpty())
    {
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }

    if (!_client || !_client->isInitialized())
    {
        requestConnectionLostStatus();
        return;
    }

    if (_lightCount > 0
        && !_client->isEventStreamConnected()
        && _lastChannelSyncOkMs != 0
        && (now - _lastChannelSyncOkMs) <= 25000UL)
    {
        _lastBridgeHealthOkMs = now;
        if (!blockConnectedStatusUntilSetup)
        {
            requestConnectedStatus();
        }
        return;
    }

    if (_lastBridgeHealthOkMs != 0 && (now - _lastBridgeHealthOkMs) <= kBridgeHealthRecentSkipPingMs)
    {
        if (!blockConnectedStatusUntilSetup)
        {
            requestConnectedStatus();
        }
        return;
    }

    if (now < sBridgeNextPingAllowedMs)
    {
        if (_bridgeStatus == BridgeStatus::CONNECTED && _lastBridgeHealthOkMs != 0 && (now - _lastBridgeHealthOkMs) > 60000UL)
        {
            Serial.println("[HueGatewayModule] Bridge health stale for >60s, marking connection lost");
            requestConnectionLostStatus();
        }
        return;
    }

    if (_client->pingBridgeApiV2())
    {
        _lastBridgeHealthOkMs = now;
        sBridgePingBackoffMs = kBridgePingBackoffMinMs;
        sBridgeNextPingAllowedMs = 0;
        if (!blockConnectedStatusUntilSetup)
        {
            requestConnectedStatus();
        }
    }
    else
    {
        requestConnectionLostStatus();
        sBridgeNextPingAllowedMs = now + sBridgePingBackoffMs;
        sBridgePingBackoffMs = min<unsigned long>(sBridgePingBackoffMs * 2UL, kBridgePingBackoffMaxMs);
    }

    if (_bridgeStatus == BridgeStatus::CONNECTED && _lastBridgeHealthOkMs != 0 && (now - _lastBridgeHealthOkMs) > 60000UL)
    {
        Serial.println("[HueGatewayModule] Bridge health stale for >60s, marking connection lost");
        requestConnectionLostStatus();
        return;
    }

    if (_bridgeStatus == BridgeStatus::CONNECTED
        && _lightCount > 0
        && !_client->isEventStreamConnected()
        && _lastChannelSyncOkMs != 0
        && (now - _lastChannelSyncOkMs) > 300000UL)
    {
        Serial.println("[HueGatewayModule] Channel sync stale for >5min while polling fallback active");
        requestConnectionLostStatus();
    }
}

void HueGatewayModule::refreshLightStatus()
{
    static bool sPollingModeMarkerLogged = false;
    if (!sPollingModeMarkerLogged)
    {
        sPollingModeMarkerLogged = true;
        Serial.println("[HueGatewayModule] Polling mode v2 active (alternating endpoint fetch + extended cache)");
    }

    if (!_client || !_client->isInitialized())
    {
        return;
    }

    unsigned long now = millis();
    if (_pollBackoffUntilMs != 0 && now < _pollBackoffUntilMs)
    {
        return;
    }

    bool dueFlags[MAX_LIGHTS] = {false};
    int dueChannels = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_devices[i] == nullptr)
        {
            continue;
        }

        bool fastTrackDue = (_channelFastTrackRemaining[i] > 0) &&
                            (_channelFastTrackNextMs[i] != 0) &&
                            (now >= _channelFastTrackNextMs[i]);
        if (fastTrackDue)
        {
            dueFlags[i] = true;
            dueChannels++;
            continue;
        }

        uint8_t _channelIndex = static_cast<uint8_t>(i);
        uint8_t syncDir = ParamHUE_CHSyncDir;
        if (syncDir > 2)
        {
            syncDir = 2;
        }
        if (!(syncDir == 1 || syncDir == 2))
        {
            continue;
        }

        uint8_t pollIntervalSec = ParamHUE_CHPollInterval;
        if (pollIntervalSec == 0)
        {
            continue;
        }

        unsigned long pollIntervalMs = static_cast<unsigned long>(pollIntervalSec) * 1000UL;
        if ((now - _channelLastPollMs[i]) >= pollIntervalMs)
        {
            dueFlags[i] = true;
            dueChannels++;
        }
    }

    if (dueChannels == 0)
    {
        return;
    }

    int dueGroupedChannels = 0;
    int dueLightChannels = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (!dueFlags[i] || _devices[i] == nullptr)
        {
            continue;
        }

        if (_devices[i]->isGroupedTarget())
        {
            dueGroupedChannels++;
        }
        else
        {
            dueLightChannels++;
        }
    }

    Serial.printf("[HueGatewayModule] Polling Hue bridge for %d due channel(s)\n", dueChannels);

    static HueGatewayLightState cachedLights[MAX_LIGHTS];
    static int cachedCount = 0;
    static unsigned long cacheValidUntilMs = 0;

    static HueGatewayLightState pollLights[MAX_LIGHTS];
    HueGatewayLightState* lightSnapshot = pollLights;
    int count = 0;

    static HueGatewayLightState cachedGroupedLights[MAX_LIGHTS];
    static int cachedGroupedCount = 0;
    static unsigned long groupedCacheValidUntilMs = 0;

    static HueGatewayLightState pollGroupedLights[MAX_LIGHTS];
    HueGatewayLightState* groupedSnapshot = pollGroupedLights;
    int groupedCount = 0;

    static unsigned long lastLightSnapshotMs = 0UL;
    static unsigned long lastGroupedSnapshotMs = 0UL;
    static bool sPollGroupedThisCycle = false;

    const bool lightCacheReusable = (dueLightChannels > 0 && cacheValidUntilMs != 0 && now <= cacheValidUntilMs);
    const bool groupedCacheReusable = (dueGroupedChannels > 0 && groupedCacheValidUntilMs != 0 && now <= groupedCacheValidUntilMs);
    const bool haveLightSnapshot = (cachedCount > 0 && lastLightSnapshotMs != 0 && (now - lastLightSnapshotMs) <= kPollingSnapshotHardMaxAgeMs);
    const bool haveGroupedSnapshot = (cachedGroupedCount > 0 && lastGroupedSnapshotMs != 0 && (now - lastGroupedSnapshotMs) <= kPollingSnapshotHardMaxAgeMs);

    bool fetchLightNow = false;
    bool fetchGroupedNow = false;

    if (dueLightChannels > 0 && dueGroupedChannels > 0)
    {
        if (!haveLightSnapshot && !haveGroupedSnapshot)
        {
            fetchLightNow = true;
            fetchGroupedNow = true;
        }
        else if (!haveLightSnapshot)
        {
            fetchLightNow = true;
        }
        else if (!haveGroupedSnapshot)
        {
            fetchGroupedNow = true;
        }
        else if (sPollGroupedThisCycle)
        {
            fetchGroupedNow = true;
        }
        else
        {
            fetchLightNow = true;
        }
        sPollGroupedThisCycle = !sPollGroupedThisCycle;
    }
    else
    {
        fetchLightNow = (dueLightChannels > 0) && !lightCacheReusable;
        fetchGroupedNow = (dueGroupedChannels > 0) && !groupedCacheReusable;
    }

    if ((lightCacheReusable || haveLightSnapshot) && !fetchLightNow)
    {
        lightSnapshot = cachedLights;
        count = cachedCount;
        Serial.printf("[HueGatewayModule] Reusing poll cache with %d light(s)\n", count);
    }
    else if (dueLightChannels > 0)
    {
        count = _client->getLights(pollLights, MAX_LIGHTS);
        Serial.printf("[HueGatewayModule] Polling returned %d light(s)\n", count);
    }

    if ((groupedCacheReusable || haveGroupedSnapshot) && !fetchGroupedNow)
    {
        groupedSnapshot = cachedGroupedLights;
        groupedCount = cachedGroupedCount;
        Serial.printf("[HueGatewayModule] Reusing grouped poll cache with %d target(s)\n", groupedCount);
    }
    else if (dueGroupedChannels > 0)
    {
        groupedCount = _client->getGroupedLights(pollGroupedLights, MAX_LIGHTS);
        Serial.printf("[HueGatewayModule] Grouped polling returned %d target(s)\n", groupedCount);
    }

    if ((dueLightChannels > 0 && count <= 0) && (dueGroupedChannels > 0 && groupedCount <= 0))
    {
        _pollFailureCount = min<uint8_t>(static_cast<uint8_t>(_pollFailureCount + 1), static_cast<uint8_t>(10));
        _pollBackoffMs = min<unsigned long>(_pollBackoffMs * 2UL, 60000UL);
        _pollBackoffUntilMs = now + _pollBackoffMs;
        cacheValidUntilMs = 0;
        cachedCount = 0;
        groupedCacheValidUntilMs = 0;
        cachedGroupedCount = 0;

        if (_bridgeStatus == BridgeStatus::CONNECTED)
        {
            updateStatus(BridgeStatus::CONNECTION_LOST);
        }
        return;
    }

    _pollFailureCount = 0;
    _pollBackoffMs = 1000;
    _pollBackoffUntilMs = 0;
    _lastBridgeHealthOkMs = now;

    if (dueLightChannels > 0 && lightSnapshot == pollLights)
    {
        cachedCount = count;
        for (int i = 0; i < count; i++)
        {
            cachedLights[i] = pollLights[i];
        }
        cacheValidUntilMs = now + kPollingSnapshotCacheMs;
        if (count > 0)
        {
            lastLightSnapshotMs = now;
        }
        lightSnapshot = cachedLights;
    }

    if (dueGroupedChannels > 0 && groupedSnapshot == pollGroupedLights)
    {
        cachedGroupedCount = groupedCount;
        for (int i = 0; i < groupedCount; i++)
        {
            cachedGroupedLights[i] = pollGroupedLights[i];
        }
        groupedCacheValidUntilMs = now + kPollingSnapshotCacheMs;
        if (groupedCount > 0)
        {
            lastGroupedSnapshotMs = now;
        }
        groupedSnapshot = cachedGroupedLights;
    }

    if (_bridgeStatus == BridgeStatus::CONNECTION_LOST || _bridgeStatus == BridgeStatus::BRIDGE_UNREACHABLE)
    {
        updateStatus(BridgeStatus::CONNECTED);
    }

    static constexpr uint8_t kMaxChannelsPerTick = 5;
    uint8_t processedThisTick = 0;
    uint8_t lastProcessedIndex = _pollCursor;

    for (int offset = 0; offset < MAX_LIGHTS && processedThisTick < kMaxChannelsPerTick; offset++)
    {
        int i = (_pollCursor + offset) % MAX_LIGHTS;

        if (_devices[i] == nullptr)
        {
            continue;
        }

        if (!dueFlags[i])
        {
            continue;
        }

        bool fastTrackDue = (_channelFastTrackRemaining[i] > 0) &&
                            (_channelFastTrackNextMs[i] != 0) &&
                            (now >= _channelFastTrackNextMs[i]);

        uint8_t _channelIndex = static_cast<uint8_t>(i);
        uint8_t syncDir = ParamHUE_CHSyncDir;
        if (syncDir > 2)
        {
            syncDir = 2;
        }
        if (!(syncDir == 1 || syncDir == 2))
        {
            continue;
        }

        uint8_t pollIntervalSec = ParamHUE_CHPollInterval;
        if (!fastTrackDue && pollIntervalSec == 0)
        {
            continue;
        }

        unsigned long pollIntervalMs = static_cast<unsigned long>(pollIntervalSec) * 1000UL;
        if (_pollFailureCount > 0)
        {
            uint8_t failureExp = min<uint8_t>(_pollFailureCount, static_cast<uint8_t>(3));
            pollIntervalMs = min<unsigned long>(pollIntervalMs * (1UL << failureExp), 120000UL);
        }
        if (!fastTrackDue && (now - _channelLastPollMs[i]) < pollIntervalMs)
        {
            continue;
        }
        if (!fastTrackDue)
        {
            _channelLastPollMs[i] = now;
        }
        processedThisTick++;
        lastProcessedIndex = static_cast<uint8_t>(i);

        HueGatewayLightState* channelSnapshot = _devices[i]->isGroupedTarget() ? groupedSnapshot : lightSnapshot;
        int channelSnapshotCount = _devices[i]->isGroupedTarget() ? groupedCount : count;

        for (int j = 0; j < channelSnapshotCount; j++)
        {
            HueGatewayLight* lightI = lightAt(i);
            if (lightI != nullptr && channelSnapshot[j].id == lightI->getLightId())
            {
                lightI->updateFromHue(
                    channelSnapshot[j].on,
                    channelSnapshot[j].brightness,
                    channelSnapshot[j].colorTempKelvin,
                    channelSnapshot[j].red,
                    channelSnapshot[j].green,
                    channelSnapshot[j].blue);

                _lastChannelSyncOkMs = now;

                if (fastTrackDue)
                {
                    if (_channelFastTrackRemaining[i] > 0)
                    {
                        _channelFastTrackRemaining[i]--;
                    }

                    if (_channelFastTrackRemaining[i] > 0)
                    {
                        _channelFastTrackNextMs[i] = now + kFastTrackSecondDelayMs;
                    }
                    else
                    {
                        _channelFastTrackNextMs[i] = 0;
                    }
                }
                break;
            }
        }

        // ---- Sensor-/Steckdosen-Polling (kein Licht-Snapshot) ----
        #ifdef ParamHUE_CHDeviceType
        if (_devices[i] != nullptr && lightAt(i) == nullptr)
        {
            const uint8_t devType = ParamHUE_CHDeviceType;
            if (devType == 1)  // Bewegungsmelder
            {
                HueGatewayMotionState ms;
                if (_client->getMotionState(_devices[i]->getResourceId(), ms))
                {
                    static_cast<HueGatewaySensor*>(_devices[i])->updateFromState(ms);
                    _lastChannelSyncOkMs = now;
                }
            }
            else if (devType == 3)  // Kontaktsensor
            {
                HueGatewayContactState cs;
                if (_client->getContactState(_devices[i]->getResourceId(), cs))
                {
                    static_cast<HueGatewayContact*>(_devices[i])->updateFromState(cs);
                    _lastChannelSyncOkMs = now;
                }
            }
            else if (devType == 4)  // Steckdose via Licht-Snapshot
            {
                for (int j = 0; j < count; j++)
                {
                    if (lightSnapshot[j].id == _devices[i]->getResourceId())
                    {
                        static_cast<HueGatewayPlug*>(_devices[i])->updateFromHue(lightSnapshot[j].on, lightSnapshot[j].reachable);
                        _lastChannelSyncOkMs = now;
                        break;
                    }
                }
            }
        }
        #endif

    }

    _pollCursor = static_cast<uint8_t>((lastProcessedIndex + 1) % MAX_LIGHTS);

    if (dueChannels > processedThisTick)
    {
        Serial.printf("[HueGatewayModule] Poll chunk processed %u/%d due channel(s), remaining queued for next tick\n",
                      static_cast<unsigned>(processedThisTick),
                      dueChannels);
    }
    else
    {
        cacheValidUntilMs = 0;
        groupedCacheValidUntilMs = 0;
    }
}

// ===== Helper Methods =====

bool HueGatewayModule::hasNetworkConnectivity() const
{
#if defined(HUEGATEWAY_HAS_OPENKNX_NETWORK) && defined(NET_ModuleVersion)
    if (openknxNetwork.established() || openknxNetwork.connected())
    {
        return true;
    }
#endif

    const IPAddress wifiIp = WiFi.localIP();
    if (isUsableIp(wifiIp))
    {
        return true;
    }

    const IPAddress ethIp = ETH.localIP();
    if (isUsableIp(ethIp))
    {
        return true;
    }

    if (ETH.linkUp())
    {
        return true;
    }

    return WiFi.status() == WL_CONNECTED;
}

uint8_t HueGatewayModule::countEnabledChannels() const
{
    uint8_t channelCount = ParamHUE_HUEChannelCount;
    if (channelCount > MAX_LIGHTS)
    {
        channelCount = MAX_LIGHTS;
    }

    uint8_t enabled = 0;
    for (uint8_t ch = 0; ch < channelCount; ch++)
    {
        uint8_t _channelIndex = ch;
        if (!ParamHUE_CHDisabled)
        {
            enabled++;
        }
    }

    return enabled;
}

const char* HueGatewayModule::hclFallbackModeToText(HclLockFallbackMode mode)
{
    switch (mode)
    {
        case HclLockFallbackMode::None: return "kein Rueckfall";
        case HclLockFallbackMode::Min1: return "1 min";
        case HclLockFallbackMode::Min2: return "2 min";
        case HclLockFallbackMode::Min5: return "5 min";
        case HclLockFallbackMode::Min10: return "10 min";
        case HclLockFallbackMode::Min20: return "20 min";
        case HclLockFallbackMode::Min30: return "30 min";
        case HclLockFallbackMode::Hour1: return "1 h";
        case HclLockFallbackMode::Hour2: return "2 h";
        case HclLockFallbackMode::Hour5: return "5 h";
        case HclLockFallbackMode::Hour8: return "8 h";
        case HclLockFallbackMode::Hour12: return "12 h";
        case HclLockFallbackMode::NextDay: return "Tageswechsel";
        default: return "unbekannt";
    }
}

const char* HueGatewayModule::hclFallbackPolicyToText(HclLockFallbackPolicy policy)
{
    switch (policy)
    {
        case HclLockFallbackPolicy::Legacy: return "legacy";
        case HclLockFallbackPolicy::Duration: return "dauer";
        case HclLockFallbackPolicy::TimeOfDay: return "uhrzeit";
        case HclLockFallbackPolicy::DurationOrTime: return "dauer-oder-uhrzeit";
        case HclLockFallbackPolicy::ExternalOnly: return "extern";
        default: return "unbekannt";
    }
}

bool HueGatewayModule::shouldReleaseByPolicyTime(int16_t activationDayOfYear, int16_t activationMinuteOfDay, uint16_t releaseMinuteOfDay, const tm* timeinfo, bool hasTime) const
{
    if (!hasTime || timeinfo == nullptr || releaseMinuteOfDay == 0xFFFF)
    {
        return false;
    }

    const int16_t currentDay = static_cast<int16_t>(timeinfo->tm_yday);
    const int16_t currentMinute = static_cast<int16_t>((timeinfo->tm_hour * 60) + timeinfo->tm_min);

    if (activationDayOfYear < 0 || activationMinuteOfDay < 0)
    {
        return false;
    }

    if (currentDay < activationDayOfYear)
    {
        return false;
    }

    if (currentDay == activationDayOfYear)
    {
        // If lock was activated after release time, release at next day's release time.
        if (releaseMinuteOfDay <= static_cast<uint16_t>(activationMinuteOfDay))
        {
            return false;
        }
        return currentMinute >= static_cast<int16_t>(releaseMinuteOfDay);
    }

    return currentMinute >= static_cast<int16_t>(releaseMinuteOfDay);
}

uint32_t HueGatewayModule::getHclFallbackDurationMs(HclLockFallbackMode mode) const
{
    switch (mode)
    {
        case HclLockFallbackMode::Min1: return 1UL * 60UL * 1000UL;
        case HclLockFallbackMode::Min2: return 2UL * 60UL * 1000UL;
        case HclLockFallbackMode::Min5: return 5UL * 60UL * 1000UL;
        case HclLockFallbackMode::Min10: return 10UL * 60UL * 1000UL;
        case HclLockFallbackMode::Min20: return 20UL * 60UL * 1000UL;
        case HclLockFallbackMode::Min30: return 30UL * 60UL * 1000UL;
        case HclLockFallbackMode::Hour1: return 1UL * 60UL * 60UL * 1000UL;
        case HclLockFallbackMode::Hour2: return 2UL * 60UL * 60UL * 1000UL;
        case HclLockFallbackMode::Hour5: return 5UL * 60UL * 60UL * 1000UL;
        case HclLockFallbackMode::Hour8: return 8UL * 60UL * 60UL * 1000UL;
        case HclLockFallbackMode::Hour12: return 12UL * 60UL * 60UL * 1000UL;
        default: return 0;
    }
}

void HueGatewayModule::publishHclLockStatus()
{
    #ifdef HUE_KoHUEHCLLockStatus
    knx.getGroupObject(HUE_KoHUEHCLLockStatus).value(_hclLockActive, Dpt(1, 1));
    #endif
}

void HueGatewayModule::publishHclManagerLockStatus(uint8_t managerNumber)
{
    if (managerNumber < 1 || managerNumber > HCL::MasterManager::MAX_MASTERS)
    {
        return;
    }

    const bool active = _hclManagerLockActive[managerNumber - 1];
    (void)active;
    switch (managerNumber)
    {
        case 1:
            #ifdef HUE_KoHUEHCLM1LockStatus
            knx.getGroupObject(HUE_KoHUEHCLM1LockStatus).value(active, Dpt(1, 1));
            #endif
            break;
        case 2:
            #ifdef HUE_KoHUEHCLM2LockStatus
            knx.getGroupObject(HUE_KoHUEHCLM2LockStatus).value(active, Dpt(1, 1));
            #endif
            break;
        case 3:
            #ifdef HUE_KoHUEHCLM3LockStatus
            knx.getGroupObject(HUE_KoHUEHCLM3LockStatus).value(active, Dpt(1, 1));
            #endif
            break;
        case 4:
            #ifdef HUE_KoHUEHCLM4LockStatus
            knx.getGroupObject(HUE_KoHUEHCLM4LockStatus).value(active, Dpt(1, 1));
            #endif
            break;
        default:
            break;
    }
}

void HueGatewayModule::publishHclChannelLockStatus(uint8_t channelIndex)
{
    if (channelIndex >= MAX_LIGHTS)
    {
        return;
    }

    const bool active = _hclChannelLockActive[channelIndex];
    (void)active;

    #ifdef HUE_KoCHHCLLockStatus
    knx.getGroupObject(HUE_KoCHHCLLockStatus).value(active, Dpt(1, 1));
    #endif
}

void HueGatewayModule::setHclLock(bool active, const char* reason)
{
    const bool changed = (_hclLockActive != active);

    _hclLockActive = active;
    HCL::masterManager.setApplyBlocked(active);

    if (_hclLockActive)
    {
        _hclLockActivatedMs = millis();
        _hclLockAutoReleaseMs = 0;
        _hclLockActivationDayOfYear = -1;
        _hclLockActivationMinuteOfDay = -1;

        const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclFallbackPolicy);

        if (fallbackPolicy == HclLockFallbackPolicy::Legacy)
        {
            const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclLockFallbackMode);
            const uint32_t durationMs = getHclFallbackDurationMs(fallbackMode);
            if (durationMs > 0)
            {
                _hclLockAutoReleaseMs = _hclLockActivatedMs + durationMs;
            }
        }
        else if (fallbackPolicy == HclLockFallbackPolicy::Duration || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
        {
            if (_hclFallbackDurationMs > 0)
            {
                _hclLockAutoReleaseMs = _hclLockActivatedMs + _hclFallbackDurationMs;
            }
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            _hclLockActivationDayOfYear = static_cast<int16_t>(timeinfo.tm_yday);
            _hclLockActivationMinuteOfDay = static_cast<int16_t>((timeinfo.tm_hour * 60) + timeinfo.tm_min);
        }

        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL lock enabled (%s), fallback=%s, policy=%s\n",
                          reason ? reason : "n/a",
                          hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclLockFallbackMode)),
                          hclFallbackPolicyToText(fallbackPolicy));
        }
    }
    else
    {
        _hclLockActivatedMs = 0;
        _hclLockAutoReleaseMs = 0;
        _hclLockActivationDayOfYear = -1;
        _hclLockActivationMinuteOfDay = -1;
        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL lock disabled (%s)\n", reason ? reason : "n/a");
        }
    }

    publishHclLockStatus();
}

void HueGatewayModule::setHclManagerLock(uint8_t managerNumber, bool active, const char* reason)
{
    if (managerNumber < 1 || managerNumber > HCL::MasterManager::MAX_MASTERS)
    {
        return;
    }

    const uint8_t idx = static_cast<uint8_t>(managerNumber - 1);
    const bool changed = (_hclManagerLockActive[idx] != active);

    _hclManagerLockActive[idx] = active;
    HCL::masterManager.setMasterApplyBlocked(managerNumber, active);

    if (active)
    {
        _hclManagerLockActivatedMs[idx] = millis();
        _hclManagerLockAutoReleaseMs[idx] = 0;
        _hclManagerLockActivationDayOfYear[idx] = -1;
        _hclManagerLockActivationMinuteOfDay[idx] = -1;

        const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclManagerFallbackPolicy[idx]);

        if (fallbackPolicy == HclLockFallbackPolicy::Legacy)
        {
            const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclManagerLockFallbackMode[idx]);
            const uint32_t durationMs = getHclFallbackDurationMs(fallbackMode);
            if (durationMs > 0)
            {
                _hclManagerLockAutoReleaseMs[idx] = _hclManagerLockActivatedMs[idx] + durationMs;
            }
        }
        else if (fallbackPolicy == HclLockFallbackPolicy::Duration || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
        {
            if (_hclManagerFallbackDurationMs[idx] > 0)
            {
                _hclManagerLockAutoReleaseMs[idx] = _hclManagerLockActivatedMs[idx] + _hclManagerFallbackDurationMs[idx];
            }
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            _hclManagerLockActivationDayOfYear[idx] = static_cast<int16_t>(timeinfo.tm_yday);
            _hclManagerLockActivationMinuteOfDay[idx] = static_cast<int16_t>((timeinfo.tm_hour * 60) + timeinfo.tm_min);
        }

        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL manager %u lock enabled (%s), fallback=%s, policy=%s\n",
                          static_cast<unsigned>(managerNumber),
                          reason ? reason : "n/a",
                          hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclManagerLockFallbackMode[idx])),
                          hclFallbackPolicyToText(fallbackPolicy));
        }
    }
    else
    {
        _hclManagerLockActivatedMs[idx] = 0;
        _hclManagerLockAutoReleaseMs[idx] = 0;
        _hclManagerLockActivationDayOfYear[idx] = -1;
        _hclManagerLockActivationMinuteOfDay[idx] = -1;
        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL manager %u lock disabled (%s)\n",
                          static_cast<unsigned>(managerNumber),
                          reason ? reason : "n/a");
        }
    }

    publishHclManagerLockStatus(managerNumber);
}

void HueGatewayModule::setHclChannelLock(uint8_t channelIndex, bool active, const char* reason)
{
    if (channelIndex >= MAX_LIGHTS)
    {
        return;
    }

    const bool changed = (_hclChannelLockActive[channelIndex] != active);
    _hclChannelLockActive[channelIndex] = active;

    if (_devices[channelIndex] != nullptr)
    {
        _devices[channelIndex]->setHCLChannelLock(active);
    }

    if (active)
    {
        _hclChannelLockActivatedMs[channelIndex] = millis();
        _hclChannelLockAutoReleaseMs[channelIndex] = 0;
        _hclChannelLockActivationDayOfYear[channelIndex] = -1;
        _hclChannelLockActivationMinuteOfDay[channelIndex] = -1;

        const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclFallbackPolicy);

        if (fallbackPolicy == HclLockFallbackPolicy::Legacy)
        {
            const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclChannelLockFallbackMode[channelIndex]);
            const uint32_t durationMs = getHclFallbackDurationMs(fallbackMode);
            if (durationMs > 0)
            {
                _hclChannelLockAutoReleaseMs[channelIndex] = _hclChannelLockActivatedMs[channelIndex] + durationMs;
            }
        }
        else if (fallbackPolicy == HclLockFallbackPolicy::Duration || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
        {
            if (_hclFallbackDurationMs > 0)
            {
                _hclChannelLockAutoReleaseMs[channelIndex] = _hclChannelLockActivatedMs[channelIndex] + _hclFallbackDurationMs;
            }
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            _hclChannelLockActivationDayOfYear[channelIndex] = static_cast<int16_t>(timeinfo.tm_yday);
            _hclChannelLockActivationMinuteOfDay[channelIndex] = static_cast<int16_t>((timeinfo.tm_hour * 60) + timeinfo.tm_min);
        }

        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL channel %u lock enabled (%s), fallback=%s, policy=%s\n",
                          static_cast<unsigned>(channelIndex + 1),
                          reason ? reason : "n/a",
                          hclFallbackModeToText(static_cast<HclLockFallbackMode>(_hclChannelLockFallbackMode[channelIndex])),
                          hclFallbackPolicyToText(fallbackPolicy));
        }
    }
    else
    {
        _hclChannelLockActivatedMs[channelIndex] = 0;
        _hclChannelLockAutoReleaseMs[channelIndex] = 0;
        _hclChannelLockActivationDayOfYear[channelIndex] = -1;
        _hclChannelLockActivationMinuteOfDay[channelIndex] = -1;
        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL channel %u lock disabled (%s)\n",
                          static_cast<unsigned>(channelIndex + 1),
                          reason ? reason : "n/a");
        }
    }

    publishHclChannelLockStatus(channelIndex);
}

void HueGatewayModule::evaluateHclLockFallback(const tm* timeinfo, bool hasTime)
{
    if (!_hclLockActive)
    {
        return;
    }

    const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclFallbackPolicy);
    if (fallbackPolicy == HclLockFallbackPolicy::ExternalOnly)
    {
        return;
    }

    if (fallbackPolicy != HclLockFallbackPolicy::Legacy)
    {
        const bool releaseByDuration = (_hclLockAutoReleaseMs != 0)
            && (static_cast<long>(millis() - _hclLockAutoReleaseMs) >= 0);
        const bool releaseByTime = (fallbackPolicy == HclLockFallbackPolicy::TimeOfDay || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
            && shouldReleaseByPolicyTime(_hclLockActivationDayOfYear, _hclLockActivationMinuteOfDay, _hclFallbackReleaseMinuteOfDay, timeinfo, hasTime);

        if (releaseByDuration || releaseByTime)
        {
            setHclLock(false, releaseByTime ? "fallback release time" : "fallback duration elapsed");
        }
        return;
    }

    const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclLockFallbackMode);
    if (fallbackMode == HclLockFallbackMode::None)
    {
        return;
    }

    if (_hclLockAutoReleaseMs != 0)
    {
        const unsigned long nowMs = millis();
        if (static_cast<long>(nowMs - _hclLockAutoReleaseMs) >= 0)
        {
            setHclLock(false, "fallback duration elapsed");
        }
        return;
    }

    if (fallbackMode == HclLockFallbackMode::NextDay && hasTime && timeinfo != nullptr)
    {
        if (_hclLockActivationDayOfYear >= 0 && timeinfo->tm_yday != _hclLockActivationDayOfYear)
        {
            setHclLock(false, "fallback day change");
        }
    }
}

void HueGatewayModule::evaluateHclManagerLockFallback(const tm* timeinfo, bool hasTime)
{
    for (uint8_t managerNumber = 1; managerNumber <= HCL::MasterManager::MAX_MASTERS; managerNumber++)
    {
        const uint8_t idx = static_cast<uint8_t>(managerNumber - 1);
        if (!_hclManagerLockActive[idx])
        {
            continue;
        }

        const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclManagerFallbackPolicy[idx]);

        if (fallbackPolicy == HclLockFallbackPolicy::ExternalOnly)
        {
            continue;
        }

        if (fallbackPolicy != HclLockFallbackPolicy::Legacy)
        {
            const bool releaseByDuration = (_hclManagerLockAutoReleaseMs[idx] != 0)
                && (static_cast<long>(millis() - _hclManagerLockAutoReleaseMs[idx]) >= 0);
            const bool releaseByTime = (fallbackPolicy == HclLockFallbackPolicy::TimeOfDay || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
                && shouldReleaseByPolicyTime(_hclManagerLockActivationDayOfYear[idx], _hclManagerLockActivationMinuteOfDay[idx], _hclManagerFallbackReleaseMinuteOfDay[idx], timeinfo, hasTime);

            if (releaseByDuration || releaseByTime)
            {
                setHclManagerLock(managerNumber, false, releaseByTime ? "fallback release time" : "fallback duration elapsed");
            }
            continue;
        }

        const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclManagerLockFallbackMode[idx]);
        if (fallbackMode == HclLockFallbackMode::None)
        {
            continue;
        }

        if (_hclManagerLockAutoReleaseMs[idx] != 0)
        {
            const unsigned long nowMs = millis();
            if (static_cast<long>(nowMs - _hclManagerLockAutoReleaseMs[idx]) >= 0)
            {
                setHclManagerLock(managerNumber, false, "fallback duration elapsed");
            }
            continue;
        }

        if (fallbackMode == HclLockFallbackMode::NextDay && hasTime && timeinfo != nullptr)
        {
            if (_hclManagerLockActivationDayOfYear[idx] >= 0
                && timeinfo->tm_yday != _hclManagerLockActivationDayOfYear[idx])
            {
                setHclManagerLock(managerNumber, false, "fallback day change");
            }
        }
    }
}

void HueGatewayModule::evaluateHclChannelLockFallback(const tm* timeinfo, bool hasTime)
{
    const HclLockFallbackPolicy fallbackPolicy = static_cast<HclLockFallbackPolicy>(_hclFallbackPolicy);

    for (uint8_t channelIndex = 0; channelIndex < MAX_LIGHTS; channelIndex++)
    {
        if (!_hclChannelLockActive[channelIndex])
        {
            continue;
        }

        if (fallbackPolicy == HclLockFallbackPolicy::ExternalOnly)
        {
            continue;
        }

        if (fallbackPolicy != HclLockFallbackPolicy::Legacy)
        {
            const bool releaseByDuration = (_hclChannelLockAutoReleaseMs[channelIndex] != 0)
                && (static_cast<long>(millis() - _hclChannelLockAutoReleaseMs[channelIndex]) >= 0);
            const bool releaseByTime = (fallbackPolicy == HclLockFallbackPolicy::TimeOfDay || fallbackPolicy == HclLockFallbackPolicy::DurationOrTime)
                && shouldReleaseByPolicyTime(_hclChannelLockActivationDayOfYear[channelIndex], _hclChannelLockActivationMinuteOfDay[channelIndex], _hclFallbackReleaseMinuteOfDay, timeinfo, hasTime);

            if (releaseByDuration || releaseByTime)
            {
                setHclChannelLock(channelIndex, false, releaseByTime ? "fallback release time" : "fallback duration elapsed");
            }
            continue;
        }

        const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclChannelLockFallbackMode[channelIndex]);
        if (fallbackMode == HclLockFallbackMode::None)
        {
            continue;
        }

        if (_hclChannelLockAutoReleaseMs[channelIndex] != 0)
        {
            const unsigned long nowMs = millis();
            if (static_cast<long>(nowMs - _hclChannelLockAutoReleaseMs[channelIndex]) >= 0)
            {
                setHclChannelLock(channelIndex, false, "fallback duration elapsed");
            }
            continue;
        }

        if (fallbackMode == HclLockFallbackMode::NextDay && hasTime && timeinfo != nullptr)
        {
            if (_hclChannelLockActivationDayOfYear[channelIndex] >= 0
                && timeinfo->tm_yday != _hclChannelLockActivationDayOfYear[channelIndex])
            {
                setHclChannelLock(channelIndex, false, "fallback day change");
            }
        }
    }
}

void HueGatewayModule::publishHclMasterValues()
{
    #ifdef ParamHUE_HUEHCLEnable
    if (ParamHUE_HUEHCLEnable == 0)
    {
        return;
    }
    #endif

    uint8_t masterCount = 4;
    #ifdef ParamHUE_HUEHCLMasterCount
    masterCount = ParamHUE_HUEHCLMasterCount;
    if (masterCount > 4)
    {
        masterCount = 4;
    }
    #endif

    for (uint8_t masterNumber = 1; masterNumber <= masterCount; masterNumber++)
    {
        HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
        if (!master || !master->isValid())
        {
            continue;
        }

        HCL::InterpolatedValue current = HCL::masterManager.getCurrentValue(masterNumber);
        const uint8_t brightness = current.brightness;
        const uint16_t kelvin = current.kelvin;
        const uint8_t index = static_cast<uint8_t>(masterNumber - 1);

        const unsigned long nowMs = millis();
        const unsigned long updateIntervalMs = static_cast<unsigned long>(HCL::masterManager.getUpdateInterval()) * 1000UL;
        const bool intervalElapsed = (_hclLastPublishMs[index] == 0) || ((nowMs - _hclLastPublishMs[index]) >= updateIntervalMs);

        if (_hclMasterValuesPublished[index] &&
            _hclLastPublishedBrightness[index] == brightness &&
            _hclLastPublishedKelvin[index] == kelvin)
        {
            continue;
        }

        if (!intervalElapsed)
        {
            continue;
        }

        switch (masterNumber)
        {
            case 1:
                #ifdef HUE_KoHUEHCLM1StatusBrightness
                knx.getGroupObject(HUE_KoHUEHCLM1StatusBrightness).value(brightness, Dpt(5, 1));
                #endif
                #ifdef HUE_KoHUEHCLM1StatusColorTemp
                knx.getGroupObject(HUE_KoHUEHCLM1StatusColorTemp).value(kelvin, Dpt(7, 600));
                #endif
                break;
            case 2:
                #ifdef HUE_KoHUEHCLM2StatusBrightness
                knx.getGroupObject(HUE_KoHUEHCLM2StatusBrightness).value(brightness, Dpt(5, 1));
                #endif
                #ifdef HUE_KoHUEHCLM2StatusColorTemp
                knx.getGroupObject(HUE_KoHUEHCLM2StatusColorTemp).value(kelvin, Dpt(7, 600));
                #endif
                break;
            case 3:
                #ifdef HUE_KoHUEHCLM3StatusBrightness
                knx.getGroupObject(HUE_KoHUEHCLM3StatusBrightness).value(brightness, Dpt(5, 1));
                #endif
                #ifdef HUE_KoHUEHCLM3StatusColorTemp
                knx.getGroupObject(HUE_KoHUEHCLM3StatusColorTemp).value(kelvin, Dpt(7, 600));
                #endif
                break;
            case 4:
                #ifdef HUE_KoHUEHCLM4StatusBrightness
                knx.getGroupObject(HUE_KoHUEHCLM4StatusBrightness).value(brightness, Dpt(5, 1));
                #endif
                #ifdef HUE_KoHUEHCLM4StatusColorTemp
                knx.getGroupObject(HUE_KoHUEHCLM4StatusColorTemp).value(kelvin, Dpt(7, 600));
                #endif
                break;
            default:
                break;
        }

        _hclLastPublishedBrightness[index] = brightness;
        _hclLastPublishedKelvin[index] = kelvin;
        _hclMasterValuesPublished[index] = true;
        _hclLastPublishMs[index] = millis();
    }
}

String HueGatewayModule::getBridgeIP()
{
    uint8_t mode = ParamHUE_HUEBridgeMode;
    
    if (mode == 0)
    {
        if (!hasNetworkConnectivity())
        {
            Serial.println("[HueGatewayModule] mDNS discovery skipped: network disconnected");
            return "";
        }

        const IPAddress wifiIp = WiFi.localIP();
        const IPAddress ethIp = ETH.localIP();
        if (!isUsableIp(wifiIp) && !isUsableIp(ethIp))
        {
            const unsigned long nowMs = millis();
            if ((nowMs - sBridgeNoIpSkipLastLogMs) >= kBridgeNoIpSkipLogThrottleMs || nowMs < sBridgeNoIpSkipLastLogMs)
            {
                Serial.println("[HueGatewayModule] mDNS discovery skipped: no local IP yet (waiting for DHCP)");
                sBridgeNoIpSkipLastLogMs = nowMs;
            }
            return "";
        }

        // Automatisch (mDNS)
        Serial.println("[HueGatewayModule] Using mDNS discovery...");
        HueGatewayDiscovery discovery;
        String ip;
        
        if (discovery.findBridge(ip))
        {
            Serial.printf("[HueGatewayModule] Bridge found via mDNS: %s\n", ip.c_str());
            return ip;
        }
        else
        {
            Serial.println("[HueGatewayModule] mDNS discovery failed");
            return "";
        }
    }
    else
    {
        // Manual IP
#if defined(HUE_HUEBridgeIPLength)
        const std::string ipStr = ParamHUE_HUEBridgeIPStr;
#else
        const IPAddress manualIp(htonl(ParamHUE_HUEBridgeIP));
        const String ipStr = manualIp.toString();
#endif
        Serial.printf("[HueGatewayModule] Using manual IP: %s\n", ipStr.c_str());
        return String(ipStr.c_str());
    }
}

void HueGatewayModule::resetDevices()
{
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_devices[i])
        {
            delete _devices[i];
            _devices[i] = nullptr;
        }
        _channelLastPollMs[i] = 0;
        _channelFastTrackNextMs[i] = 0;
        _channelFastTrackCooldownUntilMs[i] = 0;
        _channelFastTrackRemaining[i] = 0;
    }

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        _hclChannelLockActive[i] = false;
        _hclChannelLockFallbackMode[i] = 0;
        _hclChannelLockActivatedMs[i] = 0;
        _hclChannelLockAutoReleaseMs[i] = 0;
        _hclChannelLockActivationDayOfYear[i] = -1;
        _hclChannelLockActivationMinuteOfDay[i] = -1;
    }

    for (int i = 0; i < HCL::MasterManager::MAX_MASTERS; i++)
    {
        _hclManagerLockActive[i] = false;
        _hclManagerLockFallbackMode[i] = 0;
        _hclManagerLockActivatedMs[i] = 0;
        _hclManagerLockAutoReleaseMs[i] = 0;
        _hclManagerLockActivationDayOfYear[i] = -1;
        _hclManagerLockActivationMinuteOfDay[i] = -1;
    }

    _hclLockActive = false;

    _lightCount = 0;
    _devicesInitialized = false;
    _deviceSetupNeedsRetry = false;
}

void HueGatewayModule::performBridgeScan()
{
    Serial.println("========================================");
    Serial.println("   HUE BRIDGE SCAN (triggered via KO)");
    Serial.println("========================================");
    
    if (!_client || !_initialized)
    {
        Serial.println("ERROR: Module not initialized!");
        Serial.println("Check network and bridge authentication.");
        Serial.println("========================================");
        return;
    }
    
    static HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);
    
    if (count <= 0)
    {
        Serial.println("ERROR: No lights found!");
        Serial.println("Check Hue Bridge connection.");
        Serial.println("========================================");
        return;
    }
    
    Serial.printf("Found %d Lights:\n", count);
    Serial.println("---------------------------------");
    
    for (int i = 0; i < count; i++)
    {
        Serial.printf("%2d: %-25s %s\n", i+1, lights[i].name.c_str(), lights[i].id.c_str());
        Serial.printf("    Status: %s, Brightness: %d/254, Room: %s, Zone: %s\n",
                      lights[i].on ? "ON " : "OFF", lights[i].brightness,
                      lights[i].room.c_str(), lights[i].zone.c_str());
    }
    
    Serial.println("---------------------------------");
    Serial.println("Copy Light IDs above and paste into ETS parameters.");
    Serial.println("========================================\n");
}

// ============================================
// Status & LED management
// ============================================

void HueGatewayModule::updateStatus(BridgeStatus status)
{
    if (_bridgeStatus == status)
    {
        return;
    }

    _bridgeStatus = status;
    
    // Send status to KO
    const char* statusText = "";
    switch (status)
    {
        case BridgeStatus::DISCONNECTED:
            statusText = "Keine Verbindung";
            break;
        case BridgeStatus::CONNECTING:
            statusText = "Verbinde...";
            break;
        case BridgeStatus::WAIT_FOR_BUTTON:
            statusText = "Warte auf Button";
            break;
        case BridgeStatus::AUTHENTICATING:
            statusText = "Authentifiziere";
            break;
        case BridgeStatus::CONNECTED:
            statusText = "Verbunden";
            break;
        case BridgeStatus::CONNECTION_LOST:
            statusText = "Verbindung verloren";
            break;
        case BridgeStatus::BRIDGE_UNREACHABLE:
            statusText = "Bridge nicht erreichbar";
            break;
        case BridgeStatus::ERROR:
            statusText = "Fehler";
            break;
    }
    
    sendStatusKO(status == BridgeStatus::CONNECTED);
    Serial.printf("[HueGatewayModule] Status: %s\n", statusText);
    appendDiagnosticLog("INFO", "STATE", String("status=") + String(statusText));
}

void HueGatewayModule::sendStatusKO(bool connected)
{
    if (!ParamHUE_HUEShowConnectionStatus)
    {
        return;
    }

    GroupObject& ko = KoHUE_HUEConnectionStatus;
    ko.value(connected, Dpt(1, 1));
}

void HueGatewayModule::pollAuthentication()
{
    if (!_authPending)
    {
        return;
    }

    unsigned long now = millis();
    if (now - _authStartTime > _authWindowMs)
    {
        _authPending = false;
        updateStatus(BridgeStatus::CONNECTION_LOST);
        Serial.printf("[HueGatewayModule] Authentication timeout after %lu ms - button not pressed\n",
                      static_cast<unsigned long>(now - _authStartTime));
        appendDiagnosticLog("WARN", "AUTH", "pairing timeout (button not pressed)");
        return;
    }

    if (now - _authLastTry < 1000)
    {
        return;
    }

    _authLastTry = now;

    unsigned long elapsedMs = now - _authStartTime;
    unsigned long remainingMs = (_authWindowMs > elapsedMs) ? (_authWindowMs - elapsedMs) : 0;
    Serial.printf("[HueGatewayModule] Pairing attempt at t=%lus (remaining=%lus)\n",
                  static_cast<unsigned long>(elapsedMs / 1000UL),
                  static_cast<unsigned long>(remainingMs / 1000UL));

    if (_auth.requestAppKeyOnce(_bridgeIP.c_str()))
    {
        _authPending = false;
        Serial.println("[HueGatewayModule] Authentication successful");
        appendDiagnosticLog("INFO", "AUTH", "pairing successful");
        updateStatus(BridgeStatus::AUTHENTICATING);

        if (initClientWithAppKey())
        {
            updateStatus(BridgeStatus::CONNECTED);
            _reconnectBackoffMs = 10000;
            _lastReconnectTryMs = 0;
            setupDevices();
        }
        else
        {
            Serial.println("[HueGatewayModule] Authentication succeeded, but client initialization failed");
            updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        }
    }
}

bool HueGatewayModule::initClientWithAppKey()
{
    resetDevices();

    if (_client)
    {
        delete _client;
        _client = nullptr;
    }

    _client = new HueGatewayClient();
    Serial.println("[HueGatewayModule] Initializing HueGatewayClient with stored/new App-Key...");
    if (!_client->begin(_bridgeIP, _auth.getAppKey()))
    {
        Serial.println("[HueGatewayModule] ERROR: HueGatewayClient init failed!");
        delete _client;
        _client = nullptr;
        return false;
    }

    _lastEventStreamRetryMs = 0;
    _reconnectBackoffMs = 10000;
    Serial.printf("[HueGatewayModule] EventStream policy default=%d, enabled=%s\n",
                  static_cast<int>(OPENKNX_HUE_EVENTSTREAM_POLICY_DEFAULT),
                  sEventStreamEnabled ? "yes" : "no");

    uint8_t configuredChannels = ParamHUE_HUEChannelCount;
    if (configuredChannels > MAX_LIGHTS)
    {
        configuredChannels = MAX_LIGHTS;
    }
    const bool requiresDeviceSetup = configuredChannels > 0;
    const bool inWarmupWindow = (_bootStartMs != 0)
        && ((millis() - _bootStartMs) < kBootEventStreamWarmupMs);

    if (sEventStreamEnabled)
    {
        if (requiresDeviceSetup)
        {
            Serial.println("[HueGatewayModule] EventStream initial start deferred until setupDevices succeeds");
        }
        else if (inWarmupWindow)
        {
            const unsigned long warmupLeftMs = kBootEventStreamWarmupMs - (millis() - _bootStartMs);
            Serial.printf("[HueGatewayModule] EventStream initial start deferred by warmup (%lu ms left)\n",
                          static_cast<unsigned long>(warmupLeftMs));
        }
        else
        {
            bool eventStreamStarted = _client->startEventStream();
            Serial.printf("[HueGatewayModule] EventStream initial start: %s\n", eventStreamStarted ? "ok" : "failed (fallback polling active)");
        }
    }
    else
    {
        Serial.println("[HueGatewayModule] EventStream disabled by policy (polling-only mode)");
    }

    return true;
}

void HueGatewayModule::applyEventStreamUpdates(const HueGatewayEventLightUpdate* updates, int updateCount)
{
    if (updates == nullptr || updateCount <= 0)
    {
        return;
    }

    if (_lightCount <= 0)
    {
        static uint32_t lastNoChannelEventLogMs = 0;
        uint32_t nowMs = millis();
        if ((nowMs - lastNoChannelEventLogMs) >= 30000 || nowMs < lastNoChannelEventLogMs)
        {
            Serial.println("[HueGatewayModule] EventStream update ignored (no Hue channels configured)");
            lastNoChannelEventLogMs = nowMs;
        }
        return;
    }

    uint16_t ignoredUnmappedLightEvents = 0;
    uint16_t ignoredUnmappedGroupedEvents = 0;
    String ignoredLightSampleId;
    String ignoredGroupedSampleId;

    for (int u = 0; u < updateCount; u++)
    {
        bool applied = false;
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            HueGatewayLight* light = lightAt(i);
            if (light == nullptr)
            {
                continue;
            }

            if (light->isGroupedTarget() != updates[u].isGroupedResource)
            {
                continue;
            }

            if (light->getLightId() != updates[u].lightId)
            {
                continue;
            }

            uint8_t _channelIndex = static_cast<uint8_t>(i);
            uint8_t syncDir = ParamHUE_CHSyncDir;
            if (syncDir > 2)
            {
                syncDir = 2;
            }
            if (!(syncDir == 1 || syncDir == 2))
            {
                Serial.printf("[HueGatewayModule] EventStream update ignored by SyncDir on channel %d (SyncDir=%u)\n",
                              i + 1,
                              static_cast<unsigned>(syncDir));
                break;
            }

            bool on = updates[u].hasOn ? updates[u].on : light->isOn();
            uint8_t brightness = updates[u].hasBrightness ? updates[u].brightness : light->getBrightness();
            uint16_t colorTempKelvin = updates[u].hasColorTemp ? updates[u].colorTempKelvin : light->getColorTempKelvin();
            uint8_t red = updates[u].hasColorRgb ? updates[u].red : light->getRed();
            uint8_t green = updates[u].hasColorRgb ? updates[u].green : light->getGreen();
            uint8_t blue = updates[u].hasColorRgb ? updates[u].blue : light->getBlue();

            light->updateFromHue(on, brightness, colorTempKelvin, red, green, blue);
            Serial.printf("[HueGatewayModule] Event applied -> channel %d id=%s on=%d bri=%u ct=%u rgb=(%u,%u,%u)\n",
                          i + 1,
                          updates[u].lightId.c_str(),
                          on ? 1 : 0,
                          static_cast<unsigned>(brightness),
                          static_cast<unsigned>(colorTempKelvin),
                          static_cast<unsigned>(red),
                          static_cast<unsigned>(green),
                          static_cast<unsigned>(blue));

            _channelFastTrackRemaining[i] = 0;
            _channelFastTrackNextMs[i] = 0;

            applied = true;
            break;
        }

        if (!applied)
        {
            if (updates[u].isGroupedResource)
            {
                ignoredUnmappedGroupedEvents++;
                if (ignoredGroupedSampleId.length() == 0)
                {
                    ignoredGroupedSampleId = updates[u].lightId;
                }
            }
            else
            {
                ignoredUnmappedLightEvents++;
                if (ignoredLightSampleId.length() == 0)
                {
                    ignoredLightSampleId = updates[u].lightId;
                }
            }
        }
    }

    if (ignoredUnmappedLightEvents > 0 || ignoredUnmappedGroupedEvents > 0)
    {
        static uint32_t lastIgnoredSummaryMs = 0;
        static uint32_t pendingIgnoredLightEvents = 0;
        static uint32_t pendingIgnoredGroupedEvents = 0;
        static String pendingIgnoredLightSampleId;
        static String pendingIgnoredGroupedSampleId;

        pendingIgnoredLightEvents += ignoredUnmappedLightEvents;
        pendingIgnoredGroupedEvents += ignoredUnmappedGroupedEvents;
        if (pendingIgnoredLightSampleId.length() == 0 && ignoredLightSampleId.length() > 0)
        {
            pendingIgnoredLightSampleId = ignoredLightSampleId;
        }
        if (pendingIgnoredGroupedSampleId.length() == 0 && ignoredGroupedSampleId.length() > 0)
        {
            pendingIgnoredGroupedSampleId = ignoredGroupedSampleId;
        }

        uint32_t nowMs = millis();
        if ((nowMs - lastIgnoredSummaryMs) >= 30000 || nowMs < lastIgnoredSummaryMs)
        {
            Serial.printf("[HueGatewayModule] EventStream ignored unmapped updates: light=%lu grouped=%lu sample(light=%s grouped=%s)\n",
                          static_cast<unsigned long>(pendingIgnoredLightEvents),
                          static_cast<unsigned long>(pendingIgnoredGroupedEvents),
                          pendingIgnoredLightSampleId.length() > 0 ? pendingIgnoredLightSampleId.c_str() : "-",
                          pendingIgnoredGroupedSampleId.length() > 0 ? pendingIgnoredGroupedSampleId.c_str() : "-");

            pendingIgnoredLightEvents = 0;
            pendingIgnoredGroupedEvents = 0;
            pendingIgnoredLightSampleId = "";
            pendingIgnoredGroupedSampleId = "";
            lastIgnoredSummaryMs = nowMs;
        }
    }
}

void HueGatewayModule::applyEventStreamDeviceUpdates(const HueGatewayEventSensorUpdate* updates, int updateCount)
{
    if (updates == nullptr || updateCount <= 0) return;

    for (int u = 0; u < updateCount; u++)
    {
        const HueGatewayEventSensorUpdate& upd = updates[u];
        bool matched = false;
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            if (_devices[i] == nullptr) continue;
            if (!_devices[i]->matchesEventRid(upd.resourceId)) continue;
            matched = true;

            uint8_t _channelIndex = static_cast<uint8_t>(i);
            // SyncDir check only applies to lights (devType 0/4). Sensors, buttons and
            // contacts are strictly Hue→KNX input devices and must always process events.
            const uint8_t devTypeForSync = _devices[i]->deviceType();
            if (devTypeForSync == 0 || devTypeForSync == 4)
            {
                #ifdef ParamHUE_CHSyncDir
                uint8_t syncDir = ParamHUE_CHSyncDir;
                if (!(syncDir == 1 || syncDir == 2)) break;
                #endif
            }

            using Type = HueGatewayEventSensorUpdate::Type;
            switch (upd.type)
            {
                case Type::Motion:
                    if (_devices[i]->deviceType() == 1)
                    {
                        static_cast<HueGatewaySensor*>(_devices[i])->updateMotionOnly(upd.motionDetected);
                        Serial.printf("[HueGatewayModule] EventStream Motion -> ch%d motion=%d\n",
                                      i + 1, upd.motionDetected ? 1 : 0);
                    }
                    break;
                case Type::Contact:
                    if (_devices[i]->deviceType() == 3)
                    {
                        static_cast<HueGatewayContact*>(_devices[i])->updateContactOnly(upd.contactOpen);
                        Serial.printf("[HueGatewayModule] EventStream Contact -> ch%d open=%d\n",
                                      i + 1, upd.contactOpen ? 1 : 0);
                    }
                    break;
                case Type::Button:
                    if (_devices[i]->deviceType() == 2)
                    {
                        auto* btn = static_cast<HueGatewayButton*>(_devices[i]);
                        // Resolve button index from service RID (0-based) since SSE events
                        // do not reliably include metadata.control_id
                        bool isRotary = false;
                        int resolvedIdx = btn->matchServiceRid(upd.resourceId, isRotary);
                        // Convert to 1-based for handleButtonEvent (which subtracts 1 internally)
                        int effectiveCtrl = (resolvedIdx >= 0) ? (resolvedIdx + 1) : upd.buttonIndex;
                        btn->handleButtonEvent(effectiveCtrl, upd.buttonEventType);
                        Serial.printf("[HueGatewayModule] EventStream Button -> ch%d ctrl=%d (resolved=%d, raw=%d) event=%s\n",
                                      i + 1, effectiveCtrl, resolvedIdx, upd.buttonIndex, upd.buttonEventType.c_str());
                        appendDiagnosticLog("INFO", "BTNEVT", String("ch") + String(i + 1)
                            + " ctrl=" + String(effectiveCtrl)
                            + " evt=" + upd.buttonEventType);
                    }
                    break;
                case Type::Rotary:
                    if (_devices[i]->deviceType() == 2)
                    {
                        static_cast<HueGatewayButton*>(_devices[i])->handleRotaryEvent(
                            upd.rotaryClockwise, upd.rotarySteps);
                        Serial.printf("[HueGatewayModule] EventStream Rotary -> ch%d %s steps=%d\n",
                                      i + 1, upd.rotaryClockwise ? "CW" : "CCW", upd.rotarySteps);
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        if (!matched)
        {
            const char* typeName = "?";
            using Type = HueGatewayEventSensorUpdate::Type;
            switch (upd.type) {
                case Type::Button:  typeName = "Button"; break;
                case Type::Motion:  typeName = "Motion"; break;
                case Type::Contact: typeName = "Contact"; break;
                case Type::Rotary:  typeName = "Rotary"; break;
                default: break;
            }
            Serial.printf("[HueGatewayModule] EventStream %s RID=%s NOT matched to any channel\n",
                          typeName, upd.resourceId.c_str());
            appendDiagnosticLog("WARN", "BTNEVT", String(typeName)
                + " rid=" + upd.resourceId.substring(0, 8) + "... UNMATCHED");
        }
    }
}

bool HueGatewayModule::startPairing()
{
    Serial.println("[HueGatewayModule] ===== Manual commissioning trigger: Pairing start =====");

    if (!hasNetworkConnectivity())
    {
    const IPAddress wifiIp = WiFi.localIP();
    const IPAddress ethIp = ETH.localIP();
    Serial.printf("[HueGatewayModule] Network details: wifi=%s (status=%d), eth=%s (link=%u)\n",
              wifiIp.toString().c_str(),
              static_cast<int>(WiFi.status()),
              ethIp.toString().c_str(),
              ETH.linkUp() ? 1 : 0);
#if defined(HUEGATEWAY_HAS_OPENKNX_NETWORK) && defined(NET_ModuleVersion)
    Serial.printf("[HueGatewayModule] Network module: connected=%u, established=%u, ip=%s\n",
              openknxNetwork.connected() ? 1 : 0,
              openknxNetwork.established() ? 1 : 0,
              openknxNetwork.localIP().toString().c_str());
#endif
        Serial.println("[HueGatewayModule] Pairing aborted: network disconnected");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
    return false;
    }

    if (_bridgeIP.isEmpty())
    {
        _bridgeIP = getBridgeIP();
    }

    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return false;
    }

    if (_webScanInProgress || _webScanRequested || _lastWebScanText.length() > 2048 || _lastWebScanHtml.length() > 4096)
    {
        Serial.printf("[HueGatewayModule] Releasing web scan cache before pairing (text=%u, html=%u)\n",
                      static_cast<unsigned>(_lastWebScanText.length()),
                      static_cast<unsigned>(_lastWebScanHtml.length()));
    }
    _webScanRequested = false;
    _webScanInProgress = false;
    _lastWebScanError = "";
    _lastWebScanHtml = "";
    _lastWebScanText = "";

    resetDevices();
    _auth.clearAppKey();
    _manualPairingRequired = false;
    _authPending = true;
    _authStartTime = millis();
    _authLastTry = 0;
    _lastReconnectTryMs = 0;
    _reconnectBackoffMs = 10000;
    updateStatus(BridgeStatus::WAIT_FOR_BUTTON);
    Serial.println("[HueGatewayModule] Pairing started - press Hue Bridge button");
    return true;
}

void HueGatewayModule::updateInfoLED()
{
    static BridgeStatus lastStatus = BridgeStatus::DISCONNECTED;
    if (lastStatus == _bridgeStatus)
    {
        return;
    }

    lastStatus = _bridgeStatus;

    auto* led = openknx.leds.getLed(OpenKNX::Led::LedType::LED_TYPE_INFO1);
    if (!led)
    {
        return;
    }

    auto applyPattern = [led](OpenKNX::Led::Color color, uint16_t blinkMs, bool steadyOn) {
        if (led->isColor())
        {
            static_cast<OpenKNX::Led::RGB*>(led)->color(color);
        }

        if (steadyOn)
        {
            led->on(true);
            return;
        }

        if (blinkMs > 0)
        {
            led->blinking(blinkMs);
            return;
        }

        led->off();
    };

    switch (_bridgeStatus)
    {
        case BridgeStatus::DISCONNECTED:
            applyPattern(OpenKNX::Led::Color::Red, 1000, false);
            break;
        case BridgeStatus::CONNECTING:
            applyPattern(OpenKNX::Led::Color::Blue, 200, false);
            break;
        case BridgeStatus::WAIT_FOR_BUTTON:
            applyPattern(OpenKNX::Led::Color::Blue, 1000, false);
            break;
        case BridgeStatus::AUTHENTICATING:
            applyPattern(OpenKNX::Led::Color::Cyan, 200, false);
            break;
        case BridgeStatus::CONNECTED:
            applyPattern(OpenKNX::Led::Color::Green, 0, true);
            break;
        case BridgeStatus::CONNECTION_LOST:
            applyPattern(OpenKNX::Led::Color::Red, 500, false);
            break;
        case BridgeStatus::BRIDGE_UNREACHABLE:
            applyPattern(OpenKNX::Led::Color::Red, 1500, false);
            break;
        case BridgeStatus::ERROR:
            applyPattern(OpenKNX::Led::Color::Red, 100, false);
            break;
    }
}

// ============================================
// WebUI Implementation
// ============================================

static esp_err_t send_html(httpd_req_t* req, const String& html, int status)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "500 Internal Server Error");
    return httpd_resp_send(req, html.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_text(httpd_req_t* req, const String& text, int status)
{
    httpd_resp_set_type(req, "text/plain; charset=UTF-8");
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "500 Internal Server Error");
    return httpd_resp_send(req, text.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t begin_chunked_html(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_set_status(req, "200 OK");
    return ESP_OK;
}

static esp_err_t send_html_chunk(httpd_req_t* req, const String& chunk)
{
    return httpd_resp_send_chunk(req, chunk.c_str(), chunk.length());
}

static esp_err_t end_html_chunks(httpd_req_t* req)
{
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static String escape_html(const String& input)
{
    String out;
    out.reserve(input.length() + 32);
    for (size_t i = 0; i < input.length(); i++)
    {
        char ch = input[i];
        switch (ch)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static String maskKey(const String& key)
{
    if (key.length() == 0)
        return "(none)";
    if (key.length() <= 6)
        return String("***") + key;
    return String("***") + key.substring(key.length() - 6);
}

static size_t parseDiagnosticDepth(const String& value)
{
    long parsed = value.toInt();
    if (parsed <= 0)
    {
        return kDiagDefaultDepth;
    }

    if (parsed > static_cast<long>(kDiagLogCapacity))
    {
        return kDiagLogCapacity;
    }

    return static_cast<size_t>(parsed);
}

static String urlEncode(const String& input)
{
    const char* hex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(input.length() * 3);

    for (size_t i = 0; i < input.length(); i++)
    {
        const uint8_t c = static_cast<uint8_t>(input[i]);
        const bool unreserved = (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~';

        if (unreserved)
        {
            encoded += static_cast<char>(c);
        }
        else if (c == ' ')
        {
            encoded += '+';
        }
        else
        {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

static void readDiagnoseQuery(httpd_req_t* req, size_t& depth, String& note, bool& viewMode, bool& includeNetworkDetails)
{
    depth = kDiagDefaultDepth;
    note = "";
    viewMode = false;
    includeNetworkDetails = false;

    if (req == nullptr)
    {
        return;
    }

    char query[640] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        return;
    }

    char depthValue[16] = {0};
    if (httpd_query_key_value(query, "depth", depthValue, sizeof(depthValue)) == ESP_OK)
    {
        depth = parseDiagnosticDepth(String(depthValue));
    }

    char noteValue[384] = {0};
    if (httpd_query_key_value(query, "note", noteValue, sizeof(noteValue)) == ESP_OK)
    {
        note = String(noteValue);
    }

    char viewValue[8] = {0};
    if (httpd_query_key_value(query, "view", viewValue, sizeof(viewValue)) == ESP_OK)
    {
        viewMode = (strcmp(viewValue, "1") == 0);
    }

    char netValue[8] = {0};
    if (httpd_query_key_value(query, "net", netValue, sizeof(netValue)) == ESP_OK)
    {
        includeNetworkDetails = (strcmp(netValue, "1") == 0);
    }
}

void HueGatewayModule::setupWebUI()
{
    WebHandler scanTextHandler;
    scanTextHandler.name = "Hue Geräte (Text)";
    scanTextHandler.uri = "/hue/scan.txt";
    scanTextHandler.isVisible = false;
    scanTextHandler.httpd = {
        .uri = "/hue/scan.txt",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebScanText,
        .user_ctx = this
    };
    openknxWebUI.addHandler(scanTextHandler);

    WebHandler diagnoseTextHandler;
    diagnoseTextHandler.name = "Hue Diagnose (Text)";
    diagnoseTextHandler.uri = "/hue/diagnose.txt";
    diagnoseTextHandler.isVisible = false;
    diagnoseTextHandler.httpd = {
        .uri = "/hue/diagnose.txt",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebDiagnoseText,
        .user_ctx = this
    };
    openknxWebUI.addHandler(diagnoseTextHandler);

    WebPage scanPage;
    scanPage.uri = "/hue/scan";
    scanPage.name = "Hue Geräte";
    scanPage.handler = HueGatewayModule::pageWebScan;
    scanPage.arg = this;
    openknxWebUI.addPage(scanPage);

    WebPage statusPage;
    statusPage.uri = "/hue/status";
    statusPage.name = "Hue-Status";
    statusPage.handler = HueGatewayModule::pageWebStatus;
    statusPage.arg = this;
    openknxWebUI.addPage(statusPage);

    WebPage pairPage;
    pairPage.uri = "/hue/pair";
    pairPage.name = "Hue-Kopplung";
    pairPage.handler = HueGatewayModule::pageWebPair;
    pairPage.arg = this;
    openknxWebUI.addPage(pairPage);

    WebPage resetAuthPage;
    resetAuthPage.uri = "/hue/reset-auth";
    resetAuthPage.name = "Hue-Auth zurücksetzen";
    resetAuthPage.handler = HueGatewayModule::pageWebResetAuth;
    resetAuthPage.arg = this;
    openknxWebUI.addPage(resetAuthPage);

    WebPage diagnosePage;
    diagnosePage.uri = "/hue/diagnose";
    diagnosePage.name = "Hue-Diagnose";
    diagnosePage.handler = HueGatewayModule::pageWebDiagnose;
    diagnosePage.arg = this;
    openknxWebUI.addPage(diagnosePage);

    WebPage retrySetupPage;
    retrySetupPage.uri = "/hue/retry-setup";
    retrySetupPage.name = "Setup-Retry";
    retrySetupPage.handler = HueGatewayModule::pageWebRetrySetup;
    retrySetupPage.arg = this;
    openknxWebUI.addPage(retrySetupPage);

    WebPage rootPage;
    rootPage.uri = "/hue";
    rootPage.name = "Hue Gateway";
    rootPage.handler = HueGatewayModule::pageWebRoot;
    rootPage.arg = this;
    openknxWebUI.addPage(rootPage);

    Serial.printf("[HueGatewayModule] WebUI pages registered at %s\n", openknxWebUI.getBaseUri());
}

esp_err_t HueGatewayModule::handleWebRoot(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(1536);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Open KNX Hue Gateway</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "h1{color:#333;}.card{background:white;padding:20px;margin:10px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
    html += "a{display:inline-block;padding:10px 20px;margin:5px;background:#007bff;color:white;text-decoration:none;border-radius:3px;}";
    html += "a:hover{background:#0056b3;}</style></head><body>";
    html += "<h1>🏠 Open KNX Hue Gateway</h1>";
    html += "<div class='card'><h2>Funktionen</h2>";
    html += "<a href='" + hueBaseUri + "/pair'>🔗 Pairing starten</a>";
    html += "<a href='" + hueBaseUri + "/reset-auth'>🗑️ Auth zurücksetzen</a>";
    html += "<a href='" + hueBaseUri + "/scan'>🔍 Hue-Geräte laden</a>";
    html += "<a href='" + hueBaseUri + "/status'>📊 Status</a>";
    html += "<a href='" + hueBaseUri + "/diagnose'>🧰 Diagnose</a>";
    html += "<a href='" + hueBaseUri + "/retry-setup'>🔄 Setup-Retry</a>";
    html += "</div>";
    html += "<div class='card'><h3>Info</h3>";
    html += "<p><strong>Geräte-IP:</strong> " + getRequestLocalIpString(req) + "</p>";
    html += "<p><strong>Modulversion:</strong> " + String(self->version().c_str()) + "</p>";
    if (self->_bridgeStatus == BridgeStatus::WAIT_FOR_BUTTON)
    {
        html += "<p><strong>Auth:</strong> Warte auf Tastendruck an der Hue Bridge</p>";
    }
    else if (self->_bridgeStatus == BridgeStatus::CONNECTED)
    {
        html += "<p><strong>Auth:</strong> Verbunden</p>";
    }
    else
    {
        html += "<p><strong>Auth:</strong> Nicht verbunden</p>";
    }
    html += "<p><strong>App-Key:</strong> " + maskKey(self->_auth.getAppKey()) + "</p>";
    html += "<p><strong>Client-Key:</strong> " + maskKey(self->_auth.getClientKey()) + "</p>";
    html += "</div></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebScan(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    if (!self->_client || !self->_initialized || !self->_client->isInitialized())
    {
        const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

        String html;
        html.reserve(384);
        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte laden</title></head><body>";
        html += "<h1>ℹ️ Hue-Geräte laden</h1><p>Noch keine aktive Bridge-Verbindung. Bitte zuerst Pairing starten und den Hue-Bridge-Button drücken.</p>";
        html += "<a href='" + hueBaseUri + "/pair'>🔗 Pairing starten</a> ";
        html += "<a href='" + hueBaseUri + "'>← Zurück</a></body></html>";
        return send_html(req, html, 200);
    }

    if (self->_webScanInProgress)
    {
        const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";
        const unsigned long elapsedMs = (self->_webScanStartedMs == 0) ? 0 : (millis() - self->_webScanStartedMs);

        if (elapsedMs > kWebScanTimeoutMs)
        {
            self->_webScanInProgress = false;
            self->_webScanRequested = false;
            self->_diagCounterWebScanTimeouts++;
            self->_lastWebScanDurationMs = elapsedMs;
            self->_lastWebScanError = "Web-Scan Timeout nach " + String(elapsedMs / 1000UL) + " Sekunden";
            self->appendDiagnosticLog("WARN", "WEBSCAN", String("timeout after ") + String(elapsedMs) + " ms");

            String html;
            html.reserve(1024);
            html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte laden</title></head><body>";
            html += "<h1>⚠️ Hue-Scan dauert zu lange</h1>";
            html += "<p>Der Scan läuft seit " + String(elapsedMs / 1000UL) + " Sekunden. Das überschreitet den Timeout.</p>";
            html += "<p>Bitte Tester-Feedback inkl. Seriellog senden: JSON parse error, Content-Length, HTTP GET failed.</p>";
            html += "<a href='" + hueBaseUri + "/scan'><button>Erneut versuchen</button></a> ";
            html += "<a href='" + hueBaseUri + "/scan.txt'><button>TXT Status</button></a> ";
            html += "<a href='" + hueBaseUri + "'><button>← Zurück</button></a></body></html>";
            self->_lastWebScanHtml = html;
            self->_lastWebScanText = "Scan-Timeout nach " + String(elapsedMs / 1000UL) + " Sekunden. Bitte Seriellog mit JSON parse error / Content-Length / HTTP GET failed senden.\n";
            self->_lastWebScanLightCount = 0;
            return send_html(req, html, 200);
        }

        String html;
        html.reserve(384);
        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='2'><title>Hue-Geräte laden</title></head><body>";
        html += "<h1>🔄 Hue-Geräte laden</h1><p>Scan läuft... Seite aktualisiert sich automatisch.</p>";
        if (elapsedMs > 0)
        {
            html += "<p>Laufzeit: " + String(elapsedMs / 1000UL) + " Sekunden</p>";
        }
        html += "<a href='" + hueBaseUri + "'>← Zurück</a></body></html>";
        return send_html(req, html, 200);
    }

    const bool noScanYet = (self->_lastWebScanMs == 0);
    const bool stale = (!noScanYet) && ((millis() - self->_lastWebScanMs) > kWebScanCacheStaleMs);
    if (noScanYet || stale)
    {
        self->_webScanRequested = true;
        const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";
        String html;
        html.reserve(480);
        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='2'><title>Hue-Geräte laden</title></head><body>";
        html += "<h1>🔄 Hue-Geräte laden</h1><p>Scan wurde gestartet... Seite aktualisiert sich automatisch.</p>";
        html += "<a href='" + hueBaseUri + "'>← Zurück</a></body></html>";
        return send_html(req, html, 200);
    }

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    if (begin_chunked_html(req) != ESP_OK)
    {
        return httpd_resp_send_500(req);
    }

    String chunk;
    chunk.reserve(768);
    chunk = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    chunk += "<title>Hue-Geräte von Bridge</title>";
    chunk += "<style>body{font-family:'Courier New',monospace;margin:40px;background:#1e1e1e;color:#d4d4d4;}";
    chunk += "h1{color:#4ec9b0;}";
    chunk += ".light{background:#252526;padding:15px;margin:10px 0;border-left:4px solid #007acc;border-radius:3px;}";
    chunk += ".light-id{color:#ce9178;font-size:0.9em;word-break:break-all;}";
    chunk += ".status{color:#b5cea8;}";
    chunk += ".error{color:#f48771;background:#3c1f1e;padding:15px;border-left:4px solid #f48771;}";
    chunk += "button{padding:8px 15px;margin:5px;background:#007acc;color:white;border:none;border-radius:3px;cursor:pointer;}";
    chunk += "button:hover{background:#005a9e;}</style></head><body>";
    chunk += "<h1>🔍 Hue-Geräte von Bridge</h1>";

    if (send_html_chunk(req, chunk) != ESP_OK)
    {
        return httpd_resp_send_500(req);
    }

    chunk = "";
    chunk.reserve(512);
    if (self->_lastWebScanLightCount <= 0)
    {
        chunk += "<div class='error'>❌ Keine Leuchten gefunden! Bitte Bridge-Verbindung prüfen.</div>";
        if (self->_lastWebScanError.length() > 0)
        {
            chunk += "<div class='error'>Diagnose: " + self->_lastWebScanError + "</div>";
        }
    }
    else
    {
        {
            String summary = "Es wurden <strong>" + String(self->_lastWebScanLightCount) + "</strong> Leuchte" + String(self->_lastWebScanLightCount == 1 ? "" : "n");
            if (self->_lastWebScanAccessoryCount > 0)
            {
                summary += " und <strong>" + String(self->_lastWebScanAccessoryCount) + "</strong> Schalter/Sensor" + String(self->_lastWebScanAccessoryCount == 1 ? "" : "en");
            }
            summary += " gefunden:";
            chunk += "<p>" + summary + "</p>";
        }
        if (self->_lastWebScanDurationMs > 0)
        {
            chunk += "<p>Scan-Dauer: " + String(self->_lastWebScanDurationMs / 1000UL) + " Sekunden</p>";
        }
        chunk += "<p><strong>Details:</strong></p><pre style='white-space:pre-wrap;background:#252526;padding:12px;border-radius:3px;'>";
        chunk += escape_html(self->_lastWebScanText);
        chunk += "</pre>";
    }

    if (send_html_chunk(req, chunk) != ESP_OK)
    {
        return httpd_resp_send_500(req);
    }

    chunk = "<br><p><strong>Tipp:</strong> Die Leuchten-ID kopieren und in die ETS-Kanalparameter einfügen.</p>";
    chunk += "<button onclick='location.reload()'>Aktualisieren</button>";
    chunk += "<a href='" + hueBaseUri + "/scan.txt'><button>TXT herunterladen</button></a>";
    chunk += "<a href='" + hueBaseUri + "'><button>Zurück</button></a>";
    chunk += "</body></html>";

    if (send_html_chunk(req, chunk) != ESP_OK)
    {
        return httpd_resp_send_500(req);
    }

    return end_html_chunks(req);
}

esp_err_t HueGatewayModule::handleWebScanText(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    if (!self->_client || !self->_initialized || !self->_client->isInitialized())
        return send_text(req, "Keine aktive Bridge-Verbindung. Bitte zuerst Pairing starten und den Hue-Bridge-Button druecken.\n", 200);

    if (self->_webScanInProgress)
    {
        const unsigned long elapsedMs = (self->_webScanStartedMs == 0) ? 0 : (millis() - self->_webScanStartedMs);
        if (elapsedMs > kWebScanTimeoutMs)
        {
            self->_webScanInProgress = false;
            self->_webScanRequested = false;
            self->_lastWebScanDurationMs = elapsedMs;
            self->_lastWebScanError = "Web-Scan Timeout nach " + String(elapsedMs / 1000UL) + " Sekunden";
            self->_lastWebScanText = "Scan-Timeout nach " + String(elapsedMs / 1000UL) + " Sekunden. Bitte Seriellog mit JSON parse error / Content-Length / HTTP GET failed senden.\n";
            self->_lastWebScanLightCount = 0;
            return send_text(req,
                             "Scan-Timeout: Der Hue-Scan dauert zu lange. Bitte Seriellog mit JSON parse error / Content-Length / HTTP GET failed senden und Scan erneut starten.\n",
                             200);
        }

        String text = "Scan läuft, bitte in 2-3 Sekunden erneut abrufen.";
        if (elapsedMs > 0)
        {
            text += " Laufzeit=" + String(elapsedMs / 1000UL) + "s.";
        }
        text += "\n";
        return send_text(req, text, 200);
    }

    const bool noScanYet = (self->_lastWebScanMs == 0);
    const bool stale = (!noScanYet) && ((millis() - self->_lastWebScanMs) > kWebScanCacheStaleMs);
    if (noScanYet || stale)
    {
        self->_webScanRequested = true;
        return send_text(req, "Scan gestartet, bitte in 2-3 Sekunden erneut abrufen.\n", 200);
    }

    String text = self->getBridgeScanText();
    return send_text(req, text, 200);
}

esp_err_t HueGatewayModule::handleWebStatus(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(8192);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Status</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "table{width:100%;border-collapse:collapse;background:white;border-radius:5px;overflow:hidden;}";
    html += "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "th{background:#007bff;color:white;}</style></head><body>";
    html += "<h1>📊 Modulstatus</h1>";
    html += "<table><tr><th>Parameter</th><th>Wert</th></tr>";
    html += "<tr><td>Initialisiert</td><td>" + String(self->_initialized ? "✅ Ja" : "❌ Nein") + "</td></tr>";
    html += "<tr><td>Geräte-IP</td><td>" + getRequestLocalIpString(req) + "</td></tr>";
    html += "<tr><td>Aktive Leuchten</td><td>" + String(self->_lightCount) + " / " + String(MAX_LIGHTS) + "</td></tr>";
    html += "<tr><td>WiFi RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    String bridgeStatusText = "Unbekannt";
    switch (self->_bridgeStatus)
    {
        case BridgeStatus::DISCONNECTED: bridgeStatusText = "Getrennt"; break;
        case BridgeStatus::CONNECTING: bridgeStatusText = "Verbinde"; break;
        case BridgeStatus::WAIT_FOR_BUTTON: bridgeStatusText = "Warte auf Taster"; break;
        case BridgeStatus::AUTHENTICATING: bridgeStatusText = "Authentifiziere"; break;
        case BridgeStatus::CONNECTED: bridgeStatusText = "Verbunden"; break;
        case BridgeStatus::CONNECTION_LOST: bridgeStatusText = "Verbindung verloren"; break;
        case BridgeStatus::BRIDGE_UNREACHABLE: bridgeStatusText = "Bridge nicht erreichbar"; break;
        case BridgeStatus::ERROR: bridgeStatusText = "Fehler"; break;
    }
    html += "<tr><td>Bridge-Status</td><td>" + bridgeStatusText + "</td></tr>";
    html += "<tr><td>App-Key</td><td>" + maskKey(self->_auth.getAppKey()) + "</td></tr>";
    html += "<tr><td>Client-Key</td><td>" + maskKey(self->_auth.getClientKey()) + "</td></tr>";

    #ifdef ParamHUE_HUEHCLEnable
    const bool hclEnabled = (ParamHUE_HUEHCLEnable != 0);
    html += "<tr><td>HCL aktiviert</td><td>" + String(hclEnabled ? "Ja" : "Nein") + "</td></tr>";
    #else
    const bool hclEnabled = false;
    html += "<tr><td>HCL aktiviert</td><td>Nein</td></tr>";
    #endif

    if (hclEnabled)
    {
        struct tm statusTimeinfo;
        memset(&statusTimeinfo, 0, sizeof(statusTimeinfo));
        const bool hasStatusTime = getLocalTime(&statusTimeinfo, 0);
        const uint16_t statusCurrentMinutes = hasStatusTime
            ? static_cast<uint16_t>(statusTimeinfo.tm_hour * 60 + statusTimeinfo.tm_min)
            : 0;
        html += "<tr><td>HCL-Zeitbasis</td><td>" + String(hasStatusTime ? formatMinutesToClock(statusCurrentMinutes) : "n/a") + "</td></tr>";

        #ifdef ParamHUE_HUEHCLMasterCount
        const uint8_t masterCount = ParamHUE_HUEHCLMasterCount;
        html += "<tr><td>HCL-Master</td><td>" + String(masterCount) + "</td></tr>";
        #else
        const uint8_t masterCount = 0;
        html += "<tr><td>HCL-Master</td><td>0</td></tr>";
        #endif

        #ifdef ParamHUE_HUEHCLUpdateInterval
        html += "<tr><td>HCL-Aktualisierungsintervall</td><td>" + String(ParamHUE_HUEHCLUpdateInterval) + " s</td></tr>";
        #endif

        #ifdef ParamHUE_HUEHCLFadeDuration
        html += "<tr><td>HCL-Überblenddauer</td><td>" + String(ParamHUE_HUEHCLFadeDuration) + " s</td></tr>";
        #endif

        for (uint8_t masterNumber = 1; masterNumber <= 4; masterNumber++)
        {
            if (masterNumber > masterCount)
            {
                break;
            }

            HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
            if (!master)
            {
                continue;
            }

            HCL::InterpolatedValue current = HCL::masterManager.getCurrentValue(masterNumber);
                html += "<tr><td>HCL M" + String(masterNumber) + " Aktuell</td><td>" +
                    String(current.kelvin) + " K / " + String(current.brightness) + "%</td></tr>";

            uint8_t curveTypeValue = 0;
            uint16_t slewRate = 0;
            uint16_t manualKelvin = 4000;
            String sunrise = "";
            String sunset = "";
            int16_t sunriseOffset = 0;
            int16_t sunsetOffset = 0;

            switch (masterNumber)
            {
                case 1:
                    #ifdef ParamHUE_HCLM1CurveType
                    curveTypeValue = ParamHUE_HCLM1CurveType;
                    slewRate = ParamHUE_HCLM1SlewRate;
                    manualKelvin = ParamHUE_HCLM1ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM1Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM1Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                    #endif
                    break;
                case 2:
                    #ifdef ParamHUE_HCLM2CurveType
                    curveTypeValue = ParamHUE_HCLM2CurveType;
                    slewRate = ParamHUE_HCLM2SlewRate;
                    manualKelvin = ParamHUE_HCLM2ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM2Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM2Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                    #endif
                    break;
                case 3:
                    #ifdef ParamHUE_HCLM3CurveType
                    curveTypeValue = ParamHUE_HCLM3CurveType;
                    slewRate = ParamHUE_HCLM3SlewRate;
                    manualKelvin = ParamHUE_HCLM3ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM3Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM3Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                    #endif
                    break;
                case 4:
                    #ifdef ParamHUE_HCLM4CurveType
                    curveTypeValue = ParamHUE_HCLM4CurveType;
                    slewRate = ParamHUE_HCLM4SlewRate;
                    manualKelvin = ParamHUE_HCLM4ManualKelvin;
                    sunrise = readFixedTimeParam(ParamHUE_HCLM4Sunrise);
                    sunset = readFixedTimeParam(ParamHUE_HCLM4Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                    #endif
                    break;
                default:
                    break;
            }

            String curveText = "Stützpunkte";
            if (curveTypeValue == 1)
            {
                curveText = "Sonnenfenster";
            }
            else if (curveTypeValue == 2)
            {
                curveText = "Manuell";
            }
            else if (curveTypeValue == 3)
            {
                curveText = "Astronomisch";
            }

            html += "<tr><td>HCL M" + String(masterNumber) + " Kurve</td><td>" + curveText + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Steigrate</td><td>" + String(slewRate) + " K/min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Manuell</td><td>" + String(manualKelvin) + " K</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Sonne</td><td>" + sunrise + " / " + sunset + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Offset</td><td>" + String(sunriseOffset) + " / " + String(sunsetOffset) + " min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Setpoints gültig</td><td>" + String(master->getValidSetpointCount()) + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Setpoint-Liste</td><td>" + buildSetpointListDebug(master) + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Angewendet</td><td>" + String(master->getAppliedKelvin()) + " K</td></tr>";

            if (hasStatusTime)
            {
                HclFixedInterpolationDebug interpolationDebug;
                if (buildFixedInterpolationDebug(master, statusCurrentMinutes, interpolationDebug) && interpolationDebug.valid)
                {
                    html += "<tr><td>HCL M" + String(masterNumber) + " Interpolation</td><td>" +
                        formatMinutesToClock(interpolationDebug.prevTime) + " (" + String(interpolationDebug.prevKelvin) + " K / " + String(interpolationDebug.prevBrightness) + "%) → " +
                        formatMinutesToClock(interpolationDebug.nextTime) + " (" + String(interpolationDebug.nextKelvin) + " K / " + String(interpolationDebug.nextBrightness) + "%)" +
                        (interpolationDebug.wrapsMidnight ? " [Wrap]" : "") +
                        "</td></tr>";

                    html += "<tr><td>HCL M" + String(masterNumber) + " Interpolation Faktor</td><td>" +
                        String(interpolationDebug.elapsedMinutes) + " / " + String(interpolationDebug.spanMinutes) + " min (" +
                        String(interpolationDebug.factor * 100.0f, 1) + "%) → Ziel " +
                        String(interpolationDebug.targetKelvin) + " K / " + String(interpolationDebug.targetBrightness) + "%</td></tr>";
                }
                else
                {
                    html += "<tr><td>HCL M" + String(masterNumber) + " Interpolation</td><td>n/a (zu wenig gültige Setpoints)</td></tr>";
                }
            }

        }

        // HCL lock status — global
        html += "<tr><th colspan='2'>HCL-Sperre</th></tr>";
        html += "<tr><td>Globale Sperre</td><td>"
            + String(self->_hclLockActive ? "🔒 Aktiv" : "🔓 Inaktiv")
            + " | Richtlinie: " + String(HueGatewayModule::hclFallbackPolicyToText(static_cast<HclLockFallbackPolicy>(self->_hclFallbackPolicy)))
            + " | Modus: " + String(HueGatewayModule::hclFallbackModeToText(static_cast<HclLockFallbackMode>(self->_hclLockFallbackMode)));
        if (self->_hclLockActive && self->_hclLockAutoReleaseMs > 0)
        {
            const unsigned long nowMs = millis();
            const long remainMs = static_cast<long>(self->_hclLockAutoReleaseMs) - static_cast<long>(nowMs);
            html += " | Auto-Freigabe in: " + String(remainMs > 0 ? remainMs / 1000 : 0) + " s";
        }
        html += "</td></tr>";

        // HCL lock status — per manager
        for (uint8_t m = 0; m < HCL::MasterManager::MAX_MASTERS; m++)
        {
            if (!self->_hclManagerLockActive[m]) continue;
            const unsigned long nowMs = millis();
            String releaseInfo = "";
            if (self->_hclManagerLockAutoReleaseMs[m] > 0)
            {
                const long remainMs = static_cast<long>(self->_hclManagerLockAutoReleaseMs[m]) - static_cast<long>(nowMs);
                releaseInfo = " | Auto-Freigabe in: " + String(remainMs > 0 ? remainMs / 1000 : 0) + " s";
            }
            html += "<tr><td>Manager " + String(m + 1) + " Sperre</td><td>🔒 Aktiv"
                + " | " + String(HueGatewayModule::hclFallbackPolicyToText(static_cast<HclLockFallbackPolicy>(self->_hclManagerFallbackPolicy[m])))
                + releaseInfo + "</td></tr>";
        }

        // HCL lock status — per channel
        #ifdef ParamHUE_HUEChannelCount
        {
            const uint8_t configuredChannels = min(static_cast<uint8_t>(ParamHUE_HUEChannelCount), static_cast<uint8_t>(MAX_LIGHTS));
            bool anyChannelLock = false;
            for (uint8_t ch = 0; ch < configuredChannels; ch++)
            {
                if (self->_hclChannelLockActive[ch]) { anyChannelLock = true; break; }
            }
            if (anyChannelLock)
            {
                String lockedList = "";
                for (uint8_t ch = 0; ch < configuredChannels; ch++)
                {
                    if (!self->_hclChannelLockActive[ch]) continue;
                    if (lockedList.length() > 0) lockedList += ", ";
                    lockedList += "K" + String(ch + 1);
                    if (self->_devices[ch] != nullptr)
                        lockedList += " (" + self->_devices[ch]->getName() + ")";
                    if (self->_hclChannelLockAutoReleaseMs[ch] > 0)
                    {
                        const unsigned long nowMs = millis();
                        const long remainMs = static_cast<long>(self->_hclChannelLockAutoReleaseMs[ch]) - static_cast<long>(nowMs);
                        lockedList += " ≤" + String(remainMs > 0 ? remainMs / 1000 : 0) + "s";
                    }
                }
                html += "<tr><td>Kanal-Sperren aktiv</td><td>🔒 " + lockedList + "</td></tr>";
            }
            else
            {
                html += "<tr><td>Kanal-Sperren</td><td>🔓 Keine</td></tr>";
            }
        }
        #endif
    }

    html += "</table>";
    html += "<br><a href='" + hueBaseUri + "/pair'>🔗 Pairing starten</a> ";
    html += "<a href='" + hueBaseUri + "/reset-auth'>🗑️ Auth zurücksetzen</a> ";
    html += "<a href='" + hueBaseUri + "/diagnose'>🧰 Diagnose erstellen</a> ";
    html += "<a href='" + hueBaseUri + "/retry-setup'>🔄 Setup-Retry</a> ";
    html += "<a href='" + hueBaseUri + "'>← Zurück</a></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebPair(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    const bool pairingStarted = self->startPairing();

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(1024);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Kopplung</title>";
    if (pairingStarted)
    {
        html += "<meta http-equiv='refresh' content='3;url=" + hueBaseUri + "/status'>";
    }
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += ".card{background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}a{display:inline-block;padding:10px 16px;margin:5px;background:#007bff;color:#fff;text-decoration:none;border-radius:3px;}</style></head><body>";
    if (pairingStarted)
    {
        html += "<div class='card'><h1>🔗 Kopplung gestartet</h1>";
        html += "<p>Bitte jetzt den Link-Button an der Hue Bridge drücken.</p>";
        html += "<p>Weiterleitung auf Statusseite in 3 Sekunden...</p>";
    }
    else
    {
        html += "<div class='card'><h1>⚠️ Kopplung nicht gestartet</h1>";
        html += "<p>Netzwerk nicht bereit oder Bridge-IP fehlt.</p>";
        html += "<p>Bitte zuerst Netzwerk-/Bridge-Status prüfen und dann erneut starten.</p>";
    }
    html += "<a href='" + hueBaseUri + "/status'>Status jetzt öffnen</a>";
    html += "<a href='" + hueBaseUri + "'>Zurück</a></div></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebResetAuth(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    self->resetDevices();
    if (self->_client)
    {
        delete self->_client;
        self->_client = nullptr;
    }

    self->_auth.clearAppKey();
    self->_authPending = false;
    self->_manualPairingRequired = true;
    self->_authStartTime = 0;
    self->_authLastTry = 0;
    self->_lastReconnectTryMs = 0;
    self->updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
    self->appendDiagnosticLog("INFO", "AUTH", "auth reset via WebUI");
    Serial.println("[HueGatewayModule] Authentication reset via WebUI route");

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(1024);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Authentifizierung zurückgesetzt</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += ".card{background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}a{display:inline-block;padding:10px 16px;margin:5px;background:#007bff;color:#fff;text-decoration:none;border-radius:3px;}</style></head><body>";
    html += "<div class='card'><h1>🗑️ Auth-Daten verworfen</h1>";
    html += "<p>Gespeicherter App-Key und Client-Key wurden gelöscht.</p>";
    html += "<p>Für den weiteren Betrieb bitte jetzt Pairing neu starten.</p>";
    html += "<a href='" + hueBaseUri + "/pair'>🔗 Pairing starten</a>";
    html += "<a href='" + hueBaseUri + "/status'>📊 Status</a>";
    html += "<a href='" + hueBaseUri + "'>← Zurück</a></div></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebDiagnose(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    size_t depth = kDiagDefaultDepth;
    String note;
    bool viewMode = false;
    bool includeNetworkDetails = false;
    readDiagnoseQuery(req, depth, note, viewMode, includeNetworkDetails);

    String html;
    html.reserve(24000);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Diagnose</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += ".card{background:#fff;padding:16px;margin:10px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.08);}";
    html += "label{display:block;margin:8px 0 4px 0;}select,input,textarea{width:100%;padding:8px;box-sizing:border-box;}";
    html += "button,a.btn{display:inline-block;padding:10px 14px;margin:6px 6px 6px 0;background:#007bff;color:#fff;text-decoration:none;border:none;border-radius:3px;cursor:pointer;}";
    html += "pre{white-space:pre-wrap;background:#111;color:#ddd;padding:12px;border-radius:4px;max-height:420px;overflow:auto;}";
    html += "</style></head><body>";
    html += "<h1>🧰 Hue-Diagnose</h1>";

    html += "<div class='card'><form id='diagForm' method='GET' action='" + hueBaseUri + "/diagnose'>";
    html += "<input type='hidden' name='view' value='1'>";
    html += "<label>Logtiefe</label>";
    html += "<select name='depth'>";
    html += "<option value='120'" + String(depth == 120 ? " selected" : "") + ">120 (Vorschau)</option>";
    html += "<option value='150'" + String(depth == 150 ? " selected" : "") + ">150 (Maximum)</option>";
    html += "</select>";
    html += "<label>Tester-Notiz</label>";
    html += "<textarea name='note' rows='3' placeholder='Was wurde getestet? Welche Gruppenadresse? Erwartet/Passiert?'>" + escape_html(note) + "</textarea>";
    html += "<label><input type='checkbox' name='net' value='1' style='width:auto;margin-right:8px;'" + String(includeNetworkDetails ? " checked" : "") + ">Vollständige Netzwerkdetails (Device-/Bridge-IP) einfügen</label>";
    html += "<button type='submit'>Diagnose erstellen</button>";
    html += "</form></div>";

    String querySuffix = "?depth=" + String(static_cast<unsigned long>(depth));
    if (note.length() > 0)
    {
        querySuffix += "&note=" + urlEncode(note);
    }
    if (includeNetworkDetails)
    {
        querySuffix += "&net=1";
    }

    html += "<div class='card'>";
    html += "<a class='btn' id='diagDownload' href='" + hueBaseUri + "/diagnose.txt" + querySuffix + "' onclick='return downloadDiag(event)'>Diagnose herunterladen</a>";
    html += "<button type='button' onclick='copyDiag()'>Diagnose in Zwischenablage</button>";
    html += "<a class='btn' href='" + hueBaseUri + "'>Zurück</a>";
    html += "</div>";

    if (viewMode)
    {
        const size_t previewDepth = min<size_t>(depth, static_cast<size_t>(120));
        String report = self->buildDiagnosticReport(previewDepth, note, includeNetworkDetails);
        html += "<div class='card'><h3>Vorschau</h3><pre id='diagText'>" + escape_html(report) + "</pre></div>";
        if (depth > previewDepth)
        {
            html += "<div class='card'><p>Hinweis: Vorschau zeigt die letzten " + String(static_cast<unsigned long>(previewDepth)) + " Einträge. Vollständiger Export über \"Diagnose herunterladen\".</p></div>";
        }
    }
    else
    {
        html += "<div class='card'><p>Diagnose wird beim Klick auf \"Diagnose erstellen\" oder \"Diagnose herunterladen\" erzeugt.</p><pre id='diagText' style='display:none'></pre></div>";
    }

    html += "<script>";
    html += "function diagUrl(){const dl=document.getElementById('diagDownload');if(!dl||!dl.href){throw new Error('Download-Link fehlt');}const u=new URL(dl.href,window.location.href);u.searchParams.set('_ts',Date.now().toString());return u.toString();}";
    html += "function diagViewUrl(){const form=document.getElementById('diagForm');if(!form||!form.action){throw new Error('Diagnose-Formular fehlt');}const fd=new FormData(form);const u=new URL(form.action,window.location.href);u.searchParams.set('view','1');const depth=fd.get('depth');if(depth){u.searchParams.set('depth',depth.toString());}const note=fd.get('note');if(note){u.searchParams.set('note',note.toString());}if(fd.get('net')==='1'){u.searchParams.set('net','1');}u.searchParams.set('_ts',Date.now().toString());return u.toString();}";
    html += "function looksLikeHtml(text){if(!text){return false;}const head=text.slice(0,300).toLowerCase();return head.includes('<!doctype html')||head.includes('<html')||head.includes('<head')||head.includes('<body');}";
    html += "function extractDiagFromHtml(html){const parser=new DOMParser();const doc=parser.parseFromString(html,'text/html');const pre=doc.getElementById('diagText');if(!pre){return '';}const txt=pre.textContent||'';return txt.trim();}";
    html += "async function loadDiagText(){const r=await fetch(diagUrl(),{cache:'no-store'});if(!r.ok){throw new Error('HTTP '+r.status);}const primary=await r.text();if(!looksLikeHtml(primary)){return primary;}const inline=extractDiagFromHtml(primary);if(inline.length>0){return inline;}const rf=await fetch(diagViewUrl(),{cache:'no-store'});if(!rf.ok){throw new Error('Fallback HTTP '+rf.status);}const html=await rf.text();const extracted=extractDiagFromHtml(html);if(extracted.length===0){throw new Error('Diagnose-Text konnte nicht aus HTML extrahiert werden');}return extracted;}";
    html += "async function downloadDiag(ev){if(ev){ev.preventDefault();}try{const txt=await loadDiagText();const blob=new Blob([txt],{type:'text/plain;charset=utf-8'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download='hue-diagnose-'+Date.now()+'.txt';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1500);return false;}catch(e){alert('Download fehlgeschlagen: '+(e&&e.message?e.message:'unbekannter Fehler'));return false;}}";
    html += "async function copyDiag(){try{const t=await loadDiagText();const target=document.getElementById('diagText');if(target){target.textContent=t;}if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(t);alert('Diagnose in Zwischenablage kopiert');return;}const ta=document.createElement('textarea');ta.value=t;ta.setAttribute('readonly','readonly');ta.style.position='fixed';ta.style.left='-9999px';document.body.appendChild(ta);ta.focus();ta.select();const ok=document.execCommand('copy');ta.remove();if(!ok){throw new Error('Browser blockiert Kopieren');}alert('Diagnose in Zwischenablage kopiert');}catch(e){alert('Kopieren fehlgeschlagen: '+(e&&e.message?e.message:'unbekannter Fehler')+'\\nBitte Diagnose herunterladen verwenden.');}}";
    html += "</script>";

    html += "</body></html>";
    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebDiagnoseText(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    size_t depth = kDiagDefaultDepth;
    String note;
    bool viewMode = false;
    bool includeNetworkDetails = false;
    readDiagnoseQuery(req, depth, note, viewMode, includeNetworkDetails);

    self->appendDiagnosticLog("INFO", "DIAG", String("diagnose export depth=") + String(static_cast<unsigned long>(depth)) + String(" net=") + String(includeNetworkDetails ? "1" : "0"));

    const String report = self->buildDiagnosticReport(depth, note, includeNetworkDetails);
    String fileName = "hue-diagnose-" + String(millis()) + ".txt";

    httpd_resp_set_type(req, "text/plain; charset=UTF-8");
    String contentDisposition = "attachment; filename=\"" + fileName + "\"";
    httpd_resp_set_hdr(req, "Content-Disposition", contentDisposition.c_str());
    return httpd_resp_send(req, report.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t HueGatewayModule::pageWebRoot(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebRoot(req);
}

esp_err_t HueGatewayModule::pageWebScan(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebScan(req);
}

esp_err_t HueGatewayModule::pageWebStatus(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebStatus(req);
}

esp_err_t HueGatewayModule::pageWebPair(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebPair(req);
}

esp_err_t HueGatewayModule::pageWebResetAuth(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebResetAuth(req);
}

esp_err_t HueGatewayModule::pageWebDiagnose(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebDiagnose(req);
}

esp_err_t HueGatewayModule::handleWebRetrySetup(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    self->_deviceSetupNeedsRetry = true;
    self->_deviceSetupRetryBackoffMs = 0;
    self->_setupCircuitOpenUntilMs = 0;
    self->_setupCircuitTrips = 0;
    self->_consecutiveEmptyLightFetches = 0;
    self->_lastDeviceSetupRetryMs = 0;
    Serial.println("[HueGatewayModule] Manual setup retry triggered via WebUI");
    self->appendDiagnosticLog("INFO", "WEBUI", "manual setup retry triggered");

    String html;
    html.reserve(512);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='5;url=" + hueBaseUri + "/status'>";
    html += "<title>Setup-Retry</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "a{display:inline-block;padding:10px 16px;margin:5px;background:#007bff;color:#fff;text-decoration:none;border-radius:3px;}</style></head><body>";
    html += "<h1>\xF0\x9F\x94\x84 Setup-Retry ausgelöst</h1>";
    html += "<p>Die Geräte-Zuordnung wird beim nächsten Loop-Durchlauf wiederholt.</p>";
    html += "<p>Weiterleitung auf Statusseite in 5 Sekunden...</p>";
    html += "<a href='" + hueBaseUri + "/status'>\xF0\x9F\x93\x8A Status</a> ";
    html += "<a href='" + hueBaseUri + "'>\xE2\x86\x90 Zurück</a></body></html>";
    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::pageWebRetrySetup(const char* uri, httpd_req_t* req, void* arg)
{
    (void)uri;
    HueGatewayModule* self = static_cast<HueGatewayModule*>(arg);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    req->user_ctx = self;
    return HueGatewayModule::handleWebRetrySetup(req);
}

String HueGatewayModule::getBridgeScanHTML()
{
    return _lastWebScanHtml;
}

String HueGatewayModule::getBridgeScanText()
{
    return _lastWebScanText;
}

void HueGatewayModule::updateWebScanCache()
{
    _diagCounterWebScanRuns++;
    appendDiagnosticLog("INFO", "WEBSCAN", "scan start");
    _lastWebScanError = "";

    if (!_client || !_initialized || !_client->isInitialized())
    {
        _lastWebScanError = "Keine aktive Bridge-Verbindung";
        _lastWebScanHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte laden</title></head><body><h1>ℹ️ Hue-Geräte laden</h1><p>Keine aktive Bridge-Verbindung.</p></body></html>";
        _lastWebScanText = "Keine aktive Bridge-Verbindung.\n";
        _lastWebScanLightCount = -1;
        return;
    }

    if (_client->isEventStreamConnected())
    {
        _client->stopEventStream();
        _lastEventStreamRetryMs = millis();
        delay(20);
    }

    const int previousLightCount = _lastWebScanLightCount;

    const String previousText = _lastWebScanText;
    const unsigned long scanDurationMs = (_webScanStartedMs == 0) ? 0 : (millis() - _webScanStartedMs);

    HueGatewayLightState* lights = new (std::nothrow) HueGatewayLightState[kWebScanMaxLights];
    if (lights == nullptr)
    {
        _lastWebScanLightCount = 0;
        _lastWebScanError = "Nicht genug RAM fuer Hue-Scan-Liste";
        _lastWebScanText = "Web-Scan fehlgeschlagen: Nicht genug RAM fuer Hue-Scan-Liste.\n";
        return;
    }

    int count = _client->getLights(lights, kWebScanMaxLights);

    String text;
    text.reserve(256 + (count > 0 ? (count * 180) : 64));
    text = "Open KNX Hue-Geräteabfrage\n";
    text += "---------------------------------\n";

    if (count <= 0)
    {
        if (previousLightCount > 0)
        {
            _lastWebScanLightCount = previousLightCount;
            _lastWebScanText = previousText;
            _lastWebScanError = "Scan lieferte 0 Leuchten, vorheriges Ergebnis beibehalten";
            Serial.println("[HueGatewayModule] Web scan returned 0 lights, keeping previous successful result");
            appendDiagnosticLog("WARN", "WEBSCAN", "scan returned 0 lights, previous result kept");
            delete[] lights;
            return;
        }

        const bool bridgeApiReachable = _client->pingBridgeApiV2();
        if (!bridgeApiReachable)
        {
            _lastWebScanError = "Bridge API v2 nicht erreichbar oder Timeout";
        }
        else
        {
            _lastWebScanError = "Keine Leuchten gefunden oder JSON-Antwort konnte nicht vollständig geparst werden";
        }

        text += "Keine Leuchten gefunden. Bitte Bridge-Verbindung prüfen.\n";
        text += "Diagnose: " + _lastWebScanError + "\n";
        if (scanDurationMs > 0)
        {
            text += "Scan-Dauer: " + String(scanDurationMs / 1000UL) + " Sekunden\n";
        }
        text += "Hinweis: Bei großen Installationen kann ein JSON-Parsefehler durch zu große Antwort auftreten (Seriellog prüfen).\n";
        _lastWebScanLightCount = 0;
        _lastWebScanHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte laden</title></head><body><h1>⚠️ Hue-Geräte laden</h1><p>Keine Leuchten gefunden.</p></body></html>";
        _lastWebScanText = text;
        appendDiagnosticLog("WARN", "WEBSCAN", String("scan returned no lights, error=") + _lastWebScanError);
        delete[] lights;
        return;
    }

    for (int i = 0; i < count; i++)
    {
        String roomName = lights[i].room.length() ? lights[i].room : "-";
        String roomRid = lights[i].roomRid.length() ? lights[i].roomRid : "-";
        String roomNo = lights[i].roomIdV1.length() ? lights[i].roomIdV1 : "-";
        String zoneName = lights[i].zone.length() ? lights[i].zone : "-";
        String zoneRid = lights[i].zoneRid.length() ? lights[i].zoneRid : "-";
        String zoneNo = lights[i].zoneIdV1.length() ? lights[i].zoneIdV1 : "-";


        text += String(i + 1) + ") " + lights[i].name + "\n";
        text += "    ID: " + lights[i].id + "\n";
        text += "    Status: " + String(lights[i].on ? "EIN" : "AUS");
        text += " | Helligkeit: " + String(lights[i].brightness) + "/254";
        text += " | Raum: " + roomName;
        if (roomRid != "-" || roomNo != "-")
        {
            text += " (";
            bool needComma = false;
            if (roomRid != "-")
            {
                text += "ID: " + roomRid;
                needComma = true;
            }
            if (roomNo != "-")
            {
                if (needComma)
                {
                    text += ", ";
                }
                text += "Nr: " + roomNo;
            }
            text += ")";
        }
        text += " | Zone: " + zoneName;
        if (zoneRid != "-" || zoneNo != "-")
        {
            text += " (";
            bool needComma = false;
            if (zoneRid != "-")
            {
                text += "ID: " + zoneRid;
                needComma = true;
            }
            if (zoneNo != "-")
            {
                if (needComma)
                {
                    text += ", ";
                }
                text += "Nr: " + zoneNo;
            }
            text += ")";
        }
        text += "\n";
    }

    // Zubehör (Schalter, Sensoren)
    const int kMaxAccessories = 48;
    HueGatewayAccessoryDevice* accessories = new (std::nothrow) HueGatewayAccessoryDevice[kMaxAccessories];
    if (accessories != nullptr)
    {
        int accCount = _client->getAccessoryDevices(accessories, kMaxAccessories);
        _lastWebScanAccessoryCount = (accCount > 0) ? accCount : 0;
        if (accCount > 0)
        {
            text += "\nSchalter & Sensoren:\n";
            text += "---------------------------------\n";
            for (int i = 0; i < accCount; i++)
            {
                text += String(i + 1) + ") " + accessories[i].name + "\n";
                text += "    ID: " + accessories[i].id + "\n";
                text += "    Typ: " + accessories[i].type + "\n";
            }
        }
        delete[] accessories;
    }
    else
    {
        _lastWebScanAccessoryCount = 0;
    }

    // Hue-Szenen
    const int kMaxScenes = 64;
    HueGatewayScene* sceneList = new (std::nothrow) HueGatewayScene[kMaxScenes];
    if (sceneList != nullptr)
    {
        int sceneCount = _client->getScenes(sceneList, kMaxScenes);
        if (sceneCount > 0)
        {
            text += "\nHue-Szenen:\n";
            text += "---------------------------------\n";
            String lastGroup = "";
            int sceneIdx = 1;
            // Output scenes grouped by room/zone (scenes from API are typically delivered per group)
            for (int i = 0; i < sceneCount; i++)
            {
                if (sceneList[i].groupName != lastGroup)
                {
                    text += sceneList[i].groupName + ":\n";
                    lastGroup = sceneList[i].groupName;
                    sceneIdx = 1;
                }
                text += "  " + String(sceneIdx++) + ") " + sceneList[i].name + "\n";
                text += "     ID: " + sceneList[i].id + "\n";
            }
        }
        delete[] sceneList;
    }

    if (scanDurationMs > 0)
    {
        text += "Scan-Dauer: " + String(scanDurationMs / 1000UL) + " Sekunden\n";
    }

    _lastWebScanLightCount = count;
    _lastWebScanError = "";
    _lastWebScanHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Hue-Geräte geladen</title></head><body><h1>Hue-Geräte geladen</h1><p>Daten im Stream-Renderer bereit.</p></body></html>";
    _lastWebScanText = text;
    appendDiagnosticLog("INFO", "WEBSCAN", String("scan done, lights=") + String(count));
    delete[] lights;
}


