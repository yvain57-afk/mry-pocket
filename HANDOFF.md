# Mr.Y Pocket v0.1 — Handoff for Codex

**Project root**: `/Volumes/Macboy/Code/Codex相关/mry-pocket`
**Target hardware**: M5StickS3 (ESP32-S3-PICO-1, 8MB flash, 8MB PSRAM, 135×240 LCD)
**Owner intent**: Pocket-sized multi-tool for fitness/focus/recovery — daily-carry replacement for phone timers.

---

## 1. Hardware reference (verify before changing)

| Item | Value |
|---|---|
| MCU | ESP32-S3-PICO-1 |
| Flash | 8 MB |
| PSRAM | 8 MB (auto-detected by M5Unified) |
| Screen | 1.14" LCD, **135 × 240 portrait**, rotation 0 |
| Button A | GPIO 11 (main, front-center) |
| Button B | GPIO 12 (side) |
| IR LED | GPIO 46 (present in hardware, unused — see History) |
| Speaker | Built-in (PDM/I2S, small ~5mm element) |
| IMU | MPU6886-like via `M5.Imu` (accel only used so far) |

**Future target**: user has an **M5Stopwatch / Watch-S3** on order. When it arrives we extend or port — same M5Unified framework, expect different pins and likely a touch screen. Decisions to make at that point: shared codebase with `#ifdef BOARD_*`, or fork. Keep modes hardware-agnostic (they only depend on `M5Canvas`, buttons, speaker) so port cost stays low.

**Original firmware** was backed up by Codex on 2026-05-23 to:
```
/Volumes/Macboy/Code/Codex相关/sticks3_backups/sticks3_20260523_214933_full_flash_8mb.bin
```

---

## 2. Build & flash workflow (READ THIS — there's a gotcha)

### Build
```bash
cd /Volumes/Macboy/Code/Codex相关/mry-pocket
pio run
```

Toolchain: PlatformIO + `espressif32@6.7.0` + Arduino framework. See `platformio.ini`.

### Flash — DO NOT USE `pio run -t upload`
PIO's upload protocol resets via DTR/RTS which **drops the USB serial port** on this device (matches user's `project_cardputer.md` memory note). Use `esptool` directly:

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
    --baud 460800 --before default-reset --after hard-reset \
    write-flash 0x10000 .pio/build/m5stick-s3/firmware.bin
```

For full reflash (bootloader + partitions + app):
```bash
esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
    --baud 460800 --before default-reset --after hard-reset \
    write-flash \
    0x0     .pio/build/m5stick-s3/bootloader.bin \
    0x8000  .pio/build/m5stick-s3/partitions.bin \
    0x10000 .pio/build/m5stick-s3/firmware.bin
```

### Recovery — if device drops the port mid-flash
1. Hold power 6 sec to fully power down (screen black)
2. **Hold B (side) button**, short-press power once to boot into download mode
3. Port reappears as `/dev/cu.usbmodem1101`
4. Flash with `--before no-reset` (skip the DTR/RTS dance since we're already in DL mode)

### Monitor
```bash
pio device monitor -b 115200
```
USB CDC serial; first ~2 seconds of boot output may be lost (USB enumeration).

---

## 3. Code architecture

```
src/
  app.h           — shared types: Group, BtnEvent, Mode interface
  main.cpp        — setup/loop, group dispatch
  ui.h/.cpp       — full-screen back-buffer canvas, top bar, group strip
  input.h/.cpp    — A/B debounce + short/long press events
  audio.h/.cpp    — tones, glide cues, noise generators (brown/pink/white)
  mode_utility.*  — UTIL group: brightness, TV-B-Gone
  mode_timer.*    — TIMER group: stopwatch, countdown 30s/1m/2m, pomodoro
  mode_calm.*     — CALM group: breath, meditate, noise, NSDR
  nsdr.h/.cpp     — NSDR 10-min scheduler
  nsdr_audio.h    — auto-generated TTS clip arrays (DO NOT edit by hand)
tools/
  generate_nsdr_audio.sh  — regenerates nsdr_audio.h from text → say → WAV → C arrays
```

### `Mode` interface (`app.h`)
Every group implements one. Methods: `enter()`, `exit()`, `tick(now_ms)`, `onEvent(BtnEvent)`, `cycleSubMode()`, `name()`, `subName()`, `renderPanel(M5Canvas&, x, y, w, h)`.

The dispatcher in `main.cpp` routes button events to the currently active mode and calls its `tick()` every loop. `UI::renderFrame()` calls `renderPanel()` on the active mode each frame.

### Rendering rules — CRITICAL
- **Never draw directly to `M5.Display`** inside a mode. Always render into the passed `M5Canvas&`.
- UI::renderFrame composes top bar + panel + group strip into one full-screen sprite (135×240, 16-bit, PSRAM-backed), then `pushSprite(0,0)` to the LCD.
- Drawing direct = flicker. The user already complained about this; do not regress.

### Loop pacing
- Frame compose + push: **~30 FPS** (every 33 ms)
- Audio tick: every loop (~2 ms) for tight noise/glide streaming
- Mode tick: every loop

### Group/sub-mode navigation
- **B short** → next group (UTIL → TIMER → CALM → UTIL)
- **B long (≥500 ms)** → cycle sub-mode within current group
- **A short** → primary action (start/stop/send/bump)
- **A long (≥500 ms)** → secondary action (cycle preset / abort / reset)

There are only 2 buttons, so this 4-event combo is the entire input vocabulary. Use it consistently in any new mode.

---

## 4. Audio system (`audio.h`)

### Speaker channel assignments
| Channel | Owner | Notes |
|---|---|---|
| 0–5 | tones (auto-assigned) | `Audio::beep(freq, ms)` etc. |
| 6 | **NSDR voice** | reserved in `nsdr.cpp` |
| 7 | **continuous noise** | brown/pink/white, double-buffered |

Don't queue tones on channels 6 or 7.

### Volume — two-layer gain, **don't trip over this**
M5Unified's `Speaker_Class` has **two cascaded gain stages**: `master_volume` and per-`channel_volume`. Effective output = `master × channel / 255`. The per-channel default is mid-range and silently halves output. We pin all 8 channels to 255 in `Audio::begin()` so `setVolume(master)` is the only effective knob.

Default master is `g_volume = 210` (UTIL/VOLUME preset slot 3 of 5). User-adjustable via UTIL/VOLUME at runtime; defaults restored from compile-time constant on reboot (no persistence yet — see Wi-Fi/BT section for future NVS-backed config).

### Noise generation
- `playRaw` with int16 PCM, 16000 Hz, double-buffered (two `NOISE_CHUNK=512` buffers alternate to fill the 2-slot queue per channel — no inter-chunk silence)
- Brown is the default; pink and white available via `Audio::noiseStart(NoiseType)`

### Glide cues
`Audio::cueGlide(f0, f1, duration_ms)` is non-blocking — it re-triggers `tone()` every 80 ms with interpolated frequency. Used by breathing patterns. Step-quantized, not true sweep; "good enough" feel, replace with `playRaw` sine if you want it smoother.

---

## 5. Feature status (current as of v0.1)

### UTILITY group
- **BRIGHT**: 5-step backlight (20/60/120/180/240 of 255); A short cycles, A long jumps to max
- **VOLUME**: 5-step global speaker master volume (40/100/160/210/255); A short cycles with a test beep so the user can gauge loudness, A long jumps to max. Calls `Audio::setVolume()` → `M5.Speaker.setVolume(master)`. Channel-level per-stream gains are pinned to 255 in `Audio::begin()`.

### TIMER group
- **STOPWATCH**: count-up MM:SS.cs, A short start/pause, A long reset
- **COUNTDOWN**: 30s / 1m / 2m presets shown as inline pills (current highlighted), A long cycles; beeps last 3s, triple beep on zero; **after expiry, A short = re-arm to same preset and restart**
- **POMODORO**: 25+5 or 50+10 cycle; auto-toggles focus/break with distinctive chimes; **after expiry, A short = restart from new focus block**

### CALM group
- **BREATH**: 3 patterns (4-7-8 / box / **physiological sigh** — Huberman 1.5s+0.5s+6s); audio glide cues for eyes-closed use; expanding circle viz
- **MEDITATE**: 5/10/15/20 min silent timer; opening gong, midpoint chime, double end-gong; lotus-dot ring animates with rotation
- **NOISE**: brown / pink / white; A short play/stop, A long cycles color; wave-bar viz when playing
- **NSDR**: ~5-min guided session, 11 Tingting TTS clips:
  ```
   0s INTRO     15s BREATH    35s FEET      65s LEGS      95s BELLY
  130s CHEST   160s ARMS     190s FACE    225s WHOLE    255s RETURN
  275s WAKE    285s CHIME    290s DONE
  ```
  Voice clips embedded as 8 kHz mono 16-bit WAV in `nsdr_audio.h`. M5.Speaker.playWav parses the WAV header automatically. Voice channel = 6, kept separate from noise (7) and tones (auto).

### Pending / open
- IMU is initialized but not used. Potential: rep counter (push-up / squat / jump rope via accel peaks), posture detection, golf swing tempo metronome
- Wi-Fi / BT both unused — see Section 9
- Custom NSDR durations (3/7/10 min variants) and English voice option
- Port to upcoming **M5Stopwatch / Watch-S3** when hardware arrives

---

## 6. NSDR audio regeneration

When changing the script text:

1. Edit the `CLIPS=( ... )` array in `tools/generate_nsdr_audio.sh`
2. Run it:
   ```bash
   ./tools/generate_nsdr_audio.sh
   ```
3. `src/nsdr_audio.h` is overwritten. If you added/removed clips, also update:
   - `nsdr.cpp` SCHEDULE table (clip IDs and timings)
   - The `NsdrClipId` enum gets auto-regenerated; references in `nsdr.cpp` use `NSDR_INTRO`, `NSDR_FEET`, etc.

**Voice options** (all macOS `say` Chinese voices):
- `Tingting` (default, female, smooth) ← current
- `Sinji` (HK Cantonese — wrong dialect, avoid)
- `Meijia` (Taiwan Mandarin, female)
- `Lili` (HK Mandarin)

**Format gotchas — both critical, don't regress:**
1. `xxd` default emits `unsigned char` without `const`, which puts the data in DRAM and overflows it. The generator post-processes with `sed` to convert to `static const uint8_t`, which lands in flash.
2. macOS `say` output is quiet (~-15 dBFS peaks). Pipeline runs each clip through `ffmpeg -af "loudnorm=I=-9:LRA=4:tp=-0.3,acompressor=ratio=4:attack=3:release=60,volume=2dB,alimiter=limit=0.97"` to maximize audibility on the tiny built-in speaker. `-9 LUFS` is louder than broadcast standard (-23) and even louder than podcast (-16 to -14); the limiter prevents clipping. If you change clips and they sound quiet again, this filter chain probably got removed.

**Current flash budget**: 30% used (1.0 MB / 3.34 MB partition). Audio data ~700 KB after the schedule trim from 10 → 5 min. Plenty of headroom for English version, additional voice options, or more clips.

---

## 7. UI conventions (`ui.h`)

### Layout (135 × 240)
```
y 0..24   top bar      — mode label (left, orange) + battery icon (right)
y 24..208 panel        — mode renders here, 184 px tall
y 208..240 group strip — three rounded pills: UTIL / TIME / CALM
```

`PANEL_Y = 24`, `PANEL_H = 184`, `STRIP_Y = 208`. The mode receives `(x=4, y=28, w=127, h=176)` for `renderPanel`.

### Colors (RGB565, defined in `ui.h`)
```
COL_BG     0x0000   black
COL_FG     0xFFFF   white
COL_DIM    0x7BEF   grey
COL_ACCENT 0xFD20   orange  — primary highlight
COL_HOT    0xF800   red     — alerts / TV-B-Gone
COL_OK     0x07E0   green   — running state
COL_COOL   0x05FF   cyan    — breath / calm
COL_YELLOW 0xFFE0           — brightness
COL_PANEL  0x10A2           — dark muted backgrounds
```

### Fonts (M5GFX `fonts::`)
- `Font0` — small, 6×8, hints/footers
- `Font2` — medium, body text
- `Font4` — large title text
- `Font7` — 7-segment giant numerals for clocks/timers

Use `setFont(&fonts::FontN)` — `setTextFont` is deprecated in this M5GFX version.

---

## 8. Adding a new mode (recipe)

1. Decide: new top-level group, or new sub-mode in an existing group?
2. **New group**: add to `Group` enum in `app.h`, create `mode_xxx.h/.cpp` inheriting `Mode`, instantiate singleton in `main.cpp`, route in `getMode()`, add label to `UI::GROUP_LABELS`. Adjust `GRP_COUNT`. The bottom strip auto-lays out by `tileW = SCR_W / GRP_COUNT`.
3. **New sub-mode**: extend `Sub` enum, add label to `SUB_NAMES`, handle in `onEvent`, `cycleSubMode`, `tick`, and `renderPanel`'s switch.
4. Audio: use `Audio::beep / beepDouble / cueGlide` for simple sounds; reserve a channel if you need a dedicated stream.
5. Render to the canvas, not the display. Use the existing palette; don't introduce new colors casually.

---

## 9. Wi-Fi / BT / IMU — unused, but available

Quick notes on what each could realistically do on this device:

### Wi-Fi (ESP32-S3 has it native)
- **NTP** — get real wall-clock time (would let us bring back the top-bar clock from v0; the user explicitly removed it because it was uptime, not real time)
- **WeatherStation** — fetch weather, show on idle
- **MQTT publisher** — log pomodoro completions, breath sessions, etc. to home server / Home Assistant
- **OTA** — over-the-air firmware updates so future builds don't need physical USB

### Bluetooth (BLE)
- **BLE HID keyboard** — already used in user's cardputer project (`t-vk/ESP32 BLE Keyboard`). Could send media keys, slide-advance, etc.
- **Pair with phone for time sync** — alternative to Wi-Fi for real-time clock
- **Pair with HRM** (Polar H10, etc.) for true biofeedback during breath/NSDR sessions
- **Companion app** for custom NSDR scripts

### IMU (accelerometer, 3-axis)
- **Rep counter** — peak-detect on vertical accel for squats/push-ups/jumps. Tricky to calibrate but doable.
- **Posture / tilt indicator** — for desk-work nudge
- **Step counter** — full pedometer needs more code than it sounds
- **Golf swing tempo detector** — capture impact peak, measure backswing/downswing duration ratio, give audible feedback. Aligns with user's golf-warmup product line.
- **Sleep cycle detection** — if left on nightstand, detect movement to time wake within REM (research-grade, not consumer-ready)

User has not requested these. Document them here for future scoping.

---

## 10. User context / preferences (from conversation history)

- User runs a sports brand with focus on golf warm-up products + an offline studio in Shenzhen — fitness/wellness features have direct business value, not just personal use
- Has prior experience with M5 dev (cardputer project, see `~/.claude/projects/.../memory/project_cardputer.md` for related PIO flakiness pattern)
- Strong preference for **Mandarin voice prompts**, not English
- Wants **discoverable UX** — hidden long-press shortcuts have already burned them once (was the COUNTDOWN preset issue, fixed by showing 30s/1m/2m pills inline)
- Doesn't want IR for general remote use — only TV-B-Gone style mass-blast. **Verified by phone-camera test that the IR LED does fire**; failure was code/protocol selection, not hardware.
- Brown noise > white noise (StickS3 speaker too small for white to sound good)

---

## 11. Commit log / change summary

| Date | Change |
|---|---|
| 2026-05-23 | Codex backed up factory firmware |
| 2026-05-24 | v0.1 initial: scaffold, dashboard, UTIL+TIMER+CALM groups, breath patterns including physiological sigh, brown/pink/white noise, IR with hardcoded Samsung |
| 2026-05-24 | Fixed flicker via full-screen canvas double-buffer |
| 2026-05-24 | Trimmed top bar from 52→24 px (removed useless uptime clock) |
| 2026-05-24 | COUNTDOWN preset pills (30s/1m/2m) made visually discoverable |
| 2026-05-24 | Audio volume 96→220, noise amplitude ±20k→±30k, double-buffered noise stream |
| 2026-05-24 | IR → TV-B-Gone state machine, 29-code curated list |
| 2026-05-24 | Expanded TV-B-Gone to 34 codes, China-focused, sourced from Flipper-IRDB |
| 2026-05-24 | Added MEDITATE sub-mode (5/10/15/20 min) + NSDR 10-min Mandarin guided session with 12 Tingting clips |
| 2026-06-02 | **TV-B-Gone removed entirely**; IRremote dependency dropped |
| 2026-06-02 | UTIL replaced TV-B-Gone with **VOLUME** sub-mode (5-step pills, A-short cycles with test beep) |
| 2026-06-02 | NSDR compressed to **~5 min** (11 clips, NECK merged into FACE) |
| 2026-06-02 | NSDR voice loudness fixed at two layers: `setAllChannelVolume(255)` + ffmpeg loudnorm -9 LUFS |
| 2026-06-02 | Countdown/Pomodoro **restart-on-A** after expiry — single-button cycle end-to-end |
| 2026-06-02 | Pushed to `github.com/yvain57-afk/mry-pocket` (public) |

Git: `origin` = `https://github.com/yvain57-afk/mry-pocket.git`, branch `main`. Push after each meaningful change.

---

## Quick reference card (print and tape to back of device)

```
B short     → next group (UTIL → TIMER → CALM)
B long 0.5s → next sub-mode within current group
A short     → primary action  (start / stop / cycle step)
A long 0.5s → secondary action (preset / reset / jump-to-max)

UTIL sub-modes:  BRIGHT → VOLUME
TIMER sub-modes: STOPWATCH → COUNTDOWN → POMODORO
CALM sub-modes:  BREATH → MEDITATE → NOISE → NSDR

Flash:  esptool ... --before default-reset write-flash 0x10000 firmware.bin
Recovery: hold B + power-cycle for download mode
```
