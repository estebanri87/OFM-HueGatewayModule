#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "OpenKNX.h"
#include "knxprod.h"

// Forward Declarations
class HueClient;
class HueLight;

/**
 * @brief OpenKNX Hue Module - Philips Hue Integration
 * 
 * Dieses Modul ermöglicht die Integration von Philips Hue Geräten in KNX-Systeme.
 * Es kommuniziert mit der Hue Bridge via Hue API v2 und stellt die Geräte als
 * KNX-Kommunikationsobjekte bereit.
 * 
 * @version 0.1.0
 * @date 2026-02-03
 */

class HueModule : public OpenKNX::Module
{
public:
    HueModule();
    ~HueModule();
    
    // OpenKNX::Module Interface
    const std::string name() override;
    const std::string version() override;
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject& ko) override;
    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    
private:
    bool _initialized;
    unsigned long _lastLoop;
    HueClient* _client;
    WebServer* _webServer;
    
    // Device Management
    static const int MAX_LIGHTS = 20;
    HueLight* _lights[MAX_LIGHTS];
    int _lightCount;
    
    // KO numbers (relative to module offset 300)
    static constexpr uint16_t KO_STATUS = 3;           // Status KO (from KoSingleOffset)
    static constexpr uint16_t KO_SCAN_TRIGGER = 300;
    static constexpr uint16_t KO_CHANNELS_START = 301;  // First channel KO
    
    // Status values
    enum class BridgeStatus {
        DISCONNECTED,
        CONNECTING,
        WAIT_FOR_BUTTON,
        AUTHENTICATING,
        CONNECTED
    };
    
    BridgeStatus _bridgeStatus;
    unsigned long _ledBlinkTime;
    
    void setupBridge();
    void setupDevices();
    void checkConnection();
    void setupWebServer();
    void setupMDNS();
    
    // Web server handlers
    void handleRoot();
    void handleScan();
    void handleStatus();
    void handleNotFound();
    
    // Scan functions
    void performBridgeScan();
    String getBridgeScanHTML();
    
    // Helper Methods
    String getBridgeIP();
    uint16_t getChannelParamIndex(uint8_t channel, uint16_t paramOffset);
    
    // Status Methods
    void updateStatus(BridgeStatus status);
    void sendStatusKO(const char* message);
    void updateInfoLED();
};

// Globale Instanz
extern HueModule openknxHueModule;
