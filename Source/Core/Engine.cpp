#include "Engine.h"
#include <cmath>
#include <algorithm>

namespace noize
{
//==============================================================================
AssetCache::AssetCache() { formats.registerBasicFormats(); }

std::shared_ptr<const SampleData> AssetCache::get (const juce::String& path)
{
    if (path.isEmpty()) return nullptr;
    std::lock_guard<std::mutex> lg (mutex);
    auto it = cache.find (path);
    if (it != cache.end()) return it->second;

    juce::File f (path);
    if (! f.existsAsFile()) return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > 400000000LL) return nullptr;

    auto sd = std::make_shared<SampleData>();
    const int ch = juce::jlimit (1, 2, (int) reader->numChannels);
    sd->buffer.setSize (ch, (int) reader->lengthInSamples);
    sd->sampleRate = reader->sampleRate;
    reader->read (&sd->buffer, 0, (int) reader->lengthInSamples, 0, true, ch > 1);
    cache[path] = sd;
    return sd;
}

void AssetCache::clear() { std::lock_guard<std::mutex> lg (mutex); cache.clear(); }

//==============================================================================
Renderer::Renderer (AssetCache& c) : cache (c)
{
    for (auto& a : pendingSlot) a = -2;
    for (auto& a : activeMirror) a = -1;
}

void Renderer::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = juce::jmax (16, maxBlockSize);
    master.setSize (2, maxBlock); reverbBus.setSize (2, maxBlock); delayBus.setSize (2, maxBlock);
    delayDry.setSize (2, maxBlock); clickBuf.setSize (1, maxBlock);
    busReverb.setSampleRate (sr);
    juce::Reverb::Parameters rp; rp.roomSize = 0.75f; rp.damping = 0.5f; rp.wetLevel = 1.f; rp.dryLevel = 0.f; rp.width = 1.f;
    busReverb.setParameters (rp);
    busDelay = createEffect (EffectType::Delay);
    busDelay->prepare (sr, maxBlock);
    masterFx.prepare (sr, maxBlock);
    events.reserve (8192); liveEvents.reserve (512); clickEvents.reserve (64);
    std::lock_guard<std::mutex> lg (publishMutex);
    runtimes.clear();
}

void Renderer::publish (const Project& p)
{
    std::lock_guard<std::mutex> lg (publishMutex);
    auto snap = std::make_shared<Snapshot>();
    snap->project = p;
    snap->version = ++versionCounter;

    std::map<int, std::shared_ptr<TrackRuntime>> newRuntimes;
    for (auto& t : snap->project.tracks)
    {
        std::shared_ptr<TrackRuntime> rt;
        auto it = runtimes.find (t.id);
        const bool needNew = (it == runtimes.end()) || (it->second->instType != t.instrument)
                             || ((it->second->inst == nullptr) != t.audioTrack);
        if (! needNew) rt = it->second;
        else
        {
            rt = std::make_shared<TrackRuntime>();
            rt->instType = t.instrument;
            if (! t.audioTrack) { rt->inst = createInstrument (t.instrument); rt->inst->prepare (sr, maxBlock); }
            rt->fx.prepare (sr, maxBlock);
            rt->buf.setSize (2, maxBlock);
        }
        newRuntimes[t.id] = rt;
        snap->rt.push_back (rt);
        snap->samplers.push_back (t.instrument == InstrumentType::Sampler ? cache.get (t.samplerFile) : nullptr);

        std::vector<std::shared_ptr<const SampleData>> cd, sd;
        for (auto& c : t.clips) cd.push_back (c.isAudio() ? cache.get (c.audioFile) : nullptr);
        for (auto& c : t.slots) sd.push_back (c.isAudio() ? cache.get (c.audioFile) : nullptr);
        snap->clipData.push_back (std::move (cd));
        snap->slotData.push_back (std::move (sd));
        snap->anySolo = snap->anySolo || t.solo;
    }
    runtimes = std::move (newRuntimes);

    history.push_back (snap);
    while (history.size() > 8) history.pop_front();
    {
        const juce::SpinLock::ScopedLockType sl (handoff);
        pending = snap.get();
    }
    latestUi = snap;
}

//==============================================================================
void Renderer::launchSlot (int ti, int slot)
{
    if (ti < 0 || ti >= kMaxTracks) return;
    if (! playing.load())
    {
        const double bar = latestBarBeats();
        position.store (std::floor (position.load() / bar) * bar);
        playing.store (true);
    }
    pendingSlot[(size_t) ti] = slot;
}
void Renderer::stopTrackSlot (int ti) { if (ti >= 0 && ti < kMaxTracks) pendingSlot[(size_t) ti] = -1; }
void Renderer::launchScene (int slot)
{
    std::shared_ptr<Snapshot> s; { std::lock_guard<std::mutex> lg (publishMutex); s = latestUi; }
    if (s == nullptr) return;
    for (size_t i = 0; i < s->project.tracks.size() && i < (size_t) kMaxTracks; ++i)
    {
        if (slot >= 0 && slot < (int) s->project.tracks[i].slots.size() && ! s->project.tracks[i].slots[(size_t) slot].notes.empty())
            launchSlot ((int) i, slot);
        else if (slot >= 0 && slot < (int) s->project.tracks[i].slots.size() && s->project.tracks[i].slots[(size_t) slot].isAudio())
            launchSlot ((int) i, slot);
        else
            stopTrackSlot ((int) i);
    }
}
int Renderer::getActiveSlot (int ti) const { return (ti >= 0 && ti < kMaxTracks) ? activeMirror[(size_t) ti].load() : -1; }
int Renderer::getQueuedSlot (int ti) const { return (ti >= 0 && ti < kMaxTracks) ? pendingSlot[(size_t) ti].load() : -2; }

double Renderer::latestBarBeats()
{
    std::lock_guard<std::mutex> lg (publishMutex);
    if (latestUi == nullptr) return 4.0;
    return latestUi->project.timeSigNum * 4.0 / juce::jmax (1, latestUi->project.timeSigDen);
}

float Renderer::takeTrackPeak (int ti, int ch)
{
    std::shared_ptr<Snapshot> s; { std::lock_guard<std::mutex> lg (publishMutex); s = latestUi; }
    if (s == nullptr || ti < 0 || ti >= (int) s->rt.size()) return 0.f;
    return (ch == 0 ? s->rt[(size_t) ti]->peakL : s->rt[(size_t) ti]->peakR).exchange (0.f);
}
float Renderer::takeMasterPeak (int ch) { return masterPeak[ch & 1].exchange (0.f); }

void Renderer::copyLatestSamples (float* dest, int count) const
{
    count = juce::jmin (count, kVisSize);
    int w = visWrite.load();
    for (int i = 0; i < count; ++i)
        dest[i] = visBuf[(size_t) ((w - count + i + kVisSize * 2) % kVisSize)];
}

bool Renderer::popMidiRecEvent (MidiRecEvent& e)
{
    int s1, z1, s2, z2;
    midiFifo.prepareToRead (1, s1, z1, s2, z2);
    if (z1 > 0) { e = midiFifoData[(size_t) s1]; midiFifo.finishedRead (1); return true; }
    return false;
}

float Renderer::evalAutomation (const std::vector<std::pair<double, float>>& pts, double beat)
{
    if (pts.empty()) return -1.f;
    if (beat <= pts.front().first) return pts.front().second;
    if (beat >= pts.back().first) return pts.back().second;
    for (size_t i = 1; i < pts.size(); ++i)
        if (beat < pts[i].first)
        {
            const double span = pts[i].first - pts[i - 1].first;
            const double f = span > 0 ? (beat - pts[i - 1].first) / span : 0.0;
            return (float) (pts[i - 1].second + f * (pts[i].second - pts[i - 1].second));
        }
    return pts.back().second;
}

//==============================================================================
void Renderer::stopAllNotes (Snapshot& s)
{
    for (auto& rt : s.rt) if (rt->inst) rt->inst->allNotesOff();
}

void Renderer::scanMidiClip (const Clip& c, double offset, double clipLen, bool loop, double b0, double b1, int num)
{
    if (clipLen <= 0.0 || b1 <= b0) return;
    const double l0 = std::max (0.0, b0 - offset), l1 = b1 - offset;
    if (l1 <= 0.0 || l1 <= l0) return;
    const double spb = (double) num / (b1 - b0);           // samples per beat in this segment
    auto toSample = [&] (double localBeat) { return juce::jlimit (0, juce::jmax (0, num - 1), (int) ((localBeat + offset - b0) * spb)); };

    const int k0 = loop ? (int) std::floor (l0 / clipLen) : 0;
    const int k1 = loop ? (int) std::floor (l1 / clipLen) : 0;
    for (int k = k0; k <= k1; ++k)
    {
        const double base = k * clipLen;
        for (auto& n : c.notes)
        {
            if (n.startBeat >= clipLen || n.startBeat < 0.0) continue;
            const double on = base + n.startBeat;
            const double off = base + std::min (n.startBeat + std::max (0.01, n.lengthBeats), clipLen);
            if (on >= l0 && on < l1)  events.push_back ({ toSample (on), true, n.pitch, n.velocity });
            if (off >= l0 && off < l1) events.push_back ({ toSample (off), false, n.pitch, 0.f });
        }
    }
}

void Renderer::renderAudioClip (const SampleData& sd, TrackRuntime& rt, const Clip& c, double clipStart, bool loop,
                                double b0, double b1, int num)
{
    if (num <= 0 || b1 <= b0) return;
    const double bps = curBps;
    const double beatPerSample = (b1 - b0) / num;
    const double lenSecs = c.lengthBeats / bps;
    const double startSecs = c.audioStartBeat / bps;
    const int nch = sd.buffer.getNumChannels(), len = sd.buffer.getNumSamples();
    const float* L = sd.buffer.getReadPointer (0);
    const float* R = sd.buffer.getReadPointer (nch > 1 ? 1 : 0);
    float* oL = rt.buf.getWritePointer (0); float* oR = rt.buf.getWritePointer (1);
    const double ratio = sd.sampleRate;

    for (int i = 0; i < num; ++i)
    {
        double t = (b0 + i * beatPerSample - clipStart) / bps;
        if (t < 0.0) continue;
        if (loop) t = std::fmod (t, lenSecs); else if (t >= lenSecs) break;
        const double pos = (t + startSecs) * ratio;
        const int i0 = (int) pos;
        if (i0 < 0 || i0 >= len - 1) continue;
        const float f = (float) (pos - i0);
        oL[i] += L[i0] + f * (L[i0 + 1] - L[i0]);
        oR[i] += R[i0] + f * (R[i0 + 1] - R[i0]);
    }
}

void Renderer::renderSegment (Snapshot& s, int off, int num, double b0, double b1, bool isPlaying)
{
    if (num <= 0) return;
    const auto& proj = s.project;
    const double beatsPerBar = proj.timeSigNum * 4.0 / juce::jmax (1, proj.timeSigDen);

    // ---- metronome events
    clickEvents.clear();
    if (isPlaying && proj.metronome && b1 > b0)
    {
        const double spb = num / (b1 - b0);
        for (double b = std::ceil (b0 - 1e-9); b < b1 - 1e-9; b += 1.0)
        {
            const int smp = juce::jlimit (0, num - 1, (int) ((b - b0) * spb));
            const bool accent = std::fmod (std::round (b), beatsPerBar) < 0.5;
            clickEvents.push_back ({ smp, accent });
        }
    }

    // ---- tracks
    for (size_t ti = 0; ti < proj.tracks.size(); ++ti)
    {
        const auto& t = proj.tracks[ti];
        auto& rt = *s.rt[ti];
        if (rt.appliedVersion != s.version)
        {
            if (rt.inst) { rt.inst->setParams (t.instrumentParams); rt.inst->setSample (s.samplers[ti]); }
            rt.fx.update (t.effects);
            rt.appliedVersion = s.version;
        }
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::clear (rt.buf.getWritePointer (ch), num);

        events.clear();
        bool audioDone = false;
        if (isPlaying)
        {
            const int active = ti < (size_t) kMaxTracks ? rt.activeSlot : -1;
            if (active >= 0 && active < (int) t.slots.size())
            {
                const Clip& c = t.slots[(size_t) active];
                if (c.isAudio()) { if (auto sd = s.slotData[ti][(size_t) active]) renderAudioClip (*sd, rt, c, rt.activeStart, true, b0, b1, num); }
                else scanMidiClip (c, rt.activeStart, c.lengthBeats, true, b0, b1, num);
                audioDone = true;
            }
            if (! audioDone)
                for (size_t ci = 0; ci < t.clips.size(); ++ci)
                {
                    const Clip& c = t.clips[ci];
                    if (b1 <= c.startBeat || b0 >= c.startBeat + c.lengthBeats) continue;
                    if (c.isAudio()) { if (auto sd = s.clipData[ti][ci]) renderAudioClip (*sd, rt, c, c.startBeat, false, b0, b1, num); }
                    else scanMidiClip (c, c.startBeat, c.lengthBeats, false, b0, b1, num);
                }
        }
        // live MIDI
        if (t.id == liveTrackId.load())
            for (auto& e : liveEvents)
                if (e.sample >= off && e.sample < off + num) events.push_back ({ e.sample - off, e.on, e.note, e.vel });

        if (rt.inst)
        {
            std::sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b)
                       { return a.sample != b.sample ? a.sample < b.sample : (a.on < b.on); });   // offs first
            int cursor = 0;
            for (auto& e : events)
            {
                if (e.sample > cursor) { rt.inst->render (rt.buf, cursor, e.sample - cursor); cursor = e.sample; }
                if (e.on) rt.inst->noteOn (e.note, e.vel); else rt.inst->noteOff (e.note);
            }
            if (cursor < num) rt.inst->render (rt.buf, cursor, num - cursor);
        }

        juce::AudioBuffer<float> view (rt.buf.getArrayOfWritePointers(), 2, num);
        rt.fx.process (view);

        // ---- fader / pan / automation
        float vol = t.volume, pan = t.pan, sRev = t.sendReverb, sDel = t.sendDelay;
        const double beatMid = 0.5 * (b0 + b1);
        auto autoV = [&] (const char* id) { auto it = t.automation.find (id); return it == t.automation.end() ? -1.f : evalAutomation (it->second, beatMid); };
        if (isPlaying)
        {
            float a;
            if ((a = autoV ("volume")) >= 0.f) vol = a * 1.25f;
            if ((a = autoV ("pan")) >= 0.f) pan = a * 2.f - 1.f;
            if ((a = autoV ("sendReverb")) >= 0.f) sRev = a;
            if ((a = autoV ("sendDelay")) >= 0.f) sDel = a;
        }
        const bool silent = t.mute || (s.anySolo && ! t.solo);
        const float g = silent ? 0.f : vol * vol;
        const float ang = (juce::jlimit (-1.f, 1.f, pan) + 1.f) * juce::MathConstants<float>::pi * 0.25f;
        const float tgt[2] = { g * std::cos (ang) * 1.41421356f, g * std::sin (ang) * 1.41421356f };
        if (! rt.gainInit) { rt.lastGain[0] = tgt[0]; rt.lastGain[1] = tgt[1]; rt.gainInit = true; }

        float pkL = 0.f, pkR = 0.f;
        for (int ch = 0; ch < 2; ++ch)
        {
            float* in = rt.buf.getWritePointer (ch);
            float* m = master.getWritePointer (ch) + off;
            float* rb = reverbBus.getWritePointer (ch) + off;
            float* db = delayBus.getWritePointer (ch) + off;
            float gain = rt.lastGain[ch];
            const float step = (tgt[ch] - gain) / (float) num;
            float pk = 0.f;
            for (int i = 0; i < num; ++i)
            {
                gain += step;
                float x = in[i];
                if (! std::isfinite (x)) x = 0.f;
                const float y = x * gain;
                m[i] += y;
                if (sRev > 0.f) rb[i] += y * sRev;
                if (sDel > 0.f) db[i] += y * sDel;
                pk = std::max (pk, std::abs (y));
            }
            rt.lastGain[ch] = tgt[ch];
            (ch == 0 ? pkL : pkR) = pk;
        }
        float cur = rt.peakL.load(); if (pkL > cur) rt.peakL.store (pkL);
        cur = rt.peakR.load();       if (pkR > cur) rt.peakR.store (pkR);
    }

    // ---- metronome click
    if (! clickEvents.empty() || clickRemain > 0)
    {
        float* m0 = master.getWritePointer (0) + off; float* m1 = master.getWritePointer (1) + off;
        size_t ei = 0;
        for (int i = 0; i < num; ++i)
        {
            while (ei < clickEvents.size() && clickEvents[ei].sample == i)
            {
                clickTotal = clickRemain = (int) (sr * 0.04); clickInc = (clickEvents[ei].accent ? 1600.0 : 1000.0) / sr;
                clickPhase = 0.0; clickAmp = clickEvents[ei].accent ? 0.5f : 0.35f; ++ei;
            }
            if (clickRemain > 0)
            {
                const float env = (float) clickRemain / (float) clickTotal;
                const float v = std::sin ((float) (clickPhase * juce::MathConstants<double>::twoPi)) * env * env * clickAmp;
                clickPhase += clickInc; --clickRemain;
                m0[i] += v; m1[i] += v;
            }
        }
    }
}

//==============================================================================
void Renderer::process (float* const* out, int nOut, int numSamples, const juce::MidiBuffer* live)
{
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (maxBlock, numSamples - done);
        float* chans[2] = { nullptr, nullptr };
        for (int c = 0; c < 2; ++c) chans[c] = out[juce::jmin (c, nOut - 1)] + done;
        processChunk (chans, juce::jmin (2, nOut), n, live);
        done += n;
        live = nullptr;   // live MIDI only applies to the first chunk of the callback
    }
}

void Renderer::processChunk (float* const* out, int nOut, int n, const juce::MidiBuffer* live)
{
    // ---- pick up a freshly published snapshot (never blocks)
    {
        const juce::SpinLock::ScopedTryLockType tl (handoff);
        if (tl.isLocked() && pending != nullptr) { current = pending; pending = nullptr; }
    }
    if (current == nullptr)
    {
        for (int c = 0; c < nOut; ++c) juce::FloatVectorOperations::clear (out[c], n);
        return;
    }
    Snapshot& s = *current;
    const auto& proj = s.project;
    curBps = proj.bpm / 60.0;

    if (masterFxVersion != s.version)
    {
        masterFx.update (proj.masterEffects);
        ParamMap dp = defaultParams (EffectType::Delay);
        dp["timeMs"] = (float) (60000.0 / proj.bpm * 0.75); dp["feedback"] = 0.4f; dp["wet"] = 1.f;
        busDelay->setParams (dp);
        masterFxVersion = s.version;
    }

    const double seek = seekRequest.exchange (-1.0);
    if (seek >= 0.0) { position.store (seek); stopAllNotes (s); }

    const bool isPlaying = playing.load();
    if (wasPlaying && ! isPlaying) { stopAllNotes (s); for (auto& rt : s.rt) rt->activeSlot = -1; for (auto& a : activeMirror) a = -1; }
    wasPlaying = isPlaying;

    for (int c = 0; c < 2; ++c)
    {
        master.clear (c, 0, n); reverbBus.clear (c, 0, n); delayBus.clear (c, 0, n);
    }

    // ---- live MIDI -> events
    liveEvents.clear();
    const double beatsPerSample = curBps / sr;
    if (live != nullptr)
        for (const auto meta : *live)
        {
            const auto m = meta.getMessage();
            const int smp = juce::jlimit (0, n - 1, meta.samplePosition);
            if (m.isNoteOn()) liveEvents.push_back ({ smp, true, m.getNoteNumber(), m.getFloatVelocity() });
            else if (m.isNoteOff()) liveEvents.push_back ({ smp, false, m.getNoteNumber(), 0.f });
            else continue;
            if (recordMidi.load() && isPlaying)
            {
                int s1, z1, s2, z2; midiFifo.prepareToWrite (1, s1, z1, s2, z2);
                if (z1 > 0) { midiFifoData[(size_t) s1] = { m.isNoteOn(), m.getNoteNumber(), m.getFloatVelocity(), position.load() + smp * beatsPerSample }; midiFifo.finishedWrite (1); }
            }
        }

    // ---- session launches (quantised to bar)
    const double bar = proj.timeSigNum * 4.0 / juce::jmax (1, proj.timeSigDen);
    if (isPlaying)
    {
        const double b0 = position.load(), b1 = b0 + n * beatsPerSample;
        for (size_t i = 0; i < s.rt.size() && i < (size_t) kMaxTracks; ++i)
        {
            const int p = pendingSlot[i].load();
            if (p == -2) continue;
            const double nextB = std::ceil (b0 / bar - 1e-9) * bar;
            if (nextB < b1 - 1e-12 || std::abs (nextB - b0) < 1e-9)
            {
                s.rt[i]->activeSlot = p; s.rt[i]->activeStart = nextB;
                if (s.rt[i]->inst) s.rt[i]->inst->allNotesOff();
                activeMirror[i] = p;
                pendingSlot[i] = -2;
            }
        }
    }

    // ---- render (split at loop end)
    int off = 0;
    double b0 = position.load();
    int guard = 0;
    while (off < n && guard++ < 4)
    {
        int num = n - off;
        double b1 = b0 + num * beatsPerSample;
        bool wrap = false;
        if (isPlaying && proj.loopEnabled && proj.loopEndBeat > proj.loopStartBeat && b0 < proj.loopEndBeat && b1 >= proj.loopEndBeat)
        {
            num = juce::jlimit (1, num, (int) std::ceil ((proj.loopEndBeat - b0) / beatsPerSample));
            b1 = proj.loopEndBeat; wrap = true;
        }
        renderSegment (s, off, num, b0, isPlaying ? b1 : b0, isPlaying);
        off += num;
        if (isPlaying)
        {
            b0 = wrap ? proj.loopStartBeat : b1;
            if (wrap) { stopAllNotes (s); }
        }
    }
    if (isPlaying) position.store (b0);

    // ---- send returns
    {
        float* rl = reverbBus.getWritePointer (0); float* rr = reverbBus.getWritePointer (1);
        busReverb.processStereo (rl, rr, n);
        for (int c = 0; c < 2; ++c) master.addFrom (c, 0, reverbBus, c, 0, n);

        for (int c = 0; c < 2; ++c) delayDry.copyFrom (c, 0, delayBus, c, 0, n);
        juce::AudioBuffer<float> dv (delayBus.getArrayOfWritePointers(), 2, n);
        busDelay->process (dv);
        for (int c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::subtract (delayBus.getWritePointer (c), delayDry.getReadPointer (c), n);   // keep only the wet part
            master.addFrom (c, 0, delayBus, c, 0, n);
        }
    }

    // ---- master chain, volume, meters, output
    juce::AudioBuffer<float> mv (master.getArrayOfWritePointers(), 2, n);
    masterFx.process (mv);

    const float mg = proj.masterVolume * proj.masterVolume;
    if (! masterGainInit) { masterGainLast = mg; masterGainInit = true; }
    const float mstep = (mg - masterGainLast) / (float) n;
    float pk[2] = { 0.f, 0.f };
    int w = visWrite.load();
    for (int c = 0; c < 2; ++c)
    {
        float* m = master.getWritePointer (c);
        float gain = masterGainLast;
        for (int i = 0; i < n; ++i)
        {
            gain += mstep;
            float v = m[i] * gain;
            if (! std::isfinite (v)) v = 0.f;
            v = juce::jlimit (-8.f, 8.f, v);
            m[i] = v;
            pk[c] = std::max (pk[c], std::abs (v));
        }
    }
    masterGainLast = mg;
    for (int i = 0; i < n; ++i) visBuf[(size_t) ((w + i) % kVisSize)] = 0.5f * (master.getReadPointer (0)[i] + master.getReadPointer (1)[i]);
    visWrite.store ((w + n) % kVisSize);
    for (int c = 0; c < 2; ++c)
    {
        const float cur = masterPeak[c].load(); if (pk[c] > cur) masterPeak[c].store (pk[c]);
    }
    for (int c = 0; c < nOut; ++c)
        juce::FloatVectorOperations::copy (out[c], master.getReadPointer (juce::jmin (c, 1)), n);
}

//==============================================================================
// Engine
//==============================================================================
Engine::Engine() : renderer (assets) { renderer.prepare (44100.0, 512); newProject(); }

Engine::~Engine() { shutdownAudio(); }

juce::File Engine::appDataDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("NOIZE");
    d.createDirectory();
    return d;
}
juce::File Engine::recoveryFile() { return appDataDir().getChildFile ("recovery.noize"); }
bool Engine::hasRecovery() { return recoveryFile().existsAsFile(); }
void Engine::clearRecovery() { recoveryFile().deleteFile(); }
bool Engine::autosave() { return proj.saveToFile (recoveryFile()); }

juce::File Engine::defaultRecordingsDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NOIZE").getChildFile ("Recordings");
    if (! d.createDirectory()) return appDataDir().getChildFile ("Recordings");
    return d;
}

juce::String Engine::initialiseAudio()
{
    auto err = devices.initialiseWithDefaultDevices (2, 2);
    if (err.isNotEmpty())
    {
        err = devices.initialiseWithDefaultDevices (0, 2);   // no input device available
        if (err.isNotEmpty()) return err;
    }
    devices.addAudioCallback (this);
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        devices.setMidiInputDeviceEnabled (d.identifier, true);
        devices.addMidiInputDeviceCallback (d.identifier, this);
    }
    audioStarted = true;
    return {};
}

void Engine::shutdownAudio()
{
    if (! audioStarted) return;
    stopRecording();
    devices.removeAudioCallback (this);
    for (auto& d : juce::MidiInput::getAvailableDevices()) devices.removeMidiInputDeviceCallback (d.identifier, this);
    devices.closeAudioDevice();
    audioStarted = false;
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    renderer.prepare (d->getCurrentSampleRate(), juce::jmax (512, d->getCurrentBufferSizeSamples()));
    midiCollector.reset (d->getCurrentSampleRate());
    renderer.publish (proj);
}
void Engine::audioDeviceStopped() {}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) { midiCollector.addMessageToQueue (m); }

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals nd;
    midiScratch.clear();
    midiCollector.removeNextBlockOfMessages (midiScratch, n);

    // record input (non-blocking FIFO writer)
    if (recordingAudio && numIn > 0)
    {
        const juce::SpinLock::ScopedTryLockType tl (writerLock);
        if (tl.isLocked() && threadedWriter != nullptr)
        {
            const float* ch[2] = { in[0], in[numIn > 1 ? 1 : 0] };
            if (threadedWriter->write (ch, n)) recSamples += n;
        }
    }
    renderer.process (out, numOut, n, &midiScratch);
    for (int c = 0; c < numOut; ++c) juce::FloatVectorOperations::clip (out[c], out[c], -1.f, 1.f, n);
}

//---- project
void Engine::commit() { renderer.publish (proj); }
void Engine::checkpoint()
{
    undoStack.push_back (proj); redoStack.clear();
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    dirty = true;
}
bool Engine::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (proj); proj = std::move (undoStack.back()); undoStack.pop_back();
    dirty = true; commit(); return true;
}
bool Engine::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (proj); proj = std::move (redoStack.back()); redoStack.pop_back();
    dirty = true; commit(); return true;
}
void Engine::newProject()
{
    stop();
    proj = Project();
    proj.addTrack ("Lead", false, InstrumentType::PolySynth);
    proj.addTrack ("Bass", false, InstrumentType::BassSynth);
    proj.addTrack ("Drums", false, InstrumentType::DrumMachine);
    undoStack.clear(); redoStack.clear(); currentFile = juce::File(); dirty = false;
    renderer.seekRequest = 0.0;
    commit();
}
bool Engine::openProject (const juce::File& f, juce::String* err)
{
    Project p;
    if (! Project::loadFromFile (f, p, err)) return false;
    stop();
    proj = std::move (p); undoStack.clear(); redoStack.clear(); currentFile = f; dirty = false;
    renderer.seekRequest = 0.0;
    commit(); return true;
}
bool Engine::saveProject (const juce::File& f)
{
    if (! proj.saveToFile (f)) return false;
    currentFile = f; dirty = false; clearRecovery(); return true;
}

//---- transport
void Engine::play() { renderer.playing = true; }
void Engine::stop()
{
    if (recording) stopRecording();
    renderer.playing = false;
}
void Engine::togglePlay() { if (isPlaying()) stop(); else play(); }
void Engine::setPosition (double b) { renderer.seekRequest = juce::jmax (0.0, b); renderer.position = juce::jmax (0.0, b); }
void Engine::setTempo (double bpm) { proj.bpm = juce::jlimit (20.0, 400.0, bpm); dirty = true; commit(); }

//---- recording
bool Engine::startRecording (int trackId, juce::String* err)
{
    auto* t = proj.findTrack (trackId);
    if (t == nullptr || recording) { if (err) *err = "No track selected"; return false; }
    recStartBeat = std::floor (getPosition());
    if (t->audioTrack)
    {
        if (devices.getCurrentAudioDevice() == nullptr || devices.getCurrentAudioDevice()->getActiveInputChannels().isZero())
        { if (err) *err = "No audio input device is active"; return false; }
        recFile = defaultRecordingsDir().getNonexistentChildFile ("rec", ".wav");
        std::unique_ptr<juce::OutputStream> os (recFile.createOutputStream());
        if (os == nullptr) { if (err) *err = "Cannot create " + recFile.getFullPathName(); return false; }
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (renderer.getSampleRate()).withNumChannels (2).withBitsPerSample (24));
        if (w == nullptr) { if (err) *err = "Cannot start WAV writer"; return false; }
        writerThread = std::make_unique<juce::TimeSliceThread> ("NOIZE disk writer");
        writerThread->startThread();
        recSamples = 0;
        { const juce::SpinLock::ScopedLockType sl (writerLock);
          threadedWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (w.release(), *writerThread, 32768)); }
        recordingAudio = true;
    }
    else
    {
        heldNotes.clear(); recordedEvents.clear();
        renderer.liveTrackId = trackId;
        renderer.recordMidi = true;
    }
    recTrackId = trackId; recording = true;
    setPosition (recStartBeat);
    play();
    return true;
}

void Engine::drainMidiRec()
{
    MidiRecEvent e;
    while (renderer.popMidiRecEvent (e)) recordedEvents.push_back (e);
}

void Engine::stopRecording()
{
    if (! recording) return;
    recording = false;
    const double endBeat = getPosition();
    checkpoint();
    auto* t = proj.findTrack (recTrackId);
    if (recordingAudio)
    {
        recordingAudio = false;
        { const juce::SpinLock::ScopedLockType sl (writerLock); threadedWriter.reset(); }
        writerThread.reset();
        if (t != nullptr && recSamples > 0)
        {
            Clip c; c.name = "Recording"; c.audioFile = recFile.getFullPathName(); c.startBeat = recStartBeat;
            c.lengthBeats = (double) recSamples / renderer.getSampleRate() * proj.bpm / 60.0;
            t->clips.push_back (c);
        }
    }
    else
    {
        renderer.recordMidi = false;
        drainMidiRec();
        std::map<int, std::pair<double, float>> held;
        Clip c; c.name = "MIDI Recording"; c.startBeat = recStartBeat; c.lengthBeats = std::max (1.0, std::ceil (endBeat - recStartBeat));
        for (auto& ev : recordedEvents)
        {
            if (ev.on) held[ev.note] = { ev.beat, ev.vel };
            else if (held.count (ev.note))
            {
                auto h = held[ev.note]; held.erase (ev.note);
                c.notes.push_back ({ ev.note, h.first - recStartBeat, std::max (0.05, ev.beat - h.first), h.second });
            }
        }
        for (auto& h : held) c.notes.push_back ({ h.first, h.second.first - recStartBeat, std::max (0.1, endBeat - h.second.first), h.second.second });
        if (t != nullptr && ! c.notes.empty()) t->clips.push_back (c);
        recordedEvents.clear();
    }
    commit();
}

void Engine::finishMidiRecording() { drainMidiRec(); }
}
