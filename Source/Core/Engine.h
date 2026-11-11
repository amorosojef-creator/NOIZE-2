#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <array>
#include <deque>
#include <map>
#include <mutex>
#include "Project.h"
#include "Effects.h"
#include "Instruments.h"

namespace noize
{
constexpr int kMaxTracks = 64;
constexpr int kVisSize = 16384;

/** Loads and caches decoded audio files. Never called from the audio thread. */
class AssetCache
{
public:
    AssetCache();
    std::shared_ptr<const SampleData> get (const juce::String& path);
    void clear();
private:
    juce::AudioFormatManager formats;
    std::mutex mutex;
    std::map<juce::String, std::shared_ptr<const SampleData>> cache;
};

struct TrackRuntime
{
    InstrumentType instType = InstrumentType::PolySynth;
    std::unique_ptr<Instrument> inst;
    EffectChain fx;
    juce::AudioBuffer<float> buf;
    uint64_t appliedVersion = 0;
    float lastGain[2] { 0.f, 0.f };
    bool gainInit = false;
    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    int activeSlot = -1;
    double activeStart = 0.0;
    std::shared_ptr<const SampleData> sampleHold;
};

struct Snapshot
{
    Project project;
    uint64_t version = 0;
    bool anySolo = false;
    std::vector<std::shared_ptr<TrackRuntime>> rt;                       // parallel to project.tracks
    std::vector<std::shared_ptr<const SampleData>> samplers;             // parallel to project.tracks
    std::vector<std::vector<std::shared_ptr<const SampleData>>> clipData, slotData;  // audio data per track/clip (null = missing)
};

struct MidiRecEvent { bool on; int note; float vel; double beat; };

/** Device-independent audio renderer. process() is real-time safe in steady state:
    no locks (only try-lock), no allocation, no UI access. Also used for offline export. */
class Renderer
{
public:
    explicit Renderer (AssetCache&);

    void prepare (double sampleRate, int maxBlockSize);
    /** Message thread / non-realtime: build a new immutable snapshot and hand it to the audio thread. */
    void publish (const Project&);
    /** Audio thread. Writes up to 2 channels of output (adds nothing to what was there: output is overwritten). */
    void process (float* const* out, int numOutChannels, int numSamples, const juce::MidiBuffer* liveMidi);

    // transport (thread-safe)
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };           // beats
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> recordMidi { false };
    std::atomic<int> liveTrackId { -1 };
    bool offline = false;

    // session clip launching (thread-safe, quantized to the next bar)
    void launchSlot (int trackIndex, int slot);
    void stopTrackSlot (int trackIndex);
    void launchScene (int slot);
    int getActiveSlot (int trackIndex) const;
    int getQueuedSlot (int trackIndex) const;       // -2 none, -1 stop queued, >=0 slot queued

    // metering / visualisation (message thread)
    float takeTrackPeak (int trackIndex, int channel);
    float takeMasterPeak (int channel);
    void copyLatestSamples (float* dest, int count) const;
    bool popMidiRecEvent (MidiRecEvent&);
    double getSampleRate() const { return sr; }

private:
    struct Ev { int sample; bool on; int note; float vel; };
    struct ClickEv { int sample; bool accent; };

    void processChunk (float* const* out, int nOut, int n, const juce::MidiBuffer* live);
    void renderSegment (Snapshot&, int off, int num, double b0, double b1, bool isPlaying);
    void scanMidiClip (const Clip&, double offset, double clipLen, bool loop, double b0, double b1, int num);
    void renderAudioClip (const SampleData&, TrackRuntime&, const Clip&, double clipStartBeat, bool loop,
                          double b0, double b1, int num);
    void stopAllNotes (Snapshot&);
    double latestBarBeats();
    static float evalAutomation (const std::vector<std::pair<double, float>>&, double beat);

    AssetCache& cache;
    double sr = 44100.0; int maxBlock = 512;
    std::mutex publishMutex;
    juce::SpinLock handoff;
    Snapshot* pending = nullptr;   // owned by `history`
    Snapshot* current = nullptr;   // audio thread only, owned by `history`
    std::shared_ptr<Snapshot> latestUi;
    std::deque<std::shared_ptr<Snapshot>> history;
    std::map<int, std::shared_ptr<TrackRuntime>> runtimes;
    uint64_t versionCounter = 0;

    std::array<std::atomic<int>, kMaxTracks> pendingSlot;
    std::array<std::atomic<int>, kMaxTracks> activeMirror;

    juce::AudioBuffer<float> master, reverbBus, delayBus, delayDry, clickBuf;
    juce::Reverb busReverb;
    std::unique_ptr<Effect> busDelay;
    EffectChain masterFx;
    uint64_t masterFxVersion = 0;
    float masterGainLast = 0.f; bool masterGainInit = false;
    bool wasPlaying = false;
    double chunkBeat = 0.0;

    std::vector<Ev> events, liveEvents; std::vector<ClickEv> clickEvents;
    double curBps = 2.0;
    int clickRemain = 0, clickTotal = 1; double clickPhase = 0.0, clickInc = 0.0; float clickAmp = 0.f;

    std::atomic<float> masterPeak[2] { { 0.f }, { 0.f } };
    std::array<float, kVisSize> visBuf {};
    std::atomic<int> visWrite { 0 };
    juce::AbstractFifo midiFifo { 2048 };
    std::array<MidiRecEvent, 2048> midiFifoData;
};

/** Owns the project, devices, undo history, recording and autosave. */
class Engine : private juce::AudioIODeviceCallback, private juce::MidiInputCallback
{
public:
    Engine();
    ~Engine() override;

    /** Opens the default audio + MIDI devices. Returns an error string or empty. */
    juce::String initialiseAudio();
    void shutdownAudio();
    juce::AudioDeviceManager& deviceManager() { return devices; }

    // project (message thread only)
    Project& project() { return proj; }
    void commit();                      // publish edits to the audio thread
    void checkpoint();                  // push an undo state (call BEFORE modifying)
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void newProject();
    bool openProject (const juce::File&, juce::String* err = nullptr);
    bool saveProject (const juce::File&);
    juce::File getCurrentFile() const { return currentFile; }
    bool isDirty() const { return dirty; }
    void markDirty() { dirty = true; }
    juce::File defaultRecordingsDir() const;

    // autosave / crash recovery
    static juce::File appDataDir();
    static juce::File recoveryFile();
    bool autosave();
    static bool hasRecovery();
    void clearRecovery();

    // transport
    void play();
    void stop();
    void togglePlay();
    void setPosition (double beats);
    double getPosition() const { return renderer.position.load(); }
    bool isPlaying() const { return renderer.playing.load(); }
    void setTempo (double bpm);

    // recording
    void pollMidiRecording() { drainMidiRec(); }
    bool startRecording (int trackId, juce::String* err = nullptr);
    void stopRecording();
    bool isRecording() const { return recording; }

    // live MIDI
    void setLiveTrack (int trackId) { renderer.liveTrackId = trackId; }
    void injectMidi (const juce::MidiMessage& m) { midiCollector.addMessageToQueue (m); }

    Renderer& getRenderer() { return renderer; }
    AssetCache& getAssets() { return assets; }
    double getCpuLoad() { return devices.getCpuUsage(); }

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void finishMidiRecording();
    void drainMidiRec();

    AssetCache assets;
    Renderer renderer;
    juce::AudioDeviceManager devices;
    juce::MidiMessageCollector midiCollector;
    Project proj;
    std::vector<Project> undoStack, redoStack;
    juce::File currentFile;
    bool dirty = false;
    bool audioStarted = false;

    // recording state
    bool recording = false, recordingAudio = false; int recTrackId = -1; double recStartBeat = 0.0;
    juce::File recFile;
    std::unique_ptr<juce::TimeSliceThread> writerThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::SpinLock writerLock;
    juce::int64 recSamples = 0;
    std::vector<MidiRecEvent> recordedEvents;
    juce::MidiBuffer midiScratch;
    std::map<int, std::pair<double,float>> heldNotes;
};
}
