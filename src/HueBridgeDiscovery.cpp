#include "HueBridgeDiscovery.h"
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
        Serial.printf("[HueBridgeDiscovery] Using saved IP: %s\n", savedIP.c_str());
        ipAddress = savedIP;
        return true;
    }
    
    // mDNS Discovery versuchen
    if (discoverMDNS(ipAddress))
    {
        Serial.printf("[HueBridgeDiscovery] Found via mDNS: %s\n", ipAddress.c_str());
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

