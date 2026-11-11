# NOIZE 0.1.0 — honest status

Legend: **Verified** = exercised by an automated test or by launching the app in the Linux build container. **Built, not verified** = code exists and compiles, but nothing here proved it works. **Not done** = missing.

## Verified
- **Engine (headless tests, 154 checks, 0 failed — Linux build):** project save/load round-trip; all six instruments produce finite, audible audio (Polysynth, Subtractive, Bass, Drum Machine, Sampler, Piano); sampler and audio-clip playback; all 11 effects measurably change the signal (EQ, compressor, limiter, reverb, delay, chorus, distortion, low-pass, high-pass, stereo width, gain); mixer mute/solo/pan/volume; transport (loop, seek, metronome, tempo); session clip/scene launching; offline WAV export at 44.1/48/88.2/96 kHz, 16/24/32-bit float, stereo/mono; undo/redo; save/open; autosave; stress test (24 tracks × 2 effects with live edits ≈ 8% of one core in real time); the bundled demo project renders audible audio.
- **App launch (Linux, virtual display):** the full app starts, loads `First_Light.noize`, and all seven workspaces (Arrangement, Session, Channel Rack, Piano Roll, Mixer, Synth, Visualizer) plus the Samples/Plugins panel render. See `docs/screenshots/workspaces.jpg`.
- Design is fully original; no third-party branding.

## Built, not verified (needs a real machine with sound card / mouse)
- Sound output to a real audio device, device/sample-rate/buffer selection, MIDI keyboard input, audio recording from an input device. (The container has no sound hardware.)
- Interactive editing: drawing/moving/resizing clips, piano-roll note editing, step sequencer clicking, automation lane editing, drag-and-drop from the sample browser, mixer insert reordering, MIDI import/export buttons.
- Sample browser (search, favorites, preview, waveform), settings dialog, WAV export dialog, crash-recovery prompt, missing-audio warning.
- **VST3 hosting:** scanning, listing and opening a plugin's own window are implemented. No VST3 plugin was available to test with.

## Not done
- **Windows `.exe`, portable ZIP and installer have NOT been produced.** JUCE 9 does not support the MinGW cross-compiler available in the build container, and the container cannot run Windows programs. Use `build-windows.bat` on a Windows PC, or the GitHub Actions workflow, to produce them (both untested for the same reason; the installer script `installer/noize.nsi` does compile under NSIS).
- macOS build script (`build-mac.command`) is untested.
- Hosted VST3 plugins are **not routed into the mixer/tracks** and their state is not saved in projects (GUI-only hosting).
- MP3 **export** (decode only). ASIO (license-restricted SDK; WASAPI and DirectSound are used).
- Editable keyboard shortcuts (fixed bindings). Track-grouping UI (data field exists only).
- **"3D" look:** the depth, lighting, knob and fader shading are drawn with JUCE's 2D software renderer, with a quality setting (flat 2D / low / high) and no GPU/OpenGL scene. It is 3D-*styled*, not a real 3D engine, so the "3D piano roll keyboard", "3D mixer" etc. are 2D with dimensional shading.
- Piano Roll opens an empty clip if no clip is selected; double-click a clip in the Arrangement first.
