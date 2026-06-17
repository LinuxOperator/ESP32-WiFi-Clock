#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <TM1637Display.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#ifndef WIFI_CLOCK_DISPLAY_CLK
#define WIFI_CLOCK_DISPLAY_CLK 6
#endif

#ifndef WIFI_CLOCK_DISPLAY_DIO
#define WIFI_CLOCK_DISPLAY_DIO 7
#endif

static constexpr uint8_t DISPLAY_CLK_PIN = WIFI_CLOCK_DISPLAY_CLK;
static constexpr uint8_t DISPLAY_DIO_PIN = WIFI_CLOCK_DISPLAY_DIO;
static constexpr uint8_t DEFAULT_BRIGHTNESS = 5;
static constexpr uint32_t WIFI_BOOT_PAIRING_DELAY_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr uint32_t DISPLAY_REFRESH_MS = 250;
static constexpr byte DNS_PORT = 53;

static const char *HOSTNAME = "clock";
static const char *SETUP_AP_SSID = "Clock Setup";
static const char *DEFAULT_TZ = "MST7MDT,M3.2.0,M11.1.0";

#ifndef WIFI_CLOCK_VERSION
#define WIFI_CLOCK_VERSION "1.0.0"
#endif

#ifndef WIFI_CLOCK_PROVISION_SSID
#define WIFI_CLOCK_PROVISION_SSID ""
#endif

#ifndef WIFI_CLOCK_PROVISION_PASSWORD
#define WIFI_CLOCK_PROVISION_PASSWORD ""
#endif

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
TM1637Display display(DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

String wifiSsid;
String wifiPassword;
String timezoneSpec = DEFAULT_TZ;
String timezoneName = "America/Denver";
uint8_t brightness = DEFAULT_BRIGHTNESS;
bool timezoneAuto = true;
bool colonBlink = true;
bool use24Hour = false;
bool pmIndicator = false;
bool mqttEnabled = false;
String mqttUrl = "";
String mqttHost = "";
String mqttUser = "";
String mqttPassword = "";
bool pairingMode = false;
bool ntpConfigured = false;
bool mdnsStarted = false;
bool hadWifiConnection = false;
bool mqttDiscoverySent = false;
uint32_t bootMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t displayTestUntilMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t lastMqttPublishMs = 0;

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length());
  for (char c : value) {
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (char c : value) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else {
      out += c;
    }
  }
  return out;
}

void loadSettings() {
  prefs.begin("clock", false);
  wifiSsid = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("pass", "");
  if (wifiSsid.isEmpty() && strlen(WIFI_CLOCK_PROVISION_SSID) > 0) {
    wifiSsid = WIFI_CLOCK_PROVISION_SSID;
    wifiPassword = WIFI_CLOCK_PROVISION_PASSWORD;
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPassword);
  }
  timezoneSpec = prefs.getString("tz", DEFAULT_TZ);
  timezoneName = prefs.getString("tzName", "America/Denver");
  timezoneAuto = prefs.getBool("tzAuto", true);
  brightness = prefs.getUChar("bright8", 0);
  if (brightness == 0) {
    uint8_t oldPercent = prefs.getUChar("brightPct", 0);
    if (oldPercent > 0) {
      brightness = map(constrain(oldPercent, 1, 100), 1, 100, 1, 8);
    } else {
      uint8_t oldBrightness = prefs.getUChar("bright", 4);
      brightness = constrain(oldBrightness + 1, 1, 8);
    }
  }
  colonBlink = prefs.getBool("colonBlink", true);
  use24Hour = prefs.getBool("use24", false);
  pmIndicator = prefs.getBool("pmInd", false);
  mqttEnabled = prefs.getBool("mqttEn", false);
  mqttUrl = prefs.getString("mqttUrl", "");
  mqttHost = prefs.getString("mqttHost", mqttUrl);
  mqttUser = prefs.getString("mqttUser", "");
  mqttPassword = prefs.getString("mqttPass", "");
  brightness = constrain(brightness, 1, 8);
}

void saveDisplaySettings() {
  prefs.putString("tz", timezoneSpec);
  prefs.putString("tzName", timezoneName);
  prefs.putBool("tzAuto", timezoneAuto);
  prefs.putUChar("bright8", brightness);
  prefs.putBool("colonBlink", colonBlink);
  prefs.putBool("use24", use24Hour);
  prefs.putBool("pmInd", pmIndicator);
  prefs.putBool("mqttEn", mqttEnabled);
  prefs.putString("mqttUrl", mqttUrl);
  prefs.putString("mqttHost", mqttHost);
  prefs.putString("mqttUser", mqttUser);
  prefs.putString("mqttPass", mqttPassword);
}

void saveWifiSettings(const String &ssid, const String &password) {
  wifiSsid = ssid;
  wifiPassword = password;
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPassword);
}

String deviceId() {
  String id = WiFi.macAddress();
  id.replace(":", "");
  id.toLowerCase();
  if (id == "000000000000") {
    id = "clock";
  }
  return id;
}

String mqttBaseTopic() {
  return "clock/" + deviceId();
}

struct MqttConfig {
  String host;
  uint16_t port = 1883;
  String user;
  String password;
};

bool parseMqttUrl(MqttConfig &config) {
  String url = mqttHost.length() ? mqttHost : mqttUrl;
  url.trim();
  if (url.isEmpty()) {
    return false;
  }
  if (url.startsWith("mqtt://")) {
    url.remove(0, 7);
  }
  int slash = url.indexOf('/');
  if (slash >= 0) {
    url = url.substring(0, slash);
  }
  int at = url.indexOf('@');
  if (at >= 0) {
    String auth = url.substring(0, at);
    url = url.substring(at + 1);
    int colon = auth.indexOf(':');
    if (colon >= 0) {
      config.user = auth.substring(0, colon);
      config.password = auth.substring(colon + 1);
    } else {
      config.user = auth;
    }
  }
  int colon = url.lastIndexOf(':');
  if (colon > 0) {
    config.host = url.substring(0, colon);
    config.port = static_cast<uint16_t>(url.substring(colon + 1).toInt());
    if (config.port == 0) {
      config.port = 1883;
    }
  } else {
    config.host = url;
  }
  if (config.user.isEmpty()) {
    config.user = mqttUser;
    config.password = mqttPassword;
  }
  return config.host.length() > 0;
}

String mqttStateMessage(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "connection timeout";
    case MQTT_CONNECTION_LOST: return "connection lost";
    case MQTT_CONNECT_FAILED: return "TCP connection failed";
    case MQTT_DISCONNECTED: return "disconnected";
    case MQTT_CONNECTED: return "connected";
    case MQTT_CONNECT_BAD_PROTOCOL: return "broker rejected protocol version";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "broker rejected client ID";
    case MQTT_CONNECT_UNAVAILABLE: return "broker unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "bad username or password";
    case MQTT_CONNECT_UNAUTHORIZED: return "not authorized";
    default: return "unknown error " + String(state);
  }
}

void applyTimezone() {
  setenv("TZ", timezoneSpec.c_str(), 1);
  tzset();
}

void configureTimeSync() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  applyTimezone();
  configTzTime(timezoneSpec.c_str(), "pool.ntp.org", "time.nist.gov", "time.google.com");
  ntpConfigured = true;
}

void mqttPublishStates() {
  if (!mqttClient.connected()) {
    return;
  }
  String base = mqttBaseTopic();
  mqttClient.publish((base + "/brightness/state").c_str(), String(brightness).c_str(), true);
  mqttClient.publish((base + "/colon/state").c_str(), colonBlink ? "ON" : "OFF", true);
  mqttClient.publish((base + "/use24/state").c_str(), use24Hour ? "ON" : "OFF", true);
  mqttClient.publish((base + "/pm/state").c_str(), pmIndicator ? "ON" : "OFF", true);
}

void mqttPublishDiscoveryEntity(const String &component, const String &objectId, const String &name,
                                const String &extra) {
  String id = deviceId();
  String base = mqttBaseTopic();
  String topic = "homeassistant/" + component + "/clock_" + id + "_" + objectId + "/config";
  String json = "{";
  json += "\"name\":\"" + name + "\",";
  json += "\"unique_id\":\"clock_" + id + "_" + objectId + "\",";
  json += "\"availability_topic\":\"" + base + "/status\",";
  json += "\"payload_available\":\"online\",";
  json += "\"payload_not_available\":\"offline\",";
  json += "\"device\":{\"identifiers\":[\"clock_" + id + "\"],\"name\":\"WiFi Clock\",\"manufacturer\":\"ESP32-C6\",\"model\":\"TM1637 Clock\",\"sw_version\":\"" WIFI_CLOCK_VERSION "\"}";
  if (extra.length()) {
    json += ",";
    json += extra;
  }
  json += "}";
  mqttClient.publish(topic.c_str(), json.c_str(), true);
}

void mqttPublishDiscovery() {
  String base = mqttBaseTopic();
  mqttPublishDiscoveryEntity("number", "brightness", "Brightness",
                             "\"state_topic\":\"" + base + "/brightness/state\","
                             "\"command_topic\":\"" + base + "/brightness/set\","
                             "\"min\":1,\"max\":8,\"step\":1,\"mode\":\"slider\"");
  mqttPublishDiscoveryEntity("switch", "blink_colon", "Blink Colon",
                             "\"state_topic\":\"" + base + "/colon/state\","
                             "\"command_topic\":\"" + base + "/colon/set\"");
  mqttPublishDiscoveryEntity("switch", "use_24_hour", "24-hour Mode",
                             "\"state_topic\":\"" + base + "/use24/state\","
                             "\"command_topic\":\"" + base + "/use24/set\"");
  mqttPublishDiscoveryEntity("switch", "pm_indicator", "PM Indicator",
                             "\"state_topic\":\"" + base + "/pm/state\","
                             "\"command_topic\":\"" + base + "/pm/set\"");
  mqttDiscoverySent = true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String topicStr = topic;
  String value;
  value.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    value += static_cast<char>(payload[i]);
  }
  value.trim();
  value.toUpperCase();

  String base = mqttBaseTopic();
  if (topicStr == base + "/brightness/set") {
    int next = value.toInt();
    if (next >= 1 && next <= 8) {
      brightness = static_cast<uint8_t>(next);
      display.setBrightness(brightness - 1, true);
    }
  } else if (topicStr == base + "/colon/set") {
    colonBlink = value == "ON" || value == "1" || value == "TRUE";
  } else if (topicStr == base + "/use24/set") {
    use24Hour = value == "ON" || value == "1" || value == "TRUE";
    if (use24Hour) {
      pmIndicator = false;
    }
  } else if (topicStr == base + "/pm/set") {
    pmIndicator = !use24Hour && (value == "ON" || value == "1" || value == "TRUE");
  }
  saveDisplaySettings();
  mqttPublishStates();
}

void maintainMqtt() {
  if (!mqttEnabled || (mqttHost.isEmpty() && mqttUrl.isEmpty()) || WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    return;
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
    if (millis() - lastMqttPublishMs > 30000) {
      mqttPublishStates();
      lastMqttPublishMs = millis();
    }
    return;
  }

  if (millis() - lastMqttAttemptMs < 10000) {
    return;
  }
  lastMqttAttemptMs = millis();

  MqttConfig config;
  if (!parseMqttUrl(config)) {
    return;
  }
  mqttClient.setServer(config.host.c_str(), config.port);
  mqttClient.setCallback(mqttCallback);
  String clientId = "clock-" + deviceId();
  String base = mqttBaseTopic();
  bool connected = config.user.length()
    ? mqttClient.connect(clientId.c_str(), config.user.c_str(), config.password.c_str(), (base + "/status").c_str(), 0, true, "offline")
    : mqttClient.connect(clientId.c_str(), (base + "/status").c_str(), 0, true, "offline");
  if (!connected) {
    return;
  }
  mqttClient.publish((base + "/status").c_str(), "online", true);
  mqttClient.subscribe((base + "/brightness/set").c_str());
  mqttClient.subscribe((base + "/colon/set").c_str());
  mqttClient.subscribe((base + "/use24/set").c_str());
  mqttClient.subscribe((base + "/pm/set").c_str());
  mqttPublishDiscovery();
  mqttPublishStates();
  lastMqttPublishMs = millis();
}

void startMdns() {
  if (mdnsStarted || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    Serial.println("mDNS started: http://clock.local");
  } else {
    Serial.println("mDNS failed to start");
  }
}

void connectWifi() {
  if (wifiSsid.isEmpty()) {
    return;
  }
  Serial.printf("Connecting to WiFi SSID: %s\n", wifiSsid.c_str());
  WiFi.mode(pairingMode ? WIFI_AP_STA : WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiAttemptMs = millis();
}

void startPairingMode() {
  if (pairingMode) {
    return;
  }
  pairingMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.printf("Pairing portal started: SSID '%s', http://%s\n",
                SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopPairingMode() {
  if (!pairingMode) {
    return;
  }
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  pairingMode = false;
  WiFi.mode(WIFI_STA);
}

bool shouldRedirectToPortal() {
  if (!pairingMode) {
    return false;
  }
  String host = server.hostHeader();
  host.toLowerCase();
  return host.length() > 0 && host.indexOf("192.168.4.1") < 0 && host.indexOf("clock.local") < 0;
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "Redirecting to clock setup");
}

void appendTimezoneOption(String &html, const char *id, const char *label, const char *posix) {
  html += F("<option value='");
  html += id;
  html += F("' data-posix='");
  html += htmlEscape(posix);
  html += F("'");
  if (timezoneName == id || timezoneSpec == posix) {
    html += F(" selected");
  }
  html += F(">");
  html += label;
  html += F("</option>");
}


String pageHtmlV2() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String html;
  html.reserve(18000);
  html += F("<!doctype html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>WiFi Clock</title><style>"
            ":root{color-scheme:light dark;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#f7f8f5;color:#172016}"
            "body{margin:0;padding:24px;display:flex;justify-content:center;line-height:1.45}"
            "main{width:min(780px,100%)}"
            "h1{font-size:2.15rem;margin:0 0 4px;letter-spacing:0}"
            "h2{font-size:1.08rem;margin:28px 0 10px}"
            ".status{margin:0 0 22px;color:#526151}"
            "section{border-top:1px solid #d4dacd;padding-top:18px;margin-top:18px}"
            "label{display:block;font-weight:650;margin:14px 0 6px}"
            "input,select,button{font:inherit;border-radius:6px;border:1px solid #aeb8a8;padding:10px 12px;background:white;color:#172016;box-sizing:border-box}"
            "input,select{width:100%}"
            "input[type=range]{padding:0}"
            "input[type=checkbox]{width:auto;margin-right:8px}"
            "button{cursor:pointer;background:#1f6f58;color:white;border-color:#1f6f58}"
            "button.secondary{background:#fff;color:#172016;border-color:#aeb8a8}"
            ".row{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:end}"
            ".actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:14px}"
            ".inline{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
            ".note{color:#526151;font-size:.94rem}"
            ".hidden{display:none}"
            ".msg{min-height:1.4em;margin-top:10px;color:#1f6f58}"
            "@media(max-width:560px){body{padding:16px}.row{grid-template-columns:1fr}button{width:100%}}"
            "@media(prefers-color-scheme:dark){:root{background:#101510;color:#eef5eb}input,select,button.secondary{background:#182018;color:#eef5eb;border-color:#536050}.status,.note{color:#b7c4b2}}"
            "</style></head><body><main><h1>WiFi Clock</h1><p class='status'>");
  html += connected ? "Connected to " + htmlEscape(WiFi.SSID()) + " at " + WiFi.localIP().toString()
                    : "Not connected to WiFi";
  if (pairingMode) {
    html += F(" - Setup portal active");
  }
  html += F(" - Firmware ");
  html += F(WIFI_CLOCK_VERSION);
  html += F("</p><section><h2>Clock Settings</h2><form id='settingsForm'>"
            "<label class='inline'><input id='timezoneAuto' name='timezoneAuto' type='checkbox'");
  if (timezoneAuto) {
    html += F(" checked");
  }
  html += F(">Auto-detect timezone</label>"
            "<div id='timezoneBox'><label for='tzPreset'>Timezone</label><select id='tzPreset' name='timezoneName'>");
  appendTimezoneOption(html, "America/New_York", "Eastern Time", "EST5EDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "America/Chicago", "Central Time", "CST6CDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "America/Denver", "Mountain Time", "MST7MDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "America/Phoenix", "Arizona Time", "MST7");
  appendTimezoneOption(html, "America/Los_Angeles", "Pacific Time", "PST8PDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "America/Anchorage", "Alaska Time", "AKST9AKDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "Pacific/Honolulu", "Hawaii Time", "HST10");
  appendTimezoneOption(html, "America/Halifax", "Atlantic Time", "AST4ADT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "America/St_Johns", "Newfoundland Time", "NST3:30NDT,M3.2.0,M11.1.0");
  appendTimezoneOption(html, "Europe/London", "United Kingdom", "GMT0BST,M3.5.0/1,M10.5.0");
  appendTimezoneOption(html, "Europe/Berlin", "Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3");
  appendTimezoneOption(html, "Europe/Athens", "Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4");
  appendTimezoneOption(html, "Asia/Dubai", "Dubai / Gulf", "<+04>-4");
  appendTimezoneOption(html, "Asia/Kolkata", "India", "IST-5:30");
  appendTimezoneOption(html, "Asia/Bangkok", "Thailand / Indochina", "<+07>-7");
  appendTimezoneOption(html, "Asia/Shanghai", "China", "CST-8");
  appendTimezoneOption(html, "Asia/Tokyo", "Japan", "JST-9");
  appendTimezoneOption(html, "Australia/Sydney", "Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3");
  appendTimezoneOption(html, "Pacific/Auckland", "New Zealand", "NZST-12NZDT,M9.5.0,M4.1.0/3");
  html += F("</select></div><input id='timezone' name='timezone' type='hidden' value='");
  html += htmlEscape(timezoneSpec);
  html += F("'><input id='detectedTimezoneName' name='detectedTimezoneName' type='hidden' value=''>"
            "<label for='brightness'>Brightness: <span id='brightnessValue'></span></label>"
            "<input id='brightness' name='brightness' type='range' min='1' max='8' value='");
  html += String(brightness);
  html += F("'><label class='inline'><input id='colonBlink' name='colonBlink' type='checkbox'");
  if (colonBlink) {
    html += F(" checked");
  }
  html += F(">Blink colon</label><label class='inline'><input id='use24Hour' name='use24Hour' type='checkbox'");
  if (use24Hour) {
    html += F(" checked");
  }
  html += F(">24-hour mode</label><label class='inline' id='pmIndicatorLabel'><input id='pmIndicator' name='pmIndicator' type='checkbox'");
  if (pmIndicator) {
    html += F(" checked");
  }
  html += F(">PM indicator</label><p class='note'>Optional: uses the rightmost decimal point. Some TM1637 clock modules do not wire this segment.</p>"
            "<button type='submit'>Save display settings</button>"
            "<div id='settingsMsg' class='msg'></div></form></section>"
            "<section><h2>WiFi Network</h2><form id='wifiForm'>"
            "<div class='row'><div><label for='ssidSelect'>Network</label><select id='ssidSelect'><option value=''></option><option value='__manual'>Manual Entry</option></select></div>"
            "<button class='secondary' type='button' id='scanBtn'>Scan</button></div>"
            "<label for='ssid'>SSID</label><input id='ssid' name='ssid' autocomplete='off' value='");
  html += htmlEscape(wifiSsid);
  html += F("'><label for='password'>Password</label><input id='password' name='password' type='text' autocomplete='current-password' value='");
  html += htmlEscape(wifiPassword);
  html += F("'><p class='note'>Saving WiFi restarts the network connection. Leave password blank only for an open network.</p>"
            "<button type='submit'>Save WiFi</button><div id='wifiMsg' class='msg'></div></form></section>"
            "<section><h2>MQTT</h2><form id='mqttForm'>"
            "<label class='inline'><input id='mqttEnabled' name='mqttEnabled' type='checkbox'");
  if (mqttEnabled) {
    html += F(" checked");
  }
  html += F(">Enable MQTT</label><div id='mqttBox'>"
            "<label for='mqttHost'>Host</label><input id='mqttHost' name='mqttHost' type='text' value='");
  html += htmlEscape(mqttHost);
  html += F("' placeholder='homeassistant.local'>"
            "<label for='mqttUser'>Username</label><input id='mqttUser' name='mqttUser' type='text' value='");
  html += htmlEscape(mqttUser);
  html += F("'><label for='mqttPassword'>Password</label><input id='mqttPassword' name='mqttPassword' type='text' value='");
  html += htmlEscape(mqttPassword);
  html += F("'><p class='note'>Use an IP address or hostname. Port 1883 is used unless you add :port. Home Assistant discovery creates entities for brightness, Blink Colon, 24-hour Mode, and PM Indicator.</p></div>"
            "<button type='submit' id='mqttSaveBtn'>Save and test MQTT</button><div id='mqttMsg' class='msg'></div></form></section>"
            "<section><h2>Firmware</h2><form method='POST' action='/update' enctype='multipart/form-data'>"
            "<label for='firmware'>Firmware .bin</label><input id='firmware' type='file' name='firmware' accept='.bin,application/octet-stream'>"
            "<div class='actions'><button type='submit' id='flashBtn'>Flash firmware</button><button class='secondary' type='button' id='resetBtn'>Reset and reboot</button></div></form>"
            "<div id='firmwareMsg' class='msg'></div></section>"
            "<section><h2>API</h2><p class='note'>GET /api/brightness returns brightness. POST /api/brightness?value=1..8 sets brightness. GET /api/colon returns Blink Colon. POST /api/colon?value=on or off changes it. GET /api/settings returns the full current configuration.</p></section>"
            "<section><h2>Debugging</h2><button type='button' id='displayTestBtn'>Test Display</button><div id='debugMsg' class='msg'></div></section><script>const pairing=");
  html += pairingMode ? "true" : "false";
  html += F(",tzMap={"
            "'America/New_York':'EST5EDT,M3.2.0,M11.1.0','America/Chicago':'CST6CDT,M3.2.0,M11.1.0','America/Denver':'MST7MDT,M3.2.0,M11.1.0','America/Phoenix':'MST7','America/Los_Angeles':'PST8PDT,M3.2.0,M11.1.0','America/Anchorage':'AKST9AKDT,M3.2.0,M11.1.0','Pacific/Honolulu':'HST10','America/Halifax':'AST4ADT,M3.2.0,M11.1.0','America/St_Johns':'NST3:30NDT,M3.2.0,M11.1.0','Europe/London':'GMT0BST,M3.5.0/1,M10.5.0','Europe/Berlin':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Athens':'EET-2EEST,M3.5.0/3,M10.5.0/4','Asia/Dubai':'<+04>-4','Asia/Kolkata':'IST-5:30','Asia/Bangkok':'<+07>-7','Asia/Shanghai':'CST-8','Asia/Tokyo':'JST-9','Australia/Sydney':'AEST-10AEDT,M10.1.0,M4.1.0/3','Pacific/Auckland':'NZST-12NZDT,M9.5.0,M4.1.0/3'};"
            "const tzPreset=document.getElementById('tzPreset'),tz=document.getElementById('timezone'),autoTz=document.getElementById('timezoneAuto'),detTz=document.getElementById('detectedTimezoneName'),tzBox=document.getElementById('timezoneBox'),b=document.getElementById('brightness'),bv=document.getElementById('brightnessValue'),use24=document.getElementById('use24Hour'),pm=document.getElementById('pmIndicator'),pmLabel=document.getElementById('pmIndicatorLabel'),mqttEn=document.getElementById('mqttEnabled'),mqttBox=document.getElementById('mqttBox'),mqttSave=document.getElementById('mqttSaveBtn');"
            "function syncB(){bv.textContent=b.value} b.addEventListener('input',syncB); syncB();"
            "function syncMqtt(){mqttBox.classList.toggle('hidden',!mqttEn.checked);mqttSave.classList.toggle('hidden',!mqttEn.checked)}mqttEn.addEventListener('change',syncMqtt);syncMqtt();"
            "function detectedTz(){try{return Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(e){return ''}}"
            "function syncPm(){pm.disabled=use24.checked;pmLabel.style.opacity=use24.checked?'.55':'1';if(use24.checked)pm.checked=false}"
            "function applyTz(){let id=autoTz.checked?(detectedTz()||tzPreset.value):tzPreset.value;if(!tzMap[id])id=tzPreset.value;if(tzMap[id]){tzPreset.value=id;tz.value=tzMap[id];detTz.value=id}tzPreset.disabled=autoTz.checked;tzPreset.style.opacity=tzPreset.disabled?'.65':'1';tzBox.classList.toggle('hidden',pairing&&autoTz.checked)}"
            "autoTz.addEventListener('change',applyTz);tzPreset.addEventListener('change',applyTz);use24.addEventListener('change',syncPm);applyTz();syncPm();"
            "if(autoTz.checked){let body=new URLSearchParams({timezone:tz.value,timezoneName:detTz.value,timezoneAuto:'1',brightness:b.value,colonBlink:document.getElementById('colonBlink').checked?'1':'0',use24Hour:document.getElementById('use24Hour').checked?'1':'0',pmIndicator:document.getElementById('pmIndicator').checked?'1':'0'});fetch('/api/settings',{method:'POST',body}).catch(()=>{});}"
            "document.getElementById('settingsForm').addEventListener('submit',async e=>{e.preventDefault();applyTz();let body=new URLSearchParams({timezone:tz.value,timezoneName:detTz.value,timezoneAuto:autoTz.checked?'1':'0',brightness:b.value,colonBlink:document.getElementById('colonBlink').checked?'1':'0',use24Hour:document.getElementById('use24Hour').checked?'1':'0',pmIndicator:document.getElementById('pmIndicator').checked?'1':'0'});let r=await fetch('/api/settings',{method:'POST',body});document.getElementById('settingsMsg').textContent=await r.text();});"
            "const ssidSel=document.getElementById('ssidSelect'),ssidInput=document.getElementById('ssid');function syncSsidLock(){let manual=ssidSel.value==='__manual';ssidInput.disabled=!manual&&ssidInput.value.length>0;ssidInput.style.opacity=ssidInput.disabled?'.65':'1'}syncSsidLock();"
            "document.getElementById('scanBtn').addEventListener('click',async()=>{let msg=document.getElementById('wifiMsg'),sel=ssidSel;msg.textContent='Scanning...';let r=await fetch('/api/scan');let j=await r.json();let current=ssidInput.value;sel.innerHTML='<option value=\"\"></option><option value=\"__manual\">Manual Entry</option>';j.networks.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';if(n.ssid===current)o.selected=true;sel.appendChild(o)});msg.textContent='Scan complete';syncSsidLock();});"
            "ssidSel.addEventListener('change',e=>{if(e.target.value==='__manual'){ssidInput.disabled=false;ssidInput.focus()}else if(e.target.value){ssidInput.value=e.target.value}syncSsidLock();});"
            "document.getElementById('wifiForm').addEventListener('submit',async e=>{e.preventDefault();let body=new URLSearchParams({ssid:document.getElementById('ssid').value,password:document.getElementById('password').value});let r=await fetch('/api/wifi',{method:'POST',body});document.getElementById('wifiMsg').textContent=await r.text();});"
            "document.getElementById('mqttForm').addEventListener('submit',async e=>{e.preventDefault();let msg=document.getElementById('mqttMsg');msg.textContent='Testing MQTT...';let body=new URLSearchParams({mqttEnabled:mqttEn.checked?'1':'0',mqttHost:document.getElementById('mqttHost').value,mqttUser:document.getElementById('mqttUser').value,mqttPassword:document.getElementById('mqttPassword').value});let r=await fetch('/api/mqtt-test',{method:'POST',body});msg.textContent=await r.text();});"
            "document.querySelector('form[action=\"/update\"]').addEventListener('submit',()=>{document.getElementById('firmwareMsg').textContent='Uploading firmware...';document.getElementById('flashBtn').disabled=true;});"
            "document.getElementById('resetBtn').addEventListener('click',async()=>{if(!confirm('Remove saved settings and reboot?'))return;let r=await fetch('/api/reset',{method:'POST'});document.getElementById('firmwareMsg').textContent=await r.text();});"
            "document.getElementById('displayTestBtn').addEventListener('click',async()=>{let r=await fetch('/api/display-test',{method:'POST'});document.getElementById('debugMsg').textContent=await r.text();});"
            "</script></main></body></html>");
  return html;
}

void sendRoot() {
  if (shouldRedirectToPortal()) {
    redirectToPortal();
    return;
  }
  server.send(200, "text/html", pageHtmlV2());
}

void sendSettingsJson() {
  String json = "{";
  json += "\"version\":\"" WIFI_CLOCK_VERSION "\",";
  json += "\"timezone\":\"" + jsonEscape(timezoneSpec) + "\",";
  json += "\"timezoneName\":\"" + jsonEscape(timezoneName) + "\",";
  json += "\"timezoneAuto\":" + String(timezoneAuto ? "true" : "false") + ",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"colonBlink\":" + String(colonBlink ? "true" : "false") + ",";
  json += "\"use24Hour\":" + String(use24Hour ? "true" : "false") + ",";
  json += "\"pmIndicator\":" + String(pmIndicator ? "true" : "false") + ",";
  json += "\"mqttEnabled\":" + String(mqttEnabled ? "true" : "false") + ",";
  json += "\"mqttHost\":\"" + jsonEscape(mqttHost) + "\",";
  json += "\"mqttUser\":\"" + jsonEscape(mqttUser) + "\",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"savedSsid\":\"" + jsonEscape(wifiSsid) + "\",";
  json += "\"savedPassword\":\"" + jsonEscape(wifiPassword) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

int argInt(const String &name, int fallback) {
  if (server.hasArg(name)) {
    return server.arg(name).toInt();
  }
  String body = server.arg("plain");
  int at = body.indexOf(name);
  if (at >= 0) {
    int colon = body.indexOf(':', at);
    if (colon >= 0) {
      return body.substring(colon + 1).toInt();
    }
  }
  return fallback;
}

void handleBrightness() {
  int value = argInt("value", argInt("brightness", -1));
  if (value < 1 || value > 8) {
    server.send(400, "text/plain", "Brightness must be 1 through 8");
    return;
  }
  brightness = static_cast<uint8_t>(value);
  saveDisplaySettings();
  display.setBrightness(brightness - 1, true);
  mqttPublishStates();
  server.send(200, "text/plain", "Brightness saved");
}

void handleBrightnessGet() {
  String json = "{\"brightness\":" + String(brightness) + "}";
  server.send(200, "application/json", json);
}

void handleColonGet() {
  String json = "{\"colonBlink\":" + String(colonBlink ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleColonPost() {
  String value = server.hasArg("value") ? server.arg("value") : server.arg("enabled");
  value.trim();
  value.toLowerCase();
  if (value.isEmpty()) {
    colonBlink = !colonBlink;
  } else if (value == "1" || value == "true" || value == "on" || value == "yes") {
    colonBlink = true;
  } else if (value == "0" || value == "false" || value == "off" || value == "no") {
    colonBlink = false;
  } else {
    server.send(400, "text/plain", "Use value=on or value=off");
    return;
  }
  saveDisplaySettings();
  mqttPublishStates();
  server.send(200, "text/plain", "Blink Colon saved");
}

void handleSettingsPost() {
  if (server.hasArg("timezone")) {
    timezoneSpec = server.arg("timezone");
    timezoneSpec.trim();
    if (timezoneSpec.isEmpty()) {
      timezoneSpec = DEFAULT_TZ;
    }
  }
  if (server.hasArg("timezoneName")) {
    timezoneName = server.arg("timezoneName");
    timezoneName.trim();
    if (timezoneName.isEmpty()) {
      timezoneName = "America/Denver";
    }
  }
  timezoneAuto = server.hasArg("timezoneAuto") && server.arg("timezoneAuto") == "1";
  int value = argInt("brightness", brightness);
  brightness = static_cast<uint8_t>(constrain(value, 1, 8));
  colonBlink = server.hasArg("colonBlink") && server.arg("colonBlink") == "1";
  use24Hour = server.hasArg("use24Hour") && server.arg("use24Hour") == "1";
  pmIndicator = !use24Hour && server.hasArg("pmIndicator") && server.arg("pmIndicator") == "1";
  saveDisplaySettings();
  display.setBrightness(brightness - 1, true);
  configureTimeSync();
  mqttPublishStates();
  server.send(200, "text/plain", "Display settings saved");
}

void handleMqttTestPost() {
  mqttEnabled = server.hasArg("mqttEnabled") && server.arg("mqttEnabled") == "1";
  if (server.hasArg("mqttHost")) {
    mqttHost = server.arg("mqttHost");
    mqttHost.trim();
  }
  if (server.hasArg("mqttUser")) {
    mqttUser = server.arg("mqttUser");
    mqttUser.trim();
  }
  if (server.hasArg("mqttPassword")) {
    mqttPassword = server.arg("mqttPassword");
  }
  mqttUrl = "";
  saveDisplaySettings();
  mqttDiscoverySent = false;

  if (!mqttEnabled) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    server.send(200, "text/plain", "MQTT disabled and settings saved");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain", "MQTT settings saved, but WiFi is not connected");
    return;
  }

  MqttConfig config;
  if (!parseMqttUrl(config)) {
    server.send(400, "text/plain", "MQTT host is required");
    return;
  }

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  mqttClient.setServer(config.host.c_str(), config.port);
  mqttClient.setCallback(mqttCallback);
  String clientId = "clock-test-" + deviceId();
  String base = mqttBaseTopic();
  bool connected = config.user.length()
    ? mqttClient.connect(clientId.c_str(), config.user.c_str(), config.password.c_str(), (base + "/status").c_str(), 0, true, "offline")
    : mqttClient.connect(clientId.c_str(), (base + "/status").c_str(), 0, true, "offline");
  if (!connected) {
    int state = mqttClient.state();
    String message = "MQTT settings saved, but connection to ";
    message += config.host + ":" + String(config.port) + " failed: " + mqttStateMessage(state);
    if (state == MQTT_CONNECT_FAILED || state == MQTT_CONNECTION_TIMEOUT) {
      message += ". If you used a .local name, try the broker IP address.";
    }
    server.send(503, "text/plain", message);
    return;
  }
  mqttClient.publish((base + "/status").c_str(), "online", true);
  mqttClient.subscribe((base + "/brightness/set").c_str());
  mqttClient.subscribe((base + "/colon/set").c_str());
  mqttClient.subscribe((base + "/use24/set").c_str());
  mqttClient.subscribe((base + "/pm/set").c_str());
  mqttPublishDiscovery();
  mqttPublishStates();
  server.send(200, "text/plain", "MQTT connected. Home Assistant discovery published.");
}

void handleResetPost() {
  prefs.clear();
  server.send(200, "text/plain", "Settings cleared. Rebooting.");
  delay(800);
  ESP.restart();
}

void handleDisplayTestPost() {
  displayTestUntilMs = millis() + 5000;
  uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
  display.setSegments(data);
  server.send(200, "text/plain", "Display test started");
}

void handleWifiPost() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  ssid.trim();
  if (ssid.isEmpty()) {
    server.send(400, "text/plain", "SSID is required");
    return;
  }
  saveWifiSettings(ssid, password);
  ntpConfigured = false;
  hadWifiConnection = false;
  mdnsStarted = false;
  MDNS.end();
  Serial.println("mDNS stopped for WiFi change");
  WiFi.disconnect(true, false);
  delay(250);
  connectWifi();
  server.send(200, "text/plain", "WiFi saved; connecting now");
}

void handleScan() {
  int count = WiFi.scanNetworks(false, true);
  String json = "{\"networks\":[";
  for (int i = 0; i < count; ++i) {
    if (i) {
      json += ',';
    }
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":";
    json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleCaptiveProbe() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/html", "<html><head><meta http-equiv='refresh' content='0; url=/'></head><body></body></html>");
}

void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "Update complete. Rebooting." : "Update failed.");
  if (ok) {
    delay(800);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA upload started: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("OTA upload complete: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleNotFound() {
  if (pairingMode) {
    redirectToPortal();
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void setupRoutes() {
  server.on("/", HTTP_GET, sendRoot);
  server.on("/api/settings", HTTP_GET, sendSettingsJson);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/brightness", HTTP_GET, handleBrightnessGet);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/colon", HTTP_GET, handleColonGet);
  server.on("/api/colon", HTTP_POST, handleColonPost);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifiPost);
  server.on("/api/reset", HTTP_POST, handleResetPost);
  server.on("/api/display-test", HTTP_POST, handleDisplayTestPost);
  server.on("/api/mqtt-test", HTTP_POST, handleMqttTestPost);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  server.on("/fwlink", HTTP_GET, handleCaptiveProbe);
  server.onNotFound(handleNotFound);
  server.begin();
}

void showUnsyncedDisplay() {
  uint8_t data[] = {SEG_G, SEG_G, SEG_G, SEG_G};
  bool colonOn = colonBlink ? ((millis() / 500) % 2 == 0) : true;
  if (colonOn) {
    data[1] |= 0x80;
  }
  display.setSegments(data);
}

void updateDisplay() {
  if (millis() - lastDisplayMs < DISPLAY_REFRESH_MS) {
    return;
  }
  lastDisplayMs = millis();

  if (displayTestUntilMs && millis() < displayTestUntilMs) {
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    display.setSegments(data);
    return;
  }
  displayTestUntilMs = 0;

  struct tm localTime;
  if (!getLocalTime(&localTime, 5)) {
    showUnsyncedDisplay();
    return;
  }

  int hour = use24Hour ? localTime.tm_hour : localTime.tm_hour % 12;
  if (!use24Hour && hour == 0) {
    hour = 12;
  }
  int value = hour * 100 + localTime.tm_min;
  bool colonOn = colonBlink ? (localTime.tm_sec % 2 == 0) : true;
  bool pmOn = pmIndicator && localTime.tm_hour >= 12;

  if (!use24Hour && hour < 10) {
    uint8_t data[4];
    data[0] = 0x00;
    data[1] = display.encodeDigit(hour);
    data[2] = display.encodeDigit(localTime.tm_min / 10);
    data[3] = display.encodeDigit(localTime.tm_min % 10);
    if (colonOn) {
      data[1] |= 0x80;
    }
    if (pmOn) {
      data[3] |= SEG_DP;
    }
    display.setSegments(data);
  } else {
    uint8_t data[4];
    data[0] = display.encodeDigit(value / 1000);
    data[1] = display.encodeDigit((value / 100) % 10);
    data[2] = display.encodeDigit((value / 10) % 10);
    data[3] = display.encodeDigit(value % 10);
    if (colonOn) {
      data[1] |= 0x80;
    }
    if (pmOn) {
      data[3] |= SEG_DP;
    }
    display.setSegments(data);
  }
}

void maintainWifi() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!hadWifiConnection) {
      Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
      hadWifiConnection = true;
      configureTimeSync();
      startMdns();
      if (pairingMode && !wifiSsid.isEmpty()) {
        stopPairingMode();
      }
    }
    return;
  }

  if (!wifiSsid.isEmpty() && millis() - lastWifiAttemptMs > WIFI_RETRY_INTERVAL_MS) {
    connectWifi();
  }

  if (!hadWifiConnection && !wifiSsid.isEmpty() && !pairingMode &&
      millis() - bootMs > WIFI_BOOT_PAIRING_DELAY_MS) {
    startPairingMode();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  bootMs = millis();
  loadSettings();
  applyTimezone();
  mqttClient.setBufferSize(1024);

  display.setBrightness(brightness - 1, true);
  display.clear();
  showUnsyncedDisplay();

  WiFi.persistent(false);
  if (wifiSsid.isEmpty()) {
    startPairingMode();
  } else {
    connectWifi();
  }
  setupRoutes();
}

void loop() {
  if (pairingMode) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  maintainWifi();
  maintainMqtt();
  updateDisplay();
}
