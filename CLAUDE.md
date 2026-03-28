# ADHAN-C — Claude Code Instructions

## Project overview
ESP32 firmware for the Adhan AI Islamic Prayer Clock.
Board: AdhanAI-02b (Spinzer Embedded Ltd, V1.0, 2026-02-22)
Framework: Arduino + FreeRTOS via PlatformIO
Jira: jaegertech.atlassian.net — project key AD
GitHub: nyounossi1/ADHAN-C (private)

---

## Hardware constraints — check EVERY time before writing or reviewing code

### ESP32 pin assignments (AdhanAI-02b)
| Pin  | Signal   | Connected to                        | Constraint                          |
|------|----------|-------------------------------------|-------------------------------------|
| IO0  | UP_BTN   | R2 10k pullup + SW1                 | BOOT pin — NEVER drive LOW at boot  |
| IO2  | OK_LED   | Q1 NFET → LED1+LED2                 | Active HIGH via NFET                |
| IO4  | DN_BTN   | R7 10k pullup + SW3                 | External pullup — use INPUT only    |
| IO13 | BTN_LED  | Q2 NFET → LED3+LED4                 | Active HIGH via NFET                |
| IO14 | —        | NC                                  | AVOID — bootloop risk               |
| IO15 | OK_BTN   | R8 10k pullup + SW2                 | External pullup — use INPUT only    |
| IO16 | TXD2     | DFPlayer RX via R3 1k               | UART2 TX to DFPlayer                |
| IO17 | RXD2     | DFPlayer TX via R1 1k               | UART2 RX from DFPlayer              |
| IO21 | SDA      | OLED HS13L03W2C01                   | I2C SDA                             |
| IO22 | SCL      | OLED                                | I2C SCL                             |
| IO25 | DAC_R    | DFPlayer DAC_R                      | DAC — NEVER use digitalWrite()      |
| IO26 | DAC_L    | DFPlayer DAC_I (NF)                 | NF on board — mono only             |
| IO27 | DFON     | U5 TPS22912CYZVR ON pin             | DFPlayer power — HIGH=on, LOW=off   |
| IO32 | OLED_T1  | Pin switcher G1                     | OLED VCC/GND order jumper           |
| IO33 | OLED_T2  | Pin switcher G2                     | OLED VCC/GND order jumper           |
| IO34 | —        | —                                   | INPUT ONLY — no pullup, no drive    |
| IO35 | DFBUSY   | DFPlayer BUSY                       | INPUT ONLY — HIGH=idle, LOW=playing |
| TXD0 | UART0 TX | Prog connector                      | Debug/flash — do not repurpose      |
| RXD0 | UART0 RX | Prog connector                      | Debug/flash — do not repurpose      |

### Hard rules — violation blocks any PR
1. IO0 must NEVER be driven LOW during boot sequence
2. IO34 and IO35 are INPUT ONLY — never set as OUTPUT
3. IO25 and IO26 are DAC pins — never use digitalWrite() on these
4. All buttons use INPUT mode — NEVER INPUT_PULLUP (external 10k pullups fitted)
5. Audio is MONO ONLY — DAC_L (IO26) is not fitted on the board
6. DFPlayer MUST be powered via DFON (IO27 HIGH) before any UART2 communication
7. IO14 must be avoided — bootloop risk on ESP32 modules
8. OLED is I2C ONLY — SDA=IO21, SCL=IO22. No SPI variant supported

### Power
- USB-C only (AdhanAI-02b — battery removed in this revision)
- 5V → 3.3V: TPS62162DSGR buck converter
- DFPlayer power gating via TPS22912CYZVR (U5) — power off when not playing

### Memory guidelines
- RAM: 320KB — avoid large static allocations
- FreeRTOS tasks: minimum 4096 bytes stack for tasks using WiFi or JSON
- NVS namespaces: 'settings' and 'adhan_ai' — never mix them
- NVS operations must never run inside an ISR

---

## Branching strategy
```
feature/bug branch (AD-N/description)
        ↓ PR
       dev  ← primary development branch
        ↓ PR (release only, your explicit command)
       main ← production releases only
```
- All feature and bug branches are created from dev
- PRs from feature branches always target dev
- Only Release Agent merges dev → main
- Direct pushes to main and dev are blocked

## Commit message format
```
AD-N: concise description of what changed
```
Example: `AD-22: add g_duaEnabled playback trigger in prayerChimeTask`
The commit-msg hook in .git/hooks enforces this format.

## Versioning
- Dev merges: auto-tagged as `v0.X.Y-dev.N` (pre-release)
- Main releases: tagged as `v0.X.Y` (full release)
- Current firmware: v0.3.08 — next dev tag: v0.3.9-dev.1

---

## Jira workflow (using acli)

### Authenticate acli (if token expired)
```bash
echo YOUR_TOKEN | acli jira auth login \
  --site "jaegertech.atlassian.net" \
  --email "najeeb.younossi@gmail.com" \
  --token
```

### Common acli commands
```bash
# View a story
acli jira workitem view AD-22

# Search backlog
acli jira workitem search --jql "project = AD AND status != Done ORDER BY created DESC"

# Create a story
acli jira workitem create \
  --project AD \
  --type Story \
  --summary "AD-N: summary" \
  --description "description" \
  --label "label1,label2"

# Edit a story
acli jira workitem edit --key AD-22 --summary "new summary"

# Transition a story
acli jira workitem transition --key AD-22 --status "In Progress"
acli jira workitem transition --key AD-22 --status "In Review"
acli jira workitem transition --key AD-22 --status "Done"

# Search for specific status
acli jira workitem search --jql "project = AD AND status = 'To Do'"
```

### Story lifecycle
```
To Do → In Progress (when development starts)
In Progress → In Review (when PR is raised)
In Review → Done (automated via Jira automation on PR merge)
```

### Story definition of ready
Before development starts a story must have:
1. Clear acceptance criteria (minimum 3, each testable)
2. A size label: size:S, size:M, size:L, or size:XL
3. Relevant technical labels (e.g. wifi, audio, oled, nvs, ota, power)
4. No unresolved hardware constraint concerns

---

## GitHub workflow (using gh CLI)

### Common gh commands
```bash
# Create a branch
git checkout dev && git pull origin dev
git checkout -b AD-22/dua-after-fajr

# Create a PR to dev
gh pr create \
  --title "AD-22: add Dua playback after Fajr Adhan" \
  --body "Implements acceptance criteria from AD-22.\n\nCloses AD-22" \
  --base dev

# View PR status
gh pr status

# Merge a PR (squash)
gh pr merge --squash

# Create a release
gh release create v0.4.0 \
  --title "ADHAN Firmware v0.4.0" \
  --generate-notes
```

---

## PlatformIO commands
```bash
# Build firmware
pio run

# Run native unit tests (no hardware needed)
pio test -e native --without-uploading

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

---

## Agent roles and pipelines

### When asked to implement a story (e.g. "implement AD-22")

**Step 1 — Req Agent: validate story**
```bash
acli jira workitem view AD-22
```
- Check acceptance criteria are complete (minimum 3)
- Check size label exists
- If AC incomplete — draft missing criteria and update the story before proceeding

**Step 2 — Architect Agent: hardware review**
- Review the story against all 8 hard constraints above
- Check for: pin conflicts, ISR-unsafe operations, incorrect pin modes, DAC misuse
- Report findings — PAUSE and ask user to approve before proceeding
- If concerns raised — update story description with architectural notes

**Step 3 — Req Agent: finalise story**
```bash
acli jira workitem transition --key AD-22 --status "In Progress"
```
- Incorporate architect feedback into story if needed

**Step 4 — Coding Agent: implement**
```bash
git checkout dev && git pull origin dev
git checkout -b AD-22/dua-after-fajr
```
- Implement the feature per acceptance criteria
- Use constants from Globals.h — never hardcode pin numbers
- Write native unit tests in test/test_AD_22/test_main.cpp
- Use Unity test framework
- Minimum 1 test per acceptance criterion
```bash
pio test -e native --without-uploading
```
- Fix until all tests pass
```bash
git add .
git commit -m "AD-22: implement Dua playback after Fajr Adhan"
git push origin AD-22/dua-after-fajr
```

**Step 5 — Coding Agent: raise PR**
```bash
gh pr create \
  --title "AD-22: add Dua playback after Fajr Adhan" \
  --body "## Changes\n- description\n\n## Tests\n- list tests\n\n## AC covered\n- list AC\n\nCloses AD-22" \
  --base dev
```
```bash
acli jira workitem transition --key AD-22 --status "In Review"
```

**Step 6 — Review Agent: PR review**
- Fetch PR diff
- Check every AC item is implemented
- Verify all 8 hard constraints pass
- Check unit tests cover all AC
- Post review comment on GitHub PR
- PAUSE and ask user to approve merge before proceeding

**Step 7 — After user approves merge**
- Remind user to merge the PR in GitHub
- After merge confirmed: calculate next dev tag (v0.X.Y-dev.N)
```bash
gh release create v0.3.9-dev.1 \
  --title "Dev build v0.3.9-dev.1" \
  --prerelease \
  --generate-notes
```
- Prompt user to assign story to sprint if needed

---

### When asked to create a story

1. Check next available issue number:
```bash
acli jira workitem search --jql "project = AD ORDER BY created DESC" --fields "key,summary"
```
2. Draft story with: summary, user story format description, acceptance criteria, labels, size
3. Create in Jira:
```bash
acli jira workitem create \
  --project AD \
  --type Story \
  --summary "summary here" \
  --description "description here" \
  --label "label1,label2,size:M"
```
4. Confirm created issue key to user

---

### When asked to update a story

1. Fetch current story:
```bash
acli jira workitem view AD-N
```
2. Apply the requested change:
```bash
acli jira workitem edit --key AD-N --summary "new summary"
# or for description — edit via Jira UI and confirm
```
3. Add a comment documenting the change:
```bash
# Note: use Jira UI for comments if acli comment command unavailable
```

---

### When asked to cut a release

PAUSE and confirm with user before proceeding.

```bash
# Create PR from dev to main
gh pr create \
  --title "Release v0.X.Y" \
  --body "Release changelog" \
  --base main \
  --head dev

# After user confirms merge in GitHub:
gh release create v0.X.Y \
  --title "ADHAN Firmware v0.X.Y" \
  --generate-notes \
  --latest
```

---

### When asked to show the backlog
```bash
acli jira workitem search \
  --jql "project = AD AND status != Done ORDER BY created DESC" \
  --fields "key,summary,status"
```

---

## Coding standards
- All pin assignments must use named constants from Globals.h — no magic numbers
- FreeRTOS task stacks: minimum 4096 bytes for tasks using WiFi, JSON, or NVS
- Serial.print only when LOG_ENABLED is defined — never in production builds
- NVS reads/writes must be outside ISR context
- DFPlayer: always check DFBUSY (IO35) before sending commands
- OLED updates must not block — use non-blocking render patterns

## Unit test standards (PlatformIO native)
- Location: test/test_AD_N/test_main.cpp
- Framework: Unity (built into PlatformIO)
- Mock all hardware dependencies with stub functions
- Each acceptance criterion must have at least one corresponding test
- Tests must pass with: pio test -e native --without-uploading
- Test environment requires [env:native] in platformio.ini

## platformio.ini native test environment
Add this to platformio.ini to enable native tests:
```ini
[env:native]
platform = native
lib_deps =
  throwtheswitch/Unity@^2.5.2
```

---

## Active backlog (as of March 2026)
- AD-22: Play Dua after Fajr Adhan (not started)
- AD-23: Expand IANA timezone table for worldwide coverage (not started)
- AD-24: Disable Serial logging in production builds (not started)
- AD-26: Check for updates menu — system restart bug (bug)
- AD-27: WiFi router off for duration — connection recovery (bug)
- AD-28: Device gets stuck in fetching location (bug)

## Firmware version
Current: v0.3.08
Next dev tag: v0.3.9-dev.1
Next release: v0.3.9
