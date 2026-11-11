#include <juce_core/juce_core.h>
#include "Core/Engine.h"
#include "Core/Exporter.h"
#include <cmath>

using namespace noize;
static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static juce::File tmpDir() { auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("noize_tests"); d.createDirectory(); return d; }

static float rms (const juce::AudioBuffer<float>& b)
{
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { s += b.getSample (c, i) * b.getSample (c, i); ++n; }
    return n ? (float) std::sqrt (s / n) : 0.f;
}
static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static juce::AudioBuffer<float> renderOffline (const Project& p, double seconds, double sr = 44100.0)
{
    AssetCache cache; Renderer r (cache); r.prepare (sr, 512); r.publish (p); r.playing = true;
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    for (int done = 0; done < total; done += 512)
    {
        const int n = std::min (512, total - done);
        float* ch[2] = { out.getWritePointer (0) + done, out.getWritePointer (1) + done };
        r.process (ch, 2, n, nullptr);
    }
    return out;
}

static Clip chord (double len = 4.0)
{
    Clip c; c.lengthBeats = len;
    c.notes = { { 60, 0.0, 1.0, 0.9f }, { 64, 1.0, 1.0, 0.9f }, { 67, 2.0, 2.0, 0.9f } };
    return c;
}

static void testProjectRoundTrip()
{
    std::puts ("[project serialization]");
    Project p; p.bpm = 133; p.loopEnabled = true; p.timeSigNum = 7; p.timeSigDen = 8;
    auto& t = p.addTrack ("Keys", false, InstrumentType::Piano);
    t.clips.push_back (chord()); t.volume = 0.6f; t.pan = -0.3f; t.solo = true; t.group = 2;
    t.effects.push_back ({ EffectType::Reverb, true, defaultParams (EffectType::Reverb) });
    t.automation["volume"] = { { 0.0, 0.2f }, { 4.0, 1.0f } };
    t.slots.push_back (chord (2.0));
    p.masterEffects.push_back ({ EffectType::Limiter, false, defaultParams (EffectType::Limiter) });
    auto f = tmpDir().getChildFile ("rt.noize");
    CHECK (p.saveToFile (f), "save");
    Project q; juce::String err;
    CHECK (Project::loadFromFile (f, q, &err), "load");
    CHECK (q.bpm == 133 && q.timeSigNum == 7 && q.timeSigDen == 8 && q.loopEnabled, "transport settings");
    CHECK (q.tracks.size() == 1 && q.tracks[0].name == "Keys" && q.tracks[0].instrument == InstrumentType::Piano, "track");
    CHECK (q.tracks[0].clips.size() == 1 && q.tracks[0].clips[0].notes.size() == 3, "clip notes");
    CHECK (q.tracks[0].clips[0].notes[2].pitch == 67 && q.tracks[0].clips[0].notes[2].lengthBeats == 2.0, "note values");
    CHECK (q.tracks[0].effects.size() == 1 && q.tracks[0].effects[0].bypass && q.tracks[0].effects[0].type == EffectType::Reverb, "effects");
    CHECK (q.tracks[0].automation["volume"].size() == 2 && q.tracks[0].slots.size() == 1, "automation+slots");
    CHECK (q.tracks[0].solo && q.tracks[0].group == 2 && std::abs (q.tracks[0].pan + 0.3f) < 1e-5f, "mixer state");
    CHECK (q.masterEffects.size() == 1, "master fx");
    Project bad; CHECK (! Project::loadFromFile (tmpDir().getChildFile ("nonexistent.noize"), bad, &err), "missing file rejected");
    auto junk = tmpDir().getChildFile ("junk.noize"); junk.replaceWithText ("not json {{{");
    CHECK (! Project::loadFromFile (junk, bad, &err), "corrupt file rejected");
}

static void testInstruments()
{
    std::puts ("[instruments produce audio]");
    const InstrumentType types[] = { InstrumentType::PolySynth, InstrumentType::SubSynth, InstrumentType::BassSynth, InstrumentType::DrumMachine, InstrumentType::Piano };
    for (auto ty : types)
    {
        Project p; auto& t = p.addTrack ("t", false, ty);
        t.instrumentParams = defaultParams (ty);
        Clip c = (ty == InstrumentType::DrumMachine) ? makeStepPatternClip ({ { true, false, false, false, true, false, false, false } }, { 36 }, 2, 4.0) : chord();
        t.clips.push_back (c);
        auto b = renderOffline (p, 2.0);
        const float r = rms (b);
        CHECK (r > 0.003f, (toString (ty) + " non-silent").toRawUTF8());
        CHECK (allFinite (b), (toString (ty) + " finite").toRawUTF8());
        CHECK (b.getMagnitude (0, b.getNumSamples()) < 4.0f, (toString (ty) + " sane level").toRawUTF8());
        std::printf ("  %-12s rms=%.4f peak=%.3f\n", toString (ty).toRawUTF8(), r, b.getMagnitude (0, b.getNumSamples()));
    }
}

static void testSampler()
{
    std::puts ("[sampler + audio clips]");
    auto wavFile = tmpDir().getChildFile ("tone.wav"); wavFile.deleteFile();
    {
        juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os (wavFile.createOutputStream());
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (1).withBitsPerSample (16));
        juce::AudioBuffer<float> b (1, 44100);
        for (int i = 0; i < 44100; ++i) b.setSample (0, i, 0.5f * std::sin (i * 0.0626f));
        w->writeFromAudioSampleBuffer (b, 0, 44100);
    }
    {   // Sampler instrument
        Project p; auto& t = p.addTrack ("smp", false, InstrumentType::Sampler); t.samplerFile = wavFile.getFullPathName();
        t.instrumentParams = defaultParams (InstrumentType::Sampler); t.clips.push_back (chord());
        auto b = renderOffline (p, 2.0); CHECK (rms (b) > 0.01f, "sampler produces audio");
    }
    {   // audio clip playback
        Project p; auto& t = p.addTrack ("aud", true); t.volume = 1.0f;
        Clip c; c.audioFile = wavFile.getFullPathName(); c.startBeat = 1.0; c.lengthBeats = 2.0; t.clips.push_back (c);   // 1 s at 120 bpm
        auto b = renderOffline (p, 2.5, 44100.0);
        const int sOn = 22050;   // beat 1 at 120bpm
        float before = 0, during = 0, after = 0;
        for (int i = 0; i < sOn - 100; ++i) before = std::max (before, std::abs (b.getSample (0, i)));
        for (int i = sOn + 1000; i < sOn + 40000; ++i) during = std::max (during, std::abs (b.getSample (0, i)));
        for (int i = sOn + 44100 + 500; i < b.getNumSamples(); ++i) after = std::max (after, std::abs (b.getSample (0, i)));
        CHECK (before < 1e-5f, "silent before clip start");
        CHECK (during > 0.3f, "audio plays inside clip");
        CHECK (after < 1e-5f, "silent after clip end");
        CHECK (Project().findMissingAssets().isEmpty(), "no missing assets in empty project");
        p.tracks[0].clips[0].audioFile = "/nonexistent/file.wav";
        CHECK (p.findMissingAssets().size() == 1, "missing asset detected");
        auto b2 = renderOffline (p, 1.0); CHECK (rms (b2) == 0.f, "missing audio file renders silence, no crash");
    }
}

static void testEffects()
{
    std::puts ("[effects process audio]");
    const EffectType types[] = { EffectType::EQ, EffectType::Compressor, EffectType::Limiter, EffectType::Reverb, EffectType::Delay, EffectType::Chorus,
                                 EffectType::Distortion, EffectType::LowPass, EffectType::HighPass, EffectType::StereoWidth, EffectType::Gain };
    for (auto ty : types)
    {
        auto fx = createEffect (ty);
        fx->prepare (44100.0, 512);
        auto params = defaultParams (ty);
        // make each effect clearly audible
        if (ty == EffectType::EQ) params["midGain"] = 12.f;
        if (ty == EffectType::Gain) params["gainDb"] = -12.f;
        if (ty == EffectType::StereoWidth) params["width"] = 0.0f;
        if (ty == EffectType::LowPass) params["cutoff"] = 300.f;
        if (ty == EffectType::HighPass) params["cutoff"] = 4000.f;
        if (ty == EffectType::Compressor) { params["threshold"] = -30.f; params["ratio"] = 8.f; }
        if (ty == EffectType::Limiter) params["ceiling"] = -12.f;
        fx->setParams (params);
        juce::AudioBuffer<float> in (2, 512), buf (2, 512);
        double diff = 0; bool fin = true; float lastOutPeak = 0;
        double phase = 0;
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.5f * std::sin ((float) phase); phase += 2 * 3.14159265 * 1000.0 / 44100.0; in.setSample (0, i, v); in.setSample (1, i, 0.7f * v); }
            buf.makeCopyOf (in);
            fx->process (buf);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { float o = buf.getSample (c, i); if (! std::isfinite (o)) fin = false; diff += std::abs (o - in.getSample (c, i)); }
            lastOutPeak = buf.getMagnitude (0, 512);
        }
        CHECK (fin, (toString (ty) + " finite").toRawUTF8());
        CHECK (diff > 1.0, (toString (ty) + " alters the signal").toRawUTF8());
        std::printf ("  %-12s diff=%.1f lastPeak=%.3f\n", toString (ty).toRawUTF8(), diff, lastOutPeak);
        if (ty == EffectType::Limiter) CHECK (lastOutPeak <= 0.26f, "limiter respects ceiling (-12 dB ~ 0.251)");
        if (ty == EffectType::Gain)    CHECK (std::abs (lastOutPeak - 0.5f * 0.2512f) < 0.02f, "gain -12 dB correct");
        if (ty == EffectType::LowPass) CHECK (lastOutPeak < 0.1f, "lowpass 300 Hz attenuates 1 kHz");
        if (ty == EffectType::HighPass) CHECK (lastOutPeak < 0.3f, "highpass 4 kHz attenuates 1 kHz");
    }
    // chain: insert / reorder / bypass / remove
    std::vector<EffectSlot> slots = { { EffectType::Gain, false, { { "gainDb", -6.f } } }, { EffectType::Gain, false, { { "gainDb", -6.f } } } };
    EffectChain chain; chain.prepare (44100.0, 512);
    auto run = [&] { juce::AudioBuffer<float> b (2, 512); for (int k = 0; k < 4; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, 1.f); chain.update (slots); chain.process (b); } return b.getSample (0, 511); };
    CHECK (std::abs (run() - 0.25f) < 0.02f, "two -6dB gains in chain = 0.25");
    slots[0].bypass = true;  CHECK (std::abs (run() - 0.5f) < 0.02f, "bypass one -> 0.5");
    slots.erase (slots.begin()); CHECK (std::abs (run() - 0.5f) < 0.02f, "remove first -> 0.5");
    slots.clear(); CHECK (std::abs (run() - 1.0f) < 0.001f, "empty chain passes audio");
}

static void testMixerBehaviour()
{
    std::puts ("[mixer: mute / solo / pan / volume]");
    auto mk = [] { Project p; for (int i = 0; i < 2; ++i) { auto& t = p.addTrack ("t" + juce::String (i), false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth); t.clips.push_back (chord()); } return p; };
    auto base = mk(); const float rBoth = rms (renderOffline (base, 1.5));
    auto m = mk(); m.tracks[0].mute = true; m.tracks[1].mute = true;
    CHECK (rms (renderOffline (m, 1.5)) < 1e-6f, "all muted = silence");
    auto m1 = mk(); m1.tracks[0].mute = true; const float rOne = rms (renderOffline (m1, 1.5));
    CHECK (rOne > 0.001f && rOne < rBoth * 1.05f, "one muted still audible");
    auto s = mk(); s.tracks[1].solo = true; s.tracks[0].mute = false;
    CHECK (std::abs (rms (renderOffline (s, 1.5)) - rOne) < rOne * 0.05f, "solo equals the other track alone");
    auto pl = mk(); pl.tracks[0].pan = -1.f; pl.tracks[1].mute = true;
    auto bl = renderOffline (pl, 1.5);
    float l = 0, r = 0; for (int i = 0; i < bl.getNumSamples(); ++i) { l = std::max (l, std::abs (bl.getSample (0, i))); r = std::max (r, std::abs (bl.getSample (1, i))); }
    CHECK (l > 0.01f && r < 1e-4f, "hard-left pan");
    auto lo = mk(); lo.tracks[0].volume = 0.4f; lo.tracks[1].volume = 0.4f;
    CHECK (rms (renderOffline (lo, 1.5)) < rBoth * 0.5f, "lower faders lower level");
    auto mv = mk(); mv.masterVolume = 0.0f; CHECK (rms (renderOffline (mv, 1.0)) < 1e-6f, "master volume 0 = silence");
    auto sd = mk(); sd.tracks[0].sendReverb = 1.f; sd.tracks[0].sendDelay = 1.f;
    auto bs = renderOffline (sd, 4.0); CHECK (allFinite (bs) && rms (bs) > 0.001f, "sends render");
    auto au = mk(); au.tracks[0].automation["volume"] = { { 0.0, 0.0f }, { 4.0, 1.0f } }; au.tracks[1].mute = true;
    auto ba = renderOffline (au, 2.0);   // 2s = 4 beats: volume rises 0 -> 1.25
    float early = 0, late = 0; for (int i = 0; i < 8000; ++i) early = std::max (early, std::abs (ba.getSample (0, i)));
    for (int i = 60000; i < 80000; ++i) late = std::max (late, std::abs (ba.getSample (0, i)));
    CHECK (late > early * 2.f, "volume automation ramps level up");
}

static void testTransport()
{
    std::puts ("[transport: loop / seek / metronome / tempo]");
    {
        Project p; p.loopEnabled = true; p.loopStartBeat = 0; p.loopEndBeat = 4;   // 2 s loop at 120
        auto& t = p.addTrack ("t", false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth);
        Clip c; c.lengthBeats = 4; c.notes = { { 72, 0.0, 0.25, 1.f } }; t.clips.push_back (c);
        AssetCache cache; Renderer r (cache); r.prepare (44100, 512); r.publish (p); r.playing = true;
        juce::AudioBuffer<float> out (2, 44100 * 5);
        for (int d = 0; d < out.getNumSamples(); d += 512) { int n = std::min (512, out.getNumSamples() - d); float* ch[2] = { out.getWritePointer (0) + d, out.getWritePointer (1) + d }; r.process (ch, 2, n, nullptr); }
        CHECK (r.position.load() < 4.0 + 1e-6, "position stays inside loop");
        auto burst = [&] (double s0) { float m = 0; for (int i = (int) (s0 * 44100) + 200; i < (int) (s0 * 44100) + 9000; ++i) m = std::max (m, std::abs (out.getSample (0, i))); return m; };
        CHECK (burst (0.0) > 0.01f && burst (2.0) > 0.01f && burst (4.0) > 0.01f, "note retriggers each loop pass (0s, 2s, 4s)");
        // the gap between notes must be silent after release
        float gap = 0; for (int i = (int) (1.5 * 44100); i < (int) (1.9 * 44100); ++i) gap = std::max (gap, std::abs (out.getSample (0, i)));
        CHECK (gap < burst (0.0) * 0.2f, "silence between loop passes");
    }
    {
        Project p; p.metronome = true; p.addTrack ("t", false);
        auto b = renderOffline (p, 2.0);   // 4 clicks at 120 bpm
        int clicks = 0; bool inClick = false; int quiet = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            if (std::abs (b.getSample (0, i)) > 0.02f) { if (! inClick) { ++clicks; inClick = true; } quiet = 0; }
            else if (++quiet > 3000) inClick = false;
        }
        CHECK (clicks == 4, ("metronome clicks == 4, got " + juce::String (clicks)).toRawUTF8());
    }
    {
        Project p; auto& t = p.addTrack ("t", false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth);
        Clip c; c.lengthBeats = 8; c.notes = { { 60, 4.0, 0.5, 1.f } }; t.clips.push_back (c);
        AssetCache cache; Renderer r (cache); r.prepare (44100, 512); r.publish (p); r.playing = true; r.seekRequest = 4.0; r.position = 4.0;
        juce::AudioBuffer<float> out (2, 2048); float* ch[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        r.process (ch, 2, 2048, nullptr);
        CHECK (rms (out) > 0.001f, "seeking to beat 4 triggers note at beat 4");
        CHECK (r.position.load() > 4.0, "playhead advances");
    }
    {
        auto mkp = [] (double bpm) { Project p; p.bpm = bpm; auto& t = p.addTrack ("t", false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth);
            Clip c; c.lengthBeats = 4; c.notes = { { 60, 2.0, 0.5, 1.f } }; t.clips.push_back (c); return p; };
        auto firstSound = [] (const juce::AudioBuffer<float>& b) { for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 0.001f) return i; return -1; };
        const int a = firstSound (renderOffline (mkp (120), 3.0)), b = firstSound (renderOffline (mkp (60), 3.0));
        CHECK (std::abs (a - 44100) < 600 && std::abs (b - 88200) < 600, ("tempo scales note timing: " + juce::String (a) + " / " + juce::String (b)).toRawUTF8());
    }
}

static void testSession()
{
    std::puts ("[session clip launching]");
    Project p; auto& t = p.addTrack ("t", false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth);
    Clip c; c.lengthBeats = 4; c.notes = { { 64, 0.0, 1.0, 1.f } }; t.slots.push_back (c);
    AssetCache cache; Renderer r (cache); r.prepare (44100, 512); r.publish (p);
    juce::AudioBuffer<float> out (2, 512); float* ch[2] = { out.getWritePointer (0), out.getWritePointer (1) };
    r.process (ch, 2, 512, nullptr); CHECK (rms (out) == 0.f, "idle session is silent");
    r.launchSlot (0, 0);
    CHECK (r.playing.load(), "launching starts transport");
    float peak = 0; for (int k = 0; k < 40; ++k) { r.process (ch, 2, 512, nullptr); peak = std::max (peak, out.getMagnitude (0, 512)); }
    CHECK (peak > 0.01f, "launched slot is audible");
    CHECK (r.getActiveSlot (0) == 0, "active slot mirrored for UI");
    r.stopTrackSlot (0);
    for (int k = 0; k < 200; ++k) r.process (ch, 2, 512, nullptr);   // pass bar boundary (2 s = 172 blocks)
    CHECK (r.getActiveSlot (0) == -1, "stop quantised at next bar");
}

static void testExport()
{
    std::puts ("[export / offline render]");
    Project p; p.bpm = 120;
    auto& t = p.addTrack ("k", false, InstrumentType::PolySynth); t.instrumentParams = defaultParams (InstrumentType::PolySynth); t.clips.push_back (chord());
    t.effects.push_back ({ EffectType::Reverb, false, defaultParams (EffectType::Reverb) });
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    struct Case { int bits; bool mono; double sr; };
    for (auto cs : { Case { 16, false, 44100 }, Case { 24, false, 48000 }, Case { 32, false, 96000 }, Case { 24, true, 44100 }, Case { 16, false, 88200 } })
    {
        ExportOptions o; o.file = tmpDir().getChildFile ("exp.wav"); o.bitDepth = cs.bits; o.mono = cs.mono; o.sampleRate = cs.sr; o.tailSeconds = 1.0;
        auto res = renderProjectToWav (p, o);
        juce::String tag = juce::String (cs.bits) + "-bit " + (cs.mono ? "mono " : "stereo ") + juce::String (cs.sr);
        CHECK (res.ok, (tag + " export ok: " + res.message).toRawUTF8());
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (o.file));
        CHECK (rd != nullptr, (tag + " readable").toRawUTF8());
        if (! rd) continue;
        CHECK (rd->numChannels == (cs.mono ? 1u : 2u), (tag + " channels").toRawUTF8());
        CHECK (std::abs (rd->sampleRate - cs.sr) < 1, (tag + " sample rate").toRawUTF8());
        CHECK (rd->bitsPerSample == (unsigned) cs.bits, (tag + " bit depth").toRawUTF8());
        CHECK (rd->usesFloatingPointData == (cs.bits == 32), (tag + " float flag").toRawUTF8());
        const double expect = 4.0 * 0.5 + 1.0;   // 4 beats @120 + 1 s tail
        CHECK (std::abs ((double) rd->lengthInSamples / cs.sr - expect) < 0.05, (tag + " duration").toRawUTF8());
        juce::AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples); rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true);
        CHECK (rms (b) > 0.003f, (tag + " audible").toRawUTF8());
        std::printf ("  %s -> %.2fs rms=%.4f\n", tag.toRawUTF8(), (double) rd->lengthInSamples / cs.sr, rms (b));
    }
    ExportOptions o; o.file = tmpDir().getChildFile ("norm.wav"); o.normalize = true; o.tailSeconds = 0.5;
    auto res = renderProjectToWav (p, o); CHECK (res.ok && std::abs (res.peak - 0.891f) < 0.01f, "normalize to -1 dBFS");
    ExportOptions bad; bad.file = tmpDir().getChildFile ("no/such/dir/x.wav");
    Project empty; CHECK (! renderProjectToWav (empty, [&] { auto e = bad; e.endBeat = 0.0; return e; }()).ok || true, "bad paths don't crash");
    ExportOptions badBits = o; badBits.bitDepth = 12; CHECK (! renderProjectToWav (p, badBits).ok, "invalid bit depth rejected");
    int calls = 0; ExportOptions oc = o; oc.file = tmpDir().getChildFile ("cancel.wav");
    auto rc = renderProjectToWav (p, oc, [&] (float) { return ++calls < 3; }); CHECK (! rc.ok && ! oc.file.existsAsFile(), "cancel removes file");
}

static void testEngineProject()
{
    std::puts ("[engine: undo/redo, save/open, autosave, recording]");
    Engine e;
    CHECK (e.project().tracks.size() == 3, "new project has default tracks");
    e.checkpoint(); e.project().addTrack ("Extra", false); e.commit();
    CHECK (e.project().tracks.size() == 4 && e.canUndo(), "edit + undo available");
    CHECK (e.undo() && e.project().tracks.size() == 3, "undo");
    CHECK (e.redo() && e.project().tracks.size() == 4, "redo");
    e.checkpoint(); e.project().tracks[0].clips.push_back (chord()); e.setTempo (140);
    auto f = tmpDir().getChildFile ("engine.noize");
    CHECK (e.saveProject (f) && ! e.isDirty(), "save");
    Engine e2; CHECK (e2.openProject (f) && e2.project().bpm == 140 && e2.project().tracks.size() == 4 && e2.project().tracks[0].clips.size() == 1, "reopen preserves project");
    CHECK (e2.autosave() && Engine::hasRecovery(), "autosave writes recovery file");
    Project rec; CHECK (Project::loadFromFile (Engine::recoveryFile(), rec), "recovery file loads");
    e2.clearRecovery(); CHECK (! Engine::hasRecovery(), "recovery cleared");
    // MIDI recording via the real-time path
    Engine e3; auto& r = e3.getRenderer(); r.prepare (44100, 512); e3.commit();
    int id = e3.project().tracks[0].id; juce::String err;
    CHECK (e3.startRecording (id, &err), ("midi record start: " + err).toRawUTF8());
    juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
    juce::AudioBuffer<float> out (2, 512); float* ch[2] = { out.getWritePointer (0), out.getWritePointer (1) };
    r.process (ch, 2, 512, &mb);
    for (int i = 0; i < 100; ++i) r.process (ch, 2, 512, nullptr);
    juce::MidiBuffer mb2; mb2.addEvent (juce::MidiMessage::noteOff (1, 60), 0); r.process (ch, 2, 512, &mb2);
    e3.stopRecording();
    CHECK (e3.project().tracks[0].clips.size() == 1 && e3.project().tracks[0].clips[0].notes.size() == 1, "MIDI note recorded into a clip");
    if (! e3.project().tracks[0].clips.empty() && ! e3.project().tracks[0].clips[0].notes.empty())
        CHECK (e3.project().tracks[0].clips[0].notes[0].pitch == 60 && e3.project().tracks[0].clips[0].notes[0].lengthBeats > 0.3, "recorded note pitch/length");
    CHECK (! e3.startRecording (id + 999, &err), "recording on missing track rejected");
    Engine e4; juce::String err2; CHECK (! e4.startRecording (e4.project().addTrack ("Audio", true).id, &err2), "audio recording without device is rejected cleanly");
}

static void testStress()
{
    std::puts ("[stress: many tracks, rapid edits while rendering]");
    Project p;
    for (int i = 0; i < 24; ++i)
    {
        auto ty = (InstrumentType) (i % 3);
        auto& t = p.addTrack ("t" + juce::String (i), false, ty); t.instrumentParams = defaultParams (ty); t.clips.push_back (chord());
        t.effects.push_back ({ EffectType::Chorus, false, defaultParams (EffectType::Chorus) });
        t.effects.push_back ({ EffectType::Compressor, false, defaultParams (EffectType::Compressor) });
    }
    AssetCache cache; Renderer r (cache); r.prepare (44100, 256); r.publish (p); r.playing = true;
    juce::AudioBuffer<float> out (2, 256); float* ch[2] = { out.getWritePointer (0), out.getWritePointer (1) };
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    bool fin = true;
    for (int k = 0; k < 800; ++k)
    {
        if (k % 50 == 0) { p.tracks[(size_t) (k / 50) % 24].volume = 0.3f + 0.02f * (float) (k / 50); p.tracks[1].effects.push_back ({ EffectType::Gain, false, defaultParams (EffectType::Gain) }); r.publish (p); }
        r.process (ch, 2, 256, nullptr); fin = fin && allFinite (out);
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0, audioMs = 800.0 * 256 / 44.1;
    std::printf ("  24 tracks x2 fx, 16 live republishes: %.0f ms CPU for %.0f ms audio (%.1f%% of real time)\n", ms, audioMs, 100.0 * ms / audioMs);
    CHECK (fin, "all output finite under edits");
    CHECK (ms < audioMs, "faster than real time with 24 tracks (this machine)");
}

static void testDemoProject()
{
    std::puts ("[demo project]");
    auto p = makeDemoProject();
    CHECK (p.tracks.size() == 4, "demo has 4 tracks");
    for (auto& t : p.tracks) CHECK (! t.clips.empty() && ! t.clips[0].notes.empty(), "every demo track has a clip with notes");
    auto f = tmpDir().getChildFile ("demo_rt.noize");
    CHECK (p.saveToFile (f), "demo saves");
    Project q; juce::String err; CHECK (Project::loadFromFile (f, q, &err), "demo reloads");
    CHECK (q.tracks.size() == 4 && q.bpm == p.bpm, "demo round-trips");
    auto b = renderOffline (q, 6.0);
    CHECK (allFinite (b), "demo output finite");
    CHECK (rms (b) > 0.02f, "demo renders audible audio");
    std::printf ("  demo rms=%.4f\n", rms (b));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::puts ("NOIZE engine tests\n");
    testProjectRoundTrip(); testInstruments(); testSampler(); testEffects(); testMixerBehaviour();
    testTransport(); testSession(); testExport(); testEngineProject(); testDemoProject(); testStress();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
