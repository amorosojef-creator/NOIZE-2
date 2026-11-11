# Licenses and dependencies

| Component | Use | License |
|---|---|---|
| JUCE 9.0.5 | application framework, audio I/O, GUI, DSP helpers, VST3 hosting | AGPLv3 (or JUCE commercial license) |
| Steinberg VST3 SDK (bundled inside JUCE) | VST3 plugin hosting | MIT-style (VST3 SDK 3.8+) / GPLv3 per JUCE's bundle |
| FreeType, libpng, libjpeg, zlib, FLAC, Ogg/Vorbis (bundled inside JUCE) | fonts, image and audio decoding | permissive, see JUCE's `modules/*/` licenses |

Because JUCE is used under the AGPLv3, NOIZE as a whole is distributed under the AGPLv3 with full source available.
Selling closed-source builds would require a JUCE commercial license.

**Not included on purpose:** the Steinberg ASIO SDK (not redistributable) — so ASIO is off; Windows uses WASAPI and DirectSound.
**MP3:** decoding only (JUCE's built-in decoder). MP3 export is **not** implemented.

All presets, the demo song "First Light" and all branding are original to NOIZE. No Ableton or FL Studio code, names or assets were used.
