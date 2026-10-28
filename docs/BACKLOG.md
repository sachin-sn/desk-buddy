# Backlog

> Generated from `tools/backlog.json` by `node tools/backlog.js --markdown`. Edit the JSON, not this file.

Each item is written as a GitHub issue: what to do, which files it touches, and a "done when" checklist.
Create them all (and the labels and milestones) with `node tools/backlog.js --issues --create` after the repository exists;
run it without `--create` first to see the commands. Then add the issues to your GitHub Project.

Nothing in this repository has run on a real board yet, so milestone M0 is the real first step.

| ID | Title | Labels |
|---|---|---|
| M0-01 | Build and flash the first image | hardware, firmware |
| M0-02 | Check touch and knob behaviour | hardware, firmware |
| M0-03 | First-boot WiFi setup and buddy.local | firmware, web-ui |
| M0-04 | Backlight, night dimming and LED ring | hardware |
| M0-05 | Frame rate and memory with WiFi and Bluetooth running | firmware, risk |
| M0-06 | Pair with macOS and verify every default shortcut | macos, firmware |
| M0-07 | Mac helper: install and test end to end | mac-helper, macos |
| M0-08 | Google Calendar end to end over HTTPS | calendar, firmware |
| M0-09 | Timer survives a power cut | firmware |
| M0-10 | OTA firmware update from the web page | firmware, web-ui |
| M0-11 | First CI run on GitHub | ci |
| M0-12 | Check the clock faces and photo on the real display | hardware, firmware, web-ui |
| M0-13 | Check Mac notifications end to end | macos, mac-helper, firmware |
| M0-14 | Check the soft off and wake-up | hardware, firmware |
| M1-01 | Tune the default shortcut pages to your own apps | macos, firmware, web-ui |
| M1-02 | Mirror meeting alerts to macOS notifications | mac-helper, firmware |
| M1-03 | One-tap 'join meeting' | calendar, mac-helper, firmware |
| M1-04 | Frontmost-app aware shortcut page | mac-helper, firmware, web-ui |
| M1-05 | Spotlight timing and other launchers (Raycast, Alfred) | macos, firmware |
| M1-06 | Reach the helper when the Mac's address changes | mac-helper, firmware |
| M2-01 | Calendar fetch blocks the network task | firmware, risk |
| M2-02 | Mac name lookup blocks the network task | firmware, risk |
| M2-03 | Harden the web UI beyond LAN-only HTTP basic auth | web-ui, firmware, risk |
| M2-04 | More moods and idle animations | firmware |
| M2-05 | 12-hour clock and date format options | firmware |
| M2-06 | Photos and a wiring/assembly page for the README | docs |
| M3-01 | Battery backup: external module, then an optional indicator | idea, hardware |
| M3-02 | Small weather icon on the clock face | idea |
| M3-03 | Pomodoro cycles | idea |
| M3-04 | Slack / Teams status and mute indicators | idea, mac-helper |
| M3-05 | Windows / Linux helper | idea, mac-helper |

## M0 Bring-up on hardware

Everything in this repo compiles and its pure logic is unit-tested, but none of it has run on a real board yet. These issues are the first-boot checklist.

### M0-01 - Build and flash the first image

Labels: `hardware` `firmware`

Run `pio run`, then `pio run -t upload` over USB-C and `pio run -t uploadfs` for the web pages. The sources were compiled with the ESP32-S3 GCC and Arduino-ESP32 2.0.17 in a sandbox (no warnings), but never linked into an image or run, and PlatformIO itself could not be used there. Expect to fix small build or pin problems here.

Files:
- `platformio.ini`
- `partitions.csv`
- `src/lgfx_config.h`
- `src/pins.h`
- `src/main.cpp`

Done when:
- [ ] `pio run` succeeds on a fresh clone (note any library version problems in platformio.ini)
- [ ] Upload works on macOS (`/dev/cu.usbmodem*`) and the eyes appear on the display
- [ ] If the screen stays blank or is shifted: fix panel offsets or pins in src/lgfx_config.h / src/pins.h

### M0-02 - Check touch and knob behaviour

Labels: `hardware` `firmware`

Verify the CST816D touch controller (I2C 0x15) and the rotary encoder. Pin numbers come from the Elecrow wiki and are unverified on your unit.

Files:
- `src/input.cpp`
- `src/input.h`
- `src/pins.h`

Done when:
- [ ] Tap, swipe left/right/up/down and long touch each trigger the expected screen behaviour
- [ ] Knob clockwise looks right / increases values; if reversed set ENC_REVERSE to 1 in src/input.h
- [ ] One knob detent equals one step (adjust ENC_COUNTS_PER_DETENT if not)

### M0-03 - First-boot WiFi setup and buddy.local

Labels: `firmware` `web-ui`

On first boot the device opens a WPA2 access point named CrowPanel-Buddy with a random password shown on its screen, serving a setup page. Check the whole path including mDNS from the Mac.

Files:
- `src/net.cpp`
- `src/web.cpp`
- `src/setup_page.h`
- `src/screens.cpp`

Done when:
- [ ] Phone joins CrowPanel-Buddy with the on-screen password and the setup page opens (captive portal or http://192.168.4.1)
- [ ] After saving, the buddy restarts, joins the home 2.4 GHz WiFi and `http://buddy.local` opens from the Mac
- [ ] Wrong WiFi password: the setup AP comes back instead of leaving a dead device

### M0-04 - Backlight, night dimming and LED ring

Labels: `hardware`

Backlight is PWM on GPIO46, the five WS2812 LEDs are on GPIO48. Check that the sleep window (default 23:00 to 07:00) dims the screen and turns the LEDs off, and that the knob changes brightness on the clock screen.

Files:
- `src/ui.cpp`
- `src/pins.h`
- `src/settings.cpp`

Done when:
- [ ] Brightness changes smoothly and is remembered after a reboot
- [ ] At night the buddy sleeps (thin eyes), screen dims, LEDs off
- [ ] LED ring flashes on a meeting alert and shows timer progress while a timer runs

### M0-05 - Frame rate and memory with WiFi and Bluetooth running

Labels: `firmware` `risk`

The buddy is drawn into a 240x240 sprite in PSRAM and pushed at about 30 fps while the other core runs WiFi, the web server and BLE. Measure it; the heap/PSRAM numbers are on the web page's Firmware tab.

Files:
- `src/buddy.cpp`
- `src/ui.cpp`
- `platformio.ini`

Done when:
- [ ] Eyes animate smoothly (no visible stutter) while the web page is open and a shortcut is being sent
- [ ] Free heap stays stable over 24 hours (no slow leak)
- [ ] If PSRAM sprite pushes are too slow: try a smaller sprite, partial redraw, or a pushImage DMA path

### M0-06 - Pair with macOS and verify every default shortcut

Labels: `macos` `firmware`

The buddy shows up as the Bluetooth keyboard 'CrowPanel Buddy'. macOS may open Keyboard Setup Assistant the first time. Go through every default button on all four pages and the media keys.

Files:
- `src/hid.cpp`
- `src/hid.h`
- `src/keycombo.h`
- `src/layout.cpp`

Done when:
- [ ] Pairing works and survives sleep/wake of the Mac and a buddy reboot
- [ ] Spot, Shot, Rec, Lock, Apps, Desks and the Calls / Dev / Media pages all do what their label says
- [ ] `spot:` macros open the app reliably; if Spotlight is slow, increase the waits (see M1-05)

### M0-07 - Mac helper: install and test end to end

Labels: `mac-helper` `macos`

The helper's logic is covered by 18 automated tests on Linux, but launchctl, osascript, vm_stat, `open`, `shortcuts` and the macOS firewall / Local Network prompts have never been run.

Files:
- `helper/buddy-helper.js`
- `helper/install-launchagent.sh`
- `helper/com.deskbuddy.helper.plist.template`
- `src/helper_client.cpp`

Done when:
- [ ] `node buddy-helper.js --init` and `./install-launchagent.sh` work; the helper starts at login and restarts if killed
- [ ] Dev page buttons open VS Code, Terminal and Chrome; Settings -> Mac helper shows green
- [ ] A timer finishing shows a macOS notification; heavy CPU load (>= 85%) makes the buddy grumpy
- [ ] Check the three prompts: incoming connections, Local Network access, notification permission for the helper

### M0-08 - Google Calendar end to end over HTTPS

Labels: `calendar` `firmware`

The Apps Script is tested against a fake CalendarApp only. On the device the request follows Google's 302 redirect and verifies TLS with the Arduino core's built-in certificate bundle. If TLS verification fails, `-DBUDDY_TLS_INSECURE` is a temporary fallback (do not keep it).

Files:
- `google/calendar_proxy.gs`
- `src/calendar.cpp`
- `src/net.cpp`

Done when:
- [ ] Deploy the script (README steps) and paste the URL; the Agenda screen lists today's meetings
- [ ] A meeting 10 minutes away shows the alert, the buddy looks surprised and the clock shows the next-meeting line
- [ ] Private events show as Busy; declined and all-day events are hidden
- [ ] Calendar errors show in the web page without losing the previous events

### M0-09 - Timer survives a power cut

Labels: `firmware`

The timer state is saved to NVS and restored once NTP time is valid (otherwise after 90 s as paused).

Files:
- `src/ui.cpp`
- `src/settings.cpp`
- `src/countdown.h`

Done when:
- [ ] Start 10 min, unplug for 1 min, replug: it resumes with about 9 min left
- [ ] A timer that would have finished while unplugged shows 'FINISHED while away'
- [ ] NVS is not written every second (check writes only on start / pause / reset)

### M0-10 - OTA firmware update from the web page

Labels: `firmware` `web-ui`

Upload a rebuilt `.pio/build/crowpanel/firmware.bin` on the Firmware tab.

Files:
- `src/web.cpp`
- `data/www/app.js`
- `partitions.csv`

Done when:
- [ ] Upload shows progress, the buddy reboots into the new FW_VERSION
- [ ] A non-firmware file is rejected in the browser, a truncated upload leaves the old firmware running
- [ ] Upload without logging in is refused

### M0-11 - First CI run on GitHub

Labels: `ci`

The workflow runs the host tests, the helper tests, the Apps Script tests and a full `pio run`. Only the first three have been exercised locally; the PlatformIO job is the first real PlatformIO build of this project.

Files:
- `.github/workflows/ci.yml`
- `platformio.ini`
- `test/host/Makefile`

Done when:
- [ ] All four jobs are green on the first push, or the failures are fixed
- [ ] Add a status badge to README.md

### M0-12 - Check the clock faces and photo on the real display

Labels: `hardware` `firmware` `web-ui`

The six clock faces were drawn with the real LVGL on a PC and compile for the ESP32-S3, but never ran on the board. Look at each face, the 30 fps clock loop, the photo upload from a phone and a laptop, and that a photo survives a reboot.

Files:
- `src/faces.cpp`
- `src/photostore.cpp`
- `data/www/app.js`

Done when:
- [ ] Every face looks right on the round screen and the seconds move smoothly
- [ ] Choosing a face and accent in the web editor changes the clock without a reboot
- [ ] A photo uploads, shows on the Photo face, and is still there after a power cycle
- [ ] No visible tearing, and the buddy and swipes still feel responsive while the clock runs

### M0-13 - Check Mac notifications end to end

Labels: `macos` `mac-helper` `firmware`

The helper reads the macOS notification database with sqlite3 and plutil and posts the app name and title to the buddy's /api/notify. Tested only with a fake sqlite3/plutil: the real database schema, the blob keys (titl, subt) and the Full Disk Access prompt are unverified.

Files:
- `helper/notifications.js`
- `src/web.cpp`
- `src/ui.cpp`

Done when:
- [ ] With notifyApps set and Full Disk Access granted, a Slack or Messages notification shows on the buddy within a few seconds
- [ ] The helper log names the app and says it was sent
- [ ] Without Full Disk Access the log explains what to enable
- [ ] Nothing shows during the sleep window or over the settings screen

### M0-14 - Check the soft off and wake-up

Labels: `hardware` `firmware`

Holding the knob for 3 s (or the web Turn off button) blanks the screen and LEDs, stops WiFi and Bluetooth, cuts the LCD and external 5V rails and light-sleeps until the knob is pressed, then restarts. Never run on the board. Measure the current while off with a USB power meter.

Files:
- `src/power.cpp`
- `src/input.cpp`

Done when:
- [ ] Hold 3 s: the screen and LEDs go dark and stay dark, with no crash or reboot loop
- [ ] A press of the knob wakes and restarts the buddy, and the saved settings are intact
- [ ] The web Turn off button does the same and the page says what happened
- [ ] Note the current drawn while off in docs/POWER.md

## M1 macOS daily driver

Make it genuinely useful on your Mac every day.

### M1-01 - Tune the default shortcut pages to your own apps

Labels: `macos` `firmware` `web-ui`

The defaults are generic macOS shortcuts. Replace them with what you press all day (use the web editor, then copy the JSON back into DEFAULT_LAYOUT so a factory reset keeps your set).

Files:
- `src/layout.cpp`
- `helper/actions.example.json`
- `data/www/app.js`

Done when:
- [ ] Pages reflect real daily use
- [ ] Every shortcut in the default layout is still valid (host test `make -C test/host test` passes)

### M1-02 - Mirror meeting alerts to macOS notifications

Labels: `mac-helper` `firmware`

NC_NOTIFY already exists and is used when a timer finishes. Meeting reminders only show on the buddy; send them to the Mac as well (once per event, using AlertTracker).

Files:
- `src/ui.cpp`
- `src/net.cpp`
- `helper/buddy-helper.js`

Done when:
- [ ] A 'meeting in 10 min' notification appears on the Mac
- [ ] No duplicate notifications after a reboot or calendar refresh

### M1-03 - One-tap 'join meeting'

Labels: `calendar` `mac-helper` `firmware`

Return the Meet/Zoom link for each event from the script and let a tap on the Agenda row open it on the Mac through the helper (new endpoint with an allow-list of hosts such as meet.google.com and zoom.us). Keep the device sending only an index, never a URL.

Files:
- `google/calendar_proxy.gs`
- `src/agenda.h`
- `src/calendar.cpp`
- `src/ui.cpp`
- `src/screens.cpp`
- `src/helper_client.cpp`
- `helper/buddy-helper.js`

Done when:
- [ ] Tap the next meeting to open its link on the Mac
- [ ] Only allow-listed hosts open
- [ ] Tests cover the allow-list in helper/test

### M1-04 - Frontmost-app aware shortcut page

Labels: `mac-helper` `firmware` `web-ui`

The helper reports the frontmost application (needs macOS Automation permission) and the buddy switches to a matching page, for example Zoom -> Calls, Code -> Dev. Make the mapping part of the layout JSON.

Files:
- `helper/buddy-helper.js`
- `src/web.cpp`
- `src/state.h`
- `src/ui.cpp`
- `src/layout.cpp`
- `data/www/app.js`

Done when:
- [ ] Switching apps changes the shortcut page within a second or two
- [ ] Manual page changes are not overridden for 30 seconds

### M1-05 - Spotlight timing and other launchers (Raycast, Alfred)

Labels: `macos` `firmware`

`spot:Notes` types Cmd+Space, waits 350 ms, types the name, waits 300 ms and presses Return. Make the waits and the launcher key configurable and document how to use Raycast/Alfred instead.

Files:
- `src/keycombo.h`
- `test/host/test_keycombo.cpp`
- `data/www/index.html`

Done when:
- [ ] `spot:` works on your Mac without retries
- [ ] Launcher shortcut is configurable in Settings

### M1-06 - Reach the helper when the Mac's address changes

Labels: `mac-helper` `firmware`

The device caches the resolved address for 10 minutes and re-resolves after a failed call. Check that sleep/wake and WiFi roaming recover on their own, and that a router restart does not need a buddy reboot.

Files:
- `src/helper_client.cpp`
- `src/settings.cpp`

Done when:
- [ ] After the Mac sleeps and wakes the first button press may fail but the next works
- [ ] No lasting 'Mac helper unreachable' state

## M2 Polish and robustness

Known rough edges and risks found while writing the code.

### M2-01 - Calendar fetch blocks the network task

Labels: `firmware` `risk`

The fetch runs in the same loop that serves the web page and sends Bluetooth shortcuts and can take up to ~12 s on a bad connection every 5 minutes. Move it into a short-lived task with its own copy of the URL (gCfg strings are not thread safe).

Files:
- `src/calendar.cpp`
- `src/net.cpp`

Done when:
- [ ] A shortcut pressed during a slow fetch still goes out within 200 ms
- [ ] No data race on gCfg.calUrl

### M2-02 - Mac name lookup blocks the network task

Labels: `firmware` `risk`

`MDNS.queryHost` can block for 2 s when the Mac is asleep, delaying Bluetooth shortcuts queued behind it. Resolve in the background and keep the last good address.

Files:
- `src/helper_client.cpp`

Done when:
- [ ] With the Mac asleep, a Bluetooth shortcut still fires immediately

### M2-03 - Harden the web UI beyond LAN-only HTTP basic auth

Labels: `web-ui` `firmware` `risk`

Writes need the admin password plus an X-Buddy header, there is a login lockout and the helper uses a separate token, but everything is plain HTTP on your LAN. Decide whether that is enough or add a session cookie / HTTPS.

Files:
- `src/web.cpp`
- `data/www/app.js`

Done when:
- [ ] Threat model written down in docs/DESIGN.md
- [ ] Decision recorded

### M2-04 - More moods and idle animations

Labels: `firmware`

Wink, love, dizzy, looking at the clock, a startled reaction to a meeting alert. The renderer is shared between the host tests and the board, so every new mood can be previewed with `make -C test/host render`.

Files:
- `src/eyes.h`
- `test/host/test_eyes.cpp`
- `test/host/render_eyes.cpp`
- `src/buddy.cpp`

Done when:
- [ ] New moods appear in the contact sheet
- [ ] Tests cover the transitions

### M2-05 - 12-hour clock and date format options

Labels: `firmware`

The clock is 24-hour only.

Files:
- `src/screens.cpp`
- `src/settings.h`
- `src/settings.cpp`
- `src/web.cpp`
- `data/www/index.html`
- `data/www/app.js`

Done when:
- [ ] Setting changes the clock and agenda times

### M2-06 - Photos and a wiring/assembly page for the README

Labels: `docs`

Add a photo of the finished buddy, a screenshot of the web editor and a short GIF of the eyes.

Files:
- `README.md`
- `docs/DESIGN.md`

Done when:
- [ ] README shows the device and the editor

## M3 Ideas

Nice-to-haves. Not planned.

### M3-01 - Battery backup: external module, then an optional indicator

Labels: `idea` `hardware`

The board has no battery connector and no charger (Elecrow schematic V1.0): 5V only enters through the 1.25 mm 4-pin connector J37. Backup therefore needs an external charge-and-boost module feeding J37 pin 1 and 4, see docs/POWER.md. Build that, measure the buddy's real current draw, and only then decide whether a battery or mains-present indicator is worth adding (PIN_VBUS_SENSE, or an I2C fuel gauge on GPIO38/39).

Files:
- `docs/POWER.md`
- `src/state.h`
- `src/ui.cpp`
- `src/pins.h`

Done when:
- [ ] Backup module wired and the buddy keeps running when USB power is unplugged
- [ ] Measured current draw recorded in docs/POWER.md

### M3-02 - Small weather icon on the clock face

Labels: `idea`

The weather screen (swipe left from the agenda) shows the full icon, temperature, today's high and low and the city. The clock still shows only text such as 27C Partly cloudy above the time. Draw a small icon next to or instead of the words; weathericon.h paints at one fixed size, so it needs a scale parameter.

Files:
- `src/screens.cpp`
- `src/weathericon.h`

Done when:
- [ ] A small icon matching the conditions is drawn on the clock
- [ ] The time stays the most readable element

### M3-03 - Pomodoro cycles

Labels: `idea`

Focus -> break -> focus automatically, with a daily count.

Files:
- `src/countdown.h`
- `src/ui.cpp`
- `test/host/test_countdown.cpp`

Done when:
- [ ] Cycle runs hands-free; tests cover the state machine

### M3-04 - Slack / Teams status and mute indicators

Labels: `idea` `mac-helper`

Show whether the Mac mic is muted or a call is active.

Files:
- `helper/buddy-helper.js`
- `src/web.cpp`
- `src/ui.cpp`

Done when:
- [ ] Indicator visible on the buddy

### M3-05 - Windows / Linux helper

Labels: `idea` `mac-helper`

Only the launch commands and notification call are macOS specific.

Files:
- `helper/buddy-helper.js`
- `helper/package.json`

Done when:
- [ ] Actions and notifications work on a second OS
