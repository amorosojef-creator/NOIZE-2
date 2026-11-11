#pragma once
#include <functional>
#include "Engine.h"

namespace noize
{
struct ExportOptions
{
    juce::File file;
    double sampleRate = 44100.0;
    int bitDepth = 24;            // 16, 24 or 32 (32 = float)
    bool mono = false;
    double startBeat = 0.0;
    double endBeat = -1.0;        // <0: auto (project length / last clip end)
    double tailSeconds = 3.0;     // reverb/delay tail
    bool normalize = false;
};

struct ExportResult { bool ok = false; juce::String message; double seconds = 0.0; float peak = 0.f; };

/** Offline (faster than real time) render of a project to WAV. progress returns false to cancel. */
ExportResult renderProjectToWav (const Project&, const ExportOptions&, std::function<bool (float)> progress = nullptr);
double computeProjectEndBeat (const Project&);
}
