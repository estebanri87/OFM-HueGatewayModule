#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "OpenKNX.h"
#include "knxprod.h"
#include "HueGatewayAuth.h"
#include "HCL/HCLMasterManager.h"
#include "WebUI.h"

// Forward Declarations
class HueGatewayClient;
class HueGatewayLight;

/**
 * @brief OpenKNX Hue Bridge Module - Philips Hue Integration
 * 
 * Dieses Modul ermöglicht die Integration von Philips Hue Geräten in KNX-Systeme.
 * Es kommuniziert mit der Hue Bridge via Hue API v2 und stellt die Geräte als
 * KNX-Kommunikationsobjekte bereit.
 * 
 * @version 0.1.0
 * @date 2026-02-03
 */

class HueGatewayModule : public OpenKNX::Module
{
public:
    HueGatewayModule();
    ~HueGatewayModule();
    
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
    HueGatewayClient* _client;
    
    
    // Device Management
    static const int MAX_LIGHTS = 20;
    HueGatewayLight* _lights[MAX_LIGHTS];
    int _lightCount;
    
    // Status values
    enum class BridgeStatus {
        DISCONNECTED,
        CONNECTING,
        WAIT_FOR_BUTTON,
        AUTHENTICATING,
        CONNECTED,
        CONNECTION_LOST,
        BRIDGE_UNREACHABLE,
        ERROR
    };
    
    BridgeStatus _bridgeStatus;
    unsigned long _ledBlinkTime;

    // Authentication state
    HueGatewayAuth _auth;
    String _bridgeIP;
    bool _authPending;
    unsigned long _authStartTime;
    unsigned long _authLastTry;
    unsigned long _authWindowMs;
    bool _devicesInitialized;
    
    void setupBridge();
    void setupDevices();
    void setupHCL();
    void checkConnection();
    void refreshLightStatus();
    void setupWebUI();
    void setupMDNS();
    void pollAuthentication();
    bool initClientWithAppKey();
    void startPairing();
    
    // Web UI handlers (absolute URIs)
    static esp_err_t handleWebRoot(httpd_req_t* req);
    static esp_err_t handleWebScan(httpd_req_t* req);
    static esp_err_t handleWebScanText(httpd_req_t* req);
    static esp_err_t handleWebStatus(httpd_req_t* req);
    
    // Web UI pages (under WEBUI_BASE_URI)
    static esp_err_t pageWebRoot(const char* uri, httpd_req_t* req, void* arg);
    static esp_err_t pageWebScan(const char* uri, httpd_req_t* req, void* arg);
    static esp_err_t pageWebStatus(const char* uri, httpd_req_t* req, void* arg);
    
    // Scan functions
    void performBridgeScan();
    String getBridgeScanHTML();
    String getBridgeScanText();
    void resetDevices();
    
    // Helper Methods
    String getBridgeIP();
    
    // Status Methods
    void updateStatus(BridgeStatus status);
    void sendStatusKO(bool connected);
    void updateInfoLED();
};

// Globale Instanz
extern HueGatewayModule openknxHueGatewayModule;
