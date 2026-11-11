#include "Exporter.h"

namespace noize
{
double computeProjectEndBeat (const Project& p)
{
    double end = 0.0;
    for (auto& t : p.tracks)
        for (auto& c : t.clips) end = std::max (end, c.startBeat + c.lengthBeats);
    return end > 0.0 ? end : p.lengthBeats;
}

ExportResult renderProjectToWav (const Project& proj, const ExportOptions& o, std::function<bool (float)> progress)
{
    ExportResult r;
    if (o.bitDepth != 16 && o.bitDepth != 24 && o.bitDepth != 32) { r.message = "Unsupported bit depth"; return r; }
    Project p = proj;
    p.loopEnabled = false; p.metronome = false;
    const double endBeat = o.endBeat > o.startBeat ? o.endBeat : computeProjectEndBeat (p);
    const double bps = p.bpm / 60.0;
    const juce::int64 bodySamples = (juce::int64) ((endBeat - o.startBeat) / bps * o.sampleRate);
    const juce::int64 total = bodySamples + (juce::int64) (o.tailSeconds * o.sampleRate);
    if (bodySamples <= 0) { r.message = "Nothing to export (empty range)"; return r; }

    AssetCache cache;
    Renderer rend (cache);
    constexpr int block = 512;
    rend.prepare (o.sampleRate, block);
    rend.offline = true;
    rend.publish (p);
    rend.position = o.startBeat; rend.seekRequest = o.startBeat;
    rend.playing = true;

    o.file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (o.file.createOutputStream());
    if (os == nullptr) { r.message = "Cannot write " + o.file.getFullPathName(); return r; }
    juce::WavAudioFormat wav;
    const int nch = o.mono ? 1 : 2;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (o.sampleRate).withNumChannels (nch).withBitsPerSample (o.bitDepth);
    if (o.bitDepth == 32) opts = opts.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) { r.message = "WAV writer could not be created"; return r; }

    juce::AudioBuffer<float> buf (2, block), all;
    if (o.normalize) all.setSize (nch, (int) total);
    float peak = 0.f;
    juce::int64 done = 0;
    while (done < total)
    {
        const int n = (int) std::min<juce::int64> (block, total - done);
        if (done >= bodySamples) rend.playing = false;   // tail: stop the transport, let voices/effects ring out
        float* chans[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        rend.process (chans, 2, n, nullptr);
        if (o.mono)
        {
            for (int i = 0; i < n; ++i) chans[0][i] = 0.5f * (chans[0][i] + chans[1][i]);
        }
        for (int c = 0; c < nch; ++c) peak = std::max (peak, buf.getMagnitude (c, 0, n));
        if (o.normalize) for (int c = 0; c < nch; ++c) all.copyFrom (c, (int) done, buf, c, 0, n);
        else if (! writer->writeFromAudioSampleBuffer (buf, 0, n)) { r.message = "Disk write failed"; return r; }
        done += n;
        if (progress && ! progress ((float) done / (float) total)) { writer.reset(); o.file.deleteFile(); r.message = "Cancelled"; return r; }
    }
    if (o.normalize)
    {
        if (peak > 1.0e-6f) all.applyGain (0.891f / peak);   // -1 dBFS
        peak = all.getMagnitude (0, (int) total);
        writer->writeFromAudioSampleBuffer (all, 0, (int) total);
    }
    writer.reset();
    r.ok = true; r.peak = peak; r.seconds = (double) total / o.sampleRate;
    r.message = "Exported " + juce::String (r.seconds, 1) + " s to " + o.file.getFullPathName();
    return r;
}
}
