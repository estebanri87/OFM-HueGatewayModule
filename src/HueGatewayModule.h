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
struct HueGatewayEventLightUpdate;

/**
 * @brief OpenKNX Hue Bridge Module - Philips Hue Integration
 * 
 * This module integrates Philips Hue devices into KNX systems.
 * It communicates with the Hue Bridge via Hue API v2 and exposes device
 * functions through KNX communication objects.
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

    enum class HclLockFallbackMode : uint8_t {
        None = 0,
        Min1 = 1,
        Min2 = 2,
        Min5 = 3,
        Min10 = 4,
        Min20 = 5,
        Min30 = 6,
        Hour1 = 7,
        Hour2 = 8,
        Hour5 = 9,
        Hour8 = 10,
        Hour12 = 11,
        NextDay = 12
    };
    
private:
    bool _initialized;
    unsigned long _lastConnectionCheckMs;
    unsigned long _lastRefreshTickMs;
    unsigned long _lastDeviceSetupRetryMs;
    unsigned long _lastEventStreamRetryMs;
    unsigned long _eventStreamRetryBackoffMs;
    unsigned long _eventStreamPauseUntilMs;
    uint8_t _eventStreamFailureCount;
    HueGatewayClient* _client;
    
    
    // Device Management
    static const int MAX_LIGHTS = 20;
    HueGatewayLight* _lights[MAX_LIGHTS];
    unsigned long _channelLastPollMs[MAX_LIGHTS];
    unsigned long _channelFastTrackNextMs[MAX_LIGHTS];
    unsigned long _channelFastTrackCooldownUntilMs[MAX_LIGHTS];
    uint8_t _channelFastTrackRemaining[MAX_LIGHTS];
    // Round-robin cursor for chunked polling fallback.
    uint8_t _pollCursor;
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
    unsigned long _lastReconnectTryMs;
    unsigned long _reconnectBackoffMs;
    bool _manualPairingRequired;
    bool _pairingTriggerLastState;
    bool _devicesInitialized;
    bool _deviceSetupNeedsRetry;
    bool _webScanRequested;
    bool _webScanInProgress;
    unsigned long _lastWebScanMs;
    int _lastWebScanLightCount;
    String _lastWebScanHtml;
    String _lastWebScanText;
    bool _hclLockActive;
    uint8_t _hclLockFallbackMode;
    unsigned long _hclLockActivatedMs;
    unsigned long _hclLockAutoReleaseMs;
    int16_t _hclLockActivationDayOfYear;
    bool _hclManagerLockActive[HCL::MasterManager::MAX_MASTERS];
    uint8_t _hclManagerLockFallbackMode[HCL::MasterManager::MAX_MASTERS];
    unsigned long _hclManagerLockActivatedMs[HCL::MasterManager::MAX_MASTERS];
    unsigned long _hclManagerLockAutoReleaseMs[HCL::MasterManager::MAX_MASTERS];
    int16_t _hclManagerLockActivationDayOfYear[HCL::MasterManager::MAX_MASTERS];
    uint16_t _hclLastPublishedKelvin[HCL::MasterManager::MAX_MASTERS];
    uint8_t _hclLastPublishedBrightness[HCL::MasterManager::MAX_MASTERS];
    bool _hclMasterValuesPublished[HCL::MasterManager::MAX_MASTERS];
    
    void setupBridge();
    void setupDevices();
    void setupHCL();
    void checkConnection();
    void refreshLightStatus();
    void applyEventStreamUpdates(const HueGatewayEventLightUpdate* updates, int updateCount);
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
    static esp_err_t handleWebPair(httpd_req_t* req);
    
    // Web UI pages (under WEBUI_BASE_URI)
    static esp_err_t pageWebRoot(const char* uri, httpd_req_t* req, void* arg);
    static esp_err_t pageWebScan(const char* uri, httpd_req_t* req, void* arg);
    static esp_err_t pageWebStatus(const char* uri, httpd_req_t* req, void* arg);
    static esp_err_t pageWebPair(const char* uri, httpd_req_t* req, void* arg);
    
    // Scan functions
    void performBridgeScan();
    void updateWebScanCache();
    String getBridgeScanHTML();
    String getBridgeScanText();
    void resetDevices();
    
    // Helper Methods
    bool hasNetworkConnectivity() const;
    String getBridgeIP();
    uint8_t countEnabledChannels() const;
    void setHclLock(bool active, const char* reason);
    void publishHclLockStatus();
    void setHclManagerLock(uint8_t managerNumber, bool active, const char* reason);
    void publishHclManagerLockStatus(uint8_t managerNumber);
    void publishHclMasterValues();
    void evaluateHclLockFallback(const tm* timeinfo, bool hasTime);
    void evaluateHclManagerLockFallback(const tm* timeinfo, bool hasTime);
    uint32_t getHclFallbackDurationMs(HclLockFallbackMode mode) const;
    static const char* hclFallbackModeToText(HclLockFallbackMode mode);
    
    // Status Methods
    void updateStatus(BridgeStatus status);
    void sendStatusKO(bool connected);
    void updateInfoLED();
};

// Global module instance used by OpenKNX module registration.
extern HueGatewayModule openknxHueGatewayModule;
