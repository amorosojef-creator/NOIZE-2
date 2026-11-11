#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>
#include <memory>

namespace noize
{
using ParamMap = std::map<juce::String, float>;

struct Note
{
    int pitch = 60;            // MIDI note number
    double startBeat = 0.0;    // relative to clip start
    double lengthBeats = 1.0;
    float velocity = 0.8f;     // 0..1
};

struct Clip
{
    juce::String name { "Clip" };
    double startBeat = 0.0;    // position on timeline (arrangement)
    double lengthBeats = 4.0;
    std::vector<Note> notes;   // MIDI clips
    juce::String audioFile;    // audio clips (absolute or project-relative path)
    double audioStartBeat = 0.0;
    bool isAudio() const noexcept { return audioFile.isNotEmpty(); }
};

enum class InstrumentType { PolySynth, SubSynth, BassSynth, DrumMachine, Sampler, Piano };
enum class EffectType { EQ, Compressor, Limiter, Reverb, Delay, Chorus, Distortion, LowPass, HighPass, StereoWidth, Gain };

juce::String toString (InstrumentType);
juce::String toString (EffectType);
InstrumentType instrumentFromString (const juce::String&);
EffectType effectFromString (const juce::String&);
ParamMap defaultParams (InstrumentType);
ParamMap defaultParams (EffectType);

struct EffectSlot
{
    EffectType type = EffectType::Gain;
    bool bypass = false;
    ParamMap params;
};

struct Track
{
    int id = 0;
    juce::String name { "Track" };
    bool audioTrack = false;
    InstrumentType instrument = InstrumentType::PolySynth;
    ParamMap instrumentParams;
    juce::String samplerFile;      // used by Sampler instrument
    float volume = 0.8f;           // 0..1.25 (linear gain mapped in engine)
    float pan = 0.0f;              // -1..1
    bool mute = false, solo = false;
    float sendReverb = 0.0f, sendDelay = 0.0f;
    int group = -1;                // track grouping (-1 none)
    std::vector<EffectSlot> effects;
    std::vector<Clip> clips;
    std::vector<Clip> slots;       // Session view clip slots (looping, launched in real time)
    // Automation: parameter id -> sorted breakpoints (beat, value 0..1)
    std::map<juce::String, std::vector<std::pair<double, float>>> automation;
};

struct Project
{
    juce::String name { "Untitled" };
    double bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;
    bool loopEnabled = false;
    double loopStartBeat = 0.0, loopEndBeat = 8.0;
    double lengthBeats = 32.0;
    bool metronome = false;
    float masterVolume = 0.8f;
    std::vector<EffectSlot> masterEffects;
    std::vector<Track> tracks;
    int nextTrackId = 1;

    Track& addTrack (const juce::String& name, bool audio, InstrumentType inst = InstrumentType::PolySynth);
    Track* findTrack (int id);
    const Track* findTrack (int id) const;
    void removeTrack (int id);

    juce::var toVar() const;
    static Project fromVar (const juce::var&);
    bool saveToFile (const juce::File&) const;
    static bool loadFromFile (const juce::File&, Project& out, juce::String* error = nullptr);
    juce::StringArray findMissingAssets() const;
};

// Convenience builders used by the UI + tests
Clip makeStepPatternClip (const std::vector<std::vector<bool>>& rowsByDrum, const std::vector<int>& pitches,
                          int stepsPerBeat = 4, double lengthBeats = 4.0);

/** Original demo song ("First Light") used as the bundled sample project. */
Project makeDemoProject();
}
