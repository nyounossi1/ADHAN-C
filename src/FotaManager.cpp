/*
 * FotaManager.cpp — FOTA version check, download, and flash.
 */
#include "FotaManager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

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

const char* fotaVersionURL  = "https://my-adhan-firmware.s3.eu-north-1.amazonaws.com/version.txt";
const char* fotaFirmwareURL = "https://my-adhan-firmware.s3.eu-north-1.amazonaws.com/firmware.bin";

// ============================================================================
// Status helpers (thread-safe)
// ============================================================================
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

// Need to add g_fotaStatus to Globals (it's already declared extern)
String g_fotaStatus = "";

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
// Fetch latest version string from S3
// ============================================================================
String fotaGetLatestVersion() {
  WiFiClientSecure client;
  client.setCACert(rootCACertificate);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, fotaVersionURL)) { fotaSetStatus("Version begin failed"); return ""; }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code > 0) fotaSetStatus(("Version HTTP " + String(code)).c_str());
    else          fotaSetStatus(("Version err: " + http.errorToString(code)).c_str());
    http.end(); return "";
  }

  String ver = http.getString();
  ver.trim();
  http.end();

  if (ver.length() == 0) { fotaSetStatus("Empty version file"); return ""; }
  return ver;
}

// ============================================================================
// Download firmware and flash
// ============================================================================
bool fotaDownloadAndUpdate() {
  fotaSetStatus("Connecting...");

  WiFiClientSecure client;
  client.setCACert(rootCACertificate);

  HTTPClient http;
  if (!http.begin(client, fotaFirmwareURL)) { fotaSetStatus("Connect fail"); return false; }

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); fotaSetStatus("HTTP fail"); return false; }

  int len = http.getSize();
  if (len <= 0) { http.end(); fotaSetStatus("Bad size"); return false; }

  fotaSetStatus("Flashing prep...");
  if (!Update.begin((size_t)len)) { http.end(); fotaSetStatus("No space"); return false; }

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

    size_t w = Update.write(buf, r);
    if (w != r) { http.end(); Update.abort(); fotaSetStatus("Flash write error"); return false; }

    written += w;
    int pct = (int)((written * 100ULL) / (unsigned long long)len);
    if (pct != lastPct && (pct % 5 == 0 || pct == 100)) {
      lastPct = pct;
      fotaSetStatus(("Downloading " + String(pct) + "%").c_str());
    }
    vTaskDelay(1);
  }

  http.end();
  if (written != (size_t)len) { Update.abort(); fotaSetStatus("Incomplete"); return false; }

  fotaSetStatus("Flashing...");
  if (Update.end(true) && Update.isFinished()) {
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
