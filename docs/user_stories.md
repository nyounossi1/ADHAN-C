# User Stories — A:DHAN AI Adhan Clock
*Reverse-engineered from firmware v0.3.08 source code.*  
*Status key: ✅ Implemented | 🔧 Partial | ❌ Not started*

---

## Functional Area 1: First-Time Setup & WiFi Onboarding

**US-001** ✅  
As a new user, I want to connect the device to my home WiFi network via a captive portal on my phone, so that I don't need to compile or flash credentials.

*Acceptance Criteria:*
- Device broadcasts an AP SSID when no credentials are stored
- Navigating to the portal IP on a phone shows a setup page
- User can enter SSID and password; device saves and reconnects
- Portal page includes setup instructions (multi-page onboarding)

**US-002** ✅  
As a user, I want the device to remember my WiFi credentials across power cycles, so that I don't need to reconfigure it every time.

*Acceptance Criteria:*
- Credentials stored in NVS (`adhan_ai` namespace)
- On boot, device attempts STA connection automatically
- Splash screen shows connection progress

---

## Functional Area 2: Geolocation & Time Sync

**US-003** ✅  
As a user, I want the device to automatically determine my location from my WiFi network, so that I don't need to manually enter my city or coordinates.

*Acceptance Criteria:*
- Device queries `ip-api.com` after WiFi connects
- Latitude, longitude, and IANA timezone are extracted and stored
- User can override timezone via settings menu
- Splash status shows "Fetching Location..." and "Location Ready"

**US-004** ✅  
As a user, I want the device clock to automatically sync to the correct local time including DST, so that prayer times are always accurate.

*Acceptance Criteria:*
- NTP sync attempted after location and timezone are known
- IANA timezone resolved to POSIX string via lookup table
- Syncs from `pool.ntp.org`, `time.nist.gov`, `time.google.com`
- Splash shows "Syncing time..." and "Time: Synced"
- Falls back to UTC if IANA is not in the lookup table

---

## Functional Area 3: Prayer Time Display

**US-005** ✅  
As a user, I want to see today's five prayer times on the OLED display, so that I always know when to pray.

*Acceptance Criteria:*
- Prayer times fetched from Aladhan API for today and tomorrow
- Times displayed in configured format (12h or 24h)
- Current and next prayer highlighted
- Dedicated prayer screen accessible from menu

**US-006** ✅  
As a user, I want to see a visual arc on the idle screen showing where I am in the prayer day, so that I can understand the day at a glance.

*Acceptance Criteria:*
- Bell-curve arc rendered on OLED idle screen
- 5 prayer markers (Fajr, Dhuhr, Asr, Maghrib, Isha) shown on curve
- Progress along arc reflects current time of day
- Sun icon shown above horizon during day; moon below
- Current prayer period label displayed (FAJR, DAY, DHUHR, ASR, MAGHRIB, ISHA, NIGHT)
- Current time shown on screen

**US-007** ✅  
As a user, I want to see a countdown to the next prayer on the idle screen, so that I can plan my time.

*Acceptance Criteria:*
- Countdown displayed as "PRAYER in Xh Ym" or "PRAYER in Ym"
- Updates every second via tick timer
- Can be enabled/disabled from settings menu

---

## Functional Area 4: Adhan (Prayer Call) Playback

**US-008** ✅  
As a user, I want the device to play the Adhan automatically at each prayer time, so that I am reminded to pray without checking the screen.

*Acceptance Criteria:*
- `prayerChimeTask` monitors time at 1 Hz
- Adhan triggers exactly at the minute matching each prayer time
- De-duplication prevents double-play within the same minute
- DFPlayer powered on only when needed (power saving)
- DFPlayer powered off after track completes (scheduled timer)

**US-009** ✅  
As a user, I want to choose between a Full Adhan, a Short Adhan, or a Chime, so that I can select what's appropriate for my environment.

*Acceptance Criteria:*
- `adhanType` setting: 0=Full, 1=Short, 2=Chime
- Full Fajr uses a separate track (track 1) with Fajr-specific words
- Full other prayers use track 2; Short uses track 3; Chime uses track 4
- Setting persisted in NVS and changeable from menu

**US-010** ✅  
As a user, I want to control the volume (mute/low/medium/high), so that I can adjust to my surroundings.

*Acceptance Criteria:*
- 4 volume levels: MUTE (0), LOW (7), MED (14), HIGH (21) on DFPlayer scale
- Volume changeable via UP/DN buttons during idle
- Volume dots shown on OLED idle screen
- Setting persisted in NVS

**US-011** ✅  
As a user, I want to instantly mute the Adhan while it is playing by holding OK, so that I can silence it quickly if needed.

*Acceptance Criteria:*
- Long-press OK during playback activates temporary mute
- Volume restored automatically when playback ends or after timeout (5 minutes)
- Temporary mute does not persist to NVS
- OLED idle screen reflects mute state

---

## Functional Area 5: Settings & Configuration

**US-012** ✅  
As a user, I want to configure the prayer calculation method and school of jurisprudence, so that prayer times match my religious practice.

*Acceptance Criteria:*
- Method selectable from 24 options (e.g. ISNA, MWL, Makkah, Egypt, etc.)
- School selectable: Shafi or Hanafi
- Latitude adjustment method selectable (None, Mid-Night, 1/7 Night, Angle-Based)
- Settings passed to Aladhan API on next fetch

**US-013** ✅  
As a user, I want to choose 12-hour or 24-hour time format, so that the display matches my preference.

*Acceptance Criteria:*
- Format applied to idle clock, prayer screen, and all menus
- Default is 24-hour

**US-014** ✅  
As a user, I want to override my timezone manually, so that the device works correctly if geolocation returns the wrong timezone.

*Acceptance Criteria:*
- IANA timezone override stored in settings
- Empty string means "use auto-detected timezone"
- Manual override applied instead of ip-api result

---

## Functional Area 6: Display & Power Management

**US-015** ✅  
As a user, I want the OLED display to dim or sleep after inactivity, so that the device saves power and the display lasts longer.

*Acceptance Criteria:*
- Display sleeps after configurable idle timeout (15s in menu mode)
- First OK press when display is off wakes the display without entering menu
- Second OK press within window enters menu
- Display wakes automatically during Adhan playback

**US-016** ✅  
As a user, I want the device to run quietly at low power when idle, so that it can be left on continuously without significant energy use.

*Acceptance Criteria:*
- CPU throttled to 60–80 MHz at runtime
- Bluetooth disabled at boot
- DFPlayer power switch ensures audio module is off when not playing
- Light sleep disabled to maintain WiFi and timekeeping

---

## Functional Area 7: Qibla Finder

**US-017** ✅  
As a user, I want to see the Qibla direction from my current location, so that I know which way to face when praying.

*Acceptance Criteria:*
- Bearing to Kaaba calculated from stored lat/lon using spherical formula
- Displayed in degrees and 16-point compass notation (N, NNE, NE, etc.)
- Accessible from the menu → Info screen

---

## Functional Area 8: Firmware OTA Updates

**US-018** ✅  
As a device owner, I want the device to automatically check for and notify me of firmware updates, so that I receive bug fixes and new features.

*Acceptance Criteria:*
- Version check performed once per day (day-key deduplication)
- Version fetched from AWS S3 over HTTPS with pinned CA cert
- Notification banner shown on idle screen when update available
- No automatic install without user confirmation

**US-019** ✅  
As a device owner, I want to trigger a firmware update from the device menu, so that I can update without connecting to a computer.

*Acceptance Criteria:*
- FOTA menu item shows current and available version
- User confirms update via YES/NO screen
- Progress shown on OLED during download (percentage)
- Device restarts automatically after successful flash

---

## Functional Area 9: Factory Reset & Recovery

**US-020** ✅  
As a user, I want to perform a factory reset that clears all settings and WiFi credentials, so that I can give the device to someone else or recover from a bad configuration.

*Acceptance Criteria:*
- Triggered by holding OK button for 10 seconds
- 5-second countdown shown on screen with ability to cancel
- Clears both `settings` and `adhan_ai` NVS namespaces
- Device restarts and enters AP portal mode

---

## Functional Area 10: System Information

**US-021** ✅  
As a user, I want to see the firmware version, hardware revision, IP address, and uptime, so that I can report issues accurately.

*Acceptance Criteria:*
- About screen accessible from menu → Info
- Shows FW version, build date, HW revision, IP address, uptime (HH:MM)

---

## Backlog / Future Stories

**US-022** ❌  
As a user, I want to hear the Dua after Fajr Adhan, so that I can follow the complete Sunnah practice.  
*(Setting `g_duaEnabled` exists in code but playback logic not yet implemented)*

**US-023** ❌  
As a user, I want the arc idle screen to display on a wider range of timezones automatically, so that the device works correctly worldwide without a manual override.  
*(Current IANA→POSIX table covers only ~15 regions)*

**US-024** ❌  
As a developer, I want to run the firmware with logging disabled in production builds, so that Serial output does not waste cycles in deployed units.  
*(`LOG_ENABLED` flag exists but build system does not yet auto-set it)*