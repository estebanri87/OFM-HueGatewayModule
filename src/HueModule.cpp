#include "HueModule.h"
#include "HueDiscovery.h"
#include "HueAuth.h"
#include "HueClient.h"
#include "Devices/HueLight.h"

HueModule::HueModule()
    : _initialized(false)
    , _lastLoop(0)
    , _client(nullptr)
    , _webServer(nullptr)
    , _lightCount(0)
    , _bridgeStatus(BridgeStatus::DISCONNECTED)
    , _ledBlinkTime(0)
{
    // Light-Array initialisieren
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        _lights[i] = nullptr;
    }
}

HueModule::~HueModule()
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

void HueModule::setup()
{
    Serial.println("[HueModule] Setup started");
    Serial.println("[HueModule] Initializing...");
    
    setupBridge();
    setupDevices();
    setupWebServer();
    setupMDNS();
    
    _initialized = true;
    Serial.println("[HueModule] Setup complete");
}

void HueModule::loop()
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

const std::string HueModule::name()
{
    return "HueModule";
}

const std::string HueModule::version()
{
    return "0.1.0";
}

void HueModule::processInputKo(GroupObject& ko)
{
    if (!_initialized)
        return;
        
    uint16_t koNumber = ko.asap();
    Serial.printf("[HueModule] KO %d received\n", koNumber);
    
    // Check if it's the Scan Trigger KO
    if (koNumber == KO_SCAN_TRIGGER)
    {
        bool trigger = ko.value(Dpt(1, 17));  // DPT 1.017 Trigger
        if (trigger)
        {
            Serial.println("\n[HueModule] Bridge scan triggered via ETS Button/KO");
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
        Serial.printf("[HueModule] KO %d below channel range\n", koNumber);
        return;
    }
    
    uint16_t channelOffset = koNumber - KO_CHANNELS_START;
    uint8_t channel = channelOffset / 5;  // 5 KOs per channel (geändert von 3)
    uint8_t koType = channelOffset % 5;   // 0=Switch, 1=Brightness, 2=Dimming, 3=StatusSwitch, 4=StatusBrightness
    
    if (channel >= _lightCount)
    {
        Serial.printf("[HueModule] Channel %d out of range\n", channel);
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

bool HueModule::processCommand(const std::string cmd, bool diagnoseKo)
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
        HueDiscovery discovery;
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
        HueAuth auth;
        if (!auth.authenticate(bridgeIP.c_str()))
        {
            Serial.println("ERROR: Authentication failed!");
            Serial.println("Press button on Hue Bridge and retry");
            return true;
        }
        
        // Lichter abrufen
        HueClient client;
        if (!client.begin(bridgeIP, auth.getAppKey()))
        {
            Serial.println("ERROR: Client init failed!");
            return true;
        }
        
        HueLightState lights[MAX_LIGHTS];
        int count = client.getLights(lights, MAX_LIGHTS);
        
        Serial.println("Found Lights:");
        Serial.println("---------------------------------");
        
        for (int i = 0; i < count; i++)
        {
            Serial.printf("%2d: %-25s %s\n", i, lights[i].name.c_str(), lights[i].id.c_str());
            Serial.printf("    Status: %s, Brightness: %d/254\n",
                          lights[i].on ? "ON " : "OFF", lights[i].brightness);
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
        Serial.println("Hue Module Status");
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

void HueModule::setupMDNS()
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

void HueModule::setupBridge()
{
    Serial.println("[HueModule] Setting up Bridge connection...");
    
    // Prüfen ob Netzwerk verfügbar (von OFM-Network/WLAN bereitgestellt)
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[HueModule] ERROR: WiFi not connected!");
        Serial.println("[HueModule] Network must be initialized by OFM-Network or WLAN module first");
        return;
    }
    
    Serial.printf("[HueModule] Network OK - IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Bridge IP aus ETS-Parameter lesen
    String bridgeIP = getBridgeIP();
    
    if (bridgeIP.isEmpty())
    {
        Serial.println("[HueModule] ERROR: Bridge IP not configured!");
        return;
    }
    
    Serial.printf("[HueModule] Bridge configured: %s\n", bridgeIP.c_str());
    
    // Authentication
    HueAuth auth;
    
    if (!auth.authenticate(bridgeIP.c_str()))
    {
        Serial.println("[HueModule] Authentication failed!");
        return;
    }
    
    Serial.println("[HueModule] Bridge setup complete");
    Serial.printf("[HueModule] App-Key: %s\n", auth.getAppKey().c_str());
    
    // HueClient initialisieren
    _client = new HueClient();
    if (!_client->begin(bridgeIP, auth.getAppKey()))
    {
        Serial.println("[HueModule] ERROR: HueClient init failed!");
        delete _client;
        _client = nullptr;
        return;
    }
    
    Serial.println("[HueModule] HueClient ready");
}

void HueModule::setupDevices()
{
    Serial.println("[HueModule] Setting up Devices...");
    
    if (!_client || !_client->isInitialized())
    {
        Serial.println("[HueModule] ERROR: HueClient not ready");
        return;
    }
    
    // Anzahl Kanäle aus ETS lesen
    uint8_t channelCount = ParamHUE_HUEChannelCount;
    Serial.printf("[HueModule] Configured channels: %d\n", channelCount);
    
    if (channelCount == 0)
    {
        Serial.println("[HueModule] No channels configured");
        return;
    }
    
    // Alle Lichter von der Bridge abrufen (zum Validieren)
    HueLightState allLights[MAX_LIGHTS];
    int bridgeLightCount = _client->getLights(allLights, MAX_LIGHTS);
    Serial.printf("[HueModule] Bridge has %d lights\n", bridgeLightCount);
    
    // Kanäle aus ETS-Parametern laden
    for (uint8_t ch = 0; ch < channelCount && ch < MAX_LIGHTS; ch++)
    {
        // Parameter für diesen Kanal lesen
        uint16_t paramBase = getChannelParamIndex(ch, 0);
        
        bool enabled = knx.paramByte(paramBase + 0) & 0x01; // HUE_Ch%C%_Enabled
        
        if (!enabled)
        {
            Serial.printf("[HueModule] Channel %d: Disabled\n", ch);
            continue;
        }
        
        // Light ID lesen (String-Parameter)
        const char* lightId = (const char*)knx.paramData(paramBase + 1); // HUE_Ch%C%_LightId
        const char* name = (const char*)knx.paramData(paramBase + 2);    // HUE_Ch%C%_Name
        
        if (strlen(lightId) == 0)
        {
            Serial.printf("[HueModule] Channel %d: No Light ID configured\n", ch);
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
        
        // HueLight Instanz erstellen
        _lights[_lightCount] = new HueLight(String(lightId), String(name), _client);
        _lights[_lightCount]->begin(koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        Serial.printf("[HueModule] Channel %d: %s (%s) -> KO %d/%d/%d/%d/%d\n",
                      ch, name, lightId, koSwitch, koBrightness, koDimming, koStatusSwitch, koStatusBrightness);
        
        _lightCount++;
    }
    
    Serial.printf("[HueModule] Initialized %d lights\n", _lightCount);
}

void HueModule::checkConnection()
{
    // TODO: Bridge Verbindung prüfen
    // TODO: Bei Offline: Reconnect versuchen
    // Serial.println("[HueModule] Connection check (not implemented)");
}

// ===== Helper Methods =====

String HueModule::getBridgeIP()
{
    uint8_t mode = ParamHUE_HUEBridgeMode;
    
    if (mode == 0)
    {
        // Automatisch (mDNS)
        Serial.println("[HueModule] Using mDNS discovery...");
        HueDiscovery discovery;
        String ip;
        
        if (discovery.findBridge(ip))
        {
            Serial.printf("[HueModule] Bridge found via mDNS: %s\n", ip.c_str());
            return ip;
        }
        else
        {
            Serial.println("[HueModule] mDNS discovery failed");
            return "";
        }
    }
    else
    {
        // Manuelle IP
        std::string ipStr = ParamHUE_HUEBridgeIPStr;
        Serial.printf("[HueModule] Using manual IP: %s\n", ipStr.c_str());
        return String(ipStr.c_str());
    }
}

uint16_t HueModule::getChannelParamIndex(uint8_t channel, uint16_t paramOffset)
{
    // OpenKNXproducer berechnet: BlockOffset + (channel * BlockSize) + paramOffset
    // Wir müssen die tatsächlichen Offsets aus knxprod.h nutzen
    // Vereinfachte Berechnung für 20 Kanäle
    
    // Base Parameter Block Start (nach General Settings)
    const uint16_t HUE_PARAM_BASE = 1000; // Placeholder - wird von knxprod.h generiert
    const uint16_t HUE_PARAM_BLOCK_SIZE = 10; // Pro Kanal: Enabled + LightId + Name + Settings
    
    return HUE_PARAM_BASE + (channel * HUE_PARAM_BLOCK_SIZE) + paramOffset;
}

void HueModule::performBridgeScan()
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
    
    HueLightState lights[MAX_LIGHTS];
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
        Serial.printf("    Status: %s, Brightness: %d/254\n",
                      lights[i].on ? "ON " : "OFF", lights[i].brightness);
    }
    
    Serial.println("---------------------------------");
    Serial.println("Copy Light IDs above and paste into ETS parameters.");
    Serial.println("========================================\n");
}

// ============================================
// Status & LED Management
// ============================================

void HueModule::updateStatus(BridgeStatus status)
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
    
    sendStatusKO(statusText);
    Serial.printf("[HueModule] Status: %s\n", statusText);
}

void HueModule::sendStatusKO(const char* message)
{
    // Send status string to KO (DPT 16.000 - 14 bytes max)
    GroupObject& ko = knx.getGroupObject(KO_STATUS);
    char buffer[15];
    strncpy(buffer, message, 14);
    buffer[14] = '\0';
    ko.value(buffer, Dpt(16, 0));
}

void HueModule::updateInfoLED()
{
    // TODO: Implement LED status indication based on _bridgeStatus
    // Could use INFO_LED_PIN from hardware.h if available
}

// ============================================
// WebServer Implementation
// ============================================

void HueModule::setupWebServer()
{
    // Read port from ETS parameter (default 80)
    uint16_t port = ParamHUE_HUEWebServerPort;
    
    if (port == 0)
        port = 80;
    
    _webServer = new WebServer(port);
    
    // Define routes
    _webServer->on("/", [this]() { this->handleRoot(); });
    _webServer->on("/hue/scan", [this]() { this->handleScan(); });
    _webServer->on("/hue/status", [this]() { this->handleStatus(); });
    _webServer->onNotFound([this]() { this->handleNotFound(); });
    
    _webServer->begin();
    
    Serial.printf("[HueModule] Webserver started on port %d\n", port);
    Serial.printf("[HueModule] Access: http://%s:%d/hue/scan\n", WiFi.localIP().toString().c_str(), port);
}

void HueModule::handleRoot()
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>OpenKNX Hue Module</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}";
    html += "h1{color:#333;}.card{background:white;padding:20px;margin:10px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
    html += "a{display:inline-block;padding:10px 20px;margin:5px;background:#007bff;color:white;text-decoration:none;border-radius:3px;}";
    html += "a:hover{background:#0056b3;}</style></head><body>";
    html += "<h1>🏠 OpenKNX Hue Module</h1>";
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

void HueModule::handleScan()
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

void HueModule::handleStatus()
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

void HueModule::handleNotFound()
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>404</title></head><body>";
    html += "<h1>404 - Not Found</h1><p>The requested URL was not found.</p>";
    html += "<a href='/'>← Back to Home</a></body></html>";
    
    _webServer->send(404, "text/html; charset=UTF-8", html);
}

String HueModule::getBridgeScanHTML()
{
    HueLightState lights[MAX_LIGHTS];
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
        html += " | Brightness: " + String(lights[i].brightness) + "/254</span>";
        html += "</div>";
    }
    
    html += "<br><p>💡 <strong>Tip:</strong> Copy the Light ID and paste it into ETS channel parameters.</p>";
    html += "<button onclick='location.reload()'>🔄 Refresh</button>";
    html += "<a href='/'><button>← Back</button></a>";
    html += "</body></html>";
    
    return html;
}
