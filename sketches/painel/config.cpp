#include "config.h"

#include <Preferences.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef SECRET_WIFI_SSID
#define SECRET_WIFI_SSID ""
#endif
#ifndef SECRET_WIFI_PASS
#define SECRET_WIFI_PASS ""
#endif
#ifndef SECRET_SPOTIFY_CLIENT_ID
#define SECRET_SPOTIFY_CLIENT_ID ""
#endif
#ifndef SECRET_SPOTIFY_CLIENT_SECRET
#define SECRET_SPOTIFY_CLIENT_SECRET ""
#endif
#ifndef SECRET_SPOTIFY_REFRESH_TOKEN
#define SECRET_SPOTIFY_REFRESH_TOKEN ""
#endif
#ifndef SECRET_SPOTIFY_AUTH_DATE
#define SECRET_SPOTIFY_AUTH_DATE ""
#endif

static const char *NVS_NAMESPACE = "painel";

void configLoad(Config &cfg) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  cfg.wifiSsid = prefs.getString("wifi_ssid", SECRET_WIFI_SSID);
  cfg.wifiPass = prefs.getString("wifi_pass", SECRET_WIFI_PASS);
  cfg.spClientId = prefs.getString("sp_id", SECRET_SPOTIFY_CLIENT_ID);
  cfg.spClientSecret = prefs.getString("sp_secret", SECRET_SPOTIFY_CLIENT_SECRET);
  cfg.spRefreshToken = prefs.getString("sp_refresh", SECRET_SPOTIFY_REFRESH_TOKEN);
  cfg.spAuthDate = prefs.getString("sp_date", SECRET_SPOTIFY_AUTH_DATE);
  prefs.end();
}

void configSaveWifi(const String &ssid, const String &pass) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  prefs.end();
}

void configSaveSpotify(const String &clientId, const String &clientSecret, const String &refreshToken,
                       const String &authDate) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("sp_id", clientId);
  prefs.putString("sp_secret", clientSecret);
  prefs.putString("sp_refresh", refreshToken);
  prefs.putString("sp_date", authDate);
  prefs.end();
}

void configSaveRefreshToken(const String &refreshToken) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("sp_refresh", refreshToken);
  prefs.end();
}
