#include "HueGatewayStorage.h"
#include "HueGatewayNvsKeys.h"
#include <Preferences.h>

namespace HueGatewayStorage
{
namespace
{
String loadString(const char* key)
{
    Preferences prefs;
    prefs.begin(HueGatewayNvs::Namespace, true);
    String value = prefs.getString(key, "");
    prefs.end();
    return value;
}

void saveString(const char* key, const String& value)
{
    Preferences prefs;
    prefs.begin(HueGatewayNvs::Namespace, false);
    prefs.putString(key, value);
    prefs.end();
}
}

String loadAppKey()
{
    return loadString(HueGatewayNvs::AppKey);
}

String loadClientKey()
{
    return loadString(HueGatewayNvs::ClientKey);
}

String loadBridgeIp()
{
    return loadString(HueGatewayNvs::BridgeIp);
}

void saveAppKey(const String& key)
{
    saveString(HueGatewayNvs::AppKey, key);
}

void saveClientKey(const String& key)
{
    saveString(HueGatewayNvs::ClientKey, key);
}

void saveBridgeIp(const String& ip)
{
    saveString(HueGatewayNvs::BridgeIp, ip);
}

void clearAuthKeys()
{
    Preferences prefs;
    prefs.begin(HueGatewayNvs::Namespace, false);
    prefs.remove(HueGatewayNvs::AppKey);
    prefs.remove(HueGatewayNvs::ClientKey);
    prefs.end();
}
}
