#include <algorithm>
#include "Project.h"

namespace noize
{
static const char* instNames[] = { "PolySynth", "SubSynth", "BassSynth", "DrumMachine", "Sampler", "Piano" };
static const char* fxNames[] = { "EQ", "Compressor", "Limiter", "Reverb", "Delay", "Chorus", "Distortion", "LowPass", "HighPass", "StereoWidth", "Gain" };

juce::String toString (InstrumentType t) { return instNames[(int) t]; }
juce::String toString (EffectType t) { return fxNames[(int) t]; }

InstrumentType instrumentFromString (const juce::String& s)
{
    for (int i = 0; i < 6; ++i) if (s == instNames[i]) return (InstrumentType) i;
    return InstrumentType::PolySynth;
}
EffectType effectFromString (const juce::String& s)
{
    for (int i = 0; i < 11; ++i) if (s == fxNames[i]) return (EffectType) i;
    return EffectType::Gain;
}

ParamMap defaultParams (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::PolySynth:
            return { {"wave1",1}, {"wave2",0}, {"detune",0.1f}, {"mix2",0.4f}, {"attack",0.01f}, {"decay",0.2f},
                     {"sustain",0.7f}, {"release",0.3f}, {"cutoff",9000}, {"resonance",0.2f}, {"gain",0.5f} };
        case InstrumentType::SubSynth:
            return { {"wave1",0}, {"wave2",1}, {"detune",0.05f}, {"mix2",0.5f}, {"attack",0.005f}, {"decay",0.35f},
                     {"sustain",0.4f}, {"release",0.2f}, {"cutoff",2500}, {"resonance",0.45f}, {"filterEnv",4000}, {"gain",0.5f} };
        case InstrumentType::BassSynth:
            return { {"wave1",0}, {"wave2",2}, {"detune",0.0f}, {"mix2",0.5f}, {"attack",0.003f}, {"decay",0.25f},
                     {"sustain",0.6f}, {"release",0.12f}, {"cutoff",900}, {"resonance",0.35f}, {"filterEnv",1800}, {"gain",0.6f} };
        case InstrumentType::DrumMachine:
            return { {"kickTune",50}, {"kickDecay",0.35f}, {"snareTone",0.5f}, {"hatDecay",0.05f}, {"gain",0.8f} };
        case InstrumentType::Sampler:
            return { {"rootNote",60}, {"attack",0.002f}, {"release",0.15f}, {"gain",0.8f} };
        case InstrumentType::Piano:
            return { {"brightness",0.5f}, {"decay",1.8f}, {"release",0.25f}, {"gain",0.6f} };
    }
    return {};
}

ParamMap defaultParams (EffectType t)
{
    switch (t)
    {
        case EffectType::EQ:          return { {"lowFreq",120}, {"lowGain",0}, {"midFreq",1000}, {"midGain",0}, {"midQ",1.0f}, {"highFreq",8000}, {"highGain",0} };
        case EffectType::Compressor:  return { {"threshold",-18}, {"ratio",3}, {"attack",10}, {"release",120}, {"makeup",0} };
        case EffectType::Limiter:     return { {"ceiling",-0.3f}, {"release",80} };
        case EffectType::Reverb:      return { {"room",0.5f}, {"damp",0.5f}, {"wet",0.25f} };
        case EffectType::Delay:       return { {"timeMs",375}, {"feedback",0.35f}, {"wet",0.3f} };
        case EffectType::Chorus:      return { {"rate",0.8f}, {"depth",0.35f}, {"mix",0.4f} };
        case EffectType::Distortion:  return { {"drive",12}, {"mix",1.0f}, {"tone",0.6f} };
        case EffectType::LowPass:     return { {"cutoff",8000}, {"resonance",0.7f} };
        case EffectType::HighPass:    return { {"cutoff",80}, {"resonance",0.7f} };
        case EffectType::StereoWidth: return { {"width",1.0f} };
        case EffectType::Gain:        return { {"gainDb",0} };
    }
    return {};
}

Track& Project::addTrack (const juce::String& n, bool audio, InstrumentType inst)
{
    Track t;
    t.id = nextTrackId++;
    t.name = n;
    t.audioTrack = audio;
    t.instrument = inst;
    t.instrumentParams = defaultParams (inst);
    tracks.push_back (std::move (t));
    return tracks.back();
}

Track* Project::findTrack (int id)
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
const Track* Project::findTrack (int id) const
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
void Project::removeTrack (int id)
{
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [id] (const Track& t) { return t.id == id; }), tracks.end());
}

// ------------------------------ serialization ------------------------------
static juce::var paramsToVar (const ParamMap& m)
{
    auto* o = new juce::DynamicObject();
    for (auto& kv : m) o->setProperty (kv.first, (double) kv.second);
    return juce::var (o);
}
static ParamMap varToParams (const juce::var& v)
{
    ParamMap m;
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties()) m[p.name.toString()] = (float) (double) p.value;
    return m;
}
static juce::var fxToVar (const std::vector<EffectSlot>& fx)
{
    juce::Array<juce::var> a;
    for (auto& s : fx)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", toString (s.type));
        o->setProperty ("bypass", s.bypass);
        o->setProperty ("params", paramsToVar (s.params));
        a.add (juce::var (o));
    }
    return a;
}
static std::vector<EffectSlot> varToFx (const juce::var& v)
{
    std::vector<EffectSlot> out;
    if (auto* a = v.getArray())
        for (auto& e : *a)
        {
            EffectSlot s;
            s.type = effectFromString (e["type"].toString());
            s.bypass = (bool) e["bypass"];
            s.params = defaultParams (s.type);
            for (auto& kv : varToParams (e["params"])) s.params[kv.first] = kv.second;
            out.push_back (std::move (s));
        }
    return out;
}

juce::var Project::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "NOIZE-project");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("bpm", bpm);
    root->setProperty ("timeSigNum", timeSigNum);
    root->setProperty ("timeSigDen", timeSigDen);
    root->setProperty ("loopEnabled", loopEnabled);
    root->setProperty ("loopStartBeat", loopStartBeat);
    root->setProperty ("loopEndBeat", loopEndBeat);
    root->setProperty ("lengthBeats", lengthBeats);
    root->setProperty ("metronome", metronome);
    root->setProperty ("masterVolume", (double) masterVolume);
    root->setProperty ("nextTrackId", nextTrackId);
    root->setProperty ("masterEffects", fxToVar (masterEffects));

    juce::Array<juce::var> trs;
    for (auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", t.id);
        o->setProperty ("name", t.name);
        o->setProperty ("audioTrack", t.audioTrack);
        o->setProperty ("instrument", toString (t.instrument));
        o->setProperty ("instrumentParams", paramsToVar (t.instrumentParams));
        o->setProperty ("samplerFile", t.samplerFile);
        o->setProperty ("volume", (double) t.volume);
        o->setProperty ("pan", (double) t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("sendReverb", (double) t.sendReverb);
        o->setProperty ("sendDelay", (double) t.sendDelay);
        o->setProperty ("group", t.group);
        o->setProperty ("effects", fxToVar (t.effects));

        auto clipsToVar = [] (const std::vector<Clip>& src)
        {
        juce::Array<juce::var> clips;
        for (auto& c : src)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("name", c.name);
            co->setProperty ("startBeat", c.startBeat);
            co->setProperty ("lengthBeats", c.lengthBeats);
            co->setProperty ("audioFile", c.audioFile);
            co->setProperty ("audioStartBeat", c.audioStartBeat);
            juce::Array<juce::var> notes;
            for (auto& n : c.notes)
            {
                auto* no = new juce::DynamicObject();
                no->setProperty ("p", n.pitch);
                no->setProperty ("s", n.startBeat);
                no->setProperty ("l", n.lengthBeats);
                no->setProperty ("v", (double) n.velocity);
                notes.add (juce::var (no));
            }
            co->setProperty ("notes", notes);
            clips.add (juce::var (co));
        }
        return clips;
        };
        o->setProperty ("clips", clipsToVar (t.clips));
        o->setProperty ("slots", clipsToVar (t.slots));

        auto* au = new juce::DynamicObject();
        for (auto& kv : t.automation)
        {
            juce::Array<juce::var> pts;
            for (auto& p : kv.second) { juce::Array<juce::var> pr; pr.add (p.first); pr.add ((double) p.second); pts.add (pr); }
            au->setProperty (kv.first, pts);
        }
        o->setProperty ("automation", juce::var (au));
        trs.add (juce::var (o));
    }
    root->setProperty ("tracks", trs);
    return juce::var (root);
}

Project Project::fromVar (const juce::var& v)
{
    Project p;
    p.name = v["name"].toString();
    p.bpm = v.getProperty ("bpm", 120.0);
    p.timeSigNum = v.getProperty ("timeSigNum", 4);
    p.timeSigDen = v.getProperty ("timeSigDen", 4);
    p.loopEnabled = (bool) v.getProperty ("loopEnabled", false);
    p.loopStartBeat = v.getProperty ("loopStartBeat", 0.0);
    p.loopEndBeat = v.getProperty ("loopEndBeat", 8.0);
    p.lengthBeats = v.getProperty ("lengthBeats", 32.0);
    p.metronome = (bool) v.getProperty ("metronome", false);
    p.masterVolume = (float) (double) v.getProperty ("masterVolume", 0.9);
    p.nextTrackId = v.getProperty ("nextTrackId", 1);
    p.masterEffects = varToFx (v["masterEffects"]);

    if (auto* trs = v["tracks"].getArray())
        for (auto& tv : *trs)
        {
            Track t;
            t.id = tv["id"];
            t.name = tv["name"].toString();
            t.audioTrack = (bool) tv["audioTrack"];
            t.instrument = instrumentFromString (tv["instrument"].toString());
            t.instrumentParams = defaultParams (t.instrument);
            for (auto& kv : varToParams (tv["instrumentParams"])) t.instrumentParams[kv.first] = kv.second;
            t.samplerFile = tv["samplerFile"].toString();
            t.volume = (float) (double) tv["volume"];
            t.pan = (float) (double) tv["pan"];
            t.mute = (bool) tv["mute"];
            t.solo = (bool) tv["solo"];
            t.sendReverb = (float) (double) tv["sendReverb"];
            t.sendDelay = (float) (double) tv["sendDelay"];
            t.group = tv.getProperty ("group", -1);
            t.effects = varToFx (tv["effects"]);
            auto varToClips = [] (const juce::var& arr)
            {
            std::vector<Clip> result;
            if (auto* cs = arr.getArray())
                for (auto& cv : *cs)
                {
                    Clip c;
                    c.name = cv["name"].toString();
                    c.startBeat = cv["startBeat"];
                    c.lengthBeats = cv["lengthBeats"];
                    c.audioFile = cv["audioFile"].toString();
                    c.audioStartBeat = cv["audioStartBeat"];
                    if (auto* ns = cv["notes"].getArray())
                        for (auto& nv : *ns)
                            c.notes.push_back ({ (int) nv["p"], (double) nv["s"], (double) nv["l"], (float) (double) nv["v"] });
                    result.push_back (std::move (c));
                }
            return result;
            };
            t.clips = varToClips (tv["clips"]);
            t.slots = varToClips (tv["slots"]);
            if (auto* ao = tv["automation"].getDynamicObject())
                for (auto& prop : ao->getProperties())
                    if (auto* pts = prop.value.getArray())
                        for (auto& pt : *pts)
                            t.automation[prop.name.toString()].push_back ({ (double) pt[0], (float) (double) pt[1] });
            p.tracks.push_back (std::move (t));
        }
    return p;
}

bool Project::saveToFile (const juce::File& f) const
{
    // Write to a temp file first and then replace, so a crash mid-save cannot corrupt the project.
    auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
    if (! tmp.replaceWithText (juce::JSON::toString (toVar(), false))) return false;
    return tmp.moveFileTo (f);
}

bool Project::loadFromFile (const juce::File& f, Project& out, juce::String* error)
{
    if (! f.existsAsFile()) { if (error) *error = "File not found"; return false; }
    auto v = juce::JSON::parse (f);
    if (v["format"].toString() != "NOIZE-project")
    {
        if (error) *error = "Not a NOIZE project file";
        return false;
    }
    out = fromVar (v);
    return true;
}

juce::StringArray Project::findMissingAssets() const
{
    juce::StringArray missing;
    for (auto& t : tracks)
    {
        if (t.samplerFile.isNotEmpty() && ! juce::File (t.samplerFile).existsAsFile()) missing.addIfNotAlreadyThere (t.samplerFile);
        for (auto* list : { &t.clips, &t.slots })
            for (auto& c : *list)
                if (c.isAudio() && ! juce::File (c.audioFile).existsAsFile()) missing.addIfNotAlreadyThere (c.audioFile);
    }
    return missing;
}

Clip makeStepPatternClip (const std::vector<std::vector<bool>>& rows, const std::vector<int>& pitches, int stepsPerBeat, double lengthBeats)
{
    Clip c;
    c.name = "Pattern";
    c.lengthBeats = lengthBeats;
    const double stepLen = 1.0 / stepsPerBeat;
    for (size_t r = 0; r < rows.size() && r < pitches.size(); ++r)
        for (size_t s = 0; s < rows[r].size(); ++s)
            if (rows[r][s] && s * stepLen < lengthBeats)
                c.notes.push_back ({ pitches[r], s * stepLen, stepLen * 0.9, 0.9f });
    return c;
}

Project makeDemoProject()
{
    Project p; p.name = "First Light (demo)"; p.bpm = 128.0; p.lengthBeats = 32.0; p.loopEnabled = true; p.loopStartBeat = 0.0; p.loopEndBeat = 16.0;
    p.addTrack ("Lead", false, InstrumentType::PolySynth);
    p.addTrack ("Bass", false, InstrumentType::BassSynth);
    p.addTrack ("Drums", false, InstrumentType::DrumMachine);
    p.addTrack ("Keys", false, InstrumentType::Piano);
    // take references only after all tracks exist (addTrack may reallocate the vector)
    auto& lead = p.tracks[0]; auto& bass = p.tracks[1]; auto& drums = p.tracks[2]; auto& keys = p.tracks[3];
    // chord progression Am - F - C - G, one bar each, repeated over 8 bars
    const int roots[4] = { 57, 53, 60, 55 };
    const int thirds[4] = { 3, 4, 4, 4 }, fifths = 7;
    Clip chords; chords.name = "Chords"; chords.lengthBeats = 16.0;
    Clip bassC; bassC.name = "Bassline"; bassC.lengthBeats = 16.0;
    Clip arp; arp.name = "Arp"; arp.lengthBeats = 16.0;
    for (int bar = 0; bar < 4; ++bar)
    {
        const double b = bar * 4.0; const int r = roots[bar];
        for (int n : { 0, thirds[bar], fifths }) chords.notes.push_back ({ r + n, b, 3.8, 0.7f });
        for (int s = 0; s < 8; ++s) bassC.notes.push_back ({ r - 24 + ((s % 4 == 3) ? 12 : 0), b + s * 0.5, 0.45, s % 2 ? 0.7f : 0.95f });
        const int arpN[4] = { 0, thirds[bar], fifths, 12 };
        for (int s = 0; s < 16; ++s) arp.notes.push_back ({ r + 12 + arpN[s % 4], b + s * 0.25, 0.22, 0.6f + 0.05f * (s % 4) });
    }
    keys.clips.push_back (chords);
    bass.clips.push_back (bassC);
    lead.clips.push_back (arp);
    std::vector<std::vector<bool>> rows (4, std::vector<bool> (16, false));
    for (int s : { 0, 4, 8, 12 }) rows[0][(size_t) s] = true;           // kick on every beat
    for (int s : { 4, 12 }) rows[1][(size_t) s] = true;                 // snare on 2 and 4
    for (int s = 0; s < 16; s += 2) rows[2][(size_t) s] = true;         // closed hats on 8ths
    rows[3][14] = true;                                                 // open hat
    auto beat = makeStepPatternClip (rows, { 36, 38, 42, 46 }, 4, 4.0);
    beat.name = "Beat"; beat.lengthBeats = 16.0;
    for (int bar = 1; bar < 4; ++bar) { auto notes = beat.notes; for (auto n : notes) if (n.startBeat < 4.0) { n.startBeat += bar * 4.0; beat.notes.push_back (n); } }
    drums.clips.push_back (beat);
    // session slots so the Session view has something to launch
    for (auto* t : { &lead, &bass, &drums, &keys }) t->slots.resize (8);
    Clip s0 = beat; s0.lengthBeats = 4.0; s0.notes.erase (std::remove_if (s0.notes.begin(), s0.notes.end(), [] (const Note& n) { return n.startBeat >= 4.0; }), s0.notes.end());
    drums.slots[0] = s0;
    Clip b0 = bassC; b0.lengthBeats = 4.0; b0.notes.erase (std::remove_if (b0.notes.begin(), b0.notes.end(), [] (const Note& n) { return n.startBeat >= 4.0; }), b0.notes.end());
    bass.slots[0] = b0;
    for (auto* t : { &lead, &bass, &drums, &keys }) for (auto& c : t->slots) if (c.lengthBeats <= 0) c.lengthBeats = 4.0;
    lead.effects.push_back ({ EffectType::Delay, false, defaultParams (EffectType::Delay) });
    lead.sendReverb = 0.25f; keys.sendReverb = 0.2f;
    p.masterEffects.push_back ({ EffectType::Limiter, false, defaultParams (EffectType::Limiter) });
    return p;
}
}
