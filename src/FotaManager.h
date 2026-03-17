/*
 * FotaManager.h — Firmware Over-The-Air update logic.
 */
#pragma once
#include "Globals.h"

void fotaSetStatus(const String& s);
String fotaGetStatus();
int compareVersions(const String& a, const String& b);
String fotaGetLatestVersion();
bool fotaDownloadAndUpdate();
bool shouldCheckFotaNow();
String formatUptime();

extern const char* rootCACertificate;
extern const char* fotaVersionURL;
extern const char* fotaFirmwareURL;
