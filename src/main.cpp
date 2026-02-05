#include <Arduino.h>
#include <WiFi.h>
#include "HueModule.h"

/**
 * STANDALONE TEST APPLICATION
 * 
 * Dieses main.cpp ist NUR für isolierte Tests des HueModule gedacht.
 * 
 * In einer echten OpenKNX Firmware (z.B. SmartHomeBridge) würde:
 * - OFM-Network oder WLAN Module das Netzwerk bereitstellen
 * - HueModule das vorhandene WiFi/Ethernet nutzen
 * - Keine WiFi-Credentials hier notwendig
 * 
 * Für Tests: WiFi Credentials hier eintragen
 */

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

void setup()
{
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("OFM-HueModule - STANDALONE TEST MODE");
    Serial.println("========================================");
    Serial.println("Note: In real OpenKNX firmware, WiFi is");
    Serial.println("provided by OFM-Network or WLAN module");
    Serial.println("========================================\n");
    
    // WiFi verbinden (nur für standalone Tests!)
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
    
    // HueModule initialisieren
    Serial.println("\nInitializing HueModule...");
    openknxHueModule.setup();
    
    Serial.println("\nSetup complete. Entering main loop...\n");
}

void loop()
{
    openknxHueModule.loop();
    
    // Einfacher Heartbeat
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 10000)
    {
        lastHeartbeat = millis();
        Serial.printf("[Main] Heartbeat - Free Heap: %d bytes\n", ESP.getFreeHeap());
    }
    
    delay(10);
}
