#include "HueGatewayModule.h"
#include "HueGatewayDiscovery.h"
#include "HueGatewayAuth.h"
#include "HueGatewayClient.h"
#include "Devices/HueGatewayLight.h"

HueGatewayModule::HueGatewayModule()
    : _initialized(false)
    , _lastConnectionCheckMs(0)
    , _lastRefreshTickMs(0)
    , _lastEventStreamRetryMs(0)
    , _client(nullptr)
    , _lightCount(0)
    , _bridgeStatus(BridgeStatus::DISCONNECTED)
    , _ledBlinkTime(0)
    , _bridgeIP("")
    , _authPending(false)
    , _authStartTime(0)
    , _authLastTry(0)
    , _authWindowMs(30000)
    , _lastReconnectTryMs(0)
    , _reconnectBackoffMs(10000)
    , _devicesInitialized(false)
{
    // Light-Array initialisieren
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        _lights[i] = nullptr;
        _channelLastPollMs[i] = 0;
    }
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
    if (getLocalTime(&timeinfo)) {
        uint16_t currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        HCL::masterManager.loop(currentMinutes);
    }
    
    // Update all lights with HCL loop
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] != nullptr)
        {
            _lights[i]->loop();
        }
    }
    
    unsigned long now = millis();
    
    // Verbindung alle 5 Sekunden prüfen
    if (now - _lastConnectionCheckMs > 5000)
    {
        _lastConnectionCheckMs = now;
        checkConnection();
    }

    if (_client && _client->isInitialized() && !_authPending)
    {
        if (!_client->isEventStreamConnected())
        {
            if (now - _lastEventStreamRetryMs >= 10000)
            {
                _lastEventStreamRetryMs = now;
                Serial.printf("[HueGatewayModule] EventStream retry at %lu ms\n", static_cast<unsigned long>(now));
                _client->startEventStream();
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

    if (!_authPending && (_bridgeStatus == BridgeStatus::CONNECTION_LOST || _bridgeStatus == BridgeStatus::BRIDGE_UNREACHABLE || !_client || !_client->isInitialized()))
    {
        if (_bridgeIP.isEmpty())
        {
            _bridgeIP = getBridgeIP();
        }

        bool hasStoredKey = _auth.loadStoredAppKey();
        if (!hasStoredKey)
        {
            _authPending = true;
            _authStartTime = now;
            _authLastTry = 0;
            updateStatus(BridgeStatus::WAIT_FOR_BUTTON);
            Serial.println("[HueGatewayModule] No stored App-Key during reconnect, entering pairing workflow");
            return;
        }

        if (!_bridgeIP.isEmpty() && (now - _lastReconnectTryMs >= _reconnectBackoffMs))
        {
            _lastReconnectTryMs = now;
            Serial.printf("[HueGatewayModule] Reconnect attempt (backoff=%lus)\n", static_cast<unsigned long>(_reconnectBackoffMs / 1000UL));

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
    Serial.printf("[HueGatewayModule] KO %d received\n", koNumber);
    
    if (koNumber == HUE_KoHUEPairingTrigger)
    {
        bool trigger = ko.value(Dpt(1, 17));  // DPT 1.017 Trigger
        if (trigger)
        {
            Serial.println("[HueGatewayModule] Pairing triggered via ETS KO");
            startPairing();
        }
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
        Serial.printf("[HueGatewayModule] KO %d outside Hue channel range\n", koNumber);
        return;
    }

    uint8_t koType = static_cast<uint8_t>((koNumber - HUE_KoBlockOffset) % HUE_KoBlockSize);

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
        Serial.printf("[HueGatewayModule] Channel %d not configured\n", channel);
        return;
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
            uint8_t value = ko.value(Dpt(3, 7));
            _lights[channel]->processKnxDimming(value);
            break;
        }
        case 3:  // Status Switch KO (read-only, no processing)
            break;
        case 4:  // Status Brightness KO (read-only, no processing)
            break;
        case 5:  // ColorTemp KO (TODO)
        {
            uint16_t kelvin = ko.value(Dpt(7, 600));
            _lights[channel]->processKnxColorTemp(kelvin);
            break;
        }
        case 6:  // Status ColorTemp KO (read-only, no processing)
            break;
        case 7:  // ColorRGB KO (TODO)
        {
            uint8_t* rgb = ko.valueRef();
            _lights[channel]->processKnxColorRGB(rgb[0], rgb[1], rgb[2]);
            break;
        }
        case 8:  // Status ColorRGB KO (read-only, no processing)
            break;
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
        
        // Authentication prüfen
        HueGatewayAuth auth;
        if (!auth.authenticateBlocking(bridgeIP.c_str(), 30000))
        {
            Serial.println("ERROR: Authentication failed!");
            Serial.println("Press button on Hue Bridge and retry");
            return true;
        }
        
        // Lichter abrufen
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
    // mDNS Service registrieren für einfachen Zugriff via openknx-bridge.local
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
    
    // Prüfen ob Netzwerk verfügbar (von OFM-Network/WLAN bereitgestellt)
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[HueGatewayModule] ERROR: WiFi not connected!");
        Serial.println("[HueGatewayModule] Network must be initialized by OFM-Network or WLAN module first");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }
    
    Serial.printf("[HueGatewayModule] Network OK - Device IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Bridge IP aus ETS-Parameter lesen
    _bridgeIP = getBridgeIP();
    
    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
        return;
    }
    
    Serial.printf("[HueGatewayModule] Bridge configured: %s\n", _bridgeIP.c_str());

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

    Serial.println("[HueGatewayModule] No stored App-Key found, entering pairing workflow");

    updateStatus(BridgeStatus::WAIT_FOR_BUTTON);
    _authPending = true;
    _authStartTime = millis();
    _authLastTry = 0;
    Serial.println("[HueGatewayModule] Awaiting Hue Bridge link button...");
    Serial.println("[HueGatewayModule] ===== Commissioning: Bridge setup pending pairing =====");
}

void HueGatewayModule::setupDevices()
{
    Serial.println("[HueGatewayModule] Setting up Devices...");
    resetDevices();
    
    if (!_client || !_client->isInitialized())
    {
        Serial.println("[HueGatewayModule] ERROR: HueGatewayClient not ready");
        return;
    }
    
    // Anzahl Kanäle aus ETS lesen
    uint8_t channelCount = ParamHUE_HUEChannelCount;
    Serial.printf("[HueGatewayModule] Configured channels: %d\n", channelCount);
    
    if (channelCount == 0)
    {
        Serial.println("[HueGatewayModule] No channels configured");
        return;
    }
    
    // Alle Lichter von der Bridge abrufen (zum Validieren)
    HueGatewayLightState allLights[MAX_LIGHTS];
    int bridgeLightCount = _client->getLights(allLights, MAX_LIGHTS);
    Serial.printf("[HueGatewayModule] Bridge has %d lights\n", bridgeLightCount);
    
    // Kanäle aus ETS-Parametern laden
    // Bevorzugt UUID-Mapping aus ETS, Fallback auf Index bei leerer UUID
    uint8_t maxChannels = channelCount;
    if (maxChannels > MAX_LIGHTS)
    {
        maxChannels = MAX_LIGHTS;
    }
    if (maxChannels > bridgeLightCount)
    {
        maxChannels = static_cast<uint8_t>(bridgeLightCount);
    }
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

        const HueGatewayLightState* selectedLight = nullptr;
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
                Serial.printf("[HueGatewayModule] Channel %d: Configured UUID not found on bridge: %s\n", ch + 1, configuredUuid.c_str());
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
        
        // HueGatewayLight Instanz erstellen
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
    
    Serial.printf("[HueGatewayModule] Initialized %d lights\n", _lightCount);
    _devicesInitialized = true;
}

void HueGatewayModule::setupHCL()
{
    Serial.println("[HueGatewayModule] Setting up HCL...");
    
    // Check if HCL is enabled
    #ifdef ParamHUE_HUEHCLEnable
    bool hclEnabled = ParamHUE_HUEHCLEnable != 0;
    #else
    bool hclEnabled = false;
    #endif
    
    if (!hclEnabled) {
        Serial.println("[HueGatewayModule] HCL disabled");
        return;
    }
    
    Serial.println("[HueGatewayModule] HCL enabled");
    HCL::masterManager.setEnabled(true);
    
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
    Serial.println("[HueGatewayModule] HCL setup complete");
}

void HueGatewayModule::checkConnection()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        updateStatus(BridgeStatus::BRIDGE_UNREACHABLE);
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

    if (!_client->pingBridgeApiV2())
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
    bool anyChannelDue = false;
    int dueChannels = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] == nullptr)
        {
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
            anyChannelDue = true;
            dueChannels++;
        }
    }

    if (!anyChannelDue)
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

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] == nullptr)
        {
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
        if ((now - _channelLastPollMs[i]) < pollIntervalMs)
        {
            continue;
        }
        _channelLastPollMs[i] = now;

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
                break;
            }
        }
    }
}

// ===== Helper Methods =====

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
        // Manuelle IP
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
    }

    _lightCount = 0;
    _devicesInitialized = false;
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
    updateStatus(BridgeStatus::AUTHENTICATING);

    unsigned long elapsedMs = now - _authStartTime;
    unsigned long remainingMs = (_authWindowMs > elapsedMs) ? (_authWindowMs - elapsedMs) : 0;
    Serial.printf("[HueGatewayModule] Pairing attempt at t=%lus (remaining=%lus)\n",
                  static_cast<unsigned long>(elapsedMs / 1000UL),
                  static_cast<unsigned long>(remainingMs / 1000UL));

    if (_auth.requestAppKeyOnce(_bridgeIP.c_str()))
    {
        _authPending = false;
        Serial.println("[HueGatewayModule] Authentication successful");

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

    switch (_bridgeStatus)
    {
        case BridgeStatus::DISCONNECTED:
            led->blinking(1000);
            break;
        case BridgeStatus::CONNECTING:
            led->blinking(200);
            break;
        case BridgeStatus::WAIT_FOR_BUTTON:
            led->pulsing(1000);
            break;
        case BridgeStatus::AUTHENTICATING:
            led->blinking(200);
            break;
        case BridgeStatus::CONNECTED:
            led->on(true);
            break;
        case BridgeStatus::CONNECTION_LOST:
            led->blinking(500);
            break;
        case BridgeStatus::BRIDGE_UNREACHABLE:
            led->blinking(1500);
            break;
        case BridgeStatus::ERROR:
            led->blinking(100);
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
    WebHandler scanHandler;
    scanHandler.name = "Hue Scan (HTML)";
    scanHandler.uri = "/hue/scan";
    scanHandler.httpd = {
        .uri = "/hue/scan",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebScan,
        .user_ctx = this
    };
    openknxWebUI.addHandler(scanHandler);

    WebHandler scanTextHandler;
    scanTextHandler.name = "Hue Scan (Text)";
    scanTextHandler.uri = "/hue/scan.txt";
    scanTextHandler.isVisible = false;
    scanTextHandler.httpd = {
        .uri = "/hue/scan.txt",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebScanText,
        .user_ctx = this
    };
    openknxWebUI.addHandler(scanTextHandler);

    WebHandler statusHandler;
    statusHandler.name = "Hue Status";
    statusHandler.uri = "/hue/status";
    statusHandler.httpd = {
        .uri = "/hue/status",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebStatus,
        .user_ctx = this
    };
    openknxWebUI.addHandler(statusHandler);

    WebHandler pairHandler;
    pairHandler.name = "Hue Pairing";
    pairHandler.uri = "/hue/pair";
    pairHandler.httpd = {
        .uri = "/hue/pair",
        .method = HTTP_GET,
        .handler = HueGatewayModule::handleWebPair,
        .user_ctx = this
    };
    openknxWebUI.addHandler(pairHandler);

    WebPage rootPage;
    rootPage.uri = "/hue";
    rootPage.name = "Hue Gateway";
    rootPage.handler = HueGatewayModule::pageWebRoot;
    rootPage.arg = this;
    openknxWebUI.addPage(rootPage);

    WebPage scanPage;
    scanPage.uri = "/hue/scan";
    scanPage.name = "Hue Scan";
    scanPage.handler = HueGatewayModule::pageWebScan;
    scanPage.arg = this;
    openknxWebUI.addPage(scanPage);

    WebPage statusPage;
    statusPage.uri = "/hue/status";
    statusPage.name = "Hue Status";
    statusPage.handler = HueGatewayModule::pageWebStatus;
    statusPage.arg = this;
    openknxWebUI.addPage(statusPage);

    WebPage pairPage;
    pairPage.uri = "/hue/pair";
    pairPage.name = "Hue Pairing";
    pairPage.handler = HueGatewayModule::pageWebPair;
    pairPage.arg = this;
    openknxWebUI.addPage(pairPage);

    Serial.printf("[HueGatewayModule] WebUI pages registered at %s\n", openknxWebUI.getBaseUri());
}

esp_err_t HueGatewayModule::handleWebRoot(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>OpenKNX Hue Bridge Module</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "h1{color:#333;}.card{background:white;padding:20px;margin:10px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
    html += "a{display:inline-block;padding:10px 20px;margin:5px;background:#007bff;color:white;text-decoration:none;border-radius:3px;}";
    html += "a:hover{background:#0056b3;}</style></head><body>";
    html += "<h1>🏠 OpenKNX Hue Bridge Module</h1>";
    html += "<div class='card'><h2>Funktionen</h2>";
    html += "<a href='/hue/scan'>🔍 Bridge Scannen</a>";
    html += "<a href='/hue/pair'>🔗 Pairing starten</a>";
    html += "<a href='/hue/status'>📊 Status</a>";
    html += "</div>";
    html += "<div class='card'><h3>Info</h3>";
    html += "<p><strong>Device IP:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>Module Version:</strong> " + String(self->version().c_str()) + "</p>";
    if (self->_bridgeStatus == BridgeStatus::WAIT_FOR_BUTTON)
    {
        html += "<p><strong>Auth:</strong> Waiting for Hue Bridge button press</p>";
    }
    else if (self->_bridgeStatus == BridgeStatus::CONNECTED)
    {
        html += "<p><strong>Auth:</strong> Connected</p>";
    }
    else
    {
        html += "<p><strong>Auth:</strong> Not connected</p>";
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
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Scan Error</title></head><body>";
        html += "<h1>❌ Error</h1><p>Module not initialized. Check bridge authentication.</p>";
        html += "<a href='/hue'>← Back</a></body></html>";
        return send_html(req, html, 500);
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
        return send_text(req, "Module not initialized. Check bridge authentication.\n", 500);

    String text = self->getBridgeScanText();
    return send_text(req, text, 200);
}

esp_err_t HueGatewayModule::handleWebStatus(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue Status</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "table{width:100%;border-collapse:collapse;background:white;border-radius:5px;overflow:hidden;}";
    html += "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "th{background:#007bff;color:white;}</style></head><body>";
    html += "<h1>📊 Module Status</h1>";
    html += "<table><tr><th>Parameter</th><th>Value</th></tr>";
    html += "<tr><td>Initialized</td><td>" + String(self->_initialized ? "✅ Yes" : "❌ No") + "</td></tr>";
    html += "<tr><td>Device IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><td>Active Lights</td><td>" + String(self->_lightCount) + " / " + String(MAX_LIGHTS) + "</td></tr>";
    html += "<tr><td>WiFi RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    String bridgeStatusText = "Unknown";
    switch (self->_bridgeStatus)
    {
        case BridgeStatus::DISCONNECTED: bridgeStatusText = "Disconnected"; break;
        case BridgeStatus::CONNECTING: bridgeStatusText = "Connecting"; break;
        case BridgeStatus::WAIT_FOR_BUTTON: bridgeStatusText = "Waiting for button"; break;
        case BridgeStatus::AUTHENTICATING: bridgeStatusText = "Authenticating"; break;
        case BridgeStatus::CONNECTED: bridgeStatusText = "Connected"; break;
        case BridgeStatus::CONNECTION_LOST: bridgeStatusText = "Connection lost"; break;
        case BridgeStatus::BRIDGE_UNREACHABLE: bridgeStatusText = "Bridge unreachable"; break;
        case BridgeStatus::ERROR: bridgeStatusText = "Error"; break;
    }
    html += "<tr><td>Bridge Status</td><td>" + bridgeStatusText + "</td></tr>";
    html += "<tr><td>App-Key</td><td>" + maskKey(self->_auth.getAppKey()) + "</td></tr>";
    html += "<tr><td>Client-Key</td><td>" + maskKey(self->_auth.getClientKey()) + "</td></tr>";

    #ifdef ParamHUE_HUEHCLEnable
    const bool hclEnabled = (ParamHUE_HUEHCLEnable != 0);
    html += "<tr><td>HCL Enabled</td><td>" + String(hclEnabled ? "Yes" : "No") + "</td></tr>";
    #else
    const bool hclEnabled = false;
    html += "<tr><td>HCL Enabled</td><td>No</td></tr>";
    #endif

    if (hclEnabled)
    {
        #ifdef ParamHUE_HUEHCLMasterCount
        const uint8_t masterCount = ParamHUE_HUEHCLMasterCount;
        html += "<tr><td>HCL Masters</td><td>" + String(masterCount) + "</td></tr>";
        #else
        const uint8_t masterCount = 0;
        html += "<tr><td>HCL Masters</td><td>0</td></tr>";
        #endif

        #ifdef ParamHUE_HUEHCLUpdateInterval
        html += "<tr><td>HCL Update Interval</td><td>" + String(ParamHUE_HUEHCLUpdateInterval) + " s</td></tr>";
        #endif

        #ifdef ParamHUE_HUEHCLFadeDuration
        html += "<tr><td>HCL Fade Duration</td><td>" + String(ParamHUE_HUEHCLFadeDuration) + " s</td></tr>";
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
            html += "<tr><td>HCL M" + String(masterNumber) + " Current</td><td>" +
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

            String curveText = "FixedTime";
            if (curveTypeValue == 1)
            {
                curveText = "SunPosition";
            }
            else if (curveTypeValue == 2)
            {
                curveText = "Manual";
            }

            html += "<tr><td>HCL M" + String(masterNumber) + " Curve</td><td>" + curveText + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Slew</td><td>" + String(slewRate) + " K/min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Manual</td><td>" + String(manualKelvin) + " K</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Sun</td><td>" + sunrise + " / " + sunset + "</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Offset</td><td>" + String(sunriseOffset) + " / " + String(sunsetOffset) + " min</td></tr>";
            html += "<tr><td>HCL M" + String(masterNumber) + " Applied</td><td>" + String(master->getAppliedKelvin()) + " K</td></tr>";
        }
    }

    html += "</table>";
    html += "<br><a href='/hue'>← Back</a></body></html>";

    return send_html(req, html, 200);
}

esp_err_t HueGatewayModule::handleWebPair(httpd_req_t* req)
{
    HueGatewayModule* self = static_cast<HueGatewayModule*>(req->user_ctx);
    if (self == nullptr)
        return httpd_resp_send_500(req);

    self->startPairing();

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue Pairing</title>";
    html += "<meta http-equiv='refresh' content='3;url=/hue/status'>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += ".card{background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}a{display:inline-block;padding:10px 16px;margin:5px;background:#007bff;color:#fff;text-decoration:none;border-radius:3px;}</style></head><body>";
    html += "<div class='card'><h1>🔗 Pairing gestartet</h1>";
    html += "<p>Bitte jetzt den Link-Button an der Hue Bridge drücken.</p>";
    html += "<p>Weiterleitung auf Statusseite in 3 Sekunden...</p>";
    html += "<a href='/hue/status'>Status jetzt öffnen</a>";
    html += "<a href='/hue'>Zurück</a></div></body></html>";

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
    HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue Bridge Scan</title>";
    html += "<style>body{font-family:'Courier New',monospace;margin:40px;background:#1e1e1e;color:#d4d4d4;}";
    html += "h1{color:#4ec9b0;}";
    html += ".light{background:#252526;padding:15px;margin:10px 0;border-left:4px solid #007acc;border-radius:3px;}";
    html += ".light-id{color:#ce9178;font-size:0.9em;word-break:break-all;}";
    html += ".status{color:#b5cea8;}";
    html += ".error{color:#f48771;background:#3c1f1e;padding:15px;border-left:4px solid #f48771;}";
    html += "button{padding:8px 15px;margin:5px;background:#007acc;color:white;border:none;border-radius:3px;cursor:pointer;}";
    html += "button:hover{background:#005a9e;}</style></head><body>";
    html += "<h1>🔍 Hue Bridge Scan Results</h1>";
    
    if (count <= 0)
    {
        html += "<div class='error'>❌ No lights found! Check bridge connection.</div>";
        html += "<br><a href='/hue'><button>← Back</button></a></body></html>";
        return html;
    }
    
    html += "<p>Found <strong>" + String(count) + "</strong> lights:</p>";
    
    for (int i = 0; i < count; i++)
    {
        html += "<div class='light'>";
        html += "<strong>Light " + String(i+1) + ":</strong> " + String(lights[i].name.c_str()) + "<br>";
        html += "<span class='light-id'>ID: " + String(lights[i].id.c_str()) + "</span><br>";
        html += "<span class='status'>Status: " + String(lights[i].on ? "ON" : "OFF");
        String roomName = lights[i].room.length() ? lights[i].room : "-";
        String zoneName = lights[i].zone.length() ? lights[i].zone : "-";
        html += " | Brightness: " + String(lights[i].brightness) + "/254";
        html += " | Room: " + roomName + " | Zone: " + zoneName + "</span>";
        html += "</div>";
    }
    
    html += "<br><p><strong>Tip:</strong> Copy the Light ID and paste it into ETS channel parameters.</p>";
    html += "<button onclick='location.reload()'>Refresh</button>";
    html += "<a href='/hue/scan.txt'><button>Download TXT</button></a>";
    html += "<a href='/hue'><button>Back</button></a>";
    html += "</body></html>";
    
    return html;
}

String HueGatewayModule::getBridgeScanText()
{
    HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);

    String text = "OpenKNX Hue Bridge Scan\n";
    text += "---------------------------------\n";

    if (count <= 0)
    {
        text += "No lights found. Check bridge connection.\n";
        return text;
    }

    for (int i = 0; i < count; i++)
    {
        String roomName = lights[i].room.length() ? lights[i].room : "-";
        String zoneName = lights[i].zone.length() ? lights[i].zone : "-";
        text += String(i + 1) + ") " + lights[i].name + "\n";
        text += "    ID: " + lights[i].id + "\n";
        text += "    Status: " + String(lights[i].on ? "ON" : "OFF");
        text += " | Brightness: " + String(lights[i].brightness) + "/254";
        text += " | Room: " + roomName + " | Zone: " + zoneName + "\n";
    }

    return text;
}


