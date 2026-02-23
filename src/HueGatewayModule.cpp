#include "HueGatewayModule.h"
#include "HueGatewayDiscovery.h"
#include "HueGatewayAuth.h"
#include "HueGatewayClient.h"
#include "Devices/HueGatewayLight.h"
#include "OpenKNX/Led/RGB.h"
#include <ETH.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

namespace
{
static constexpr unsigned long kFastTrackFirstDelayMs = 200UL;
static constexpr unsigned long kFastTrackSecondDelayMs = 300UL;
static constexpr unsigned long kFastTrackCooldownMs = 800UL;
static constexpr uint8_t kFastTrackChecksPerCommand = 2;
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

HueGatewayModule::HueGatewayModule()
    : _initialized(false)
    , _lastConnectionCheckMs(0)
    , _lastRefreshTickMs(0)
    , _lastDeviceSetupRetryMs(0)
    , _lastEventStreamRetryMs(0)
    , _eventStreamRetryBackoffMs(10000)
    , _eventStreamPauseUntilMs(0)
    , _eventStreamFailureCount(0)
    , _client(nullptr)
    , _lightCount(0)
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
    , _manualPairingRequired(false)
    , _pairingTriggerLastState(false)
    , _devicesInitialized(false)
    , _deviceSetupNeedsRetry(false)
    , _webScanRequested(false)
    , _webScanInProgress(false)
    , _lastWebScanMs(0)
    , _lastWebScanLightCount(-1)
    , _hclLockActive(false)
    , _hclLockFallbackMode(static_cast<uint8_t>(HueGatewayModule::HclLockFallbackMode::None))
    , _hclLockActivatedMs(0)
    , _hclLockAutoReleaseMs(0)
    , _hclLockActivationDayOfYear(-1)
{
    // Light-Array initialisieren
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        _lights[i] = nullptr;
        _channelLastPollMs[i] = 0;
        _channelFastTrackNextMs[i] = 0;
        _channelFastTrackCooldownUntilMs[i] = 0;
        _channelFastTrackRemaining[i] = 0;
    }

    for (uint8_t i = 0; i < HCL::MasterManager::MAX_MASTERS; i++)
    {
        _hclManagerLockActive[i] = false;
        _hclManagerLockFallbackMode[i] = static_cast<uint8_t>(HclLockFallbackMode::None);
        _hclManagerLockActivatedMs[i] = 0;
        _hclManagerLockAutoReleaseMs[i] = 0;
        _hclManagerLockActivationDayOfYear[i] = -1;
        _hclLastPublishedKelvin[i] = 0;
        _hclLastPublishedBrightness[i] = 0;
        _hclMasterValuesPublished[i] = false;
    }

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
    
    setupBridge();
    setupDevices();
    setupHCL();
    setupWebUI();
    setupMDNS();
    
    _initialized = true;
    Serial.println("[HueGatewayModule] Setup complete");
}

void HueGatewayModule::loop()
{
    static bool fallbackActiveLogged = false;

    if (!_initialized)
        return;
    
    // Update Info-LED pattern
    updateInfoLED();

    // Non-blocking authentication polling
    pollAuthentication();
    
    // Update HCL Manager with current time
    struct tm timeinfo;
    const bool hasTime = getLocalTime(&timeinfo, 0);
    if (hasTime) {
        uint16_t currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        HCL::masterManager.loop(currentMinutes);
    }

    evaluateHclLockFallback(hasTime ? &timeinfo : nullptr, hasTime);
    evaluateHclManagerLockFallback(hasTime ? &timeinfo : nullptr, hasTime);
    publishHclMasterValues();
    
    // Update all lights with HCL loop
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] != nullptr)
        {
            _lights[i]->loop();
        }
    }
    
    unsigned long now = millis();
    
    // Re-check bridge connectivity every 5 seconds.
    if (now - _lastConnectionCheckMs > 5000)
    {
        _lastConnectionCheckMs = now;
        checkConnection();
    }

    if (_client && _client->isInitialized() && !_authPending)
    {
        if (!_client->isEventStreamConnected())
        {
            if (_eventStreamPauseUntilMs != 0 && now >= _eventStreamPauseUntilMs)
            {
                _eventStreamPauseUntilMs = 0;
                _eventStreamRetryBackoffMs = 10000;
                _eventStreamFailureCount = 0;
                Serial.println("[HueGatewayModule] EventStream cooldown elapsed, retrying");
            }

            if (_eventStreamPauseUntilMs == 0 && (now - _lastEventStreamRetryMs >= _eventStreamRetryBackoffMs))
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

            HueGatewayEventLightUpdate updates[MAX_LIGHTS];
            int updateCount = _client->pollEventStream(updates, MAX_LIGHTS);
            if (updateCount > 0)
            {
                Serial.printf("[HueGatewayModule] EventStream updates received: %d\n", updateCount);
                applyEventStreamUpdates(updates, updateCount);
            }
        }
        else
        {
            _eventStreamRetryBackoffMs = 10000;
            _eventStreamPauseUntilMs = 0;
            _eventStreamFailureCount = 0;

            if (fallbackActiveLogged)
            {
                Serial.println("[HueGatewayModule] EventStream active, polling fallback suspended");
                fallbackActiveLogged = false;
            }

            HueGatewayEventLightUpdate updates[MAX_LIGHTS];
            int updateCount = _client->pollEventStream(updates, MAX_LIGHTS);
            if (updateCount > 0)
            {
                Serial.printf("[HueGatewayModule] EventStream updates received: %d\n", updateCount);
                applyEventStreamUpdates(updates, updateCount);
            }
        }
    }

    if (!_authPending && !_manualPairingRequired && (_bridgeStatus == BridgeStatus::CONNECTION_LOST || _bridgeStatus == BridgeStatus::BRIDGE_UNREACHABLE || !_client || !_client->isInitialized()))
    {
        if (_bridgeIP.isEmpty())
        {
            _bridgeIP = getBridgeIP();
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

            if (initClientWithAppKey())
            {
                setupDevices();
                updateStatus(BridgeStatus::CONNECTED);
                _reconnectBackoffMs = 10000;
                Serial.println("[HueGatewayModule] Reconnect successful");
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
            if ((now - _lastDeviceSetupRetryMs) >= 5000UL)
            {
                _lastDeviceSetupRetryMs = now;
                Serial.printf("[HueGatewayModule] Device map requires retry (%u channels configured), retrying setupDevices()\n",
                              static_cast<unsigned>(configuredChannels));
                setupDevices();
            }
        }
    }

    // Hue->KNX Refresh in 1s-Ticks (kanalspezifisches PollInterval greift in refreshLightStatus)
    if (now - _lastRefreshTickMs >= 1000)
    {
        _lastRefreshTickMs = now;
        if (!(_client && _client->isEventStreamConnected()))
        {
            if (!fallbackActiveLogged)
            {
                Serial.println("[HueGatewayModule] Polling fallback active (EventStream disconnected)");
                fallbackActiveLogged = true;
            }
            refreshLightStatus();
        }
    }

    if (_webScanRequested && !_webScanInProgress && _client && _initialized && _client->isInitialized())
    {
        _webScanRequested = false;
        _webScanInProgress = true;
        updateWebScanCache();
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
    return "0.1.0";
}

void HueGatewayModule::processInputKo(GroupObject& ko)
{
    if (!_initialized)
        return;
        
    uint16_t koNumber = ko.asap();
    
    if (koNumber == HUE_KoHUEPairingTrigger)
    {
        bool trigger = ko.value(Dpt(1, 17));  // DPT 1.017 Trigger
        if (trigger && !_pairingTriggerLastState)
        {
            Serial.println("[HueGatewayModule] Pairing triggered via ETS KO");
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

    #ifdef HUE_KoHUEHCLM1Lock
    if (koNumber == HUE_KoHUEHCLM1Lock)
    {
        setHclManagerLock(1, ko.value(Dpt(1, 1)), "KO");
        return;
    }
    #endif
    #ifdef HUE_KoHUEHCLM2Lock
    if (koNumber == HUE_KoHUEHCLM2Lock)
    {
        setHclManagerLock(2, ko.value(Dpt(1, 1)), "KO");
        return;
    }
    #endif
    #ifdef HUE_KoHUEHCLM3Lock
    if (koNumber == HUE_KoHUEHCLM3Lock)
    {
        setHclManagerLock(3, ko.value(Dpt(1, 1)), "KO");
        return;
    }
    #endif
    #ifdef HUE_KoHUEHCLM4Lock
    if (koNumber == HUE_KoHUEHCLM4Lock)
    {
        setHclManagerLock(4, ko.value(Dpt(1, 1)), "KO");
        return;
    }
    #endif

    if (koNumber < HUE_KoBlockOffset)
    {
        return;
    }

    // KO an entsprechendes Light weiterleiten
    // Struktur pro Kanal (9 KOs):
    // KO 0: Switch (DPT 1.001)
    // KO 1: Brightness absolut (DPT 5.001)
    // KO 2: Dimming relativ (DPT 3.007)
    // KO 3: Status Switch (DPT 1.001)
    // KO 4: Status Brightness (DPT 5.001)
    // KO 5: ColorTemp (DPT 7.600)
    // KO 6: Status ColorTemp (DPT 7.600)
    // KO 7: ColorRGB (DPT 232.600)
    // KO 8: Status ColorRGB (DPT 232.600)
    
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

    uint8_t _channelIndex = static_cast<uint8_t>(channel);
    uint8_t syncDir = ParamHUE_CHSyncDir;

    // SyncDir: 0=None, 1=KNX->Hue, 2=Hue->KNX, 3=Bidirectional
    bool isCommandKo = (koType == 0 || koType == 1 || koType == 2 || koType == 5 || koType == 7);
    if (isCommandKo && !(syncDir == 1 || syncDir == 3))
    {
        Serial.printf("[HueGatewayModule] Channel %d SyncDir=%u blocks KNX->Hue command (KO type %u)\n",
                      channel + 1,
                      static_cast<unsigned>(syncDir),
                      static_cast<unsigned>(koType));
        return;
    }

    if (_lights[channel] == nullptr)
    {
        static uint32_t lastRecoverTryMs = 0;
        if (_client && _client->isInitialized() && ((nowMs - lastRecoverTryMs) >= 5000 || nowMs < lastRecoverTryMs))
        {
            lastRecoverTryMs = nowMs;
            Serial.println("[HueGatewayModule] Channel map missing, retrying setupDevices()...");
            setupDevices();
        }

        if (_lights[channel] == nullptr)
        {
            static uint32_t lastNotConfiguredLogMs = 0;
            if ((nowMs - lastNotConfiguredLogMs) >= 10000 || nowMs < lastNotConfiguredLogMs)
            {
                Serial.printf("[HueGatewayModule] Channel %d not configured\n", channel + 1);
                lastNotConfiguredLogMs = nowMs;
            }
            return;
        }
    }
    
    switch (koType)
    {
        case 0:  // Switch KO
        {
            bool value = ko.value(Dpt(1, 1));
            _lights[channel]->processKnxSwitch(value);
            break;
        }
        case 1:  // Brightness KO (absolut)
        {
            uint8_t value = ko.value(Dpt(5, 1));
            _lights[channel]->processKnxBrightness(value);
            break;
        }
        case 2:  // Dimming KO (relativ)
        {
            uint8_t controlBit = ko.value(Dpt(3, 7, 0));
            uint8_t stepCode = ko.value(Dpt(3, 7, 1));
            uint8_t value = static_cast<uint8_t>(((controlBit & 0x01) << 3) | (stepCode & 0x07));
            _lights[channel]->processKnxDimming(value);
            break;
        }
        case 3:  // Status Switch KO (read-only, no processing)
            break;
        case 4:  // Status Brightness KO (read-only, no processing)
            break;
        case 5:  // ColorTemp KO
        {
            uint16_t kelvin = ko.value(Dpt(7, 600));
            _lights[channel]->processKnxColorTemp(kelvin);
            break;
        }
        case 6:  // Status ColorTemp KO (read-only, no processing)
            break;
        case 7:  // ColorRGB KO
        {
            uint8_t* rgb = ko.valueRef();
            _lights[channel]->processKnxColorRGB(rgb[0], rgb[1], rgb[2]);
            break;
        }
        case 8:  // Status ColorRGB KO (read-only, no processing)
            break;
    }

    if (isCommandKo && _lights[channel] != nullptr)
    {
        if (syncDir == 2 || syncDir == 3)
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

bool HueGatewayModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (cmd == "hue")
    {
        openknx.console.printHelpLine("hue scan", "Scan for Hue Bridge and list all lights");
        openknx.console.printHelpLine("hue pair", "Start pairing workflow (press bridge button)");
        openknx.console.printHelpLine("hue status", "Show current module status");
        openknx.console.printHelpLine("hue hcl", "Show HCL runtime and advanced master settings");
        return true;
    }

    if (cmd == "hue pair")
    {
        startPairing();
        Serial.println("[HueGatewayModule] Pairing command accepted. Press Hue Bridge button now.");
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
        
        // Check whether authentication is available.
        HueGatewayAuth auth;
        if (!auth.authenticateBlocking(bridgeIP.c_str(), 30000))
        {
            Serial.println("ERROR: Authentication failed!");
            Serial.println("Press button on Hue Bridge and retry");
            return true;
        }
        
        // Fetch available lights from the bridge.
        HueGatewayClient client;
        if (!client.begin(bridgeIP, auth.getAppKey()))
        {
            Serial.println("ERROR: Client init failed!");
            return true;
        }
        
        HueGatewayLightState lights[MAX_LIGHTS];
        int count = client.getLights(lights, MAX_LIGHTS);
        
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
        Serial.println("To use in ETS:");
        Serial.println("1. Copy the Light ID (UUID)");
        Serial.println("2. Paste into ETS parameter 'Light ID'");
        Serial.println("3. Set channel to 'Active'");
        Serial.println("=================================");
        
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
            if (_lights[i] == nullptr)
                continue;
            Serial.printf("%2d: %-25s %s\n", i, 
                          _lights[i]->getName().c_str(),
                          _lights[i]->getLightId().c_str());
            Serial.printf("    Status: %s, Brightness: %d\n",
                          _lights[i]->isOn() ? "ON " : "OFF",
                          _lights[i]->getBrightness());
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
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                        #endif
                        break;
                    case 2:
                        #ifdef ParamHUE_HCLM2CurveType
                        curveTypeValue = ParamHUE_HCLM2CurveType;
                        slewRate = ParamHUE_HCLM2SlewRate;
                        manualKelvin = ParamHUE_HCLM2ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                        #endif
                        break;
                    case 3:
                        #ifdef ParamHUE_HCLM3CurveType
                        curveTypeValue = ParamHUE_HCLM3CurveType;
                        slewRate = ParamHUE_HCLM3SlewRate;
                        manualKelvin = ParamHUE_HCLM3ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                        #endif
                        break;
                    case 4:
                        #ifdef ParamHUE_HCLM4CurveType
                        curveTypeValue = ParamHUE_HCLM4CurveType;
                        slewRate = ParamHUE_HCLM4SlewRate;
                        manualKelvin = ParamHUE_HCLM4ManualKelvin;
                        sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunrise);
                        sunset = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunset);
                        sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                        sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                        #endif
                        break;
                    default:
                        break;
                }

                const char* curveText = "FixedTime";
                if (curveTypeValue == 1)
                {
                    curveText = "SunPosition";
                }
                else if (curveTypeValue == 2)
                {
                    curveText = "Manual";
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
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                    #endif
                    break;
                case 2:
                    #ifdef ParamHUE_HCLM2CurveType
                    curveTypeValue = ParamHUE_HCLM2CurveType;
                    slewRate = ParamHUE_HCLM2SlewRate;
                    manualKelvin = ParamHUE_HCLM2ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                    #endif
                    break;
                case 3:
                    #ifdef ParamHUE_HCLM3CurveType
                    curveTypeValue = ParamHUE_HCLM3CurveType;
                    slewRate = ParamHUE_HCLM3SlewRate;
                    manualKelvin = ParamHUE_HCLM3ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                    #endif
                    break;
                case 4:
                    #ifdef ParamHUE_HCLM4CurveType
                    curveTypeValue = ParamHUE_HCLM4CurveType;
                    slewRate = ParamHUE_HCLM4SlewRate;
                    manualKelvin = ParamHUE_HCLM4ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                    #endif
                    break;
                default:
                    break;
            }

            const char* curveText = "FixedTime";
            if (curveTypeValue == 1)
            {
                curveText = "SunPosition";
            }
            else if (curveTypeValue == 2)
            {
                curveText = "Manual";
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
    
    return false;  // Nicht mein Befehl
}

void HueGatewayModule::setupMDNS()
{
    // Register mDNS service for convenient local access via openknx-bridge.local.
    if (!MDNS.begin("openknx-bridge"))
    {
        logErrorP("mDNS start failed!");
        return;
    }
    
    // HTTP Service anmelden
    MDNS.addService("http", "tcp", 80);
    
    logInfoP("mDNS started: openknx-bridge.local");
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
    
    // Bridge IP aus ETS-Parameter lesen
    _bridgeIP = getBridgeIP();
    
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
    Serial.println("[HueGatewayModule] Setting up Devices...");
    _lastDeviceSetupRetryMs = millis();
    _deviceSetupNeedsRetry = false;

    uint8_t channelCount = ParamHUE_HUEChannelCount;
    if (channelCount > MAX_LIGHTS)
    {
        channelCount = MAX_LIGHTS;
    }

    resetDevices();
    
    if (!_client || !_client->isInitialized())
    {
        if (channelCount > 0)
        {
            _deviceSetupNeedsRetry = true;
            Serial.printf("[HueGatewayModule] Client not ready, scheduling setup retry for %u configured channels\n",
                          static_cast<unsigned>(channelCount));
        }
        Serial.println("[HueGatewayModule] ERROR: HueGatewayClient not ready");
        return;
    }
    
    // Anzahl Kanäle aus ETS lesen
    Serial.printf("[HueGatewayModule] Configured channels: %d\n", channelCount);
    
    if (channelCount == 0)
    {
        Serial.println("[HueGatewayModule] No channels configured");
        return;
    }
    
    // Retrieve all lights from the bridge for validation/mapping.
    if (_client->isEventStreamConnected())
    {
        _client->stopEventStream();
        _lastEventStreamRetryMs = millis();
        delay(20);
    }

    HueGatewayLightState allLights[MAX_LIGHTS];
    int bridgeLightCount = _client->getLights(allLights, MAX_LIGHTS);
    if (bridgeLightCount <= 0)
    {
        delay(60);
        bridgeLightCount = _client->getLights(allLights, MAX_LIGHTS);
    }
    Serial.printf("[HueGatewayModule] Bridge has %d lights\n", bridgeLightCount);
    
    // Kanäle aus ETS-Parametern laden
    // Bevorzugt UUID-Mapping aus ETS, Fallback auf Index bei leerer UUID
    uint8_t maxChannels = channelCount;
    if (maxChannels > MAX_LIGHTS)
    {
        maxChannels = MAX_LIGHTS;
    }
    bool hasIndexMappedChannel = false;
    for (uint8_t ch = 0; ch < maxChannels; ch++)
    {
        uint8_t _channelIndex = ch;

        if (ParamHUE_CHDisabled)
        {
            Serial.printf("[HueGatewayModule] Channel %d: Disabled\n", ch + 1);
            continue;
        }

        std::string configuredUuidStd = ParamHUE_CHLightUUIDStr;
        String configuredUuid(configuredUuidStd.c_str());
        configuredUuid.trim();
        if (configuredUuid.length() == 0)
        {
            hasIndexMappedChannel = true;
        }

        const HueGatewayLightState* selectedLight = nullptr;
        HueGatewayLightState fallbackLight;
        if (configuredUuid.length() > 0)
        {
            for (int i = 0; i < bridgeLightCount; i++)
            {
                if (allLights[i].id.equalsIgnoreCase(configuredUuid))
                {
                    selectedLight = &allLights[i];
                    break;
                }
            }

            if (selectedLight == nullptr)
            {
                fallbackLight.id = configuredUuid;
                fallbackLight.name = String("Channel ") + String(ch + 1);
                fallbackLight.supportsColorTemp = true;
                fallbackLight.supportsColor = true;
                selectedLight = &fallbackLight;

                Serial.printf("[HueGatewayModule] Channel %d: UUID %s currently not in scan result, using ETS UUID fallback\n",
                              ch + 1,
                              configuredUuid.c_str());
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
        
        // Create a HueGatewayLight instance for this channel.
        _lights[ch] = new HueGatewayLight(selectedLight->id, selectedLight->name, _client);
        _lights[ch]->begin(koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness, koStatusColorTemp, koStatusColorRGB);

        uint8_t lightType = ParamHUE_CHLightType;
        uint8_t effectiveLightType = lightType;

        if (effectiveLightType >= 3 && !selectedLight->supportsColor)
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

        if (effectiveLightType == 2 && !selectedLight->supportsColorTemp)
        {
            effectiveLightType = 1;
            Serial.printf("[HueGatewayModule] Channel %d: ETS type %u downgraded to %u (light has no CT support)\n",
                          ch + 1,
                          static_cast<unsigned>(lightType),
                          static_cast<unsigned>(effectiveLightType));
        }

        _lights[ch]->setLightType(effectiveLightType);

        uint8_t hclMaster = ParamHUE_CHHCLMaster;
        if (hclMaster > 4)
        {
            hclMaster = 0;
        }
        _lights[ch]->setHCLMaster(hclMaster);

        uint8_t minBrightness = ParamHUE_CHMinBrightness;
        _lights[ch]->setMinBrightness(minBrightness);
        _channelLastPollMs[ch] = 0;

        Serial.printf("[HueGatewayModule] Channel %d: %s (%s), Type:%u Sync:%u Poll:%us MinBri:%u%% HCL:%u -> KO %d/%d/%d/%d/%d\n",
                      ch + 1,
                      selectedLight->name.c_str(),
                      selectedLight->id.c_str(),
                      static_cast<unsigned>(effectiveLightType),
                      static_cast<unsigned>(ParamHUE_CHSyncDir),
                      static_cast<unsigned>(ParamHUE_CHPollInterval),
                      static_cast<unsigned>(minBrightness),
                      static_cast<unsigned>(hclMaster),
                      koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        _lightCount++;
    }

    uint8_t enabledChannels = countEnabledChannels();
    if (enabledChannels > 0 && (_lightCount == 0 || (bridgeLightCount <= 0 && hasIndexMappedChannel)))
    {
        _deviceSetupNeedsRetry = true;
        Serial.printf("[HueGatewayModule] setupDevices incomplete (enabled=%u, mapped=%d, bridgeLights=%d), retry scheduled\n",
                      static_cast<unsigned>(enabledChannels),
                      _lightCount,
                      bridgeLightCount);
    }
    else
    {
        _deviceSetupNeedsRetry = false;
    }
    
    Serial.printf("[HueGatewayModule] Initialized %d lights\n", _lightCount);
    _devicesInitialized = true;
}

void HueGatewayModule::setupHCL()
{
    Serial.println("[HueGatewayModule] Setting up HCL...");
    setHclLock(false, "setup");
    for (uint8_t manager = 1; manager <= HCL::MasterManager::MAX_MASTERS; manager++)
    {
        setHclManagerLock(manager, false, "setup");
    }
    
    // Check if HCL is enabled
    #ifdef ParamHUE_HUEHCLEnable
    bool hclEnabled = ParamHUE_HUEHCLEnable != 0;
    #else
    bool hclEnabled = false;
    #endif
    
    if (!hclEnabled) {
        Serial.println("[HueGatewayModule] HCL disabled");
        HCL::masterManager.setEnabled(false);
        return;
    }
    
    Serial.println("[HueGatewayModule] HCL enabled");
    HCL::masterManager.setEnabled(true);

    #ifdef ParamHUE_HUEHCLLockFallback
    _hclLockFallbackMode = ParamHUE_HUEHCLLockFallback;
    #else
    _hclLockFallbackMode = static_cast<uint8_t>(HclLockFallbackMode::None);
    #endif

    #ifdef ParamHUE_HUEHCLLockFallbackEnable
    if (ParamHUE_HUEHCLLockFallbackEnable == 0)
    {
        _hclLockFallbackMode = static_cast<uint8_t>(HclLockFallbackMode::None);
    }
    #endif

    _hclManagerLockFallbackMode[0] = static_cast<uint8_t>(HclLockFallbackMode::None);
    _hclManagerLockFallbackMode[1] = static_cast<uint8_t>(HclLockFallbackMode::None);
    _hclManagerLockFallbackMode[2] = static_cast<uint8_t>(HclLockFallbackMode::None);
    _hclManagerLockFallbackMode[3] = static_cast<uint8_t>(HclLockFallbackMode::None);

    #ifdef ParamHUE_HUEHCLM1LockFallback
    _hclManagerLockFallbackMode[0] = ParamHUE_HUEHCLM1LockFallback;
    #endif
    #ifdef ParamHUE_HUEHCLM2LockFallback
    _hclManagerLockFallbackMode[1] = ParamHUE_HUEHCLM2LockFallback;
    #endif
    #ifdef ParamHUE_HUEHCLM3LockFallback
    _hclManagerLockFallbackMode[2] = ParamHUE_HUEHCLM3LockFallback;
    #endif
    #ifdef ParamHUE_HUEHCLM4LockFallback
    _hclManagerLockFallbackMode[3] = ParamHUE_HUEHCLM4LockFallback;
    #endif

    #ifdef ParamHUE_HUEHCLM1LockFallbackEnable
    if (ParamHUE_HUEHCLM1LockFallbackEnable == 0) _hclManagerLockFallbackMode[0] = static_cast<uint8_t>(HclLockFallbackMode::None);
    #endif
    #ifdef ParamHUE_HUEHCLM2LockFallbackEnable
    if (ParamHUE_HUEHCLM2LockFallbackEnable == 0) _hclManagerLockFallbackMode[1] = static_cast<uint8_t>(HclLockFallbackMode::None);
    #endif
    #ifdef ParamHUE_HUEHCLM3LockFallbackEnable
    if (ParamHUE_HUEHCLM3LockFallbackEnable == 0) _hclManagerLockFallbackMode[2] = static_cast<uint8_t>(HclLockFallbackMode::None);
    #endif
    #ifdef ParamHUE_HUEHCLM4LockFallbackEnable
    if (ParamHUE_HUEHCLM4LockFallbackEnable == 0) _hclManagerLockFallbackMode[3] = static_cast<uint8_t>(HclLockFallbackMode::None);
    #endif

    // Read HCL configuration from ETS
    #ifdef ParamHUE_HUEHCLUpdateInterval
    uint16_t updateInterval = ParamHUE_HUEHCLUpdateInterval;
    HCL::masterManager.setUpdateInterval(updateInterval);
    Serial.printf("[HueGatewayModule] HCL update interval: %d seconds\n", updateInterval);
    #endif
    
    #ifdef ParamHUE_HUEHCLFadeDuration
    uint8_t fadeDuration = ParamHUE_HUEHCLFadeDuration;
    HCL::masterManager.setFadeDuration(fadeDuration);
    Serial.printf("[HueGatewayModule] HCL fade duration: %d seconds\n", fadeDuration);
    #endif

    auto loadMasterSetpoints = [](uint8_t masterNumber,
                                  const char* const (&times)[10],
                                  const uint16_t (&kelvins)[10],
                                  const uint8_t (&brightnesses)[10]) {
        HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
        if (!master)
        {
            Serial.printf("[HueGatewayModule] HCL Master %u not available\n", masterNumber);
            return;
        }

        Serial.printf("[HueGatewayModule] Loading HCL Master %u setpoints...\n", masterNumber);

        for (int i = 0; i < 10; i++)
        {
            uint16_t minutes = HCL::Setpoint::parseTime(times[i]);
            if (minutes == 0xFFFF)
            {
                continue;
            }

            master->setSetpoint(i, HCL::Setpoint(minutes, kelvins[i], brightnesses[i]));
            Serial.printf("  SP%d: %s (%dmin) -> %dK, %d%%\n", i + 1, times[i], minutes, kelvins[i], brightnesses[i]);
        }

        master->sortSetpoints();
        Serial.printf("[HueGatewayModule] HCL Master %u loaded with %d valid setpoints\n",
                      masterNumber,
                      master->getValidSetpointCount());
    };

    auto applyMasterAdvanced = [](uint8_t masterNumber,
                                  uint8_t curveType,
                                  uint16_t slewRateKelvinPerMinute,
                                  uint16_t manualKelvin,
                                  const char* sunriseTime,
                                  const char* sunsetTime,
                                  int16_t sunriseOffset,
                                  int16_t sunsetOffset) {
        HCL::Master* master = HCL::masterManager.getMaster(masterNumber);
        if (!master)
        {
            return;
        }

        if (curveType > static_cast<uint8_t>(HCL::CurveType::Manual))
        {
            curveType = static_cast<uint8_t>(HCL::CurveType::FixedTime);
        }

        master->setCurveType(static_cast<HCL::CurveType>(curveType));
        master->setSlewRateKelvinPerMinute(slewRateKelvinPerMinute);
        master->setManualKelvin(manualKelvin);

        uint16_t sunriseMinutes = HCL::Setpoint::parseTime(sunriseTime);
        uint16_t sunsetMinutes = HCL::Setpoint::parseTime(sunsetTime);
        if (sunriseMinutes != 0xFFFF && sunsetMinutes != 0xFFFF)
        {
            master->setSunTimes(sunriseMinutes, sunsetMinutes);
        }
        else
        {
            master->clearSunTimes();
        }

        master->setSunOffsets(sunriseOffset, sunsetOffset);

        Serial.printf("[HueGatewayModule] HCL Master %u advanced: curve=%u slew=%uK/min manual=%uK sunrise=%s sunset=%s offsets=%d/%d\n",
                      masterNumber,
                      static_cast<unsigned>(curveType),
                      static_cast<unsigned>(slewRateKelvinPerMinute),
                      static_cast<unsigned>(manualKelvin),
                      sunriseTime,
                      sunsetTime,
                      static_cast<int>(sunriseOffset),
                      static_cast<int>(sunsetOffset));
    };

    #ifdef ParamHUE_HCLM1SP0Time
    {
        const char* const times[10] = {
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP0Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP1Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP2Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP3Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP4Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP5Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP6Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP7Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP8Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM1SP9Time)
        };
        const uint16_t kelvins[10] = {
            ParamHUE_HCLM1SP0Kelvin, ParamHUE_HCLM1SP1Kelvin, ParamHUE_HCLM1SP2Kelvin,
            ParamHUE_HCLM1SP3Kelvin, ParamHUE_HCLM1SP4Kelvin, ParamHUE_HCLM1SP5Kelvin,
            ParamHUE_HCLM1SP6Kelvin, ParamHUE_HCLM1SP7Kelvin, ParamHUE_HCLM1SP8Kelvin, ParamHUE_HCLM1SP9Kelvin
        };
        const uint8_t brightnesses[10] = {
            ParamHUE_HCLM1SP0Brightness, ParamHUE_HCLM1SP1Brightness, ParamHUE_HCLM1SP2Brightness,
            ParamHUE_HCLM1SP3Brightness, ParamHUE_HCLM1SP4Brightness, ParamHUE_HCLM1SP5Brightness,
            ParamHUE_HCLM1SP6Brightness, ParamHUE_HCLM1SP7Brightness, ParamHUE_HCLM1SP8Brightness, ParamHUE_HCLM1SP9Brightness
        };
        loadMasterSetpoints(1, times, kelvins, brightnesses);

        #ifdef ParamHUE_HCLM1CurveType
        applyMasterAdvanced(
            1,
            ParamHUE_HCLM1CurveType,
            ParamHUE_HCLM1SlewRate,
            ParamHUE_HCLM1ManualKelvin,
            reinterpret_cast<const char*>(ParamHUE_HCLM1Sunrise),
            reinterpret_cast<const char*>(ParamHUE_HCLM1Sunset),
            static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset),
            static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset));
        #endif
    }
    #endif

    #ifdef ParamHUE_HCLM2SP0Time
    {
        const char* const times[10] = {
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP0Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP1Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP2Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP3Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP4Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP5Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP6Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP7Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP8Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM2SP9Time)
        };
        const uint16_t kelvins[10] = {
            ParamHUE_HCLM2SP0Kelvin, ParamHUE_HCLM2SP1Kelvin, ParamHUE_HCLM2SP2Kelvin,
            ParamHUE_HCLM2SP3Kelvin, ParamHUE_HCLM2SP4Kelvin, ParamHUE_HCLM2SP5Kelvin,
            ParamHUE_HCLM2SP6Kelvin, ParamHUE_HCLM2SP7Kelvin, ParamHUE_HCLM2SP8Kelvin, ParamHUE_HCLM2SP9Kelvin
        };
        const uint8_t brightnesses[10] = {
            ParamHUE_HCLM2SP0Brightness, ParamHUE_HCLM2SP1Brightness, ParamHUE_HCLM2SP2Brightness,
            ParamHUE_HCLM2SP3Brightness, ParamHUE_HCLM2SP4Brightness, ParamHUE_HCLM2SP5Brightness,
            ParamHUE_HCLM2SP6Brightness, ParamHUE_HCLM2SP7Brightness, ParamHUE_HCLM2SP8Brightness, ParamHUE_HCLM2SP9Brightness
        };
        loadMasterSetpoints(2, times, kelvins, brightnesses);

        #ifdef ParamHUE_HCLM2CurveType
        applyMasterAdvanced(
            2,
            ParamHUE_HCLM2CurveType,
            ParamHUE_HCLM2SlewRate,
            ParamHUE_HCLM2ManualKelvin,
            reinterpret_cast<const char*>(ParamHUE_HCLM2Sunrise),
            reinterpret_cast<const char*>(ParamHUE_HCLM2Sunset),
            static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset),
            static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset));
        #endif
    }
    #endif

    #ifdef ParamHUE_HCLM3SP0Time
    {
        const char* const times[10] = {
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP0Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP1Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP2Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP3Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP4Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP5Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP6Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP7Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP8Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM3SP9Time)
        };
        const uint16_t kelvins[10] = {
            ParamHUE_HCLM3SP0Kelvin, ParamHUE_HCLM3SP1Kelvin, ParamHUE_HCLM3SP2Kelvin,
            ParamHUE_HCLM3SP3Kelvin, ParamHUE_HCLM3SP4Kelvin, ParamHUE_HCLM3SP5Kelvin,
            ParamHUE_HCLM3SP6Kelvin, ParamHUE_HCLM3SP7Kelvin, ParamHUE_HCLM3SP8Kelvin, ParamHUE_HCLM3SP9Kelvin
        };
        const uint8_t brightnesses[10] = {
            ParamHUE_HCLM3SP0Brightness, ParamHUE_HCLM3SP1Brightness, ParamHUE_HCLM3SP2Brightness,
            ParamHUE_HCLM3SP3Brightness, ParamHUE_HCLM3SP4Brightness, ParamHUE_HCLM3SP5Brightness,
            ParamHUE_HCLM3SP6Brightness, ParamHUE_HCLM3SP7Brightness, ParamHUE_HCLM3SP8Brightness, ParamHUE_HCLM3SP9Brightness
        };
        loadMasterSetpoints(3, times, kelvins, brightnesses);

        #ifdef ParamHUE_HCLM3CurveType
        applyMasterAdvanced(
            3,
            ParamHUE_HCLM3CurveType,
            ParamHUE_HCLM3SlewRate,
            ParamHUE_HCLM3ManualKelvin,
            reinterpret_cast<const char*>(ParamHUE_HCLM3Sunrise),
            reinterpret_cast<const char*>(ParamHUE_HCLM3Sunset),
            static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset),
            static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset));
        #endif
    }
    #endif

    #ifdef ParamHUE_HCLM4SP0Time
    {
        const char* const times[10] = {
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP0Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP1Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP2Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP3Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP4Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP5Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP6Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP7Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP8Time),
            reinterpret_cast<const char*>(ParamHUE_HCLM4SP9Time)
        };
        const uint16_t kelvins[10] = {
            ParamHUE_HCLM4SP0Kelvin, ParamHUE_HCLM4SP1Kelvin, ParamHUE_HCLM4SP2Kelvin,
            ParamHUE_HCLM4SP3Kelvin, ParamHUE_HCLM4SP4Kelvin, ParamHUE_HCLM4SP5Kelvin,
            ParamHUE_HCLM4SP6Kelvin, ParamHUE_HCLM4SP7Kelvin, ParamHUE_HCLM4SP8Kelvin, ParamHUE_HCLM4SP9Kelvin
        };
        const uint8_t brightnesses[10] = {
            ParamHUE_HCLM4SP0Brightness, ParamHUE_HCLM4SP1Brightness, ParamHUE_HCLM4SP2Brightness,
            ParamHUE_HCLM4SP3Brightness, ParamHUE_HCLM4SP4Brightness, ParamHUE_HCLM4SP5Brightness,
            ParamHUE_HCLM4SP6Brightness, ParamHUE_HCLM4SP7Brightness, ParamHUE_HCLM4SP8Brightness, ParamHUE_HCLM4SP9Brightness
        };
        loadMasterSetpoints(4, times, kelvins, brightnesses);

        #ifdef ParamHUE_HCLM4CurveType
        applyMasterAdvanced(
            4,
            ParamHUE_HCLM4CurveType,
            ParamHUE_HCLM4SlewRate,
            ParamHUE_HCLM4ManualKelvin,
            reinterpret_cast<const char*>(ParamHUE_HCLM4Sunrise),
            reinterpret_cast<const char*>(ParamHUE_HCLM4Sunset),
            static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset),
            static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset));
        #endif
    }
    #endif
    
    HCL::masterManager.setup();
    publishHclMasterValues();
    Serial.println("[HueGatewayModule] HCL setup complete");
}

void HueGatewayModule::checkConnection()
{
    if (_client && _client->isInitialized() && _client->isEventStreamConnected())
    {
        if (!_authPending && _bridgeStatus != BridgeStatus::CONNECTED)
        {
            updateStatus(BridgeStatus::CONNECTED);
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
        if (!_authPending)
        {
            updateStatus(BridgeStatus::CONNECTION_LOST);
        }
        return;
    }

    if (!_authPending && _bridgeStatus != BridgeStatus::CONNECTED)
    {
        updateStatus(BridgeStatus::CONNECTED);
    }
}

void HueGatewayModule::refreshLightStatus()
{
    if (!_client || !_client->isInitialized())
    {
        return;
    }

    unsigned long now = millis();
    bool dueFlags[MAX_LIGHTS] = {false};
    int dueChannels = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] == nullptr)
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
        if (!(syncDir == 2 || syncDir == 3))
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

    Serial.printf("[HueGatewayModule] Polling Hue bridge for %d due channel(s)\n", dueChannels);

    HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);
    Serial.printf("[HueGatewayModule] Polling returned %d light(s)\n", count);
    if (count <= 0)
    {
        if (_bridgeStatus == BridgeStatus::CONNECTED)
        {
            updateStatus(BridgeStatus::CONNECTION_LOST);
        }
        return;
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

        if (_lights[i] == nullptr)
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
        if (!(syncDir == 2 || syncDir == 3))
        {
            continue;
        }

        uint8_t pollIntervalSec = ParamHUE_CHPollInterval;
        if (!fastTrackDue && pollIntervalSec == 0)
        {
            continue;
        }

        unsigned long pollIntervalMs = static_cast<unsigned long>(pollIntervalSec) * 1000UL;
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

        for (int j = 0; j < count; j++)
        {
            if (lights[j].id == _lights[i]->getLightId())
            {
                _lights[i]->updateFromHue(
                    lights[j].on,
                    lights[j].brightness,
                    lights[j].colorTempKelvin,
                    lights[j].red,
                    lights[j].green,
                    lights[j].blue);

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
    }

    _pollCursor = static_cast<uint8_t>((lastProcessedIndex + 1) % MAX_LIGHTS);

    if (dueChannels > processedThisTick)
    {
        Serial.printf("[HueGatewayModule] Poll chunk processed %u/%d due channel(s), remaining queued for next tick\n",
                      static_cast<unsigned>(processedThisTick),
                      dueChannels);
    }
}

// ===== Helper Methods =====

bool HueGatewayModule::hasNetworkConnectivity() const
{
    const IPAddress localIp = WiFi.localIP();
    if (localIp[0] != 0 || localIp[1] != 0 || localIp[2] != 0 || localIp[3] != 0)
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

        const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclLockFallbackMode);
        const uint32_t durationMs = getHclFallbackDurationMs(fallbackMode);
        if (durationMs > 0)
        {
            _hclLockAutoReleaseMs = _hclLockActivatedMs + durationMs;
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            _hclLockActivationDayOfYear = static_cast<int16_t>(timeinfo.tm_yday);
        }

        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL lock enabled (%s), fallback=%s\n",
                          reason ? reason : "n/a",
                          hclFallbackModeToText(fallbackMode));
        }
    }
    else
    {
        _hclLockActivatedMs = 0;
        _hclLockAutoReleaseMs = 0;
        _hclLockActivationDayOfYear = -1;
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

        const HclLockFallbackMode fallbackMode = static_cast<HclLockFallbackMode>(_hclManagerLockFallbackMode[idx]);
        const uint32_t durationMs = getHclFallbackDurationMs(fallbackMode);
        if (durationMs > 0)
        {
            _hclManagerLockAutoReleaseMs[idx] = _hclManagerLockActivatedMs[idx] + durationMs;
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            _hclManagerLockActivationDayOfYear[idx] = static_cast<int16_t>(timeinfo.tm_yday);
        }

        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL manager %u lock enabled (%s), fallback=%s\n",
                          static_cast<unsigned>(managerNumber),
                          reason ? reason : "n/a",
                          hclFallbackModeToText(fallbackMode));
        }
    }
    else
    {
        _hclManagerLockActivatedMs[idx] = 0;
        _hclManagerLockAutoReleaseMs[idx] = 0;
        _hclManagerLockActivationDayOfYear[idx] = -1;
        if (changed)
        {
            Serial.printf("[HueGatewayModule] HCL manager %u lock disabled (%s)\n",
                          static_cast<unsigned>(managerNumber),
                          reason ? reason : "n/a");
        }
    }

    publishHclManagerLockStatus(managerNumber);
}

void HueGatewayModule::evaluateHclLockFallback(const tm* timeinfo, bool hasTime)
{
    if (!_hclLockActive)
    {
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

        if (_hclMasterValuesPublished[index] &&
            _hclLastPublishedBrightness[index] == brightness &&
            _hclLastPublishedKelvin[index] == kelvin)
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
    }
}

String HueGatewayModule::getBridgeIP()
{
    uint8_t mode = ParamHUE_HUEBridgeMode;
    
    if (mode == 0)
    {
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
        std::string ipStr = ParamHUE_HUEBridgeIPStr;
        Serial.printf("[HueGatewayModule] Using manual IP: %s\n", ipStr.c_str());
        return String(ipStr.c_str());
    }
}

void HueGatewayModule::resetDevices()
{
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i])
        {
            delete _lights[i];
            _lights[i] = nullptr;
        }
        _channelLastPollMs[i] = 0;
        _channelFastTrackNextMs[i] = 0;
        _channelFastTrackCooldownUntilMs[i] = 0;
        _channelFastTrackRemaining[i] = 0;
    }

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
    
    HueGatewayLightState lights[MAX_LIGHTS];
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
// Status & LED Management
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
    bool eventStreamStarted = _client->startEventStream();
    Serial.printf("[HueGatewayModule] EventStream initial start: %s\n", eventStreamStarted ? "ok" : "failed (fallback polling active)");

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

    for (int u = 0; u < updateCount; u++)
    {
        bool applied = false;
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            if (_lights[i] == nullptr)
            {
                continue;
            }

            if (_lights[i]->getLightId() != updates[u].lightId)
            {
                continue;
            }

            uint8_t _channelIndex = static_cast<uint8_t>(i);
            uint8_t syncDir = ParamHUE_CHSyncDir;
            if (!(syncDir == 2 || syncDir == 3))
            {
                Serial.printf("[HueGatewayModule] EventStream update ignored by SyncDir on channel %d (SyncDir=%u)\n",
                              i + 1,
                              static_cast<unsigned>(syncDir));
                break;
            }

            bool on = updates[u].hasOn ? updates[u].on : _lights[i]->isOn();
            uint8_t brightness = updates[u].hasBrightness ? updates[u].brightness : _lights[i]->getBrightness();
            uint16_t colorTempKelvin = updates[u].hasColorTemp ? updates[u].colorTempKelvin : _lights[i]->getColorTempKelvin();
            uint8_t red = updates[u].hasColorRgb ? updates[u].red : _lights[i]->getRed();
            uint8_t green = updates[u].hasColorRgb ? updates[u].green : _lights[i]->getGreen();
            uint8_t blue = updates[u].hasColorRgb ? updates[u].blue : _lights[i]->getBlue();

            _lights[i]->updateFromHue(on, brightness, colorTempKelvin, red, green, blue);
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
            Serial.printf("[HueGatewayModule] Event light id not mapped in configured channels: %s\n",
                          updates[u].lightId.c_str());
        }
    }
}

void HueGatewayModule::startPairing()
{
    Serial.println("[HueGatewayModule] ===== Manual commissioning trigger: Pairing start =====");
    if (_bridgeIP.isEmpty())
    {
        _bridgeIP = getBridgeIP();
    }

    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }

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

static String maskKey(const String& key)
{
    if (key.length() == 0)
        return "(none)";
    if (key.length() <= 6)
        return String("***") + key;
    return String("***") + key.substring(key.length() - 6);
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
    html += "<a href='" + hueBaseUri + "/scan'>🔍 Hue-Geräte laden</a>";
    html += "<a href='" + hueBaseUri + "/status'>📊 Status</a>";
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

    if (!self->_client || !self->_initialized)
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
        String html;
        html.reserve(384);
        html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='2'><title>Hue-Geräte laden</title></head><body>";
        html += "<h1>🔄 Hue-Geräte laden</h1><p>Scan läuft... Seite aktualisiert sich automatisch.</p>";
        html += "<a href='" + hueBaseUri + "'>← Zurück</a></body></html>";
        return send_html(req, html, 200);
    }

    const bool noScanYet = (self->_lastWebScanMs == 0);
    const bool stale = (!noScanYet) && ((millis() - self->_lastWebScanMs) > 15000UL);
    const bool retryAfterNoLights = (self->_lastWebScanLightCount <= 0);
    if (noScanYet || stale || retryAfterNoLights)
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

    String html = self->getBridgeScanHTML();
    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebScanText(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    if (!self->_client || !self->_initialized)
        return send_text(req, "Keine aktive Bridge-Verbindung. Bitte zuerst Pairing starten und den Hue-Bridge-Button druecken.\n", 200);

    if (self->_webScanInProgress)
    {
        return send_text(req, "Scan läuft, bitte in 2-3 Sekunden erneut abrufen.\n", 200);
    }

    const bool noScanYet = (self->_lastWebScanMs == 0);
    const bool stale = (!noScanYet) && ((millis() - self->_lastWebScanMs) > 15000UL);
    const bool retryAfterNoLights = (self->_lastWebScanLightCount <= 0);
    if (noScanYet || stale || retryAfterNoLights)
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
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM1Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM1SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM1SunsetOffset);
                    #endif
                    break;
                case 2:
                    #ifdef ParamHUE_HCLM2CurveType
                    curveTypeValue = ParamHUE_HCLM2CurveType;
                    slewRate = ParamHUE_HCLM2SlewRate;
                    manualKelvin = ParamHUE_HCLM2ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM2Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM2SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM2SunsetOffset);
                    #endif
                    break;
                case 3:
                    #ifdef ParamHUE_HCLM3CurveType
                    curveTypeValue = ParamHUE_HCLM3CurveType;
                    slewRate = ParamHUE_HCLM3SlewRate;
                    manualKelvin = ParamHUE_HCLM3ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM3Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM3SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM3SunsetOffset);
                    #endif
                    break;
                case 4:
                    #ifdef ParamHUE_HCLM4CurveType
                    curveTypeValue = ParamHUE_HCLM4CurveType;
                    slewRate = ParamHUE_HCLM4SlewRate;
                    manualKelvin = ParamHUE_HCLM4ManualKelvin;
                    sunrise = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunrise);
                    sunset = reinterpret_cast<const char*>(ParamHUE_HCLM4Sunset);
                    sunriseOffset = static_cast<int16_t>(ParamHUE_HCLM4SunriseOffset);
                    sunsetOffset = static_cast<int16_t>(ParamHUE_HCLM4SunsetOffset);
                    #endif
                    break;
                default:
                    break;
            }

            String curveText = "Fixzeit";
            if (curveTypeValue == 1)
            {
                curveText = "Sonnenstand";
            }
            else if (curveTypeValue == 2)
            {
                curveText = "Manuell";
            }

            html += "<tr><td>HCL M" + String(masterNumber) + " Kurve</td><td>" + curveText + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Steigrate</td><td>" + String(slewRate) + " K/min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Manuell</td><td>" + String(manualKelvin) + " K</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Sonne</td><td>" + sunrise + " / " + sunset + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Offset</td><td>" + String(sunriseOffset) + " / " + String(sunsetOffset) + " min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Angewendet</td><td>" + String(master->getAppliedKelvin()) + " K</td></tr>";
        }
    }

    html += "</table>";
    html += "<br><a href='" + hueBaseUri + "'>← Zurück</a></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebPair(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    self->startPairing();

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(1024);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Kopplung</title>";
    html += "<meta http-equiv='refresh' content='3;url=" + hueBaseUri + "/status'>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += ".card{background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}a{display:inline-block;padding:10px 16px;margin:5px;background:#007bff;color:#fff;text-decoration:none;border-radius:3px;}</style></head><body>";
    html += "<div class='card'><h1>🔗 Kopplung gestartet</h1>";
    html += "<p>Bitte jetzt den Link-Button an der Hue Bridge drücken.</p>";
    html += "<p>Weiterleitung auf Statusseite in 3 Sekunden...</p>";
    html += "<a href='" + hueBaseUri + "/status'>Status jetzt öffnen</a>";
    html += "<a href='" + hueBaseUri + "'>Zurück</a></div></body></html>";

    return send_html(req, html, 200);
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
    if (!_client || !_initialized || !_client->isInitialized())
    {
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
    const String previousHtml = _lastWebScanHtml;
    const String previousText = _lastWebScanText;

    static HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);
    if (count <= 0)
    {
        delay(60);
        count = _client->getLights(lights, MAX_LIGHTS);
    }

    const String hueBaseUri = String(openknxWebUI.getBaseUri()) + "/hue";

    String html;
    html.reserve(2048 + (count > 0 ? (count * 320) : 256));
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue-Geräte von Bridge</title>";
    html += "<style>body{font-family:'Courier New',monospace;margin:40px;background:#1e1e1e;color:#d4d4d4;}";
    html += "h1{color:#4ec9b0;}";
    html += ".light{background:#252526;padding:15px;margin:10px 0;border-left:4px solid #007acc;border-radius:3px;}";
    html += ".light-id{color:#ce9178;font-size:0.9em;word-break:break-all;}";
    html += ".status{color:#b5cea8;}";
    html += ".error{color:#f48771;background:#3c1f1e;padding:15px;border-left:4px solid #f48771;}";
    html += "button{padding:8px 15px;margin:5px;background:#007acc;color:white;border:none;border-radius:3px;cursor:pointer;}";
    html += "button:hover{background:#005a9e;}</style></head><body>";
    html += "<h1>🔍 Hue-Geräte von Bridge</h1>";

    String text;
    text.reserve(256 + (count > 0 ? (count * 180) : 64));
    text = "Open KNX Hue-Geräteabfrage\n";
    text += "---------------------------------\n";

    if (count <= 0)
    {
        if (previousLightCount > 0)
        {
            _lastWebScanLightCount = previousLightCount;
            _lastWebScanHtml = previousHtml;
            _lastWebScanText = previousText;
            Serial.println("[HueGatewayModule] Web scan returned 0 lights, keeping previous successful result");
            return;
        }

        html += "<div class='error'>❌ Keine Leuchten gefunden! Bitte Bridge-Verbindung prüfen.</div>";
        html += "<br><a href='" + hueBaseUri + "'><button>← Zurück</button></a></body></html>";
        text += "Keine Leuchten gefunden. Bitte Bridge-Verbindung prüfen.\n";
        _lastWebScanLightCount = 0;
        _lastWebScanHtml = html;
        _lastWebScanText = text;
        return;
    }

    html += "<p>Es wurden <strong>" + String(count) + "</strong> Leuchten gefunden:</p>";
    for (int i = 0; i < count; i++)
    {
        html += "<div class='light'>";
        html += "<strong>Leuchte " + String(i + 1) + ":</strong> " + String(lights[i].name.c_str()) + "<br>";
        html += "<span class='light-id'>ID: " + String(lights[i].id.c_str()) + "</span><br>";
        html += "<span class='status'>Status: " + String(lights[i].on ? "EIN" : "AUS");
        String roomName = lights[i].room.length() ? lights[i].room : "-";
        String zoneName = lights[i].zone.length() ? lights[i].zone : "-";
        html += " | Helligkeit: " + String(lights[i].brightness) + "/254";
        html += " | Raum: " + roomName + " | Zone: " + zoneName + "</span>";
        html += "</div>";

        text += String(i + 1) + ") " + lights[i].name + "\n";
        text += "    ID: " + lights[i].id + "\n";
        text += "    Status: " + String(lights[i].on ? "EIN" : "AUS");
        text += " | Helligkeit: " + String(lights[i].brightness) + "/254";
        text += " | Raum: " + roomName + " | Zone: " + zoneName + "\n";
    }

    html += "<br><p><strong>Tipp:</strong> Die Leuchten-ID kopieren und in die ETS-Kanalparameter einfügen.</p>";
    html += "<button onclick='location.reload()'>Aktualisieren</button>";
    html += "<a href='" + hueBaseUri + "/scan.txt'><button>TXT herunterladen</button></a>";
    html += "<a href='" + hueBaseUri + "'><button>Zurück</button></a>";
    html += "</body></html>";

    _lastWebScanLightCount = count;
    _lastWebScanHtml = html;
    _lastWebScanText = text;
}


