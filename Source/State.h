#pragma once
// Turns the UI's JSON state (the same shape the browser prototype saves) into an engine Snapshot.
#include <juce_core/juce_core.h>
#include "Engine.h"

namespace kg
{
inline float num (const juce::var& o, const char* k, float d) { const auto v = o.getProperty (k, d); return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : (v.isBool() ? ((bool) v ? 1.f : 0.f) : d); }
inline bool flag (const juce::var& o, const char* k, bool d) { const auto v = o.getProperty (k, d); return v.isBool() ? (bool) v : (v.isVoid() ? d : (double) v != 0); }
inline juce::String str (const juce::var& o, const char* k, const char* d) { const auto v = o.getProperty (k, d); return v.isString() ? v.toString() : juce::String (d); }

inline int modelFrom (const juce::String& s)
{
    if (s == "808") return M808;
    if (s == "606") return M606;
    if (s == "LM")  return MLM;
    if (s == "DMX") return MDMX;
    if (s == "USR") return MUSR;
    return M909;
}
inline int satFrom (const juce::String& s) { if (s == "fet") return SAT_FET; if (s == "tube") return SAT_TUBE; if (s == "xfmr") return SAT_XFMR; return SAT_OFF; }

inline void readPattern (const juce::var& P, Pattern& out)
{
    out = Pattern();
    for (int t = 0; t < NT; ++t)
        for (int s = 0; s < MAXS; ++s) { out.prob[t][s] = 100; out.rat[t][s] = 1; }
    if (! P.isObject()) return;
    for (int t = 0; t < NT; ++t)
    {
        const auto d = P["d"][TRACK_IDS[t]], pr = P["prob"][TRACK_IDS[t]], rt = P["rat"][TRACK_IDS[t]];
        for (int s = 0; s < MAXS; ++s)
        {
            if (d.isArray() && s < d.size()) out.d[t][s] = (bool) d[s];
            if (pr.isArray() && s < pr.size()) out.prob[t][s] = (uint8_t) juce::jlimit (0, 100, (int) pr[s]);
            if (rt.isArray() && s < rt.size()) out.rat[t][s] = (uint8_t) juce::jlimit (1, 4, (int) rt[s]);
        }
    }
    const auto acc = P["acc"], bass = P["bass"];
    for (int s = 0; s < MAXS; ++s)
    {
        if (acc.isArray() && s < acc.size()) out.acc[s] = (bool) acc[s];
        if (bass.isArray() && s < bass.size())
        {
            const auto b = bass[s];
            auto& o = out.bass[s];
            o.on = flag (b, "on", false); o.n = juce::jlimit (0, 12, (int) num (b, "n", 0));
            o.acc = flag (b, "acc", false); o.sl = flag (b, "sl", false); o.oct = juce::jlimit (-1, 1, (int) num (b, "oct", 0));
        }
    }
}

// samples[] holds the decoded user samples per track (kept by the processor, not in the JSON)
inline std::unique_ptr<Snapshot> snapshotFromJson (const juce::var& st,
                                                   const std::vector<std::shared_ptr<const SampleData>> (&samples)[NT])
{
    auto S = std::make_unique<Snapshot>();
    readPattern (st["PATS"]["A"], S->pat[0]);
    readPattern (st["PATS"]["B"], S->pat[1]);
    S->act = str (st, "act", "A") == "B" ? 1 : 0;
    S->chain = flag (st, "chain", false);

    const auto T = st["T"];
    S->bpm = num (T, "bpm", 128); S->master = num (T, "master", .8f);
    S->len = juce::jlimit (16, MAXS, (int) num (T, "len", 16));
    S->satType = satFrom (str (T, "satType", "tube")); S->sat = num (T, "sat", 20);
    const auto mm = str (T, "midiMode", "drums");
    S->midiMode = mm == "bass" ? MIDI_BASS : mm == "channels" ? MIDI_CHANNELS : MIDI_DRUMS;
    S->seqDrums = flag (T, "seqDrums", true); S->seqBass = flag (T, "seqBass", true);

    const auto mute = st["mute"], vo = st["vo"];
    for (int t = 0; t < NT; ++t)
    {
        auto& tp = S->tr[t];
        const auto v = vo[TRACK_IDS[t]], p = v["p"];
        tp.model = modelFrom (str (v, "model", "909"));
        tp.tune = num (p, "tune", 0); tp.decay = num (p, "decay", 1); tp.tone = num (p, "tone", .5f); tp.level = num (p, "level", .7f);
        tp.panL = num (p, "panL", 1); tp.panR = num (p, "panR", 1);
        tp.swing = num (p, "swing", 0); tp.swGrid = juce::jlimit (1, 4, (int) num (p, "swGrid", 1));
        tp.satType = satFrom (str (p, "satType", "off")); tp.sat = num (p, "sat", 0); tp.human = num (p, "human", 25);
        tp.mute = flag (mute, TRACK_IDS[t], false);
        tp.samples = samples[t];
    }

    const auto B = st["B"]; auto& b = S->bass;
    const auto eng = str (B, "eng", "303");
    b.eng = eng == "MINI" ? EMINI : eng == "TAURUS" ? ETAURUS : E303;
    b.square = str (B, "wave", "sawtooth") == "square";
    b.key = juce::jlimit (0, 11, (int) num (B, "key", 9));
    b.cutoff = num (B, "cutoff", 380); b.reso = num (B, "reso", .72f); b.envmod = num (B, "envmod", .6f); b.decay = num (B, "decay", .28f);
    b.accent = num (B, "accent", .6f); b.drive = num (B, "drive", .4f); b.glide = num (B, "glide", .06f); b.level = num (B, "level", .55f);
    b.swing = num (B, "swing", 0); b.swGrid = juce::jlimit (1, 4, (int) num (B, "swGrid", 1));
    b.satType = satFrom (str (B, "satType", "off")); b.sat = num (B, "sat", 0); b.human = num (B, "human", 10);
    return S;
}
} // namespace kg
