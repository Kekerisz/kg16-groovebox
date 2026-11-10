// Offline test: renders a state JSON through the engine (internal clock, then MIDI piano-roll input)
// and writes a WAV, printing level statistics so broken voices show up as silence, NaN or clipping.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/State.h"

using namespace kg;

static void stats (const char* label, const juce::AudioBuffer<float>& b)
{
    double sum = 0; float peak = 0; int nans = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (c, i);
            if (! std::isfinite (x)) { ++nans; continue; }
            sum += x * x; peak = std::max (peak, std::abs (x));
        }
    const double rms = std::sqrt (sum / (b.getNumChannels() * (double) b.getNumSamples()));
    std::printf ("%-28s rms %6.1f dBFS  peak %5.2f  nan %d\n", label, 20 * std::log10 (rms + 1e-12), peak, nans);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: RenderTest state.json out.wav [mode]\n"); return 1; }
    const auto json = juce::File (juce::String (argv[1])).loadFileAsString();
    auto st = juce::JSON::parse (json);
    if (! st.isObject()) { std::printf ("bad json\n"); return 1; }
    const juce::String mode = argc > 3 ? argv[3] : "seq";

    std::vector<std::shared_ptr<const SampleData>> samples[NT];
    // a synthetic "user sample" on the cowbell track exercises the sample player
    if (mode == "usr")
    {
        auto sd = std::make_shared<SampleData>(); sd->rate = 44100; sd->buf.setSize (1, 8000);
        for (int i = 0; i < 8000; ++i) sd->buf.setSample (0, i, std::sin (i * 0.05f) * std::exp (-i / 2000.f));
        samples[COW].push_back (sd);
        st["vo"]["cow"].getDynamicObject()->setProperty ("model", "USR");
    }
    auto S = snapshotFromJson (st, samples);

    const double sr = 48000; const int block = 256;
    Engine e; e.prepare (sr);
    const int total = (int) (sr * 8);
    juce::AudioBuffer<float> out (2, total);
    juce::MidiBuffer midi;
    Engine::HostPos host;

    if (mode == "midi" || mode == "bassmidi")
    {
        // simulated DAW: transport stopped, piano roll sends notes
        S->seqDrums = false; S->seqBass = false;
        S->midiMode = mode == "midi" ? MIDI_DRUMS : MIDI_BASS;
    }
    else e.internalPlay.store (true);

    const int drumNotes[] = { 36, 42, 38, 42, 36, 46, 39, 42, 45, 49, 56, 37, 35, 41, 50, 44 };
    int noteIdx = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        midi.clear();
        if (mode == "midi" && (pos / block) % 24 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, drumNotes[noteIdx++ % 16], (juce::uint8) 100), 10);
        if (mode == "bassmidi")
        {
            const int beat = (pos / block) % 48;
            if (beat == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 90), 0);
            if (beat == 20) midi.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 120), 0);   // legato -> slide
            if (beat == 24) midi.addEvent (juce::MidiMessage::noteOff (1, 45), 0);
            if (beat == 40) midi.addEvent (juce::MidiMessage::noteOff (1, 52), 0);
        }
        e.process (*S, out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, host, midi);
    }
    stats (mode.toRawUTF8(), out);

    // per-model check: each kit plays one hit of every voice
    if (mode == "seq")
    {
        const char* kits[] = { "808", "909", "606", "LM", "DMX" };
        for (int k = 0; k < 5; ++k)
            for (int t = 0; t < NT; ++t)
            {
                Snapshot one = *S; one.seqDrums = false; one.seqBass = false; one.tr[t].model = modelFrom (kits[k]); one.tr[t].mute = false;
                Engine e2; e2.prepare (sr);
                juce::AudioBuffer<float> b (2, (int) sr);
                juce::MidiBuffer none; Engine::HostPos h;
                e2.auditionDrum (t);
                for (int p = 0; p < b.getNumSamples(); p += block)
                    e2.process (one, b.getWritePointer (0, p), b.getWritePointer (1, p), std::min (block, b.getNumSamples() - p), h, none);
                char lab[64]; std::snprintf (lab, sizeof lab, "  %s %s", kits[k], TRACK_IDS[t]);
                stats (lab, b);
            }
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (juce::File (juce::String (argv[2]))), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}
