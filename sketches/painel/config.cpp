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

// Rede 0 nas chaves de sempre (wifi_ssid/wifi_pass, compatível com as versões anteriores); as outras em wifi_ssidN
static String wifiKey(const char *base, int i) { return i == 0 ? String(base) : String(base) + i; }

void configLoad(Config &cfg) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  cfg.wifiCount = 0;
  for (int i = 0; i < CONFIG_WIFI_MAX; i++) {
    String ssid = prefs.getString(wifiKey("wifi_ssid", i).c_str(), i == 0 ? SECRET_WIFI_SSID : "");
    if (ssid.length() == 0) break;
    cfg.wifi[cfg.wifiCount++] = {ssid, prefs.getString(wifiKey("wifi_pass", i).c_str(), i == 0 ? SECRET_WIFI_PASS : "")};
  }
  cfg.spClientId = prefs.getString("sp_id", SECRET_SPOTIFY_CLIENT_ID);
  cfg.spClientSecret = prefs.getString("sp_secret", SECRET_SPOTIFY_CLIENT_SECRET);
  cfg.spRefreshToken = prefs.getString("sp_refresh", SECRET_SPOTIFY_REFRESH_TOKEN);
  cfg.spAuthDate = prefs.getString("sp_date", SECRET_SPOTIFY_AUTH_DATE);
  cfg.kidPlaylist = prefs.getString("kid_playlist", "5BLKGFcPOFthBIkYROwHLK");  // playlist escolhida pelo usuário
  prefs.end();
}

void configSaveWifi(const String &ssid, const String &pass) {
  Config cfg;
  configLoad(cfg);
  WifiNet list[CONFIG_WIFI_MAX] = {{ssid, pass}};
  int count = 1;
  for (int i = 0; i < cfg.wifiCount && count < CONFIG_WIFI_MAX; i++)
    if (cfg.wifi[i].ssid != ssid) list[count++] = cfg.wifi[i];
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  for (int i = 0; i < CONFIG_WIFI_MAX; i++) {
    if (i < count) {
      prefs.putString(wifiKey("wifi_ssid", i).c_str(), list[i].ssid);
      prefs.putString(wifiKey("wifi_pass", i).c_str(), list[i].pass);
    } else {
      prefs.remove(wifiKey("wifi_ssid", i).c_str());
      prefs.remove(wifiKey("wifi_pass", i).c_str());
    }
  }
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

void configSaveKidPlaylist(const String &playlistId) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("kid_playlist", playlistId);
  prefs.end();
}

void configSaveRefreshToken(const String &refreshToken) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("sp_refresh", refreshToken);
  prefs.end();
}
