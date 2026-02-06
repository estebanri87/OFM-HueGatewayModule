#include "HueBridgeModule.h"
#include "HueBridgeDiscovery.h"
#include "HueBridgeAuth.h"
#include "HueBridgeClient.h"
#include "Devices/HueBridgeLight.h"

HueBridgeModule::HueBridgeModule()
    : _initialized(false)
    , _lastLoop(0)
    , _client(nullptr)
    , _webServer(nullptr)
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

HueBridgeModule::~HueBridgeModule()
{
    // Cleanup Lights
    for (int i = 0; i < _lightCount; i++)
    {
        if (_lights[i])
        {
            delete _lights[i];
            _lights[i] = nullptr;
        }
    }
    
    // Cleanup WebServer
    if (_webServer)
    {
        _webServer->stop();
        delete _webServer;
        _webServer = nullptr;
    }
    
    // Cleanup Client
    if (_client)
    {
        delete _client;
        _client = nullptr;
    }
}

void HueBridgeModule::setup()
{
    Serial.println("[HueBridgeModule] Setup started");
    Serial.println("[HueBridgeModule] Initializing...");
    
    setupBridge();
    setupDevices();
    setupWebServer();
    setupMDNS();
    
    _initialized = true;
    Serial.println("[HueBridgeModule] Setup complete");
}

void HueBridgeModule::loop()
{
    if (!_initialized)
        return;
    
    // Handle web server requests
    if (_webServer != nullptr)
    {
        _webServer->handleClient();
    }
    
    // Update Info-LED pattern
    updateInfoLED();

    // Non-blocking authentication polling
    pollAuthentication();
    
    unsigned long now = millis();
    
    // Alle 5 Sekunden Status prüfen
    if (now - _lastLoop > 5000)
    {
        _lastLoop = now;
        checkConnection();
    }
    
    // TODO: Event Stream handling
    // TODO: Status updates
}

const std::string HueBridgeModule::name()
{
    return "HueBridgeModule";
}

const std::string HueBridgeModule::version()
{
    return "0.1.0";
}

void HueBridgeModule::processInputKo(GroupObject& ko)
{
    if (!_initialized)
        return;
        
    uint16_t koNumber = ko.asap();
    Serial.printf("[HueBridgeModule] KO %d received\n", koNumber);
    
    if (koNumber == HUE_KoHUEPairingTrigger)
    {
        bool trigger = ko.value(Dpt(1, 17));  // DPT 1.017 Trigger
        if (trigger)
        {
            Serial.println("[HueBridgeModule] Pairing triggered via ETS KO");
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
            Serial.println("\n[HueBridgeModule] Bridge scan triggered via ETS Button/KO");
            performBridgeScan();
        }
        return;
    }
    
    // KO an entsprechendes Light weiterleiten
    // Neue Struktur pro Kanal (5 KOs):
    // KO 0: Switch (DPT 1.001)
    // KO 1: Brightness absolut (DPT 5.001)
    // KO 2: Dimming relativ (DPT 3.007)
    // KO 3: Status Switch (DPT 1.001)
    // KO 4: Status Brightness (DPT 5.001)
    
    if (koNumber < KO_CHANNELS_START)
    {
        Serial.printf("[HueBridgeModule] KO %d below channel range\n", koNumber);
        return;
    }
    
    uint16_t channelOffset = koNumber - KO_CHANNELS_START;
    uint8_t channel = channelOffset / 5;  // 5 KOs per channel (geändert von 3)
    uint8_t koType = channelOffset % 5;   // 0=Switch, 1=Brightness, 2=Dimming, 3=StatusSwitch, 4=StatusBrightness
    
    if (channel >= _lightCount)
    {
        Serial.printf("[HueBridgeModule] Channel %d out of range\n", channel);
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
    }
}

bool HueBridgeModule::processCommand(const std::string cmd, bool diagnoseKo)
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
        HueBridgeDiscovery discovery;
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
        HueBridgeAuth auth;
        if (!auth.authenticate(bridgeIP.c_str()))
        {
            Serial.println("ERROR: Authentication failed!");
            Serial.println("Press button on Hue Bridge and retry");
            return true;
        }
        
        // Lichter abrufen
        HueBridgeClient client;
        if (!client.begin(bridgeIP, auth.getAppKey()))
        {
            Serial.println("ERROR: Client init failed!");
            return true;
        }
        
        HueBridgeLightState lights[MAX_LIGHTS];
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
        
        for (int i = 0; i < _lightCount; i++)
        {
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

void HueBridgeModule::setupMDNS()
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

void HueBridgeModule::setupBridge()
{
    Serial.println("[HueBridgeModule] Setting up Bridge connection...");
    
    // Prüfen ob Netzwerk verfügbar (von OFM-Network/WLAN bereitgestellt)
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[HueBridgeModule] ERROR: WiFi not connected!");
        Serial.println("[HueBridgeModule] Network must be initialized by OFM-Network or WLAN module first");
        return;
    }
    
    Serial.printf("[HueBridgeModule] Network OK - IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Bridge IP aus ETS-Parameter lesen
    _bridgeIP = getBridgeIP();
    
    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueBridgeModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::DISCONNECTED);
        return;
    }
    
    Serial.printf("[HueBridgeModule] Bridge configured: %s\n", _bridgeIP.c_str());

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
            Serial.println("[HueBridgeModule] HueBridgeClient ready");
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
    Serial.println("[HueBridgeModule] Awaiting Hue Bridge link button...");
}

void HueBridgeModule::setupDevices()
{
    Serial.println("[HueBridgeModule] Setting up Devices...");

    if (_devicesInitialized)
    {
        return;
    }
    
    if (!_client || !_client->isInitialized())
    {
        Serial.println("[HueBridgeModule] ERROR: HueBridgeClient not ready");
        return;
    }
    
    // Anzahl Kanäle aus ETS lesen
    uint8_t channelCount = ParamHUE_HUEChannelCount;
    Serial.printf("[HueBridgeModule] Configured channels: %d\n", channelCount);
    
    if (channelCount == 0)
    {
        Serial.println("[HueBridgeModule] No channels configured");
        return;
    }
    
    // Alle Lichter von der Bridge abrufen (zum Validieren)
    HueBridgeLightState allLights[MAX_LIGHTS];
    int bridgeLightCount = _client->getLights(allLights, MAX_LIGHTS);
    Serial.printf("[HueBridgeModule] Bridge has %d lights\n", bridgeLightCount);
    
    // Kanäle aus ETS-Parametern laden
    for (uint8_t ch = 0; ch < channelCount && ch < MAX_LIGHTS; ch++)
    {
        // Parameter für diesen Kanal lesen
        uint16_t paramBase = getChannelParamIndex(ch, 0);
        
        bool enabled = knx.paramByte(paramBase + 0) & 0x01; // HUE_Ch%C%_Enabled
        
        if (!enabled)
        {
            Serial.printf("[HueBridgeModule] Channel %d: Disabled\n", ch);
            continue;
        }
        
        // Light ID lesen (String-Parameter)
        const char* lightId = (const char*)knx.paramData(paramBase + 1); // HUE_Ch%C%_LightId
        const char* name = (const char*)knx.paramData(paramBase + 2);    // HUE_Ch%C%_Name
        
        if (strlen(lightId) == 0)
        {
            Serial.printf("[HueBridgeModule] Channel %d: No Light ID configured\n", ch);
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
        
        // HueBridgeLight Instanz erstellen
        _lights[_lightCount] = new HueBridgeLight(String(lightId), String(name), _client);
        _lights[_lightCount]->begin(koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        Serial.printf("[HueBridgeModule] Channel %d: %s (%s) -> KO %d/%d/%d/%d/%d\n",
                      ch, name, lightId, koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        _lightCount++;
    }
    
    Serial.printf("[HueBridgeModule] Initialized %d lights\n", _lightCount);
    _devicesInitialized = true;
}

void HueBridgeModule::checkConnection()
{
    // TODO: Bridge Verbindung prüfen
    // TODO: Bei Offline: Reconnect versuchen
    // Serial.println("[HueBridgeModule] Connection check (not implemented)");
}

// ===== Helper Methods =====

String HueBridgeModule::getBridgeIP()
{
    uint8_t mode = ParamHUE_HUEBridgeMode;
    
    if (mode == 0)
    {
        // Automatisch (mDNS)
        Serial.println("[HueBridgeModule] Using mDNS discovery...");
        HueBridgeDiscovery discovery;
        String ip;
        
        if (discovery.findBridge(ip))
        {
            Serial.printf("[HueBridgeModule] Bridge found via mDNS: %s\n", ip.c_str());
            return ip;
        }
        else
        {
            Serial.println("[HueBridgeModule] mDNS discovery failed");
            return "";
        }
    }
    else
    {
        // Manuelle IP
        std::string ipStr = ParamHUE_HUEBridgeIPStr;
        Serial.printf("[HueBridgeModule] Using manual IP: %s\n", ipStr.c_str());
        return String(ipStr.c_str());
    }
}

uint16_t HueBridgeModule::getChannelParamIndex(uint8_t channel, uint16_t paramOffset)
{
    // OpenKNXproducer berechnet: BlockOffset + (channel * BlockSize) + paramOffset
    // Wir müssen die tatsächlichen Offsets aus knxprod.h nutzen
    // Vereinfachte Berechnung für 20 Kanäle
    
    // Base Parameter Block Start (nach General Settings)
    const uint16_t HUE_PARAM_BASE = 1000; // Placeholder - wird von knxprod.h generiert
    const uint16_t HUE_PARAM_BLOCK_SIZE = 10; // Pro Kanal: Enabled + LightId + Name + Settings
    
    return HUE_PARAM_BASE + (channel * HUE_PARAM_BLOCK_SIZE) + paramOffset;
}

void HueBridgeModule::performBridgeScan()
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
    
    HueBridgeLightState lights[MAX_LIGHTS];
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

void HueBridgeModule::updateStatus(BridgeStatus status)
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
    Serial.printf("[HueBridgeModule] Status: %s\n", statusText);
}

void HueBridgeModule::sendStatusKO(bool connected)
{
    if (!ParamHUE_HUEShowConnectionStatus)
    {
        return;
    }

    GroupObject& ko = KoHUE_HUEConnectionStatus;
    ko.value(connected, Dpt(1, 1));
}

void HueBridgeModule::pollAuthentication()
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
        Serial.println("[HueBridgeModule] Authentication timeout - button not pressed");
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
        Serial.println("[HueBridgeModule] Authentication successful");

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

bool HueBridgeModule::initClientWithAppKey()
{
    if (_client)
    {
        delete _client;
        _client = nullptr;
    }

    _client = new HueBridgeClient();
    if (!_client->begin(_bridgeIP, _auth.getAppKey()))
    {
        Serial.println("[HueBridgeModule] ERROR: HueBridgeClient init failed!");
        delete _client;
        _client = nullptr;
        return false;
    }

    return true;
}

void HueBridgeModule::startPairing()
{
    if (_bridgeIP.isEmpty())
    {
        _bridgeIP = getBridgeIP();
    }

    if (_bridgeIP.isEmpty())
    {
        Serial.println("[HueBridgeModule] ERROR: Bridge IP not configured!");
        updateStatus(BridgeStatus::DISCONNECTED);
        return;
    }

    _auth.clearAppKey();
    _authPending = true;
    _authStartTime = millis();
    _authLastTry = 0;
    updateStatus(BridgeStatus::WAIT_FOR_BUTTON);
    Serial.println("[HueBridgeModule] Pairing started - press Hue Bridge button");
}

void HueBridgeModule::updateInfoLED()
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
// WebServer Implementation
// ============================================

void HueBridgeModule::setupWebServer()
{
    // Read port from ETS parameter (default 80)
    uint16_t port = ParamHUE_HUEWebServerPort;
    
    if (port == 0)
        port = 80;
    
    _webServer = new WebServer(port);
    
    // Define routes
    _webServer->on("/", [this]() { this->handleRoot(); });
    _webServer->on("/hue/scan", [this]() { this->handleScan(); });
    _webServer->on("/hue/scan.txt", [this]() { this->handleScanText(); });
    _webServer->on("/hue/status", [this]() { this->handleStatus(); });
    _webServer->onNotFound([this]() { this->handleNotFound(); });
    
    _webServer->begin();
    
    Serial.printf("[HueBridgeModule] Webserver started on port %d\n", port);
    Serial.printf("[HueBridgeModule] Access: http://%s:%d/hue/scan\n", WiFi.localIP().toString().c_str(), port);
}

void HueBridgeModule::handleRoot()
{
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
    html += "<p><strong>Module Version:</strong> " + String(version().c_str()) + "</p>";
    html += "</div></body></html>";
    
    _webServer->send(200, "text/html; charset=UTF-8", html);
}

void HueBridgeModule::handleScan()
{
    if (!_client || !_initialized)
    {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Scan Error</title></head><body>";
        html += "<h1>❌ Error</h1><p>Module not initialized. Check bridge authentication.</p>";
        html += "<a href='/'>← Back</a></body></html>";
        _webServer->send(500, "text/html; charset=UTF-8", html);
        return;
    }
    
    String html = getBridgeScanHTML();
    _webServer->send(200, "text/html; charset=UTF-8", html);
}

void HueBridgeModule::handleScanText()
{
    if (!_client || !_initialized)
    {
        _webServer->send(500, "text/plain; charset=UTF-8", "Module not initialized. Check bridge authentication.\n");
        return;
    }

    String text = getBridgeScanText();
    _webServer->send(200, "text/plain; charset=UTF-8", text);
}

void HueBridgeModule::handleStatus()
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Hue Status</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "table{width:100%;border-collapse:collapse;background:white;border-radius:5px;overflow:hidden;}";
    html += "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "th{background:#007bff;color:white;}</style></head><body>";
    html += "<h1>📊 Module Status</h1>";
    html += "<table><tr><th>Parameter</th><th>Value</th></tr>";
    html += "<tr><td>Initialized</td><td>" + String(_initialized ? "✅ Yes" : "❌ No") + "</td></tr>";
    html += "<tr><td>Device IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><td>Active Lights</td><td>" + String(_lightCount) + " / " + String(MAX_LIGHTS) + "</td></tr>";
    html += "<tr><td>WiFi RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    html += "</table>";
    html += "<br><a href='/'>← Back</a></body></html>";
    
    _webServer->send(200, "text/html; charset=UTF-8", html);
}

void HueBridgeModule::handleNotFound()
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>404</title></head><body>";
    html += "<h1>404 - Not Found</h1><p>The requested URL was not found.</p>";
    html += "<a href='/'>← Back to Home</a></body></html>";
    
    _webServer->send(404, "text/html; charset=UTF-8", html);
}

String HueBridgeModule::getBridgeScanHTML()
{
    HueBridgeLightState lights[MAX_LIGHTS];
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
        _webServer->send(200, "text/html; charset=UTF-8", html);
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

String HueBridgeModule::getBridgeScanText()
{
    HueBridgeLightState lights[MAX_LIGHTS];
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


