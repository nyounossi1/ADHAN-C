/*
 * FotaManager.h — Firmware Over-The-Air update logic.
 */
#pragma once
#include "Globals.h"

void fotaSetStatus(const String& s);
String fotaGetStatus();
int compareVersions(const String& a, const String& b);
String fotaGetLatestVersion();
bool fotaUpdateOffered(const String& latest);       // newer and fits the OTA slot (AD-53)
const char* fotaCheckStatus(const String& latest);  // "Up-to-date" / "Update available" / "Needs USB reflash"
bool fotaDownloadAndUpdate();
bool shouldCheckFotaNow();
String formatUptime();

extern const char* rootCACertificate;
