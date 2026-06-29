/*
 * FotaManager.cpp — FOTA version check, download, and flash.
 */
#include "FotaManager.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

// ============================================================================
// AWS Root CA certificate
// ============================================================================
const char* rootCACertificate = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// Manifest URL — stable pointer; versioned firmware URL is embedded inside the JSON.
static const char* FOTA_MANIFEST_URL =
    "https://my-adhan-firmware.s3.eu-north-1.amazonaws.com/esp32dev/latest.json";

// Cached values populated by fotaGetLatestVersion(); consumed by fotaDownloadAndUpdate().
static String s_firmwareURL;
static String s_sha256Expected;

// ============================================================================
// Status helpers (thread-safe)
// ============================================================================
String g_fotaStatus = "";

void fotaSetStatus(const String& s) {
  if (g_fotaMtx) xSemaphoreTake(g_fotaMtx, portMAX_DELAY);
  g_fotaStatus = s;
  if (g_fotaMtx) xSemaphoreGive(g_fotaMtx);
}

String fotaGetStatus() {
  if (g_fotaMtx) xSemaphoreTake(g_fotaMtx, portMAX_DELAY);
  String s = g_fotaStatus;
  if (g_fotaMtx) xSemaphoreGive(g_fotaMtx);
  return s;
}

// ============================================================================
// Version comparison
// ============================================================================
int compareVersions(const String& a, const String& b) {
  int A[3]={0}, B[3]={0};
  sscanf(a.c_str(), "%d.%d.%d", &A[0], &A[1], &A[2]);
  sscanf(b.c_str(), "%d.%d.%d", &B[0], &B[1], &B[2]);
  for (int i=0; i<3; i++) {
    if (A[i] > B[i]) return 1;
    if (A[i] < B[i]) return -1;
  }
  return 0;
}

// ============================================================================
// Fetch and parse latest.json manifest from S3.
// Populates s_firmwareURL and s_sha256Expected as a side-effect.
// Returns the version string, or "" on failure.
// ============================================================================
String fotaGetLatestVersion() {
  s_firmwareURL    = "";
  s_sha256Expected = "";

  WiFiClientSecure client;
  client.setCACert(rootCACertificate);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  LOGI(LOG_TAG_SYS, "FOTA: begin HTTPS to S3");
  if (!http.begin(client, FOTA_MANIFEST_URL)) {
    fotaSetStatus("Manifest begin failed");
    return "";
  }

  http.setTimeout(10000); // 10-second timeout — prevents wifiTask watchdog stall
  LOGI(LOG_TAG_SYS, "FOTA: sending GET");
  int code = http.GET();
  LOGI(LOG_TAG_SYS, "FOTA: GET returned %d", code);
  if (code != HTTP_CODE_OK) {
    if (code > 0) fotaSetStatus(("Manifest HTTP " + String(code)).c_str());
    else          fotaSetStatus(("Manifest err: " + http.errorToString(code)).c_str());
    http.end();
    return "";
  }

  LOGI(LOG_TAG_SYS, "FOTA: reading body");
  String body = http.getString();
  http.end();
  LOGI(LOG_TAG_SYS, "FOTA: body len=%d", body.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    fotaSetStatus("Manifest JSON error");
    return "";
  }

  const char* ver = doc["version"];
  const char* url = doc["url"];
  const char* sha = doc["sha256"];

  if (!ver || !url || !sha) {
    fotaSetStatus("Manifest missing fields");
    return "";
  }

  String version = String(ver);
  version.trim();
  if (version.isEmpty()) {
    fotaSetStatus("Empty version in manifest");
    return "";
  }

  s_firmwareURL    = String(url);
  s_sha256Expected = String(sha);
  return version;
}

// ============================================================================
// Download firmware, verify SHA256, and flash via Update.h.
// Caller must have already called fotaGetLatestVersion() successfully.
// ============================================================================
bool fotaDownloadAndUpdate() {
  if (s_firmwareURL.isEmpty()) {
    fotaSetStatus("No firmware URL — call fotaGetLatestVersion first");
    return false;
  }

  fotaSetStatus("Connecting...");

  WiFiClientSecure client;
  client.setCACert(rootCACertificate);

  HTTPClient http;
  if (!http.begin(client, s_firmwareURL)) {
    fotaSetStatus("Connect fail");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    fotaSetStatus("HTTP fail");
    return false;
  }

  int len = http.getSize();
  if (len <= 0) {
    http.end();
    fotaSetStatus("Bad size");
    return false;
  }

  fotaSetStatus("Flashing prep...");
  if (!Update.begin((size_t)len)) {
    http.end();
    fotaSetStatus("No space");
    return false;
  }

  // SHA256 context — fed each chunk as it arrives, no extra buffer needed.
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0); // 0 = SHA-256 (not SHA-224)

  fotaSetStatus("Downloading...");
  WiFiClient* s = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  int lastPct = -1;

  while (http.connected() && written < (size_t)len) {
    int avail = s->available();
    if (avail <= 0) { vTaskDelay(1); continue; }

    size_t remaining = (size_t)len - written;
    size_t toRead = min(sizeof(buf), remaining);
    if ((size_t)avail < toRead) toRead = (size_t)avail;

    size_t r = s->readBytes(buf, toRead);
    if (r == 0) { vTaskDelay(1); continue; }

    mbedtls_sha256_update(&shaCtx, buf, r);

    size_t w = Update.write(buf, r);
    if (w != r) {
      http.end();
      Update.abort();
      mbedtls_sha256_free(&shaCtx);
      fotaSetStatus("Flash write error");
      return false;
    }

    written += w;
    int pct = (int)((written * 100ULL) / (unsigned long long)len);
    if (pct != lastPct && (pct % 5 == 0 || pct == 100)) {
      lastPct = pct;
      fotaSetStatus(("Downloading " + String(pct) + "%").c_str());
    }
    vTaskDelay(1);
  }

  http.end();

  if (written != (size_t)len) {
    Update.abort();
    mbedtls_sha256_free(&shaCtx);
    fotaSetStatus("Incomplete download");
    return false;
  }

  // Finalise SHA256 and convert to lowercase hex string.
  uint8_t digest[32];
  mbedtls_sha256_finish(&shaCtx, digest);
  mbedtls_sha256_free(&shaCtx);

  char hexBuf[65];
  for (int i = 0; i < 32; i++) snprintf(hexBuf + i * 2, 3, "%02x", digest[i]);
  hexBuf[64] = '\0';

  if (!s_sha256Expected.isEmpty() && s_sha256Expected != String(hexBuf)) {
    LOGE(LOG_TAG_SYS, "SHA256 mismatch: got %s expected %s", hexBuf, s_sha256Expected.c_str());
    Update.abort();
    fotaSetStatus("SHA256 mismatch");
    return false;
  }

  fotaSetStatus("Flashing...");
  if (Update.end(true) && Update.isFinished()) {
    g_fotaUpdateAvailable = false;
    saveSettings();
    fotaSetStatus("Restarting...");
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP.restart();
    return true;
  } else {
    fotaSetStatus("Flash error");
    Update.abort();
    return false;
  }
}

// ============================================================================
// Policy: should we check for updates now?
// ============================================================================
bool shouldCheckFotaNow() {
  if (!g_timeSynced || !g_tzConfigured) return false;
  const uint32_t today = dayKeyNowLocal();
  if (today == 0) return false;
  if (g_fotaLastCheckDayKey == 0) return true;
  return (today != g_fotaLastCheckDayKey);
}

// ============================================================================
// Uptime formatting
// ============================================================================
String formatUptime() {
  uint32_t sec = millis() / 1000;
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)h, (unsigned long)m);
  return String(buf);
}
