# Design

How the Desk Buddy fits together, and why. For setup see the [README](../README.md); for open work see [BACKLOG.md](BACKLOG.md).

## Overview

```
                        Wi-Fi (home LAN)
   Google Calendar  <--------------------------+
        ^  (Apps Script, runs as you)           |
        |  HTTPS GET /exec?t=TOKEN every 5 min  |
   +----+---------------------------------------+----+          +------------------+
   |            CrowPanel ESP32-S3 (the buddy)       |  BLE HID |   your Mac       |
   |  core 1: UI   eyes / LVGL / touch / knob / LEDs |--------->| keyboard input   |
   |  core 0: NET  WiFi, web UI, NTP, calendar, BLE  |  HTTP    |                  |
   |                                                 |--------->| buddy-helper.js  |
   |  /api/*  <-- browser (web editor)               |<---------| CPU/RAM, actions |
   +-------------------------------------------------+          +------------------+
```

Two ways to act on the Mac, because a Bluetooth keyboard can only press keys:

| Path | Good for | Limits |
|---|---|---|
| **BLE keyboard** (`hid` buttons) | shortcuts, media keys, typing, Spotlight macros | cannot start a specific app reliably, no feedback |
| **Mac helper** (`mac` buttons) | open an app or URL, run a Shortcuts automation, show a notification | needs the helper running and the Mac on the same network |

## Threads and who owns what

The display must never stutter, and WiFi, flash writes and Bluetooth all block. So:

- **Core 1, `uiTask`** (`ui.cpp`, `buddy.cpp`, `screens.cpp`, `input.cpp`): the buddy sprite or LVGL, touch, knob, LED ring, brightness. It never touches files, NVS, WiFi or BLE.
- **Core 0, `netTask`** (`net.cpp`, `web.cpp`, `calendar.cpp`, `helper_client.cpp`, `hid.cpp`, `settings.cpp`): everything that may block.
- They communicate only through
  - `qToUi` (`UiMsg`: new layout, toast, error) and `qToNet` (`NetCmd`: send keys, run a Mac action, notify, save timer / brightness, refresh calendar), both FreeRTOS queues of fixed-size structs; a new `Layout` travels as a heap pointer the UI takes ownership of;
  - small status blocks (`gNet`, `gMac`, `gAgenda`) copied under a spinlock (`gMux`).
- `gCfg` (settings) is written only on core 0. The UI reads a few single-byte fields (brightness, sleep hours, ...) directly; single-byte reads are atomic.

Known weak spot: calendar fetches and Mac name lookups block core 0 for up to a few seconds, which delays web requests and Bluetooth shortcuts queued behind them (backlog M2-01, M2-02).

## The buddy

`eyes.h` is a header-only, dependency-free engine: blink timing with double blinks, wandering gaze, mood morphing (eyelids, scale, colour) and a template `draw<G>()` that renders with only `fillScreen`, `fillRoundRect` and `fillTriangle`. The same code is compiled on the host (`test/host/hostcanvas.h` draws into a pixel buffer, `render_eyes.cpp` writes a contact sheet of every mood) and on the board (`buddy.cpp` draws into a 240x240 `LGFX_Sprite` in PSRAM and pushes it at about 30 fps). While the buddy is on screen LVGL is idle.

`chooseMood()` picks the mood, in priority order: meeting within 10 minutes -> surprised; sleep window (default 23:00-07:00) -> asleep; Mac CPU >= 85% -> grumpy; just touched -> happy; idle 45+ minutes late in the evening -> tired; else normal. Night: dim backlight, LEDs off.

## Shortcuts

A button holds either a `hid` string or a `mac` id (never both).

`keycombo.h` parses `hid` strings into steps (key combo, media key, text, wait; `spot:App` expands to Cmd+Space, wait 350, text, wait 300, Return). Limits: 8 steps, 6 keys per combo, waits clamped to 10-1500 ms and 1.5 s in total, 63 characters. The same rules are enforced in four places that must agree: `keycombo.h` (authoritative), `layout.cpp` (when a layout is saved), `web.cpp` (test button), and a JavaScript mirror in `data/www/app.js` for instant feedback in the editor. The key codes follow the ESP32 BLE Keyboard library; `hid.cpp` has `static_assert`s so the build fails if the library ever disagrees.

Layouts are one JSON document (pages, slots, timer presets, at most 3800 characters). It is stored in **NVS**, not in the filesystem, so `pio run -t uploadfs` (which replaces the whole filesystem image) can never wipe it. If nothing valid is stored the built-in macOS default in `layout.cpp` is used; `test/host/test_default_layout.cpp` checks that default, including that every Mac action id it uses exists in `helper/actions.example.json`.

## Google Calendar

Google's OAuth for limited-input devices does not allow the Calendar scope, and an ESP32 cannot run a browser sign-in. So `google/calendar_proxy.gs` runs on Google as the user and answers `GET /exec?t=TOKEN` with `{"ok":true,"events":[{"t":title,"s":start,"e":end}]}` (epoch seconds, so no time zone handling on the device). The script handles recurring events, skips declined and all-day events, shows private events as "Busy" (unless `SHOW_PRIVATE_TITLES` is set), merges several calendars, and returns at most 8 events for the next 36 hours. The device fetches every 5 minutes (retry after 60 s on failure), follows Apps Script's 302 redirect, verifies TLS with the Arduino core's built-in certificate bundle, and keeps showing the previous events if a fetch fails. It refuses to fetch until NTP has set the clock (TLS needs it). The URL must start with `https://script.google.com/macros/s/`.

`agenda.h` holds the event list, sorting, "in 12 min" formatting and `AlertTracker`, which raises each alert exactly once (meeting within `leadMin` minutes, then at the start; an event that began more than 60 s ago never alerts).

## Mac helper

`helper/buddy-helper.js` (Node 18+, no dependencies) listens on port 8765:

- `POST /action {"id"}` runs the entry with that id from `actions.json`: `open` (`open -a`), `url` (`open <url>`), `shortcut` (`shortcuts run`), `exec` (argv array). Processes start without a shell. The response is sent as soon as the command exits successfully or has run for 1.2 s, so long automations do not hold the buddy up.
- `POST /notify {"title","body"}` uses `osascript` with the text passed as separate argv entries (never spliced into the script).
- `GET /health`.
- It also pushes `{cpu, ram}` to the buddy's `POST /api/stats` every 10 s.

The device resolves the Mac with an IPv4 literal or `MDNS.queryHost` (cached for 10 minutes, dropped after a failed call) and only sends action *ids*.

## Storage

| Where | What |
|---|---|
| NVS namespace `buddy` | WiFi credentials, admin password, name, time zone, calendar URL, Mac address and port, API token, brightness and sleep settings, layout JSON (`layout`), timer state (`t_st`, `t_dur`, `t_rem`, `t_at`) |
| `spiffs` partition (LittleFS, ~8 MB) | the web editor in `/www` |
| `app0` / `app1` (4 MB each) | firmware, OTA alternates |
| `coredump` | crash dumps |

The timer is saved on start, pause and reset (not every second) with the wall-clock time; after a reboot it is restored once NTP is valid (otherwise after 90 s, paused), so a timer that ran out during a power cut says "FINISHED while away".

## Security model

Assumptions: a home LAN, and attackers who can reach the LAN or trick the owner's browser. Not in scope: someone with physical access, or the internet (the ports must not be forwarded).

| Asset | Protection |
|---|---|
| Changing shortcuts, settings, firmware | HTTP Basic admin password; writes also need an `X-Buddy` header, which another website cannot add without a CORS preflight that the device never answers (stops CSRF through cached credentials); 6 wrong attempts lock logins for 30 s; the password is only ever sent over the LAN in clear text (plain HTTP) |
| Setup hotspot | WPA2, random 8-digit password shown only on the device screen, reachable only until WiFi is configured |
| Calendar data | secret token in the script URL; the buddy never sends it anywhere else (URL prefix is enforced) |
| What the Mac runs | only ids from `actions.json`; no shell; the buddy cannot add actions; token required; requests from non-private addresses refused; body limit, rate limit and lockout |
| The API token | shown only to the admin; regenerating it invalidates the Mac helper until its config is updated; the buddy refuses to send it to a non-private address |

Plain HTTP on the LAN is the main residual risk (backlog M2-03).

## Why these choices

- **Pure logic in dependency-free headers** (`eyes.h`, `keycombo.h`, `agenda.h`, `countdown.h`): the firmware could not be run during development, so everything that could be tested without hardware lives where `g++` can test it, compiled as C++11 like the ESP32 core.
- **Layout in NVS**: survives `uploadfs`.
- **Action ids instead of commands**: a stolen token or a bug in the buddy cannot become remote code execution on the Mac.
- **Apps Script proxy instead of Google OAuth on the device**: the only route that works for a screenless-login microcontroller, and it keeps Google credentials off the device entirely.
- **Plain JS web editor served from flash**: no build step, works offline on the LAN.
- **Sprite in PSRAM for the buddy, LVGL only for the information screens**: the eyes need full-frame control; LVGL is the right tool for text screens.

## Constraints worth knowing

- Arduino-ESP32 2.0.x compiles C++ as **C++11**: no `std::optional`, no structured bindings, no C++14 digit separators.
- LVGL 8.3.x, LovyanGFX 1.1.x, ArduinoJson 7. PlatformIO's `espressif32@^6.9` provides Arduino core 2.0.17.
- ESP32-S3R8: octal PSRAM, so `board_build.arduino.memory_type = qio_opi`.
- Fonts are ASCII only; calendar titles are sanitised (`?` for other characters).
