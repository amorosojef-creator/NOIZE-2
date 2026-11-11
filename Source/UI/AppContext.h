#pragma once
#include "Core/Engine.h"
#include "Core/Exporter.h"
#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace noize
{
struct EditTarget
{
    int trackId = -1; bool slot = false; int index = -1;
    bool valid() const { return trackId >= 0 && index >= 0; }
};

class Settings
{
public:
    int accentIndex = 0, quality = 2, scalePct = 100, autosaveMinutes = 2;
    double defaultBpm = 120.0; int defaultTimeSigNum = 4;
    juce::StringArray sampleDirs, pluginDirs, favorites;

    void load()
    {
        juce::PropertiesFile::Options o; o.applicationName = "NOIZE"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        o.folderName = Engine::appDataDir().getFullPathName();
        props = std::make_unique<juce::PropertiesFile> (Engine::appDataDir().getChildFile ("NOIZE.settings"), o);
        accentIndex = props->getIntValue ("accent", 0); quality = props->getIntValue ("quality", 2); scalePct = props->getIntValue ("scale", 100);
        autosaveMinutes = props->getIntValue ("autosaveMin", 2); defaultBpm = props->getDoubleValue ("defaultBpm", 120.0);
        defaultTimeSigNum = props->getIntValue ("defaultTsNum", 4);
        sampleDirs.addTokens (props->getValue ("sampleDirs"), "|", ""); sampleDirs.removeEmptyStrings();
        pluginDirs.addTokens (props->getValue ("pluginDirs"), "|", ""); pluginDirs.removeEmptyStrings();
        favorites.addTokens (props->getValue ("favorites"), "|", ""); favorites.removeEmptyStrings();
        if (sampleDirs.isEmpty()) sampleDirs.add (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName());
        applyTheme();
    }
    void save()
    {
        if (! props) return;
        props->setValue ("accent", accentIndex); props->setValue ("quality", quality); props->setValue ("scale", scalePct);
        props->setValue ("autosaveMin", autosaveMinutes); props->setValue ("defaultBpm", defaultBpm); props->setValue ("defaultTsNum", defaultTimeSigNum);
        props->setValue ("sampleDirs", sampleDirs.joinIntoString ("|")); props->setValue ("pluginDirs", pluginDirs.joinIntoString ("|"));
        props->setValue ("favorites", favorites.joinIntoString ("|"));
        props->saveIfNeeded();
    }
    static juce::StringArray accentNames() { return { "Neon Lime", "Cyan", "Magenta", "Amber", "White" }; }
    void applyTheme()
    {
        static const juce::uint32 c[] = { 0xffccff00, 0xff35e0ff, 0xffff4fd8, 0xffffb000, 0xffeeeeee };
        Theme::accent = juce::Colour (c[juce::jlimit (0, 4, accentIndex)]);
        Theme::quality = juce::jlimit (0, 2, quality);
    }
    juce::PropertiesFile* getProps() { return props.get(); }
private:
    std::unique_ptr<juce::PropertiesFile> props;
};

/** Everything the views share: engine, settings, selection and the edit/undo plumbing. */
class AppContext : public juce::ChangeBroadcaster
{
public:
    Engine engine;
    Settings settings;
    int selectedTrack = -1;
    EditTarget target;
    struct SlotRec { int trackId = -1, slot = -1; } slotRecord;   // pending 'record into session slot'
    int pattern = 0;           // current pattern = session slot index (0..7)
    double snap = 0.25;        // beats

    Project& project() { return engine.project(); }
    Track* selTrack() { return project().findTrack (selectedTrack); }
    Clip* targetClip()
    {
        if (! target.valid()) return nullptr;
        auto* t = project().findTrack (target.trackId);
        if (t == nullptr) return nullptr;
        auto& v = target.slot ? t->slots : t->clips;
        return target.index < (int) v.size() ? &v[(size_t) target.index] : nullptr;
    }
    /** Called after Engine::stopRecording(): moves a freshly recorded arrangement clip into the armed session slot. */
    void finishSlotRecord()
    {
        if (slotRecord.trackId < 0) return;
        if (auto* t = project().findTrack (slotRecord.trackId))
            if (! t->clips.empty() && t->clips.back().name == "MIDI Recording" && slotRecord.slot >= 0)
            {
                ensureSlots (*t);
                Clip c = t->clips.back(); t->clips.pop_back(); c.startBeat = 0; c.name = "Recorded";
                c.lengthBeats = juce::jmax (1.0, std::ceil (c.lengthBeats / 4.0) * 4.0);
                t->slots[(size_t) slotRecord.slot] = c;
            }
        slotRecord = {}; touch();
    }
    void beginEdit() { engine.checkpoint(); }
    void touch() { engine.commit(); engine.markDirty(); sendChangeMessage(); }
    template <class F> void edit (F&& f) { beginEdit(); f(); touch(); }
    void selectTrack (int id) { selectedTrack = id; sendChangeMessage(); }
    void editClip (int trackId, bool slot, int index) { target = { trackId, slot, index }; selectedTrack = trackId; sendChangeMessage(); }
    /** Ensures session slots 0..7 exist for a track. */
    static void ensureSlots (Track& t) { if (t.slots.size() < 8) t.slots.resize (8); for (auto& c : t.slots) if (c.lengthBeats <= 0) c.lengthBeats = 4.0; }
    static juce::String beatsToBBT (double beats, int num, int den)
    {
        const double perBar = num * 4.0 / den;
        const int bar = (int) std::floor (beats / perBar) + 1;
        const double r = beats - (bar - 1) * perBar;
        return juce::String (bar) + "." + juce::String ((int) r + 1) + "." + juce::String ((int) ((r - std::floor (r)) * 100.0)).paddedLeft ('0', 2);
    }
};
}
