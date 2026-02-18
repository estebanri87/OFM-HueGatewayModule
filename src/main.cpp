#include <Arduino.h>
#include <WiFi.h>
#include "HueGatewayModule.h"

/**
 * STANDALONE TEST APPLICATION
 * 
 * This main.cpp is ONLY intended for isolated module testing.
 * 
 * In real OpenKNX firmware (for example OAM-HueGateway):
 * - OFM-Network or WLAN modules provide network connectivity
 * - HueGatewayModule uses the existing WiFi/Ethernet stack
 * - No credentials are required in this file
 * 
 * For local testing only: provide WiFi credentials below.
 */

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

void setup()
{
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("OFM-HueGatewayModule - STANDALONE TEST MODE");
    Serial.println("========================================");
    Serial.println("Note: In real OpenKNX firmware, WiFi is");
    Serial.println("provided by OFM-Network or WLAN module");
    Serial.println("========================================\n");
    
    // Connect WiFi (standalone test mode only).
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi connected!");
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.println("\nWiFi connection failed!");
    }
    
    // Initialize and start module lifecycle.
    Serial.println("\nInitializing HueGatewayModule...");
    openknxHueGatewayModule.setup();
    
    Serial.println("\nSetup complete. Entering main loop...\n");
}

void loop()
{
    openknxHueGatewayModule.loop();
    
    // Lightweight heartbeat for quick runtime diagnostics.
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 10000)
    {
        lastHeartbeat = millis();
        Serial.printf("[Main] Heartbeat - Free Heap: %d bytes\n", ESP.getFreeHeap());
    }
    
    delay(10);
}

