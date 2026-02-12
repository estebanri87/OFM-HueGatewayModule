#include "HueGatewayModule.h"
#include "HueGatewayDiscovery.h"
#include "HueGatewayAuth.h"
#include "HueGatewayClient.h"
#include "Devices/HueGatewayLight.h"

HueGatewayModule::HueGatewayModule()
    : _initialized(false)
    , _lastLoop(0)
    , _client(nullptr)
    , _lightCount(0)
    , _bridgeStatus(BridgeStatus::DISCONNECTED)
    , _ledBlinkTime(0)
    , _bridgeIP("")
    , _authPending(false)
    , _authStartTime(0)
    , _authLastTry(0)
    , _authWindowMs(30000)
    , _devicesInitialized(false)
{
    // Light-Array initialisieren
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        _lights[i] = nullptr;
    }
}

HueGatewayModule::~HueGatewayModule()
{
    // Cleanup Lights
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i])
        {
            delete _lights[i];
            _lights[i] = nullptr;
        }
    }
    
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
    
    // Alle 5 Sekunden Status prüfen
    if (now - _lastLoop > 5000)
    {
        _lastLoop = now;
        checkConnection();
        refreshLightStatus();
    }
    
    // TODO: Event Stream handling
    // TODO: Status updates
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

    // Check if it's the Scan Trigger KO
    if (koNumber == KO_SCAN_TRIGGER)
    {
        bool trigger = ko.value(Dpt(1, 17));  // DPT 1.017 Trigger
        if (trigger)
        {
            Serial.println("\n[HueGatewayModule] Bridge scan triggered via ETS Button/KO");
            performBridgeScan();
        }
        return;
    }
    
    // KO an entsprechendes Light weiterleiten
    // Neue Struktur pro Kanal (9 KOs):
    // KO 0: Switch (DPT 1.001)
    // KO 1: Brightness absolut (DPT 5.001)
    // KO 2: Dimming relativ (DPT 3.007)
    // KO 3: Status Switch (DPT 1.001)
    // KO 4: Status Brightness (DPT 5.001)
    // KO 5: ColorTemp (DPT 7.600)
    // KO 6: Status ColorTemp (DPT 7.600)
    // KO 7: ColorRGB (DPT 232.600)
    // KO 8: Status ColorRGB (DPT 232.600)
    
    if (koNumber < KO_CHANNELS_START)
    {
        Serial.printf("[HueGatewayModule] KO %d below channel range\n", koNumber);
        return;
    }
    
    uint16_t channelOffset = koNumber - KO_CHANNELS_START;
    uint8_t channel = channelOffset / 9;  // 9 KOs per channel
    uint8_t koType = channelOffset % 9;   // 0-8 siehe oben
    
    if (channel >= MAX_LIGHTS || _lights[channel] == nullptr)
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
            break;
        case 6:  // Status ColorTemp KO (read-only, no processing)
            break;
        case 7:  // ColorRGB KO (TODO)
            break;
        case 8:  // Status ColorRGB KO (read-only, no processing)
            break;
    }
}

bool HueGatewayModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (cmd == "hue")
    {
        openknx.console.printHelpLine("hue scan", "Scan for Hue Bridge and list all lights");
        openknx.console.printHelpLine("hue status", "Show current module status");
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
    Serial.println("[HueGatewayModule] Setting up Bridge connection...");
    
    // Prüfen ob Netzwerk verfügbar (von OFM-Network/WLAN bereitgestellt)
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[HueGatewayModule] ERROR: WiFi not connected!");
        Serial.println("[HueGatewayModule] Network must be initialized by OFM-Network or WLAN module first");
        return;
    }
    
    Serial.printf("[HueGatewayModule] Network OK - IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Bridge IP aus ETS-Parameter lesen
    _bridgeIP = getBridgeIP();
    
    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::DISCONNECTED);
        return;
    }
    
    Serial.printf("[HueGatewayModule] Bridge configured: %s\n", _bridgeIP.c_str());

    if (ParamHUE_HUEResetAuth)
    {
        _auth.clearAppKey();
    }

    uint8_t pairingWindowSec = ParamHUE_HUEPairingWindow;
    if (pairingWindowSec < 5)
    {
        pairingWindowSec = 30;
    }
    _authWindowMs = static_cast<unsigned long>(pairingWindowSec) * 1000UL;
    
    // Authentication (non-blocking if no stored key)
    if (_auth.loadStoredAppKey())
    {
        if (initClientWithAppKey())
        {
            updateStatus(BridgeStatus::CONNECTED);
            Serial.println("[HueGatewayModule] HueGatewayClient ready");
        }
        else
        {
            updateStatus(BridgeStatus::DISCONNECTED);
        }
        return;
    }

    updateStatus(BridgeStatus::WAIT_FOR_BUTTON);
    _authPending = true;
    _authStartTime = millis();
    _authLastTry = 0;
    Serial.println("[HueGatewayModule] Awaiting Hue Bridge link button...");
}

void HueGatewayModule::setupDevices()
{
    Serial.println("[HueGatewayModule] Setting up Devices...");

    if (_devicesInitialized)
    {
        return;
    }
    
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
    for (uint8_t ch = 0; ch < channelCount && ch < MAX_LIGHTS; ch++)
    {
        // Parameter für diesen Kanal lesen
        uint16_t paramBase = getChannelParamIndex(ch, 0);
        
        bool enabled = knx.paramByte(paramBase + 0) & 0x01; // HUE_Ch%C%_Enabled
        
        if (!enabled)
        {
            Serial.printf("[HueGatewayModule] Channel %d: Disabled\n", ch);
            continue;
        }
        
        // Light ID lesen (String-Parameter)
        const char* lightId = (const char*)knx.paramData(paramBase + 1); // HUE_Ch%C%_LightId
        const char* name = (const char*)knx.paramData(paramBase + 2);    // HUE_Ch%C%_Name
        
        if (strlen(lightId) == 0)
        {
            Serial.printf("[HueGatewayModule] Channel %d: No Light ID configured\n", ch);
            continue;
        }
        
        // KO-Nummern berechnen (9 KOs pro Kanal mit KoBlockSize=9)
        uint16_t koBase = KO_CHANNELS_START + (ch * 9);
        uint16_t koSwitch = koBase + 0;
        uint16_t koBrightness = koBase + 1;
        uint16_t koDimming = koBase + 2;
        uint16_t koStatusSwitch = koBase + 3;
        uint16_t koStatusBrightness = koBase + 4;
        // KO+5/6: ColorTemp (bei Typ 2/3)
        // KO+7/8: ColorRGB (bei Typ 3)
        
        // HueGatewayLight Instanz erstellen
        _lights[ch] = new HueGatewayLight(String(lightId), String(name), _client);
        _lights[ch]->begin(koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        // HCL Master Zuordnung lesen (wenn Parameter verfügbar)
        #ifdef ParamHUE_CH1HCLMaster
        // TODO: Dies muss angepasst werden wenn die Parameter-Indizes verfügbar sind
        // Für jetzt: Placeholder - wird ignoriert bis XML komplett ist
        uint8_t hclMaster = 0;  // 0 = kein HCL, 1-4 = Master Nummer
        _lights[ch]->setHCLMaster(hclMaster);
        #endif
        
        Serial.printf("[HueGatewayModule] Channel %d: %s (%s) -> KO %d/%d/%d/%d/%d\n",
                      ch, name, lightId, koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
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
    
    // Load HCL Master 1 setpoints from ETS
    #ifdef ParamHUE_HCLM1SP0Time
    HCL::Master* master1 = HCL::masterManager.getMaster(1);
    if (master1) {
        Serial.println("[HueGatewayModule] Loading HCL Master 1 setpoints...");
        
        // Setpoint 0
        const char* time0 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP0Time);
        uint16_t kelvin0 = ParamHUE_HCLM1SP0Kelvin;
        uint8_t brightness0 = ParamHUE_HCLM1SP0Brightness;
        uint16_t minutes0 = HCL::Setpoint::parseTime(time0);
        if (minutes0 != 0xFFFF) {
            master1->setSetpoint(0, HCL::Setpoint(minutes0, kelvin0, brightness0));
            Serial.printf("  SP1: %s (%dmin) -> %dK, %d%%\n", time0, minutes0, kelvin0, brightness0);
        }
        
        // Setpoint 1
        const char* time1 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP1Time);
        uint16_t kelvin1 = ParamHUE_HCLM1SP1Kelvin;
        uint8_t brightness1 = ParamHUE_HCLM1SP1Brightness;
        uint16_t minutes1 = HCL::Setpoint::parseTime(time1);
        if (minutes1 != 0xFFFF) {
            master1->setSetpoint(1, HCL::Setpoint(minutes1, kelvin1, brightness1));
            Serial.printf("  SP2: %s (%dmin) -> %dK, %d%%\n", time1, minutes1, kelvin1, brightness1);
        }
        
        // Setpoint 2
        const char* time2 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP2Time);
        uint16_t kelvin2 = ParamHUE_HCLM1SP2Kelvin;
        uint8_t brightness2 = ParamHUE_HCLM1SP2Brightness;
        uint16_t minutes2 = HCL::Setpoint::parseTime(time2);
        if (minutes2 != 0xFFFF) {
            master1->setSetpoint(2, HCL::Setpoint(minutes2, kelvin2, brightness2));
            Serial.printf("  SP3: %s (%dmin) -> %dK, %d%%\n", time2, minutes2, kelvin2, brightness2);
        }
        
        // Setpoint 3
        const char* time3 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP3Time);
        uint16_t kelvin3 = ParamHUE_HCLM1SP3Kelvin;
        uint8_t brightness3 = ParamHUE_HCLM1SP3Brightness;
        uint16_t minutes3 = HCL::Setpoint::parseTime(time3);
        if (minutes3 != 0xFFFF) {
            master1->setSetpoint(3, HCL::Setpoint(minutes3, kelvin3, brightness3));
            Serial.printf("  SP4: %s (%dmin) -> %dK, %d%%\n", time3, minutes3, kelvin3, brightness3);
        }
        
        // Setpoint 4
        const char* time4 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP4Time);
        uint16_t kelvin4 = ParamHUE_HCLM1SP4Kelvin;
        uint8_t brightness4 = ParamHUE_HCLM1SP4Brightness;
        uint16_t minutes4 = HCL::Setpoint::parseTime(time4);
        if (minutes4 != 0xFFFF) {
            master1->setSetpoint(4, HCL::Setpoint(minutes4, kelvin4, brightness4));
            Serial.printf("  SP5: %s (%dmin) -> %dK, %d%%\n", time4, minutes4, kelvin4, brightness4);
        }
        
        // Setpoint 5
        const char* time5 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP5Time);
        uint16_t kelvin5 = ParamHUE_HCLM1SP5Kelvin;
        uint8_t brightness5 = ParamHUE_HCLM1SP5Brightness;
        uint16_t minutes5 = HCL::Setpoint::parseTime(time5);
        if (minutes5 != 0xFFFF) {
            master1->setSetpoint(5, HCL::Setpoint(minutes5, kelvin5, brightness5));
            Serial.printf("  SP6: %s (%dmin) -> %dK, %d%%\n", time5, minutes5, kelvin5, brightness5);
        }
        
        // Setpoint 6
        const char* time6 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP6Time);
        uint16_t kelvin6 = ParamHUE_HCLM1SP6Kelvin;
        uint8_t brightness6 = ParamHUE_HCLM1SP6Brightness;
        uint16_t minutes6 = HCL::Setpoint::parseTime(time6);
        if (minutes6 != 0xFFFF) {
            master1->setSetpoint(6, HCL::Setpoint(minutes6, kelvin6, brightness6));
            Serial.printf("  SP7: %s (%dmin) -> %dK, %d%%\n", time6, minutes6, kelvin6, brightness6);
        }
        
        // Setpoint 7
        const char* time7 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP7Time);
        uint16_t kelvin7 = ParamHUE_HCLM1SP7Kelvin;
        uint8_t brightness7 = ParamHUE_HCLM1SP7Brightness;
        uint16_t minutes7 = HCL::Setpoint::parseTime(time7);
        if (minutes7 != 0xFFFF) {
            master1->setSetpoint(7, HCL::Setpoint(minutes7, kelvin7, brightness7));
            Serial.printf("  SP8: %s (%dmin) -> %dK, %d%%\n", time7, minutes7, kelvin7, brightness7);
        }
        
        // Setpoint 8
        const char* time8 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP8Time);
        uint16_t kelvin8 = ParamHUE_HCLM1SP8Kelvin;
        uint8_t brightness8 = ParamHUE_HCLM1SP8Brightness;
        uint16_t minutes8 = HCL::Setpoint::parseTime(time8);
        if (minutes8 != 0xFFFF) {
            master1->setSetpoint(8, HCL::Setpoint(minutes8, kelvin8, brightness8));
            Serial.printf("  SP9: %s (%dmin) -> %dK, %d%%\n", time8, minutes8, kelvin8, brightness8);
        }
        
        // Setpoint 9
        const char* time9 = reinterpret_cast<const char*>(ParamHUE_HCLM1SP9Time);
        uint16_t kelvin9 = ParamHUE_HCLM1SP9Kelvin;
        uint8_t brightness9 = ParamHUE_HCLM1SP9Brightness;
        uint16_t minutes9 = HCL::Setpoint::parseTime(time9);
        if (minutes9 != 0xFFFF) {
            master1->setSetpoint(9, HCL::Setpoint(minutes9, kelvin9, brightness9));
            Serial.printf("  SP10: %s (%dmin) -> %dK, %d%%\n", time9, minutes9, kelvin9, brightness9);
        }
        
        // Sort setpoints by time
        master1->sortSetpoints();
        Serial.printf("[HueGatewayModule] HCL Master 1 loaded with %d valid setpoints\n", master1->getValidSetpointCount());
    }
    #endif
    
    // Load HCL Master 2 setpoints from ETS
    #ifdef ParamHUE_HCLM2SP0Time
    HCL::Master* master2 = HCL::masterManager.getMaster(2);
    if (master2) {
        Serial.println("[HueGatewayModule] Loading HCL Master 2 setpoints...");
        
        // Setpoints 0-9 for Master 2
        const char* times[] = {
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
        uint16_t kelvins[] = {ParamHUE_HCLM2SP0Kelvin, ParamHUE_HCLM2SP1Kelvin, ParamHUE_HCLM2SP2Kelvin,
                              ParamHUE_HCLM2SP3Kelvin, ParamHUE_HCLM2SP4Kelvin, ParamHUE_HCLM2SP5Kelvin,
                              ParamHUE_HCLM2SP6Kelvin, ParamHUE_HCLM2SP7Kelvin, ParamHUE_HCLM2SP8Kelvin, ParamHUE_HCLM2SP9Kelvin};
        uint8_t brightnesses[] = {ParamHUE_HCLM2SP0Brightness, ParamHUE_HCLM2SP1Brightness, ParamHUE_HCLM2SP2Brightness,
                                  ParamHUE_HCLM2SP3Brightness, ParamHUE_HCLM2SP4Brightness, ParamHUE_HCLM2SP5Brightness,
                                  ParamHUE_HCLM2SP6Brightness, ParamHUE_HCLM2SP7Brightness, ParamHUE_HCLM2SP8Brightness, ParamHUE_HCLM2SP9Brightness};
        
        for (int i = 0; i < 10; i++) {
            uint16_t minutes = HCL::Setpoint::parseTime(times[i]);
            if (minutes != 0xFFFF) {
                master2->setSetpoint(i, HCL::Setpoint(minutes, kelvins[i], brightnesses[i]));
                Serial.printf("  SP%d: %s (%dmin) -> %dK, %d%%\n", i+1, times[i], minutes, kelvins[i], brightnesses[i]);
            }
        }
        
        master2->sortSetpoints();
        Serial.printf("[HueGatewayModule] HCL Master 2 loaded with %d valid setpoints\n", master2->getValidSetpointCount());
    }
    #endif
    
    // Load HCL Master 3 setpoints from ETS
    #ifdef ParamHUE_HCLM3SP0Time
    HCL::Master* master3 = HCL::masterManager.getMaster(3);
    if (master3) {
        Serial.println("[HueGatewayModule] Loading HCL Master 3 setpoints...");
        
        const char* times[] = {
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
        uint16_t kelvins[] = {ParamHUE_HCLM3SP0Kelvin, ParamHUE_HCLM3SP1Kelvin, ParamHUE_HCLM3SP2Kelvin,
                              ParamHUE_HCLM3SP3Kelvin, ParamHUE_HCLM3SP4Kelvin, ParamHUE_HCLM3SP5Kelvin,
                              ParamHUE_HCLM3SP6Kelvin, ParamHUE_HCLM3SP7Kelvin, ParamHUE_HCLM3SP8Kelvin, ParamHUE_HCLM3SP9Kelvin};
        uint8_t brightnesses[] = {ParamHUE_HCLM3SP0Brightness, ParamHUE_HCLM3SP1Brightness, ParamHUE_HCLM3SP2Brightness,
                                  ParamHUE_HCLM3SP3Brightness, ParamHUE_HCLM3SP4Brightness, ParamHUE_HCLM3SP5Brightness,
                                  ParamHUE_HCLM3SP6Brightness, ParamHUE_HCLM3SP7Brightness, ParamHUE_HCLM3SP8Brightness, ParamHUE_HCLM3SP9Brightness};
        
        for (int i = 0; i < 10; i++) {
            uint16_t minutes = HCL::Setpoint::parseTime(times[i]);
            if (minutes != 0xFFFF) {
                master3->setSetpoint(i, HCL::Setpoint(minutes, kelvins[i], brightnesses[i]));
                Serial.printf("  SP%d: %s (%dmin) -> %dK, %d%%\n", i+1, times[i], minutes, kelvins[i], brightnesses[i]);
            }
        }
        
        master3->sortSetpoints();
        Serial.printf("[HueGatewayModule] HCL Master 3 loaded with %d valid setpoints\n", master3->getValidSetpointCount());
    }
    #endif
    
    // Load HCL Master 4 setpoints from ETS
    #ifdef ParamHUE_HCLM4SP0Time
    HCL::Master* master4 = HCL::masterManager.getMaster(4);
    if (master4) {
        Serial.println("[HueGatewayModule] Loading HCL Master 4 setpoints...");
        
        const char* times[] = {
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
        uint16_t kelvins[] = {ParamHUE_HCLM4SP0Kelvin, ParamHUE_HCLM4SP1Kelvin, ParamHUE_HCLM4SP2Kelvin,
                              ParamHUE_HCLM4SP3Kelvin, ParamHUE_HCLM4SP4Kelvin, ParamHUE_HCLM4SP5Kelvin,
                              ParamHUE_HCLM4SP6Kelvin, ParamHUE_HCLM4SP7Kelvin, ParamHUE_HCLM4SP8Kelvin, ParamHUE_HCLM4SP9Kelvin};
        uint8_t brightnesses[] = {ParamHUE_HCLM4SP0Brightness, ParamHUE_HCLM4SP1Brightness, ParamHUE_HCLM4SP2Brightness,
                                  ParamHUE_HCLM4SP3Brightness, ParamHUE_HCLM4SP4Brightness, ParamHUE_HCLM4SP5Brightness,
                                  ParamHUE_HCLM4SP6Brightness, ParamHUE_HCLM4SP7Brightness, ParamHUE_HCLM4SP8Brightness, ParamHUE_HCLM4SP9Brightness};
        
        for (int i = 0; i < 10; i++) {
            uint16_t minutes = HCL::Setpoint::parseTime(times[i]);
            if (minutes != 0xFFFF) {
                master4->setSetpoint(i, HCL::Setpoint(minutes, kelvins[i], brightnesses[i]));
                Serial.printf("  SP%d: %s (%dmin) -> %dK, %d%%\n", i+1, times[i], minutes, kelvins[i], brightnesses[i]);
            }
        }
        
        master4->sortSetpoints();
        Serial.printf("[HueGatewayModule] HCL Master 4 loaded with %d valid setpoints\n", master4->getValidSetpointCount());
    }
    #endif
    
    HCL::masterManager.setup();
    Serial.println("[HueGatewayModule] HCL setup complete");
}

void HueGatewayModule::checkConnection()
{
    // TODO: Bridge Verbindung prüfen
    // TODO: Bei Offline: Reconnect versuchen
    // Serial.println("[HueGatewayModule] Connection check (not implemented)");
}

void HueGatewayModule::refreshLightStatus()
{
    if (!_client || !_client->isInitialized())
    {
        return;
    }

    HueGatewayLightState lights[MAX_LIGHTS];
    int count = _client->getLights(lights, MAX_LIGHTS);
    if (count <= 0)
    {
        return;
    }

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (_lights[i] == nullptr)
        {
            continue;
        }

        for (int j = 0; j < count; j++)
        {
            if (lights[j].id == _lights[i]->getLightId())
            {
                _lights[i]->updateFromHue(lights[j].on, lights[j].brightness);
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

uint16_t HueGatewayModule::getChannelParamIndex(uint8_t channel, uint16_t paramOffset)
{
    // OpenKNXproducer berechnet: BlockOffset + (channel * BlockSize) + paramOffset
    // Wir müssen die tatsächlichen Offsets aus knxprod.h nutzen
    // Vereinfachte Berechnung für 20 Kanäle
    
    // Base Parameter Block Start (nach General Settings)
    const uint16_t HUE_PARAM_BASE = 1000; // Placeholder - wird von knxprod.h generiert
    const uint16_t HUE_PARAM_BLOCK_SIZE = 10; // Pro Kanal: Enabled + LightId + Name + Settings
    
    return HUE_PARAM_BASE + (channel * HUE_PARAM_BLOCK_SIZE) + paramOffset;
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
        updateStatus(BridgeStatus::DISCONNECTED);
        Serial.println("[HueGatewayModule] Authentication timeout - button not pressed");
        return;
    }

    if (now - _authLastTry < 1000)
    {
        return;
    }

    _authLastTry = now;
    updateStatus(BridgeStatus::AUTHENTICATING);

    if (_auth.requestAppKeyOnce(_bridgeIP.c_str()))
    {
        _authPending = false;
        Serial.println("[HueGatewayModule] Authentication successful");

        if (initClientWithAppKey())
        {
            updateStatus(BridgeStatus::CONNECTED);
            setupDevices();
        }
        else
        {
            updateStatus(BridgeStatus::DISCONNECTED);
        }
    }
}

bool HueGatewayModule::initClientWithAppKey()
{
    if (_client)
    {
        delete _client;
        _client = nullptr;
    }

    _client = new HueGatewayClient();
    if (!_client->begin(_bridgeIP, _auth.getAppKey()))
    {
        Serial.println("[HueGatewayModule] ERROR: HueGatewayClient init failed!");
        delete _client;
        _client = nullptr;
        return false;
    }

    return true;
}

void HueGatewayModule::startPairing()
{
    if (_bridgeIP.isEmpty())
    {
        _bridgeIP = getBridgeIP();
    }

    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueGatewayModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::DISCONNECTED);
        return;
    }

    _auth.clearAppKey();
    _authPending = true;
    _authStartTime = millis();
    _authLastTry = 0;
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
        html += "<a href='/'>← Back</a></body></html>";
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
    }
    html += "<tr><td>Bridge Status</td><td>" + bridgeStatusText + "</td></tr>";
    html += "<tr><td>App-Key</td><td>" + maskKey(self->_auth.getAppKey()) + "</td></tr>";
    html += "<tr><td>Client-Key</td><td>" + maskKey(self->_auth.getClientKey()) + "</td></tr>";
    html += "</table>";
    html += "<br><a href='/'>← Back</a></body></html>";

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
        html += "<br><a href='/'><button>← Back</button></a></body></html>";
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
    html += "<a href='/'><button>Back</button></a>";
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


