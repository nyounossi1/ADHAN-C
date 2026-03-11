/*
 * WifiManager.cpp — WiFi STA/AP, captive portal, session runners.
 */
#include "WifiManager.h"
#include "PrayerEngine.h"
#include "FotaManager.h"
#include "DFPlayerManager.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Module-private portal state
static DNSServer dnsServer;
static WebServer webServer(80);
static volatile bool g_doScan = false;
static volatile bool g_connectRequested = false;
static String g_reqSsid, g_reqPass;

// Forward declarations
void handleScanStart();
void handleScanJson();

int g_apOnboardPage = 0;
const int AP_ONBOARD_PAGES = 5;
const char* AP_SSID = "Adhan-AI";
static const byte DNS_PORT = 53;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;  // 15 seconds per attempt
static constexpr uint8_t  MAX_WIFI_RETRIES = 3;             // Try 3 times before giving up

// ============================================================================
// WiFi Radio Control
// ============================================================================
void wifiRadioOff() {
  LOGI(LOG_TAG_WIFI, "WiFi radio OFF");
  WiFi.disconnect(true, true);     // drop STA + forget current connection state
  vTaskDelay(pdMS_TO_TICKS(50));
  WiFi.mode(WIFI_OFF);             // <--- key for power
  // Optional extra (sometimes helps): btStop(); // if BT not used
}

void wifiRadioOnSta() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);             // modem sleep while connected (still higher than OFF)
  // Optional tuning:
  // WiFi.setTxPower(WIFI_POWER_11dBm); // reduce peak current (range tradeoff)
}

// ============================================================================
// STA Connect
// ============================================================================
bool connectStaOnce(const String& ssid, const String& pass,uint32_t timeoutMs) {
  LOGI(LOG_TAG_WIFI, "STA connect attempt SSID='%s'", ssid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  vTaskDelay(pdMS_TO_TICKS(200));

  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      LOGI(LOG_TAG_WIFI, "STA connected IP=%s", WiFi.localIP().toString().c_str());
      
      // Clear WiFi failure banner on successful connection
      if (g_wifiConnectFailed) {
        g_wifiConnectFailed = false;
        forceIdleRedraw = true;
      }
      
      sendUi(UI_EVT_WIFI_STA_CONNECTED);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  LOGW(LOG_TAG_WIFI, "STA connect failed status=%d", (int)WiFi.status());
  return false;
}

// ============================================================================
// HTML helpers (portal-private)
// ============================================================================
String htmlEscape(const String& s) {
  String o = s;
  o.replace("&","&amp;");
  o.replace("<","&lt;");
  o.replace(">","&gt;");
  o.replace("\"","&quot;");
  o.replace("'","&#39;");
  return o;
}

String htmlOptionInt(const char* label, int value, int selected) {
  String o = "<option value='";
  o += String(value);
  o += "'";
  if (value == selected) o += " selected";
  o += ">";
  o += htmlEscape(String(label));
  o += "</option>";
  return o;
}

// ============================================================================
// Portal HTML Builder
// ============================================================================
String buildPortalPage(const String& msg,
                              const String& ssidPrefill,
                              int methodSel,
                              int schoolSel,
                              int latAdjSel) {
  String page;

  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Adhan-AI Setup</title>");

  page += F("<style>"
            "body{font-family:Arial,Helvetica,sans-serif;margin:16px;background:#fafafa;color:#111}"
            "h2{margin:0 0 10px 0}"
            ".card{max-width:720px;margin:0 auto;background:#fff;padding:14px 14px 10px 14px;"
            "border:1px solid #ddd;border-radius:10px;box-shadow:0 1px 3px rgba(0,0,0,0.06)}"
            "fieldset{border:1px solid #ddd;border-radius:10px;padding:12px;margin:12px 0}"
            "legend{padding:0 8px;font-weight:700}"
            "label{font-weight:600;display:block;margin:8px 0 6px 0}"
            "input,select{width:100%;padding:10px;border:1px solid #ccc;border-radius:8px;font-size:16px;box-sizing:border-box}"
            ".row{display:flex;gap:10px;flex-wrap:wrap}"
            ".col{flex:1;min-width:220px}"
            ".btn{display:inline-block;padding:10px 14px;border-radius:10px;border:0;font-weight:700;font-size:16px;cursor:pointer}"
            ".btn-primary{background:#1976d2;color:#fff}"
            ".btn-secondary{background:#eee;color:#111}"
            ".small{font-size:12px;color:#555;margin-top:6px}"
            ".radioRow label{font-weight:normal;display:block;margin:4px 0}"
            "a{color:#1976d2;text-decoration:none}"
            "hr{border:none;border-top:1px solid #eee;margin:12px 0}"
            "</style></head>");

  page += F("<body><div class='card'>");
  page += F("<h2>Adhan-AI Setup</h2>");

  if (msg.length()) {
    page += F("<p><b>");
    page += htmlEscape(msg);
    page += F("</b></p>");
  }

  // Main connect form
  page += F("<form method='POST' action='/connect' onsubmit='return syncSsidBeforeSubmit();'>");

  // --- Wi-Fi Settings ---
  page += F("<fieldset><legend>Wi-Fi Settings</legend>");

  // Hidden field actually submitted to /connect (keeps backend unchanged)
  page += F("<input type='hidden' name='ssid' id='ssidFinal' value='");
  page += htmlEscape(ssidPrefill);
  page += F("'>");

  page += F("<label>Wi-Fi SSID</label>");

  page += F("<div class='radioRow'>");
  page += F("<label><input type='radio' name='ssid_mode' id='modeScan' value='scan' checked> ");
  page += F("Select from scanned networks</label>");
  page += F("<label><input type='radio' name='ssid_mode' id='modeManual' value='manual'> ");
  page += F("Enter SSID manually</label>");
  page += F("</div>");

  page += F("<select id='ssidSelect'>");
  page += F("<option value=''>-- Scanning... --</option>");
  page += F("</select>");

  page += F("<input id='ssidManual' type='text' placeholder='Type SSID' disabled "
            "style='margin-top:10px;display:none;'>");

  page += F("<div class='small' id='scanStatus'>Not scanned yet.</div>");

  // Password under SSID (as requested)
  page += F("<label for='pass' style='margin-top:12px;'>Wi-Fi Password</label>");
  page += F("<input id='pass' name='pass' type='password' placeholder='Enter password'>");
  page += F("<label style='font-weight:normal;margin:6px 0 0 0;display:flex;align-items:center;gap:6px;'>"
            "<input type='checkbox' id='showPass' style='width:auto;'> Show password</label>");
  page += F("<div class='small'>Leave blank only if your Wi-Fi is open.</div>");

  page += F("</fieldset>");

  // --- Prayer Settings ---
  page += F("<fieldset><legend>Prayer Settings</legend>");

  page += F("<label for='pmethod'>Prayer Calculation</label>");
  page += F("<select name='pmethod' id='pmethod'>");
  for (int i = 0; i < 24; i++) {
    const char* name = (kMethodNames[i] && kMethodNames[i][0]) ? kMethodNames[i] : nullptr;
    if (!name) continue;
    page += htmlOptionInt(name, i, methodSel);
  }
  page += F("</select>");

  page += F("<div class='row'>");

  page += F("<div class='col'>");
  page += F("<label for='pschool'>Asr School</label>");
  page += F("<select name='pschool' id='pschool'>");
  page += htmlOptionInt(kSchoolNames[0], 0, schoolSel);
  page += htmlOptionInt(kSchoolNames[1], 1, schoolSel);
  page += F("</select>");
  page += F("</div>");

  page += F("<div class='col'>");
  page += F("<label for='platadj'>High Latitude Rule</label>");
  page += F("<select name='platadj' id='platadj'>");
  for (int i = 0; i < 4; i++) {
    page += htmlOptionInt(kLatAdjNames[i], i, latAdjSel);
  }
  page += F("</select>");
  page += F("</div>");

  page += F("</div>"); // row

  // ---- NEW: Timezone dropdown (IANA) ----
  page += F("<label for='tz' style='margin-top:12px;'>Timezone</label>");
  page += F("<select name='tz' id='tz'>");

  // Auto option (blank => Auto)
  page += F("<option value=''");
  if (g_tzOverride.length() == 0) page += F(" selected");
  page += F(">Auto (from Wi-Fi/IP)</option>");

  // Build dropdown from IANA_TO_POSIX array (only those you support)
  for (int i = 0; i < IANA_TO_POSIX_COUNT; i++) {
    const char* iana = IANA_TO_POSIX[i].iana;
    page += F("<option value='");
    page += htmlEscape(String(iana));
    page += F("'");
    if (g_tzOverride.length() && g_tzOverride == iana) page += F(" selected");
    page += F(">");
    page += htmlEscape(String(iana));
    page += F("</option>");
  }

  page += F("</select>");
  page += F("<div class='small'>Choose <b>Auto</b> for automatic detection. Dropdown only shows supported zones.</div>");

  page += F("</fieldset>");

  page += F("<button class='btn btn-primary' type='submit'>Save & Connect</button>");
  page += F("</form>");

  // Keep a small link to the legacy results page as fallback (optional)
  page += F("<p class='small' style='margin-top:10px;'>Trouble scanning? <a href='/results'>View scan results</a></p>");

  // ---- JS: scan + dropdown + radio gating ----
  page += F("<script>"
            "const sel=document.getElementById('ssidSelect');"
            "const man=document.getElementById('ssidManual');"
            "const fin=document.getElementById('ssidFinal');"
            "const st=document.getElementById('scanStatus');"
            "const modeScan=document.getElementById('modeScan');"
            "const modeManual=document.getElementById('modeManual');"
            "const passField=document.getElementById('pass');"
            "const showPass=document.getElementById('showPass');"
            ""
            "function setMode(){"
            "  const manual=modeManual.checked;"
            "  sel.disabled=manual;"
            "  sel.style.display=manual?'none':'';"
            "  man.disabled=!manual;"
            "  man.style.display=manual?'':'none';"
            "  if(manual){ fin.value=(man.value||''); }"
            "  else { fin.value=(sel.value||''); }"
            "}"
            ""
            "showPass.addEventListener('change',()=>{"
            "  passField.type=showPass.checked?'text':'password';"
            "});"
            ""
            "function syncSsidBeforeSubmit(){"
            "  setMode();"
            "  if(!fin.value){"
            "    alert('Please select a Wi-Fi network or enter SSID manually.');"
            "    return false;"
            "  }"
            "  return true;"
            "}"
            ""
            "modeScan.addEventListener('change',setMode);"
            "modeManual.addEventListener('change',setMode);"
            "sel.addEventListener('change',()=>{ fin.value=(sel.value||''); });"
            "man.addEventListener('input',()=>{ if(modeManual.checked) fin.value=(man.value||''); });"
            ""
            "async function startScan(){"
            "  st.textContent='Starting scan...';"
            "  try{ await fetch('/scan_start',{method:'POST'}); }catch(e){}"
            "  pollScan(0);"
            "}"
            ""
            "async function pollScan(tryNo){"
            "  try{"
            "    const r=await fetch('/scan.json',{cache:'no-store'});"
            "    const j=await r.json();"
            "    if(j.running){"
            "      st.textContent='Scanning...';"
            "      if(tryNo<30) setTimeout(()=>pollScan(tryNo+1),500);"
            "      else st.textContent='Scan timeout. Tap Scan again.';"
            "      return;"
            "    }"
            "    const nets=j.nets||[];"
            "    sel.innerHTML='';"
            "    const opt0=document.createElement('option');"
            "    opt0.value='';"
            "    opt0.textContent = nets.length ? '-- Select --' : '-- No networks found --';"
            "    sel.appendChild(opt0);"
            "    nets.sort((a,b)=> (b.rssi||-999)-(a.rssi||-999));"
            "    for(const n of nets){"
            "      const o=document.createElement('option');"
            "      o.value=n.ssid;"
            "      o.textContent = n.ssid + ' (' + n.rssi + ' dBm)';"
            "      sel.appendChild(o);"
            "    }"
            "    st.textContent = nets.length ? ('Found ' + nets.length + ' networks.') : 'No networks found.';"
            "    setMode();"
            "  }catch(e){"
            "    st.textContent='Scan read error. Tap Scan again.';"
            "  }"
            "}"
            ""
            "setMode();"
            "startScan();"
            "</script>");

  page += F("</div></body></html>");
  return page;
}

// ============================================================================
// URL encoding + scan results page
// ============================================================================
String urlEncode(const String& s) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);

  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    // Unreserved RFC3986: ALPHA / DIGIT / "-" / "." / "_" / "~"
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '.' || c == '_' || c == '~';
    if (ok) out += (char)c;
    else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

String buildScanResultsPage(int n) {
  String page;
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Scan Results</title>");
  page += F("<style>"
            "body{font-family:Arial,Helvetica,sans-serif;margin:16px;background:#fafafa;color:#111}"
            ".card{max-width:720px;margin:0 auto;background:#fff;padding:14px;border:1px solid #ddd;border-radius:10px}"
            "a{color:#1976d2;text-decoration:none}"
            "ul{padding-left:18px}"
            ".btn{display:inline-block;padding:10px 14px;border-radius:10px;border:0;font-weight:700;font-size:16px;cursor:pointer}"
            ".btn-secondary{background:#eee;color:#111}"
            ".small{font-size:12px;color:#555;margin-top:6px}"
            "</style></head><body>");

  page += F("<div class='card'>");
  page += F("<h2>Scan Results</h2>");
  page += F("<p class='small'>Tip: Use the main setup page for the dropdown + manual SSID option.</p>");

  if (n <= 0) {
    page += F("<p>No networks found.</p>");
  } else {
    page += F("<p>Select a network to prefill the SSID:</p><ul>");
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String ssidEnc = urlEncode(ssid);

      page += F("<li><a href='/?ssid=");
      page += ssidEnc;
      page += F("'>");
      page += htmlEscape(ssid);
      page += F("</a> (");
      page += String(rssi);
      page += F(" dBm)</li>");
    }
    page += F("</ul>");
  }

  page += F("<form method='POST' action='/scan_start'>"
            "<button class='btn btn-secondary' type='submit'>Scan again</button>"
            "</form>");

  page += F("<p style='margin-top:10px;'><a href='/'>Back to setup</a></p>");
  page += F("</div></body></html>");
  return page;
}

void handleRoot() {
  String ssidPrefill = "";
  if (webServer.hasArg("ssid")) ssidPrefill = webServer.arg("ssid");
  webServer.send(200, "text/html", buildPortalPage("", ssidPrefill, g_method, g_school, g_latAdj));
}

// ============================================================================
// HTTP Handlers
// ============================================================================
void handleScanPost() {
  g_doScan = true;
  webServer.send(200, "text/html",
                 buildPortalPage("Scanning started. Wait a few seconds, then open Scan Results.",
                                 "", g_method, g_school, g_latAdj));
}

void handleResults() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    webServer.send(200, "text/html", buildPortalPage("Scan still running. Try again.", "", g_method, g_school, g_latAdj));
    return;
  }
  if (n < 0) {
    webServer.send(200, "text/html", buildPortalPage("No scan results. Try Scan again.", "", g_method, g_school, g_latAdj));
    return;
  }
  webServer.send(200, "text/html", buildScanResultsPage(n));
  WiFi.scanDelete();
}

void handleConnect() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  ssid.trim();

  // Validate
  if (ssid.length() == 0) {
    webServer.send(200, "text/html", buildPortalPage("SSID is required.", "", g_method, g_school, g_latAdj));
    return;
  }

  // ---- Parse prayer settings (optional; fall back to current globals) ----
  int newMethod = g_method;
  int newSchool = g_school;
  int newLatAdj = g_latAdj;

  if (webServer.hasArg("pmethod")) {
    int v = webServer.arg("pmethod").toInt();
    if (v >= 0 && v <= 23) newMethod = v;
  }
  if (webServer.hasArg("pschool")) {
    int v = webServer.arg("pschool").toInt();
    if (v >= 0 && v <= 1) newSchool = v;
  }
  if (webServer.hasArg("platadj")) {
    int v = webServer.arg("platadj").toInt();
    if (v >= 0 && v <= 3) newLatAdj = v;
  }
  if (webServer.hasArg("tz")) {
    String tz = webServer.arg("tz");
    tz.trim();
    g_tzOverride = tz;   // may be "" for Auto
    markSettingsDirty();
  }

  // Apply
  g_method = newMethod;
  g_school = newSchool;
  g_latAdj = newLatAdj;

  // Persist immediately (so if WiFi connect fails/reboots, settings remain)
  markSettingsDirty();
  saveSettings();
  g_settingsDirty = false;

  // Store creds for WiFi task to consume
  g_reqSsid = ssid;
  g_reqPass = pass;
  g_connectRequested = true;

  // Return portal page (with the newly selected prayer settings shown)
  webServer.send(200, "text/html", buildPortalPage("Connecting... you can close this page.", ssid, g_method, g_school, g_latAdj));
}

void handleNotFound() {
  webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + String("/"), true);
  webServer.send(302, "text/plain", "");
}

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '\"': o += "\\\""; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
          o += buf;
        } else o += c;
        break;
    }
  }
  return o;
}

// ============================================================================
// Portal start / stop
// ============================================================================
void startCaptivePortal() {
  // Keep STA enabled so WiFi.scanNetworks() is reliable in AP mode (ESP32 quirk)
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true, true); // don't auto-connect while in portal

  bool ok = WiFi.softAP(AP_SSID);
  IPAddress ip = WiFi.softAPIP();
  LOGI(LOG_TAG_WIFI, "AP start '%s' ok=%d IP=%s", AP_SSID, ok ? 1 : 0, ip.toString().c_str());

  dnsServer.start(DNS_PORT, "*", ip);

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/connect", HTTP_POST, handleConnect);

  // NEW endpoints for nicer scan UI:
  webServer.on("/scan_start", HTTP_POST, handleScanStart);
  webServer.on("/scan.json",  HTTP_GET,  handleScanJson);

  webServer.onNotFound(handleNotFound);
  webServer.begin();
  g_doScan = true;
  sendUi(UI_EVT_WIFI_AP_STARTED);
}

void stopCaptivePortal() {
  dnsServer.stop();
  webServer.stop();
  WiFi.softAPdisconnect(true);
  LOGI(LOG_TAG_WIFI, "AP stopped");
}

void handleScanStart() {
  // Request async scan; wifi portal loop will actually start it
  g_doScan = true;
  webServer.send(200, "text/plain", "OK");
}

void handleScanJson() {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    webServer.send(200, "application/json", "{\"running\":true,\"n\":0,\"nets\":[]}");
    return;
  }

  if (n < 0) {
    // -2 typically means "no scan started" / "no results"
    webServer.send(200, "application/json", "{\"running\":false,\"n\":0,\"nets\":[]}");
    return;
  }

  String js;
  js.reserve(256 + n * 64);
  js += "{\"running\":false,\"n\":";
  js += String(n);
  js += ",\"nets\":[";

  for (int i = 0; i < n; i++) {
    if (i) js += ",";
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);

    js += "{\"ssid\":\"";
    js += jsonEscape(ssid);
    js += "\",\"rssi\":";
    js += String(rssi);
    js += "}";
  }
  js += "]}";

  webServer.send(200, "application/json", js);

  // Keep results until next scan; do NOT delete here (the dropdown may repoll)
  // WiFi.scanDelete();
}

// ============================================================================
// Session Runners
// ============================================================================
bool runWifiSessionRefresh(const String& ssid, const String& pass) {
  wifiRadioOnSta();

  if (!connectStaWithRetry(ssid, pass)) {
    wifiRadioOff();
    
    // TRACK FAILURE - NO TIMEOUT
    g_wifiConnectFailed = true;
    forceIdleRedraw = true;
    
    return false;
  }

  // SUCCESS - clear failure flag
  if (g_wifiConnectFailed) {
    g_wifiConnectFailed = false;
    forceIdleRedraw = true;
  }

  // Location + TZ override applied in fetchLocationFromWifi -> applyTimezonePosix()
  for (int i = 0; i < 3; i++) {
    if (fetchLocationFromWifi()) break;
    vTaskDelay(pdMS_TO_TICKS(1200));
  }

  // NTP (TZ already set via configTzTime in applyTimezonePosix)
  syncTimeNtp(20000);

  // Prayers
  for (int i = 0; i < 3 && !g_prayerReady; i++) {
    if (fetchPrayerTimesTwoDays()) break;
    vTaskDelay(pdMS_TO_TICKS(1500));
  }

  wifiRadioOff();
  return true;
}

bool runWifiSessionFota(const String& ssid, const String& pass) {
  wifiRadioOnSta();

  if (!connectStaWithRetry(ssid, pass)) {
    wifiRadioOff();
    g_fotaBusy = false;
    fotaSetStatus("WiFi connect failed");

    // TRACK FAILURE
    g_wifiConnectFailed = true;
    forceIdleRedraw = true;

    return false;
  }
  // SUCCESS - clear failure flag
  if (g_wifiConnectFailed) {
    g_wifiConnectFailed = false;
    forceIdleRedraw = true;
  }

  fotaSetStatus("Checking version...");
  String latest = fotaGetLatestVersion();
  if (latest.isEmpty()) {
    wifiRadioOff();
    g_fotaBusy = false;
    // fotaGetLatestVersion already sets status with details
    return false;
  }

  // ---- Update cache (no extra reconnect) ----
  g_fotaLatestCached = latest;
  g_fotaUpdateAvailable = (compareVersions(latest, FW_VER) > 0);
  g_idleUpdateBanner    = g_fotaUpdateAvailable; 
  if (g_timeSynced && g_tzConfigured) {
    g_fotaLastCheckDayKey = dayKeyNowLocal();
  }
  saveSettings(); // persist badge + latest string

  if (g_fotaUpdateAvailable) {
    fotaSetStatus("Update found: " + latest);
    vTaskDelay(pdMS_TO_TICKS(400));

    bool ok = fotaDownloadAndUpdate(); // reboots on success
    if (!ok) {
      String st = fotaGetStatus();
      if (st.length() == 0) fotaSetStatus("Update failed");
    }
  } else {
    fotaSetStatus("Up-to-date");
    vTaskDelay(pdMS_TO_TICKS(600));
  }

  wifiRadioOff();
  g_fotaBusy = false;
  return true;
}

void runWifiSessionCheckVersionOnly(const String& ssid, const String& pass) {
  wifiRadioOnSta();

  if (!connectStaOnce(ssid, pass, 12000)) {
    wifiRadioOff();
    return;
  }

  fotaSetStatus("Checking version...");
  String latest = fotaGetLatestVersion();

  if (!latest.isEmpty() && compareVersions(latest, FW_VER) > 0) {
    fotaSetStatus("Update available");
    // optionally store latest version string
  } else {
    fotaSetStatus("Up-to-date");
  }

  wifiRadioOff();
}


bool runWifiSessionFotaCheckOnly(const String& ssid, const String& pass, bool force) {
  if (!force && !shouldCheckFotaNow()) {
    return true; // skipped by policy
  }

  wifiRadioOnSta();

  if (!connectStaWithRetry(ssid, pass)) {
    wifiRadioOff();
    // Keep old cache; just update status
    fotaSetStatus("WiFi connect failed");

    // TRACK FAILURE
    g_wifiConnectFailed = true;
    forceIdleRedraw = true;

    return false;
  }

  // SUCCESS - clear failure flag
  if (g_wifiConnectFailed) {
    g_wifiConnectFailed = false;
    forceIdleRedraw = true;
  }

  fotaSetStatus("Checking version...");
  String latest = fotaGetLatestVersion();

  // Update last-check dayKey even if version fetch fails? Your choice.
  // Recommendation: only update lastDay on successful fetch, so it retries later.
  if (latest.isEmpty()) {
    wifiRadioOff();
    // fotaGetLatestVersion already set a specific status
    return false;
  }

  // Cache values
  g_fotaLatestCached    = latest;
  g_fotaUpdateAvailable = (compareVersions(latest, FW_VER) > 0);
  g_idleUpdateBanner    = g_fotaUpdateAvailable; 
  g_fotaLastCheckDayKey = dayKeyNowLocal();
  saveSettings();
  
  if (g_fotaUpdateAvailable) {
    fotaSetStatus("Update available");
  } else {
    fotaSetStatus("Up-to-date");
  }

  wifiRadioOff();
  return true;
}

// ============================================================================
// WiFi Task
// ============================================================================
void wifiTask(void*) {
  LOGI(LOG_TAG_WIFI, "WiFi task started");
  vTaskDelay(pdMS_TO_TICKS(5200)); // wait splash

  // Update status
  updateSplashStatus("Connecting...");

  // Default OFF
  wifiRadioOff();

  String ssid, pass;
  bool haveCreds = loadCreds(ssid, pass);
  LOGI(LOG_TAG_WIFI, "Creds %s", haveCreds ? "present" : "missing");

  // Boot-time: single consolidated WiFi session
  if (haveCreds && ssid.length() > 0) {
    updateSplashStatus("Connecting...");
    
    // Single WiFi connection does everything
    wifiRadioOnSta();
    
    if (connectStaWithRetry(ssid, pass)) {
      // Connected successfully - do all boot tasks in one session
      
      // Location
      updateSplashStatus("Fetching location...");
      for (int i = 0; i < 3; i++) {
        if (fetchLocationFromWifi()) break;
        vTaskDelay(pdMS_TO_TICKS(1200));
      }
      
      // Time sync
      updateSplashStatus("Syncing time...");
      syncTimeNtp(20000);
      
      // Prayers
      updateSplashStatus("Fetching prayers...");
      for (int i = 0; i < 3 && !g_prayerReady; i++) {
        if (fetchPrayerTimesTwoDays()) break;
        vTaskDelay(pdMS_TO_TICKS(1500));
      }
      
      // FOTA check (while still connected)
      if (shouldCheckFotaNow()) {
        updateSplashStatus("Checking updates...");
        fotaSetStatus("Checking version...");
        String latest = fotaGetLatestVersion();
        
        if (!latest.isEmpty()) {
          g_fotaLatestCached = latest;
          g_fotaUpdateAvailable = (compareVersions(latest, FW_VER) > 0);
          g_idleUpdateBanner = g_fotaUpdateAvailable;
          g_fotaLastCheckDayKey = dayKeyNowLocal();
          saveSettings();
          
          if (g_fotaUpdateAvailable) {
            fotaSetStatus("Update available");
          } else {
            fotaSetStatus("Up-to-date");
          }
        }
      }
      
      updateSplashStatus("Ready");
      wifiRadioOff();  // Only turn off radio on success
      
    } else {
      // connectStaWithRetry already started AP mode
      // DON'T call wifiRadioOff() here - AP needs to stay on!
      updateSplashStatus("AP MODE");
      // AP is now active and will be handled by the portal loop below
    }
  } else {
    updateSplashStatus("No credentials");
    updateSplashStatus("AP MODE");
    startCaptivePortal();  // Start AP if no creds at all
  }

  // Initialize lastDailyKey to TODAY to prevent immediate daily refresh
  // after successful boot or portal connection
  uint32_t lastDailyKey = 0;
  if (g_timeSynced && g_tzConfigured) {
    lastDailyKey = dayKeyNowLocal();
  }

  for (;;) {
    // ----- Commands -----
    WifiCmd cmd;
    if (xQueueReceive(wifiCmdQueue, &cmd, pdMS_TO_TICKS(250)) == pdTRUE) {

      if (cmd.type == WIFI_CMD_FORGET_AND_START_AP) {
        eraseCreds();
        haveCreds = false;
        ssid = ""; pass = "";

        xSemaphoreTake(g_dataMtx, portMAX_DELAY);
        g_locationReady = false;
        g_prayerReady   = false;
        g_lat = g_lon = 0.0;
        g_tzIana = "UTC";
        g_today = PrayerTimesDay{};
        g_tomorrow = PrayerTimesDay{};
        xSemaphoreGive(g_dataMtx);

        wifiRadioOff();
        startCaptivePortal();
      }

      else if (cmd.type == WIFI_CMD_START_AP_PORTAL) {
        wifiRadioOff();
        startCaptivePortal();
      }

      else if (cmd.type == WIFI_CMD_RUN_DAILY_REFRESH) {
        // Manual "Refresh now" from menu
        if (!loadCreds(ssid, pass) || ssid.length() == 0) {
          haveCreds = false;
          continue;
        }
        haveCreds = true;

        runWifiSessionRefresh(ssid, pass);
        runWifiSessionFotaCheckOnly(ssid, pass, false); // also refresh cached update state
      }

      else if (cmd.type == WIFI_CMD_RUN_FOTA_CHECK) {
        // Manual "Check updates" / install path
        if (!loadCreds(ssid, pass) || ssid.length() == 0) {
          haveCreds = false;
          fotaSetStatus("No WiFi creds");
          continue;
        }
        haveCreds = true;

        runWifiSessionFota(ssid, pass); // installs if newer; may reboot on success
      }
    }

    // ----- AP portal mode -----
    if (WiFi.getMode() & WIFI_AP) {
      dnsServer.processNextRequest();
      webServer.handleClient();

      if (g_doScan) {
        g_doScan = false;
        LOGI(LOG_TAG_WIFI, "Starting WiFi scan...");
        int st = WiFi.scanComplete();
        if (st != WIFI_SCAN_RUNNING) {
          WiFi.scanDelete();
          WiFi.scanNetworks(true, true);
        }
      }

      if (g_connectRequested) {
        g_connectRequested = false;

        ssid = g_reqSsid;
        pass = g_reqPass;
        ssid.trim();

        saveCreds(ssid, pass);
        haveCreds = (ssid.length() > 0);

        stopCaptivePortal();

        // After portal, do ONE consolidated session (no restart needed)
        if (haveCreds) {
          updateSplashStatus("Connecting...");
          
          wifiRadioOnSta();
          if (connectStaWithRetry(ssid, pass)) {
            // Connected - do boot tasks
            updateSplashStatus("Fetching location...");
            for (int i = 0; i < 3; i++) {
              if (fetchLocationFromWifi()) break;
              vTaskDelay(pdMS_TO_TICKS(1200));
            }
            
            updateSplashStatus("Syncing time...");
            syncTimeNtp(20000);
            
            updateSplashStatus("Fetching prayers...");
            for (int i = 0; i < 3 && !g_prayerReady; i++) {
              if (fetchPrayerTimesTwoDays()) break;
              vTaskDelay(pdMS_TO_TICKS(1500));
            }
            
            updateSplashStatus("Ready");
            wifiRadioOff();  // Turn off radio after successful connection
            
            // Update lastDailyKey to prevent immediate daily refresh
            if (g_timeSynced && g_tzConfigured) {
              lastDailyKey = dayKeyNowLocal();
            }
          } else {
            // connectStaWithRetry handles AP fallback if connection fails
            // DON'T call wifiRadioOff() - AP mode is now active!
          }
        }
        
        // No restart - continue loop will handle AP mode
      }

      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // ----- Scheduled daily refresh (radio stays OFF otherwise) -----
    // Run once per day after 01:00 local time, only if time+TZ are valid.
    // Minimum 1 hour between refreshes to prevent excessive connections
    static uint32_t lastRefreshMs = 0;
    const uint32_t MIN_REFRESH_INTERVAL_MS = 3600000; // 1 hour
    
    if (haveCreds && ssid.length() > 0 && g_timeSynced && g_tzConfigured) {
      uint32_t now = millis();
      
      // Skip if we refreshed recently (within last hour)
      if ((now - lastRefreshMs) >= MIN_REFRESH_INTERVAL_MS || lastRefreshMs == 0) {
        time_t nowT = time(nullptr);
        struct tm lt;
        if (localtime_r(&nowT, &lt)) {
          uint32_t dayKey =
            (uint32_t)(lt.tm_year + 1900) * 10000u +
            (uint32_t)(lt.tm_mon + 1) * 100u +
            (uint32_t)lt.tm_mday;

          if (dayKey != lastDailyKey && lt.tm_hour >= 1) {
            LOGI(LOG_TAG_PRAY, "Auto daily refresh session");

            // Reload creds defensively in case user changed router/ssid via portal earlier
            if (loadCreds(ssid, pass) && ssid.length() > 0) {
              haveCreds = true;
              runWifiSessionRefresh(ssid, pass);
              runWifiSessionFotaCheckOnly(ssid, pass, false); // refresh cached update state daily

              lastDailyKey = dayKey;
              lastRefreshMs = now;  // Track when we last refreshed
            } else {
              haveCreds = false;
            }
          }
        }
      }
    }

    // Ensure radio stays OFF in idle mode (but never kill AP)
    uint8_t mode = WiFi.getMode();
    bool isApMode = (mode == WIFI_AP || mode == WIFI_AP_STA);
    if (mode != WIFI_OFF && !isApMode) {
      wifiRadioOff();
    }
  }
}

// ============================================================================
// FOTA Check Task (spawned from WiFi context)
// ============================================================================
void fotaCheckTask(void*) {
  g_fotaBusy = true;

  // Helpful default
  fotaSetStatus("Starting...");

  if (WiFi.status() != WL_CONNECTED) {
    fotaSetStatus("WiFi not connected");
    g_fotaBusy = false;
    vTaskDelete(nullptr);
    return;
  }

  fotaSetStatus("Checking version...");
  String latest = fotaGetLatestVersion();

  if (latest.isEmpty()) {
    // Keep the specific message set by fotaGetLatestVersion()
    String st = fotaGetStatus();
    if (st.length() == 0) fotaSetStatus("Version fetch failed");
    g_fotaBusy = false;
    vTaskDelete(nullptr);
    return;
  }

  if (compareVersions(latest, FW_VER) > 0) {
    fotaSetStatus("Update found: " + latest);
    vTaskDelay(pdMS_TO_TICKS(600));

    bool ok = fotaDownloadAndUpdate();

    if (!ok) {
      String st = fotaGetStatus();
      if (st.length() == 0) fotaSetStatus("Update failed");
    }

  } else {
    fotaSetStatus("Up-to-date");
    vTaskDelay(pdMS_TO_TICKS(800));
  }

  g_fotaBusy = false;
  vTaskDelete(nullptr);
  return;
}

bool connectStaWithRetry(const String& ssid, const String& pass) {
  LOGI(LOG_TAG_WIFI, "Attempting WiFi connection with retry (max %d attempts)", MAX_WIFI_RETRIES);
  
  // Clear AP screen and show splash with connection status
  sendUi(UI_EVT_WIFI_STA_CONNECTING);
  vTaskDelay(pdMS_TO_TICKS(100));

  for (uint8_t attempt = 1; attempt <= MAX_WIFI_RETRIES; attempt++) {
    updateSplashStatus(("WiFi Try " + String(attempt) + "/" + String(MAX_WIFI_RETRIES)).c_str());
    LOGI(LOG_TAG_WIFI, "Connection attempt %d/%d", attempt, MAX_WIFI_RETRIES);
    
    if (connectStaOnce(ssid, pass, WIFI_CONNECT_TIMEOUT_MS)) {
      updateSplashStatus("WiFi Connected");
      g_wifiConnectFailed = false;
      return true;
    }
    
    LOGW(LOG_TAG_WIFI, "Attempt %d failed", attempt);
    WiFi.disconnect(true);
    
    if (attempt < MAX_WIFI_RETRIES) {
      vTaskDelay(pdMS_TO_TICKS(2000));  // Wait 2 seconds between retries
    }
  }
  
  // All attempts failed
  LOGE(LOG_TAG_WIFI, "All %d connection attempts failed", MAX_WIFI_RETRIES);
  updateSplashStatus("WiFi Failed!");
  g_wifiConnectFailed = true;
  
  // Fall back to AP mode
  LOGI(LOG_TAG_WIFI, "Falling back to AP mode");
  vTaskDelay(pdMS_TO_TICKS(2000));  // Show error message briefly
  
  // Clear the failed credentials
  eraseCreds();
  
  // Start captive portal
  startCaptivePortal();
  sendUi(UI_EVT_WIFI_AP_STARTED);

  vTaskDelay(pdMS_TO_TICKS(500));

  return false;
}
