#include "HueDiscovery.h"
#include <Preferences.h>

Preferences prefs;

HueDiscovery::HueDiscovery()
{
}

bool HueDiscovery::findBridge(String& ipAddress)
{
    Serial.println("[HueDiscovery] Searching for Hue Bridge...");
    
    // Zuerst gespeicherte IP laden
    String savedIP = loadIP();
    if (savedIP.length() > 0)
    {
        Serial.printf("[HueDiscovery] Using saved IP: %s\n", savedIP.c_str());
        ipAddress = savedIP;
        return true;
    }
    
    // mDNS Discovery versuchen
    if (discoverMDNS(ipAddress))
    {
        Serial.printf("[HueDiscovery] Found via mDNS: %s\n", ipAddress.c_str());
        saveIP(ipAddress);
        return true;
    }
    
    Serial.println("[HueDiscovery] No bridge found");
    return false;
}

bool HueDiscovery::setManualIP(const char* ip)
{
    Serial.printf("[HueDiscovery] Manual IP set: %s\n", ip);
    
    // Einfache IP-Validierung
    IPAddress testIP;
    if (!testIP.fromString(ip))
    {
        Serial.println("[HueDiscovery] Invalid IP address");
        return false;
    }
    
    saveIP(String(ip));
    return true;
}

bool HueDiscovery::discoverMDNS(String& ip)
{
    Serial.println("[HueDiscovery] Starting mDNS discovery...");
    
    if (!MDNS.begin("openknx-hue"))
    {
        Serial.println("[HueDiscovery] mDNS init failed");
        return false;
    }
    
    Serial.println("[HueDiscovery] Querying for _hue._tcp.local...");
    
    int n = MDNS.queryService("hue", "tcp");
    
    if (n == 0)
    {
        Serial.println("[HueDiscovery] No Hue Bridge found via mDNS");
        return false;
    }
    
    Serial.printf("[HueDiscovery] Found %d service(s)\n", n);
    
    // Erste gefundene Bridge verwenden
    ip = MDNS.address(0).toString();
    Serial.printf("[HueDiscovery] Bridge IP: %s\n", ip.c_str());
    Serial.printf("[HueDiscovery] Bridge Hostname: %s\n", MDNS.hostname(0).c_str());
    
    return true;
}

void HueDiscovery::saveIP(const String& ip)
{
    prefs.begin("hue", false);
    prefs.putString("bridge_ip", ip);
    prefs.end();
    Serial.printf("[HueDiscovery] IP saved: %s\n", ip.c_str());
}

String HueDiscovery::loadIP()
{
    prefs.begin("hue", true); // read-only
    String ip = prefs.getString("bridge_ip", "");
    prefs.end();
    return ip;
}
