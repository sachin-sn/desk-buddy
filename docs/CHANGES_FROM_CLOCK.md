# Changes from the watch-face clock project

The earlier project (`crowpanel-clock`: configurable watch faces, a face editor, PC stats) was abandoned for this one. Most of the hardware layer carried over. Use this table to see which file did what.

## Carried over unchanged

| File | Purpose |
|---|---|
| `partitions.csv` | 16 MB flash map: NVS, two 4 MB OTA slots, ~8 MB LittleFS, coredump |
| `src/pins.h` | CrowPanel pin map |
| `src/lgfx_config.h` | LovyanGFX panel and touch configuration |
| `src/input.h`, `src/input.cpp` | touch gestures + rotary encoder events |
| `src/web.h` | web server entry points |

## Changed

| File | What changed |
|---|---|
| `README.md` | rewritten for the buddy |
| `.gitignore` | also ignores `helper/config.json`, `helper/actions.json`, `test/host/out/`, `.DS_Store`, `node_modules/` |
| `platformio.ini` | new header comments; the two optional flags `-DNO_BLE_HID` and `-DBUDDY_TLS_INSECURE` documented (libraries and board settings are the same set) |
| `src/lv_conf.h` | proper `LV_CONF_H` include guard (silences an LVGL warning) |
| `src/main.cpp` | globals now `gMac` and `gAgenda` instead of the clock's stats; starts the same two tasks |
| `src/state.h` | layout/slot/preset types, calendar and helper status, UI and network message types |
| `src/settings.h`, `settings.cpp` | buddy settings (calendar URL, Mac helper address, API token, sleep hours, ...); layout and timer persistence in NVS |
| `src/hid.h`, `hid.cpp` | from "media knob" to full shortcut sequences, media keys and typing, driven by `keycombo.h` |
| `src/net.h`, `net.cpp` | calendar polling, Mac helper commands, setup hotspot with random password |
| `src/web.cpp` | new API (layout, test, token, agenda, stats from the helper), CSRF header, lockout, OTA kept; face-editor endpoints removed |
| `src/setup_page.h` | retitled, default name `buddy` |
| `src/ui.h`, `ui.cpp` | clock faces replaced by the buddy / clock / shortcuts / timer / agenda carousel, alerts, LED ring |
| `data/www/index.html`, `app.js`, `style.css` | new web editor (the face editor is gone) |

## Removed

| File | Why |
|---|---|
| `src/face.h`, `face.cpp`, `faces_store.h`, `faces_store.cpp` | watch-face renderer and storage |
| `data/faces/*/face.json` (3), `data/www/render.js` | watch-face definitions and their browser preview |
| `tools/pc_stats.py` | replaced by the Mac helper's CPU/RAM push (and you do not need Python) |

## New

| Area | Files |
|---|---|
| Buddy | `src/eyes.h`, `src/buddy.h`, `src/buddy.cpp` |
| Screens | `src/screens.cpp`, `src/ui_internal.h` |
| Logic (pure) | `src/countdown.h`, `src/agenda.h`, `src/keycombo.h` |
| Layout | `src/layout.h`, `src/layout.cpp` |
| Network clients | `src/calendar.h`, `src/calendar.cpp`, `src/helper_client.h`, `src/helper_client.cpp` |
| Mac helper | `helper/buddy-helper.js`, `package.json`, `actions.example.json`, `config.example.json`, `install-launchagent.sh`, `com.deskbuddy.helper.plist.template`, `test/helper.test.js` |
| Google | `google/calendar_proxy.gs`, `google/test/calendar_proxy.test.js` |
| Tests | `test/host/` (`Makefile`, `check.h`, `hostcanvas.h`, `test_*.cpp` including `test_default_layout.cpp`, `render_eyes.cpp`, `make_sheet.py`) |
| Project | `docs/`, `tools/backlog.json`, `tools/backlog.js`, `.github/workflows/ci.yml` |
