# CLAUDE.md — A:DHAN AI Adhan Clock

## Project Overview
**A:DHAN AI** is an ESP32-based Islamic prayer time clock (firmware name: ADHAI).
It displays a live arc/bell-curve OLED visualisation of the Islamic prayer day, plays
the Adhan (call to prayer) at the correct times via a DFPlayer Mini audio module, and
auto-configures itself using WiFi for geolocation, NTP time sync, prayer time API
calls, and OTA firmware updates.

Current firmware version: **0.3.08** | Hardware revision: **AdhanAI-01b**

---

## Hardware

| Peripheral | Details |
|---|---|
| MCU | ESP32 Dev Module (Xtensa 240 MHz, 320 KB RAM, 4 MB Flash) |
| Display | SH1106G 128x64 OLED via I2C (addr 0x3C, SDA=IO21, SCL=IO22) |
| Audio | DFRobot DFPlayer Mini (UART Serial2, TX=IO16, RX=IO17, BUSY=IO35) |
| Buttons | UP=IO35, OK=IO32, DN=IO25 (all hardware interrupts) |
| LEDs | OK_LED=IO26 (via Q3 NFET), BTN_LED=IO27 (via Q4 NFET) |
| DFPlayer power switch | IO13 (active HIGH via U5 TPS22912CYZVR) |
| LDR sensor | IO34 (input only, via R10 GL5516 divider) |
| USB-C | CP2102N USB-to-serial (U2) |
| Power | 5V USB-C → AMS1117-3.3 LDO (U1) |

### Pin Map (AdhanAI-01b — confirmed against schematic)

| Pin  | Signal   | Connected to                   | Constraint                          |
|------|----------|--------------------------------|-------------------------------------|
| IO0  | UP_BTN   | R3 10k pullup + SW1            | BOOT pin — NEVER drive LOW at boot  |
| IO13 | DFON     | U5 TPS22912CYZVR ON pin        | DFPlayer power — HIGH=on, LOW=off   |
| IO16 | TXD2     | DFPlayer RX via R22 1k         | UART2 TX to DFPlayer                |
| IO17 | RXD2     | DFPlayer TX via R17 1k         | UART2 RX from DFPlayer              |
| IO21 | SDA      | OLED HS13L03W2C01              | I2C SDA — 400 kHz                   |
| IO22 | SCL      | OLED                           | I2C SCL                             |
| IO25 | DN_BTN   | R14 10k pullup + SW3           | External pullup — use INPUT only    |
| IO26 | OK_LED   | Q3 2N7002K-7 NFET → LED2+LED3 | Active HIGH via NFET                |
| IO27 | BTN_LED  | Q4 2N7002K-7 NFET → LED4+LED5 | Active HIGH via NFET                |
| IO32 | OK_BTN   | R4 10k pullup + SW2            | External pullup — use INPUT only    |
| IO34 | LDR      | R10 GL5516 divider             | INPUT ONLY — no pullup, no drive    |
| IO35 | UP_BTN   | R12 10k pullup + SW1           | INPUT ONLY — also DFBUSY signal     |
| TXD0 | UART0 TX | CP2102N (U2)                   | Debug/flash — do not repurpose      |
| RXD0 | UART0 RX | CP2102N (U2)                   | Debug/flash — do not repurpose      |

### Key Hardware Notes
- DFPlayer powered OFF at boot, switched on only for playback (power saving).
- IO35 is shared between UP_BTN and DFPlayer BUSY — firmware handles this conflict.
- SH1106G I2C runs at 400 kHz.
- ESP32 CPU throttled to 60-80 MHz at runtime for power saving.
- Bluetooth disabled via `btStop()` in `enablePm()`.
- Flash at ~93.6% — avoid large string literals; prefer PROGMEM.
- All buttons use external 10k pullups — NEVER use INPUT_PULLUP.

### Hard Rules — violation blocks any PR
1. IO0 must NEVER be driven LOW during boot sequence
2. IO34 and IO35 are INPUT ONLY — never set as OUTPUT
3. All buttons use EXTERNAL 10k pullups — NEVER use INPUT_PULLUP
4. DFPlayer MUST be powered via DFON (IO13 HIGH) before any UART2 communication
5. OLED is I2C ONLY — SDA=IO21, SCL=IO22
6. Never add large string literals — flash is at 93.6%
7. ISRs must be IRAM_ATTR — minimal work, queue send only
8. NVS operations must never run inside an ISR

---

## Architecture

### FreeRTOS Task Layout

| Task | Core | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `uiTask` | 1 | 3 | 4096 | UI state machine, menu, OLED drawing |
| `inputTask` | 1 | 3 | 2048 | Debounce, long-press detection, queue |
| `wifiTask` | 0 | 2 | 8192 | WiFi STA/AP, location, NTP, prayers, FOTA |
| `prayerChimeTask` | 1 | 1 | 4096 | Monitor time, trigger Adhan playback |

### Module Files

| File | Responsibility |
|---|---|
| `main.cpp` / `ADHAI_0_3_08.ino` | `setup()`: GPIO, OLED init, RTOS handles, task spawn |
| `Globals.h` / `Globals.cpp` | Shared types, enums, all global variables, settings persistence |
| `UiTask.cpp` / `.h` | UI state machine (8 screen states), menu engine, arc idle renderer integration, ISRs, timers |
| `ArcIdleRenderer.cpp` / `.h` | Bell-curve arc OLED visualisation of the prayer day |
| `PrayerEngine.cpp` / `.h` | Location fetch (ip-api.com), prayer time fetch (Aladhan API), NTP sync, next prayer computation, Qibla bearing |
| `WifiManager.cpp` / `.h` | WiFi STA connect/retry, captive portal (AP mode), session runners |
| `DFPlayerManager.cpp` / `.h` | DFPlayer power, UART init, volume, playback, scheduled power-off timer |
| `FotaManager.cpp` / `.h` | HTTPS firmware version check (AWS S3), download, flash, reboot |

### Screen State Machine
```
SCREEN_SPLASH → SCREEN_ARC_IDLE (default idle)
SCREEN_ARC_IDLE → SCREEN_PRAYER_SCREEN (on prayer time)
SCREEN_ARC_IDLE → SCREEN_MENU_ENGINE (on OK press)
SCREEN_MENU_ENGINE → SCREEN_INFO | SCREEN_CONFIRM | SCREEN_WIFI_STATUS | SCREEN_LIST
SCREEN_WIFI_AP → onboarding flow (captive portal)
SCREEN_FACTORY_RESET → countdown then ESP.restart()
```

### RTOS Communication
- **`uiQueue`** (64 deep) — all UI events (`UiEvent` structs with `UiEventType`)
- **`rawBtnQueue`** (16 deep) — raw button ISR events for `inputTask`
- **`wifiCmdQueue`** (8 deep) — commands to `wifiTask`
- **`g_dataMtx`** — guards `g_lat`, `g_lon`, `g_tzIana`, `g_today`, `g_tomorrow`
- **`g_displayMtx`** — guards all `display.*` calls
- **`g_fotaMtx`** — guards `g_fotaStatus`
- **`g_splashMtx`** — guards `g_splashStatus`
- **`g_logMtx`** — serialises `Serial.printf` across tasks

---

## Key External APIs

| API | Use | Auth |
|---|---|---|
| `http://ip-api.com/json` | Geolocation (lat/lon/timezone) | None |
| `https://api.aladhan.com/v1/timings` | Daily prayer times | None |
| `pool.ntp.org` / `time.nist.gov` / `time.google.com` | NTP | None |
| AWS S3 `my-adhan-firmware.s3.eu-north-1.amazonaws.com` | FOTA version + firmware | AWS Root CA cert pinned |

---

## Settings & Persistence (NVS via Preferences)

| Namespace | Key | Type | Default | Description |
|---|---|---|---|---|
| `settings` | `timeFmt` | uint8 | 1 | 0=12Hr, 1=24Hr |
| `settings` | `adhanType` | uint8 | 0 | 0=Full, 1=Short, 2=Chime |
| `settings` | `dua` | bool | true | Play Dua after Fajr |
| `settings` | `volIdx` | uint8 | 1 | Volume index 0-3 |
| `settings` | `pMethod` | int | 2 | Calculation method (0-23, ISNA=2) |
| `settings` | `pSchool` | int | 1 | Juristic school (0=Shafi, 1=Hanafi) |
| `settings` | `pLatAdj` | int | 3 | Latitude adjustment (0-3) |
| `settings` | `tz` | String | "" | IANA timezone override ("" = auto) |
| `settings` | `fotaLatest` | String | "" | Cached latest firmware version |
| `settings` | `fotaAvail` | bool | false | Update available flag |
| `settings` | `fotaLastDay` | uint32 | 0 | Day key of last FOTA check |
| `settings` | `cntBanner` | bool | true | Show countdown banner on idle |
| `adhan_ai` | `ssid` | String | "" | WiFi SSID |
| `adhan_ai` | `pass` | String | "" | WiFi password |

---

## Audio: DFPlayer Track Mapping

| Track | Adhan Type | Prayer |
|---|---|---|
| 1 | Full | Fajr (special Fajr adhan) |
| 2 | Full | All other prayers |
| 3 | Short | All prayers |
| 4 | Chime | All prayers |

Volume levels: `{ 0, 7, 14, 21 }` = `{ MUTE, LOW, MED, HIGH }`

---

## Coding Standards

### Language & Style
- C++11/Arduino framework; source files are `.cpp`/`.h` — not `.ino` (except legacy `ADHAI_0_3_08.ino`)
- Always `#include <Arduino.h>` at top of `.cpp` files — NOT in `.h` files
- Include guards: use `#pragma once`
- Module-private state: declare `static` at file scope
- ISRs: must be `IRAM_ATTR`; minimal work (queue send only)
- Constants: `#define` for pin numbers; `static constexpr` for class constants

### Thread Safety
- Always take `g_dataMtx` before reading/writing `g_lat`, `g_lon`, `g_tzIana`, `g_today`, `g_tomorrow`
- Always take `g_displayMtx` before any `display.*` call
- ISRs must use `xQueueSendFromISR` / `xSemaphoreGiveFromISR` — never blocking calls
- Prefer snapshot pattern: take mutex, copy data, release mutex, then process

### Memory
- Flash at 93.6% — avoid large string literals; use `PROGMEM` or move to SD
- Prefer `char buf[16]` over `String` concatenation in hot paths
- `StaticJsonDocument<N>` is deprecated in ArduinoJson v7 — use `JsonDocument`

### Logging
```cpp
LOGI(LOG_TAG_SYS, "message %d", value);
LOGW(LOG_TAG_WIFI, "warning");
LOGE(LOG_TAG_OLED, "error %s", msg);
```
Tags: `LOG_TAG_UI`, `LOG_TAG_BTN`, `LOG_TAG_SYS`, `LOG_TAG_OLED`, `LOG_TAG_WIFI`,
`LOG_TAG_NTP`, `LOG_TAG_LOC`, `LOG_TAG_PRAY`, `LOG_TAG_ADHAN`, `LOG_TAG_MENU`

Disable for release builds: `#define LOG_ENABLED 0` in `Globals.h`.

### Functions
- Keep under 60 lines; extract helpers if longer
- Use `snprintf` not `sprintf`; always pass buffer size
- Time values: store as minutes from midnight (`int16_t`, range 0-1439)

---

## SDLC Setup

### Tools
- **Jira**: jaegertech.atlassian.net — project key `AD`
- **GitHub**: nyounossi1/ADHAN-C (private)
- **acli**: Atlassian CLI for Jira automation
- **gh**: GitHub CLI for PR and release automation

### Branching Strategy
```
main     — production releases only (PR from dev on explicit user command)
dev      — primary development branch (all features merge here)
AD-N/description  — feature/bug branches (created from dev)
```

### Commit Message Format
```
AD-N: concise description of what changed
```
A git commit-msg hook enforces this. Example: `AD-22: add g_duaEnabled playback trigger`

### Versioning
- Dev merges auto-tagged: `v0.X.Y-dev.N` (pre-release)
- Main releases tagged: `v0.X.Y` (latest)
- Current: v0.3.08 — next: v0.3.9-dev.1

### Definition of Done
- [ ] Zero build errors, zero new warnings
- [ ] Flash usage below 95%
- [ ] Native unit tests pass: `pio test -e native --without-uploading`
- [ ] All 8 hard rules checked
- [ ] Tested on hardware or marked untested
- [ ] CLAUDE.md updated if architecture changes
- [ ] Committed with `AD-N:` prefix

---

## Jira Workflow (acli)

```bash
# Authenticate (if token expired)
echo YOUR_TOKEN | acli jira auth login \
  --site "jaegertech.atlassian.net" \
  --email "najeeb.younossi@gmail.com" \
  --token

# View a story
acli jira workitem view AD-22

# Search backlog
acli jira workitem search \
  --jql "project = AD AND status != Done ORDER BY created DESC"

# Create a story
acli jira workitem create \
  --project AD --type Story \
  --summary "summary" \
  --description "description" \
  --label "label1,label2,size:M"

# Edit a story
acli jira workitem edit --key AD-22 --summary "new summary"

# Transition a story
acli jira workitem transition --key AD-22 --status "In Progress"
acli jira workitem transition --key AD-22 --status "In Review"
acli jira workitem transition --key AD-22 --status "Done"
```

### Story lifecycle
```
To Do → In Progress (development starts)
In Progress → In Review (PR raised)
In Review → Done (automated on PR merge via Jira automation rule)
```

---

## GitHub Workflow (gh CLI)

```bash
# Create feature branch from dev
git checkout dev && git pull origin dev
git checkout -b AD-22/dua-after-fajr

# Build and test
pio run
pio test -e native --without-uploading

# Commit and push
git add .
git commit -m "AD-22: implement Dua playback after Fajr Adhan"
git push origin AD-22/dua-after-fajr

# Raise PR to dev
gh pr create \
  --title "AD-22: add Dua playback after Fajr Adhan" \
  --body "## Changes\n- what changed\n\n## Tests\n- tests added\n\n## AC covered\n- AC items\n\nCloses AD-22" \
  --base dev

# Create dev pre-release (after PR merges to dev)
gh release create v0.3.9-dev.1 \
  --title "Dev build v0.3.9-dev.1" \
  --prerelease --generate-notes

# Create production release (after dev merges to main — explicit user command only)
gh release create v0.3.9 \
  --title "ADHAN Firmware v0.3.9" \
  --generate-notes --latest
```

---

## Agent Pipelines

### Implement a story ("implement AD-22")

**Step 1 — Req Agent: validate story**
```bash
acli jira workitem view AD-22
```
Verify: min 3 AC, size label, technical labels. Draft missing AC if needed.

**Step 2 — Architect Agent: hardware review**
Check story against all 8 hard rules. Check pin usage, ISR safety, flash impact.
**PAUSE — present findings and wait for explicit user approval before continuing.**

**Step 3 — Req Agent: finalise**
```bash
acli jira workitem transition --key AD-22 --status "In Progress"
```
Incorporate architect notes into story if needed.

**Step 4 — Coding Agent: implement**
```bash
git checkout dev && git pull origin dev
git checkout -b AD-22/dua-after-fajr
# implement per AC, write tests in test/test_AD_22/test_main.cpp
pio test -e native --without-uploading
git commit -m "AD-22: implement Dua playback after Fajr Adhan"
git push origin AD-22/dua-after-fajr
gh pr create --title "AD-22: ..." --base dev
acli jira workitem transition --key AD-22 --status "In Review"
```

**Step 5 — Review Agent: PR review**
Check every AC implemented, all 8 hard rules pass, tests cover all AC.
**PAUSE — wait for explicit user approval and manual PR merge.**

**Step 6 — After user merges PR**
```bash
gh release create v0.3.9-dev.1 --prerelease --generate-notes
```
Prompt user to assign story to sprint if not already assigned.

---

### Create a story

```bash
# Get next available number
acli jira workitem search --jql "project = AD ORDER BY created DESC" | head -3

# Create
acli jira workitem create \
  --project AD --type Story \
  --summary "AD-N: summary" \
  --description "As a user, I want X so that Y.

Acceptance Criteria:
- criterion 1
- criterion 2
- criterion 3" \
  --label "label1,label2,size:M"
```

---

### Cut a release (explicit user command only)

```bash
# Confirm with user before proceeding
gh pr create --title "Release vX.Y.Z" --base main --head dev
# After user confirms merge:
gh release create vX.Y.Z --generate-notes --latest
```

---

## Unit Test Standards

- Location: `test/test_AD_N/test_main.cpp`
- Framework: Unity (built into PlatformIO)
- Mock all hardware with stub functions
- Minimum 1 test per AC
- Run: `pio test -e native --without-uploading`

Add to `platformio.ini`:
```ini
[env:native]
platform = native
lib_deps =
  throwtheswitch/Unity@^2.5.2
```

---

## File Structure
```
ADHAN-C/
├── src/
│   ├── ADHAI_0_3_08.ino      # Legacy main (being merged into main.cpp)
│   ├── main.cpp
│   ├── Globals.cpp / .h
│   ├── UiTask.cpp / .h
│   ├── ArcIdleRenderer.cpp / .h
│   ├── PrayerEngine.cpp / .h
│   ├── WifiManager.cpp / .h
│   ├── DFPlayerManager.cpp / .h
│   └── FotaManager.cpp / .h
├── test/
│   └── test_AD_N/            # Native unit tests per story
├── docs/
│   ├── user-stories.md
│   ├── adhan-stories.json
│   └── tech-requirements.md
├── .github/
│   └── workflows/
│       └── build.yml
├── platformio.ini
└── CLAUDE.md
```

---

## Open Technical Debt
1. **ArduinoJson migration**: Replace `StaticJsonDocument<N>` with `JsonDocument` in `PrayerEngine.cpp`
2. **Dual main files**: Consolidate `ADHAI_0_3_08.ino` into `main.cpp`
3. **Flash headroom**: At 93.6% — identify largest contributors and optimise
4. **Timezone table**: `IANA_TO_POSIX[]` covers ~15 regions — extend or replace with lookup service
5. **`lib_extra_dirs`**: Remove local Desktop path from `platformio.ini` before sharing
6. **IO35 shared use**: UP_BTN and DFBUSY share IO35 — document firmware conflict handling

---

## Active Backlog (March 2026)
| Key | Type | Summary | Status |
|-----|------|---------|--------|
| AD-22 | Story | Play Dua after Fajr Adhan | To Do |
| AD-23 | Story | Expand IANA timezone table for worldwide coverage | To Do |
| AD-24 | Story | Disable Serial logging in production builds | To Do |
| AD-26 | Bug | Check for updates menu causes system restart | To Do |
| AD-27 | Bug | WiFi router off duration — connection recovery | To Do |
| AD-28 | Bug | Device gets stuck in fetching location | To Do |
| AD-30 | Story | Prayer time per-prayer offset adjustment (±5 min) | To Do |

---

## Rules for Claude
- DO NOT modify `IANA_TO_POSIX[]` without updating the count
- DO NOT add `#include <Arduino.h>` to `.h` files
- DO NOT use `String` concatenation in ISRs or timer callbacks
- DO NOT introduce new globals without `extern` in `Globals.h` and definition in `Globals.cpp`
- DO NOT use `INPUT_PULLUP` — all buttons have external 10k pullups
- ALWAYS check flash impact before adding code — budget is 93.6%
- ALWAYS run mental build check: does change need new `lib_deps`?
- When adding a screen state: update `ScreenState` enum AND `uiTask` switch-case
- When touching FOTA: note flash budget impact
- PAUSE at Architect Agent and Review Agent checkpoints — wait for explicit user approval
- NEVER simulate tool calls — always run real acli and gh commands
