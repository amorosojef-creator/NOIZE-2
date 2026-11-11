# NOIZE user guide

## Basics
- **Space** play/stop · **Ctrl+Z / Ctrl+Y** undo/redo · **Ctrl+N/O/S** new/open/save · **Ctrl+Shift+S** save as · **Ctrl+E** export WAV · **Ctrl+L** loop · **Ctrl+M** metronome · **F1–F7** switch workspace
- Top bar: Play, Stop, Rec, Loop, Metronome, tempo (drag), time signature, position (bar.beat.tick), Undo/Redo, **3D FX** (cycles visual quality: flat 2D → low → high), CPU meter and master meter.
- Right panel: **SAMPLES** (search, favorites, add folders, import, click to preview, drag onto an audio track) and **PLUGINS** (VST3).

## Workspaces
1. **Arrangement** — MIDI and audio tracks; add tracks, draw/move/resize/duplicate clips, snap, zoom, loop region, automation lanes (volume, pan, sends).
2. **Session** — clip slots × scenes. Click a slot or scene to launch (quantized to the next bar). Double-click an empty slot to create a clip.
3. **Channel Rack** — step sequencer per channel, 8 patterns, duplicate pattern, per-channel volume/mute, "Place on timeline".
4. **Piano Roll** — draw/edit notes, velocity lane, quantize, snap, Import/Export MIDI file.
5. **Mixer** — faders, pan, mute/solo, reverb/delay sends, insert effect chain (add, reorder, bypass, remove), master channel with its own inserts, meters.
Plus **Synth** (oscillator, filter, envelope controls with live waveform/filter/envelope graphs and an on-screen keyboard) and **Visualizer** (spectrum, fullscreen button; Esc leaves).

## Recording
Select a track, press **Rec**. MIDI tracks record notes from a connected MIDI keyboard. Audio tracks record from the selected input device into a 24-bit WAV.

## Projects and export
- Projects are `.noize` files (JSON). Autosave runs on the interval in Settings; after a crash NOIZE offers to restore.
- **File > Export WAV**: 44.1/48/88.2/96 kHz, 16/24/32-bit float, stereo or mono, whole project or loop region, rendered faster than real time with a reverb/delay tail.

## Settings (Tools > Settings)
Audio device, sample rate, buffer size, MIDI inputs · interface scale, visual quality, accent colour, autosave, default tempo/time signature · sample library folders and extra VST3 folders · shortcut list (fixed).
