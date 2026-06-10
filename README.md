# Mix2Go — Plugin (sender)

Mix2Go streams a DAW's master audio over the local network to a phone, in real
time, so you can monitor your mix on a phone with no cable. **This repo is the
audio plugin** (the *sender*) that runs inside the DAW. The receiver is the
companion [Mix2Go app](https://github.com/aksoya19/Mix2Go) (Flutter).

```
DAW master ─▶ Mix2Go plugin ─(resample → Opus → UDP)─▶ phone app ─▶ speaker
```

## What it does
- Sits on the DAW's master bus (VST3 / AU / Standalone, macOS + Windows).
- Resamples the master to 48 kHz, **Opus-encodes** it (10 ms frames, 256 kbps),
  and sends it over UDP to the phone.
- **Auto-discovery:** the phone app broadcasts a heartbeat; the plugin hears it
  and connects automatically — no IP/port setup.
- Transport-aware: fades audio in/out on DAW play/pause so it doesn't click.

## Tech
- **C++ / JUCE**, built with **CMake**. Opus is fetched & built by CMake
  (FetchContent) — no system Opus needed.
- Multithreaded: DAW audio thread → lock-free FIFO → network thread; a separate
  discovery thread; UI on the JUCE message thread.

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
This builds and installs the **VST3** and **AU** to `~/Library/Audio/Plug-Ins/`,
and produces a **Standalone** app under `build/Mix2Go_artefacts/Release/`.
On macOS the build produces a universal (x86_64 + arm64) binary.

## Use
1. Load **Mix2Go** on your DAW's master bus.
2. Open the **Mix2Go app** on a phone on the same Wi-Fi (or the phone's hotspot).
3. Tap **Start receiving** in the app — they connect automatically and the
   plugin shows `STREAMING`.

## Layout
```
source/
  PluginProcessor.*      DAW entry; processBlock (metering, transport fade, FIFO push)
  PluginEditor.*         the streaming UI (painted)
  GUI/Style/
    Mix2GoLookAndFeel.h  palette + control styling
  Streaming/
    AudioStreamManager.h orchestrator + packet builder + state machine
    ThreadSafeFIFO.h     lock-free audio↔network handoff
    OpusEncoder.h        resample + Opus encode
    NetworkSender.h      UDP send thread (absolute timing)
    DiscoveryReceiver.h  listens for the app's heartbeat
    AudioPacket.h        v2 packet format
assets/                  embedded fonts (Barlow Condensed, JetBrains Mono)
Resources/branding/      logo SVGs (design source)
```

## Architecture & protocol
See **[ARCHITECTURE.md](ARCHITECTURE.md)** for the full technical reference:
the discovery + audio wire protocols, the threading model, the adaptive jitter
buffer, the latency budget, and known limitations.

> Note: this repo still carries inert DSP-template code (an effect rack / macro
> system) from the plugin template it was scaffolded on. It is **not used** by
> the streamer.
