#include "HueGatewayDiscovery.h"
#include "HueGatewayStorage.h"
#include <ArduinoJson.h>

HueGatewayDiscovery::HueGatewayDiscovery()
{
}

bool HueGatewayDiscovery::findBridge(String& ipAddress)
{
    Serial.println("[HueGatewayDiscovery] ===== Commissioning: Discovery start =====");
    
    // Try the persisted bridge IP first to avoid unnecessary discovery traffic.
    String savedIP = loadIP();
    if (savedIP.length() > 0)
    {
        if (isBridgeReachable(savedIP))
        {
            Serial.printf("[HueGatewayDiscovery] Using saved IP: %s\n", savedIP.c_str());
            ipAddress = savedIP;
            return true;
        }

        Serial.printf("[HueGatewayDiscovery] Saved IP not reachable: %s\n", savedIP.c_str());
        Serial.println("[HueGatewayDiscovery] Trying mDNS discovery next...");
    }
    
    // Try local mDNS discovery.
    if (discoverMDNS(ipAddress))
    {
        Serial.printf("[HueGatewayDiscovery] Found via mDNS: %s\n", ipAddress.c_str());
        saveIP(ipAddress);
        return true;
    }

    // N-UPnP discovery fallback
    if (discoverNupnp(ipAddress))
    {
        Serial.printf("[HueGatewayDiscovery] Found via N-UPnP: %s\n", ipAddress.c_str());
        saveIP(ipAddress);
        return true;
    }
    
    Serial.println("[HueGatewayDiscovery] No bridge found");
    Serial.println("[HueGatewayDiscovery] ===== Commissioning: Discovery failed =====");
    return false;
}

bool HueGatewayDiscovery::setManualIP(const char* ip)
{
    Serial.printf("[HueGatewayDiscovery] Manual IP set: %s\n", ip);
    
    // Basic IPv4 validation.
    IPAddress testIP;
    if (!testIP.fromString(ip))
    {
        Serial.println("[HueGatewayDiscovery] Invalid IP address");
        return false;
    }
    
    const String ipString(ip);
    if (!isBridgeReachable(ipString))
    {
        Serial.printf("[HueGatewayDiscovery] Manual IP not reachable or not a Hue Bridge: %s\n", ip);
        return false;
    }

    saveIP(ipString);
    return true;
}

bool HueGatewayDiscovery::discoverMDNS(String& ip)
{
    Serial.println("[HueGatewayDiscovery] Starting mDNS discovery...");
    
    static bool mdnsStarted = false;
    if (!mdnsStarted)
    {
        if (!MDNS.begin("openknx-hue"))
        {
            Serial.println("[HueGatewayDiscovery] mDNS init failed");
            return false;
        }
        mdnsStarted = true;
    }
    
    Serial.println("[HueGatewayDiscovery] Querying for _hue._tcp.local...");
    
    int n = MDNS.queryService("hue", "tcp");
    
    if (n == 0)
    {
        Serial.println("[HueGatewayDiscovery] No Hue Bridge found via mDNS");
        Serial.println("[HueGatewayDiscovery] Falling back to N-UPnP");
        return false;
    }
    
    Serial.printf("[HueGatewayDiscovery] Found %d service(s)\n", n);
    
    // Use the first discovered bridge entry.
    ip = MDNS.address(0).toString();
    Serial.printf("[HueGatewayDiscovery] Bridge IP: %s\n", ip.c_str());
    Serial.printf("[HueGatewayDiscovery] Bridge Hostname: %s\n", MDNS.hostname(0).c_str());
    
    return true;
}

bool HueGatewayDiscovery::discoverNupnp(String& ip)
{
    Serial.println("[HueGatewayDiscovery] Starting N-UPnP discovery...");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(5000);

    const char* nupnpUrl = "https://discovery.meethue.com/";
    Serial.printf("[HueGatewayDiscovery] GET %s\n", nupnpUrl);
    if (!http.begin(client, nupnpUrl))
    {
        Serial.println("[HueGatewayDiscovery] N-UPnP HTTP begin failed (TLS/connection)");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        Serial.printf("[HueGatewayDiscovery] N-UPnP HTTP error: %d\n", httpCode);
        Serial.println("[HueGatewayDiscovery] N-UPnP could not provide bridge address");
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error || !doc.is<JsonArray>())
    {
        Serial.printf("[HueGatewayDiscovery] N-UPnP JSON parse error: %s\n", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0)
    {
        Serial.println("[HueGatewayDiscovery] N-UPnP returned no bridges");
        return false;
    }

    JsonObject first = arr[0];
    const char* internalIp = first["internalipaddress"] | "";
    if (internalIp[0] == '\0')
    {
        Serial.println("[HueGatewayDiscovery] N-UPnP missing internal IP");
        return false;
    }

    ip = String(internalIp);
    return true;
}

bool HueGatewayDiscovery::isBridgeReachable(const String& ip)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String("https://") + ip + "/clip/v2/resource/bridge";

    Serial.printf("[HueGatewayDiscovery] Reachability check: %s\n", url.c_str());

    if (!http.begin(client, url))
    {
        Serial.println("[HueGatewayDiscovery] Reachability HTTP begin failed (TLS/connection)");
        return false;
    }
    http.setTimeout(2000);

    String appKey = HueGatewayStorage::loadAppKey();

    if (appKey.length() > 0)
    {
        http.addHeader("hue-application-key", appKey);
    }

    int httpCode = http.GET();
    if (httpCode < 200 || httpCode >= 300)
    {
        Serial.printf("[HueGatewayDiscovery] Reachability HTTP status: %d\n", httpCode);
        http.end();
        return false;
    }

    String response = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error || !doc.is<JsonObject>())
    {
        Serial.printf("[HueGatewayDiscovery] Reachability JSON parse error: %s\n", error.c_str());
        http.end();
        return false;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    if (data.isNull() || data.size() == 0)
    {
        Serial.println("[HueGatewayDiscovery] Reachability response has no bridge data");
        http.end();
        return false;
    }

    bool bridgeTypeFound = false;
    for (JsonObjectConst item : data)
    {
        const char* type = item["type"] | "";
        if (strcmp(type, "bridge") == 0)
        {
            bridgeTypeFound = true;
            break;
        }
    }

    if (!bridgeTypeFound)
    {
        Serial.println("[HueGatewayDiscovery] Reachability response is not a Hue bridge resource");
        http.end();
        return false;
    }

    Serial.printf("[HueGatewayDiscovery] Reachability check passed for %s (HTTP %d)\n", ip.c_str(), httpCode);

    http.end();
    return true;
}

void HueGatewayDiscovery::saveIP(const String& ip)
{
    HueGatewayStorage::saveBridgeIp(ip);
    Serial.printf("[HueGatewayDiscovery] IP saved: %s\n", ip.c_str());
}

String HueGatewayDiscovery::loadIP()
{
    return HueGatewayStorage::loadBridgeIp();
}

