#include "HueBridgeDiscovery.h"
#include <ArduinoJson.h>
#include <Preferences.h>

Preferences prefs;

HueBridgeDiscovery::HueBridgeDiscovery()
{
}

bool HueBridgeDiscovery::findBridge(String& ipAddress)
{
    Serial.println("[HueBridgeDiscovery] Searching for Hue Bridge...");
    
    // Zuerst gespeicherte IP laden
    String savedIP = loadIP();
    if (savedIP.length() > 0)
    {
        if (isBridgeReachable(savedIP))
        {
            Serial.printf("[HueBridgeDiscovery] Using saved IP: %s\n", savedIP.c_str());
            ipAddress = savedIP;
            return true;
        }

        Serial.printf("[HueBridgeDiscovery] Saved IP not reachable: %s\n", savedIP.c_str());
    }
    
    // mDNS Discovery versuchen
    if (discoverMDNS(ipAddress))
    {
        Serial.printf("[HueBridgeDiscovery] Found via mDNS: %s\n", ipAddress.c_str());
        saveIP(ipAddress);
        return true;
    }

    // N-UPnP discovery fallback
    if (discoverNupnp(ipAddress))
    {
        Serial.printf("[HueBridgeDiscovery] Found via N-UPnP: %s\n", ipAddress.c_str());
        saveIP(ipAddress);
        return true;
    }
    
    Serial.println("[HueBridgeDiscovery] No bridge found");
    return false;
}

bool HueBridgeDiscovery::setManualIP(const char* ip)
{
    Serial.printf("[HueBridgeDiscovery] Manual IP set: %s\n", ip);
    
    // Einfache IP-Validierung
    IPAddress testIP;
    if (!testIP.fromString(ip))
    {
        Serial.println("[HueBridgeDiscovery] Invalid IP address");
        return false;
    }
    
    saveIP(String(ip));
    return true;
}

bool HueBridgeDiscovery::discoverMDNS(String& ip)
{
    Serial.println("[HueBridgeDiscovery] Starting mDNS discovery...");
    
    if (!MDNS.begin("openknx-hue"))
    {
        Serial.println("[HueBridgeDiscovery] mDNS init failed");
        return false;
    }
    
    Serial.println("[HueBridgeDiscovery] Querying for _hue._tcp.local...");
    
    int n = MDNS.queryService("hue", "tcp");
    
    if (n == 0)
    {
        Serial.println("[HueBridgeDiscovery] No Hue Bridge found via mDNS");
        return false;
    }
    
    Serial.printf("[HueBridgeDiscovery] Found %d service(s)\n", n);
    
    // Erste gefundene Bridge verwenden
    ip = MDNS.address(0).toString();
    Serial.printf("[HueBridgeDiscovery] Bridge IP: %s\n", ip.c_str());
    Serial.printf("[HueBridgeDiscovery] Bridge Hostname: %s\n", MDNS.hostname(0).c_str());
    
    return true;
}

bool HueBridgeDiscovery::discoverNupnp(String& ip)
{
    Serial.println("[HueBridgeDiscovery] Starting N-UPnP discovery...");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(5000);

    if (!http.begin(client, "https://discovery.meethue.com/"))
    {
        Serial.println("[HueBridgeDiscovery] N-UPnP HTTP begin failed");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        Serial.printf("[HueBridgeDiscovery] N-UPnP HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error || !doc.is<JsonArray>())
    {
        Serial.printf("[HueBridgeDiscovery] N-UPnP JSON parse error: %s\n", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0)
    {
        Serial.println("[HueBridgeDiscovery] N-UPnP returned no bridges");
        return false;
    }

    JsonObject first = arr[0];
    const char* internalIp = first["internalipaddress"] | "";
    if (internalIp[0] == '\0')
    {
        Serial.println("[HueBridgeDiscovery] N-UPnP missing internal IP");
        return false;
    }

    ip = String(internalIp);
    return true;
}

bool HueBridgeDiscovery::isBridgeReachable(const String& ip)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String("https://") + ip + "/api/config";

    http.begin(client, url);
    http.setTimeout(2000);

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    http.end();
    return true;
}

void HueBridgeDiscovery::saveIP(const String& ip)
{
    prefs.begin("hue", false);
    prefs.putString("bridge_ip", ip);
    prefs.end();
    Serial.printf("[HueBridgeDiscovery] IP saved: %s\n", ip.c_str());
}

String HueBridgeDiscovery::loadIP()
{
    prefs.begin("hue", true); // read-only
    String ip = prefs.getString("bridge_ip", "");
    prefs.end();
    return ip;
}

