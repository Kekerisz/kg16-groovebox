#pragma once
// KG-16 sound engine. A sample-accurate C++ port of the browser prototype's WebAudio graph:
// every drum voice is built from the same layers (oscillator, noise, metallic square cluster,
// clap bursts, sample playback) with WebAudio-compatible filters and automation envelopes.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace kg
{
constexpr int NT = 10, MAXS = 64, MAXLAYERS = 40;
enum { KICK, SNARE, CLAP, RIM, TOM, CHH, OHH, CYM, COW, SUB };
inline const char* const TRACK_IDS[NT] = { "kick", "snare", "clap", "rim", "tom", "chh", "ohh", "cym", "cow", "sub" };
enum Model { M808, M909, M606, MLM, MDMX, MUSR };
enum SatType { SAT_OFF, SAT_FET, SAT_TUBE, SAT_XFMR };
enum BassEng { E303, EMINI, ETAURUS };
enum MidiMode { MIDI_DRUMS, MIDI_BASS, MIDI_CHANNELS };
constexpr double PI = 3.14159265358979323846;

//==============================================================================
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    float uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.f - 1.f; }
    float gauss() { return (uni() + uni() + uni() + uni() - 2.f) / 0.577f; }
};

//==============================================================================
// Biquad with the WebAudio BiquadFilterNode conventions: lowpass/highpass Q is a resonance in dB,
// bandpass/peaking Q is the usual quality factor.
struct Biquad
{
    enum Type { LP, HP, BP, PK };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (Type t, double f, double Q, double gainDb, double sr)
    {
        f = std::clamp (f, 10.0, sr * 0.49);
        const double w = 2 * PI * f / sr, c = std::cos (w), s = std::sin (w);
        double a0 = 1;
        switch (t)
        {
            case LP: { const double al = s / (2 * std::pow (10.0, Q / 20)); b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break; }
            case HP: { const double al = s / (2 * std::pow (10.0, Q / 20)); b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break; }
            case BP: { const double al = s / (2 * Q); b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break; }
            case PK: { const double A = std::pow (10.0, gainDb / 40), al = s / (2 * Q); b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break; }
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    // plain linear-Q lowpass/highpass (used for the Linkwitz-Riley split of the transformer saturation)
    void setLinear (bool high, double f, double q, double sr)
    {
        const double w = 2 * PI * std::clamp (f, 10.0, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        if (high) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; } else { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; }
        a1 = -2 * c; a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
};

struct FSpec { Biquad::Type t; double f; double q = 1; };

//==============================================================================
// Emulates WebAudio setValueAtTime / exponentialRampToValueAtTime automation on a gain.
struct Env
{
    struct Pt { double t; float v; bool ramp; };
    Pt p[24]; int n = 0;
    int i = 0; float val = 0, mult = 1; int64_t left = -1; bool ramp = false; double sr = 48000;

    void clear() { n = 0; }
    void set (double t, float v) { if (n < 24) p[n++] = { t, v, false }; }
    void exp (double t, float v) { if (n < 24) p[n++] = { t, std::max (v, 1.0e-4f), true }; }
    void std_ (float peak, double dec) { clear(); set (0, 1.0e-4f); exp (0.0015, std::max (peak, 2.0e-4f)); exp (std::max (0.005, dec), 1.0e-4f); }

    void begin (double sampleRate)
    {
        sr = sampleRate;
        std::stable_sort (p, p + n, [] (const Pt& a, const Pt& b) { return a.t < b.t; });
        i = 0; val = n ? p[0].v : 0.f;
        seg();
    }
    void seg()
    {
        if (i + 1 >= n) { left = -1; ramp = false; return; }
        const auto& a = p[i]; const auto& b = p[i + 1];
        left = std::max<int64_t> (1, (int64_t) std::llround ((b.t - a.t) * sr));
        ramp = b.ramp;
        mult = ramp ? (float) std::pow (std::max (b.v, 1.0e-6f) / std::max (val, 1.0e-6f), 1.0 / (double) left) : 1.f;
    }
    inline float next()
    {
        const float o = val;
        if (left > 0)
        {
            if (ramp) val *= mult;
            if (--left == 0) { val = p[i + 1].v; ++i; seg(); }
        }
        return o;
    }
    bool finished() const { return left < 0 && val < 3.0e-4f; }
};

//==============================================================================
struct SampleData
{
    juce::AudioBuffer<float> buf;
    double rate = 44100;
    juce::String name, path;
};

struct Layer
{
    enum Kind { OSC, NOISE, METAL, PAIR, SAMPLE };
    enum Wave { SINE, TRI, SQUARE };
    bool active = false;
    int kind = OSC, wave = SINE;
    double ph = 0, f = 100, fMult = 1; int64_t fLeft = 0;
    double mph[6] {}; double mfr[6] {}; int nm = 0; float mixGain = 1;
    Biquad flt[4]; int nf = 0;
    Env env;
    float choke = 1, chokeC = 1;
    // sample playback
    std::shared_ptr<const SampleData> smp; double pos = 0, step = 1; int64_t t = 0, fadeStart = 0, fadeEnd = 0; float sAmp = 1;

    static inline float osc (int w, double ph)
    {
        switch (w)
        {
            case SINE: return (float) std::sin (2 * PI * ph);
            case TRI:  return (float) (ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4);
            default:   return ph < 0.5 ? 1.f : -1.f;
        }
    }

    inline float process (Rng& rng, double sr)
    {
        float s = 0;
        switch (kind)
        {
            case OSC:
                s = osc (wave, ph);
                ph += f / sr; if (ph >= 1) ph -= std::floor (ph);
                if (fLeft > 0) { f *= fMult; --fLeft; }
                break;
            case NOISE: s = rng.bi(); break;
            case METAL:
            case PAIR:
                for (int k = 0; k < nm; ++k) { s += mph[k] < 0.5 ? 1.f : -1.f; mph[k] += mfr[k] / sr; if (mph[k] >= 1) mph[k] -= 1; }
                s *= mixGain;
                break;
            case SAMPLE:
            {
                const auto& b = smp->buf;
                const int len = b.getNumSamples(), ip = (int) pos;
                if (ip + 1 >= len || t >= fadeEnd) { active = false; return 0; }
                const float fr = (float) (pos - ip);
                for (int c = 0; c < b.getNumChannels(); ++c)
                {
                    const float* d = b.getReadPointer (c);
                    s += d[ip] + (d[ip + 1] - d[ip]) * fr;
                }
                s /= (float) b.getNumChannels();
                pos += step;
                const float g = t < fadeStart ? sAmp : sAmp * (float) (fadeEnd - t) / (float) std::max<int64_t> (1, fadeEnd - fadeStart);
                ++t;
                for (int k = 0; k < nf; ++k) s = flt[k].process (s);
                return s * g * choke;
            }
        }
        for (int k = 0; k < nf; ++k) s = flt[k].process (s);
        s *= env.next() * choke;
        if (chokeC < 1) choke *= chokeC;
        if (env.finished() || choke < 1.0e-4f) active = false;
        return s;
    }
};

//==============================================================================
// Saturation module shared by every track, the bass and the master: dry / FET / tube / transformer.
struct Saturator
{
    int type = SAT_OFF; float k = .01f, post = 1, tk = 1, tk16 = 1, tk35 = 1, e1 = 1, e2 = 1;
    Biquad dc, xlo, xhi, xtop; double sr = 48000; bool ready = false;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dc.set (Biquad::HP, 15, .7, 0, sr);
        xlo.setLinear (false, 180, .5, sr);
        xhi.setLinear (true, 180, .5, sr);
        xtop.set (Biquad::LP, 17000, .5, 0, sr);
        ready = true;
    }
    void set (int t, float amount)
    {
        type = t;
        k = .01f + amount / 100.f * 10.f;
        post = t == SAT_OFF ? 1.f : 1.f / (.8f * (1.f + amount / 100.f * 1.1f));
        tk = std::tanh (k); tk16 = std::tanh (k * 1.6f); tk35 = std::tanh (k * .35f);
        e1 = 1.f - std::exp (-k); e2 = 1.f - std::exp (-k * .7f);
    }
    static inline float cl (float x) { return std::clamp (x, -1.f, 1.f); }
    inline float process (float x)
    {
        if (type == SAT_OFF) return x;
        x *= .8f;
        float y;
        switch (type)
        {
            case SAT_FET: { const float c = cl (x); y = std::tanh (k * (c < 0 ? 1.12f * c : c)) / tk; break; }
            case SAT_TUBE:
            {
                const float c = cl (x);
                y = c >= 0 ? (1.f - std::exp (-k * c)) / e1 : -(1.f - std::exp (k * .7f * c)) / e2;
                y = dc.process (y);
                break;
            }
            default:
            {
                const float lo = cl (xlo.process (x)), hi = cl (xhi.process (x));
                y = xtop.process (std::tanh (k * 1.6f * lo) / tk16 - std::tanh (k * .35f * hi) / tk35);
            }
        }
        return y * post;
    }
};

//==============================================================================
struct TrackParams
{
    int model = M909;
    float tune = 0, decay = 1, tone = .5f, level = .7f, panL = 1, panR = 1;
    float swing = 0; int swGrid = 1;
    int satType = SAT_OFF; float sat = 0, human = 25;
    bool mute = false;
    std::vector<std::shared_ptr<const SampleData>> samples;
};
struct BassStep { bool on = false; int n = 0; bool acc = false, sl = false; int oct = 0; };
struct Pattern
{
    bool d[NT][MAXS] {}; uint8_t prob[NT][MAXS] {}; uint8_t rat[NT][MAXS] {};
    bool acc[MAXS] {}; BassStep bass[MAXS] {};
};
struct BassParams
{
    int eng = E303; bool square = false; int key = 9;
    float cutoff = 380, reso = .72f, envmod = .6f, decay = .28f, accent = .6f, drive = .4f, glide = .06f, level = .55f;
    float swing = 0; int swGrid = 1; int satType = SAT_OFF; float sat = 0, human = 10;
};
struct Snapshot
{
    TrackParams tr[NT];
    BassParams bass;
    Pattern pat[2];
    int len = 16, act = 0; bool chain = false;
    float bpm = 128, master = .8f; int satType = SAT_TUBE; float sat = 20;
    int midiMode = MIDI_DRUMS; bool seqDrums = true, seqBass = true;
};

//==============================================================================
struct BassVoice
{
    struct E { float q, sus, dm; int oct; float mix[3]; float det, att, gate; };
    static const E& eng (int e)
    {
        static const E tab[3] = { { 22, 0, 1, 0, { 1, 0, 0 }, 0, .002f, .55f },
                                  { 13, .35f, 1.6f, 0, { .75f, .65f, .55f }, 8, .004f, .8f },
                                  { 9, .5f, 2.5f, -12, { .9f, .85f, 0 }, -6, .012f, .85f } };
        return tab[std::clamp (e, 0, 2)];
    }
    double sr = 48000, ph[3] {};
    float freq = 110, target = 110; bool gliding = false;
    float cut = 380, cutTarget = 380; int64_t cutHold = 0; float cutC = 0;
    float vca = 0, vcaTarget = 0, vcaC = 0;
    int64_t releaseAt = -1; float releaseTau = .012f;
    Biquad f1, f2; int coefCount = 0;
    Saturator sat;
    bool prevSlide = false; float vel = 1;
    const BassParams* P = nullptr;

    void prepare (double s) { sr = s; sat.prepare (s); }
    static float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1); }
        if (t > 1 - dt) { t = (t - 1) / dt; return (float) (t * t + t + t + 1); }
        return 0;
    }
    static float coef (float tau, double sr) { return 1.f - (float) std::exp (-1.0 / (std::max (1.0e-4, (double) tau) * sr)); }

    // a sequencer step or MIDI note at absolute sample `now`
    void note (float f, bool acc, bool legato, int64_t now, int64_t gateEnd)
    {
        const auto& e = eng (P->eng);
        releaseAt = -1;
        if (legato) { target = f; gliding = true; }
        else
        {
            freq = target = f; gliding = false;
            const float ac = acc ? 1 + P->accent : 1;
            const float peak = std::min (16000.f, P->cutoff + P->envmod * 5200.f * ac);
            const float dec = P->decay * e.dm * (acc ? .6f : 1.f);
            cut = peak; cutTarget = P->cutoff + (peak - P->cutoff) * e.sus;
            cutHold = (int64_t) (.003 * sr); cutC = coef (dec / 3, sr);
            vcaTarget = (acc ? .9f + P->accent * .3f : .7f) * vel; vcaC = coef (e.att, sr);
        }
        if (gateEnd >= 0) { releaseAt = gateEnd; releaseTau = .012f; }
        (void) now;
    }
    void release (float tau) { releaseAt = -1; vcaTarget = 0; vcaC = coef (tau, sr); }

    inline float process (int64_t now)
    {
        const auto& e = eng (P->eng);
        if (releaseAt >= 0 && now >= releaseAt) release (releaseTau);
        if (gliding) freq += (target - freq) * coef (P->glide / 3, sr);
        if (cutHold > 0) --cutHold; else cut += (cutTarget - cut) * cutC;
        vca += (vcaTarget - vca) * vcaC;
        if (vca < 1.0e-5f && vcaTarget == 0) { vca = 0; return sat.process (0); }

        const double fr[3] = { freq, freq * std::pow (2.0, e.det / 1200.0), P->eng == EMINI ? freq * .5 : freq };
        float mix = 0;
        for (int k = 0; k < 3; ++k)
        {
            if (e.mix[k] == 0) continue;
            const double dt = fr[k] / sr;
            ph[k] += dt; if (ph[k] >= 1) ph[k] -= 1;
            const bool sq = (k == 0 && P->square) || (k == 2 && P->eng == EMINI);
            float o;
            if (sq) { o = ph[k] < .5 ? 1.f : -1.f; o += blep (ph[k], dt); o -= blep (std::fmod (ph[k] + .5, 1.0), dt); }
            else o = (float) (2 * ph[k] - 1) - blep (ph[k], dt);
            mix += o * e.mix[k] * .8f;
        }
        if (--coefCount <= 0)
        {
            coefCount = 8;
            f1.set (Biquad::LP, cut, 0, 0, sr);
            f2.set (Biquad::LP, cut, P->reso * e.q, 0, sr);
        }
        float y = f2.process (f1.process (mix));
        const float kd = P->drive * 40;
        const float x = std::clamp (y * (1 + P->drive * 5), -1.f, 1.f);
        y = (1 + kd) * x / (1 + kd * std::abs (x)) / (1 + P->drive * 1.5f);
        return sat.process (y * vca * P->level);
    }
};

//==============================================================================
struct Track
{
    Layer layers[MAXLAYERS]; int rr = 0;
    Saturator sat; Biquad lp; bool lpOn = false; int bits = 0; int lastModel = -1;
    float lastOut = 0; bool silent = true;

    Layer& alloc()
    {
        for (auto& l : layers) if (! l.active) { l = Layer(); return l; }
        auto& l = layers[rr]; rr = (rr + 1) % MAXLAYERS; l = Layer(); return l;
    }
    void chokeAll (double sr) { for (auto& l : layers) if (l.active) l.chokeC = (float) std::exp (-1.0 / (.008 * sr)); }
};

//==============================================================================
class Engine
{
public:
    static constexpr int GRID_LATENCY_MS = 25;
    std::atomic<int> posStep { -1 }, posPat { 0 };
    std::atomic<bool> running { false }, internalPlay { false };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        latency = (int) std::round (GRID_LATENCY_MS * .001 * sr);
        for (auto& t : tracks) { t.sat.prepare (sr); t.lp.set (Biquad::LP, 20000, .5, 0, sr); }
        bass.prepare (sr); masterSat.prepare (sr); masterSatR.prepare (sr);
        events.clear(); events.reserve (4096);
        compEnv = 0; compAtk = (float) std::exp (-1 / (.003 * sr)); compRel = (float) std::exp (-1 / (.15 * sr));
        wasRunning = false; now = 0;
    }
    int getLatencySamples() const { return latency; }

    // --- requests from the UI thread (lock-free single-producer queue)
    void auditionDrum (int track) { pushCmd ({ 1, track, {} }); }
    void auditionBass (BassStep s) { pushCmd ({ 2, 0, s }); }

    struct HostPos { bool playing = false; double ppq = 0, bpm = 120; };

    void process (const Snapshot& S, float* L, float* R, int n, const HostPos& host, const juce::MidiBuffer& midi)
    {
        cur = &S;
        bass.P = &S.bass;
        configure (S);

        // ---- clock: host transport first, otherwise the UI's Play button
        bool run = false; double ppq0 = 0, bpm = S.bpm;
        if (host.playing) { run = true; ppq0 = host.ppq; bpm = host.bpm > 1 ? host.bpm : S.bpm; }
        else if (internalPlay.load()) { run = true; ppq0 = internalPpq; }
        const double ppqPerSample = bpm / 60.0 / sr, sd = 60.0 / bpm / 4.0;

        if (run)
        {
            if (! wasRunning || std::abs (ppq0 - expectedPpq) > ppqPerSample * 8)
            {
                nextK = (int64_t) std::ceil (ppq0 * 4.0 - 1.0e-7);
                firstK = nextK; bass.prevSlide = false; playPat = S.act; queued = -1; lastAct = S.act;
            }
            // a change of the edited pattern while playing is a cue: it starts at the next loop
            if (S.act != lastAct) { queued = S.act != playPat ? S.act : -1; lastAct = S.act; }
        }
        else if (wasRunning)
        {
            bass.release (.01f); bass.prevSlide = false;
            posStep.store (-1);
        }
        wasRunning = run; running.store (run);

        // UI commands
        Cmd c;
        while (popCmd (c))
        {
            if (c.kind == 1) trigDrum (c.track, .9f, 0, now, true);
            else { bass.prevSlide = false; playBassStep (c.step, now, (int) (.15 * sr)); }
        }

        // ---- schedule the steps that start inside this block
        if (run)
        {
            for (;;)
            {
                const double off = ((double) nextK * .25 - ppq0) / ppqPerSample;
                if (off >= n) break;
                fireStep (nextK, now + (int64_t) std::max (0.0, off), sd);
                ++nextK;
            }
        }

        // ---- MIDI from the host's piano roll (delayed by the reported latency like everything else)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            handleMidi (m, now + meta.samplePosition + latency);
        }

        // ---- render
        for (int i = 0; i < n; ++i)
        {
            while (! events.empty() && events.front().t <= now)
            {
                std::pop_heap (events.begin(), events.end(), later);
                const Ev e = events.back(); events.pop_back();
                execute (e);
            }
            float l = 0, r = 0;
            for (int ti = 0; ti < NT; ++ti)
            {
                const float y = renderTrack (ti);
                if (y == 0) continue;
                const auto& p = S.tr[ti];
                const bool centre = ti == KICK || ti == SNARE || ti == SUB;
                l += y * (centre ? 1 : p.panL);
                r += y * (centre ? 1 : p.panR);
            }
            const float b = bass.process (now);
            l += b; r += b;
            l *= S.master; r *= S.master;
            l = masterSat.process (l); r = masterSatR.process (r);
            // master bus compressor (soft knee) with make-up gain, then a soft safety clip
            const float pk = std::max (std::abs (l), std::abs (r));
            compEnv = pk > compEnv ? compAtk * compEnv + (1 - compAtk) * pk : compRel * compEnv + (1 - compRel) * pk;
            const float g = compGain (compEnv) * 1.5f;
            L[i] = std::tanh (l * g);
            R[i] = std::tanh (r * g);
            ++now;
        }
        if (run && ! host.playing) internalPpq = ppq0 + n * ppqPerSample;
        if (! run) internalPpq = 0;
        expectedPpq = ppq0 + n * ppqPerSample;
    }

private:
    struct Ev
    {
        int64_t t; int type; int track; float amp, r, D, Tn; int step;
        BassStep bs; int gate; float freq; bool acc, legato;
    };
    enum { EV_DRUM, EV_BASS_STEP, EV_BASS_NOTE, EV_BASS_OFF, EV_POS };
    static bool later (const Ev& a, const Ev& b) { return a.t > b.t; }

    struct Cmd { int kind; int track; BassStep step; };
    std::array<Cmd, 64> cmds; std::atomic<int> cmdW { 0 }, cmdR { 0 };
    void pushCmd (Cmd c) { const int w = cmdW.load(); const int nw = (w + 1) % 64; if (nw == cmdR.load()) return; cmds[(size_t) w] = c; cmdW.store (nw); }
    bool popCmd (Cmd& c) { const int r = cmdR.load(); if (r == cmdW.load()) return false; c = cmds[(size_t) r]; cmdR.store ((r + 1) % 64); return true; }

    double sr = 48000; int latency = 0; int64_t now = 0;
    const Snapshot* cur = nullptr;
    Track tracks[NT]; BassVoice bass; Saturator masterSat, masterSatR;
    std::vector<Ev> events;
    Rng rng; float drift = 0;
    bool wasRunning = false; double internalPpq = 0, expectedPpq = -1;
    int64_t nextK = 0, firstK = 0; int playPat = 0, queued = -1, lastAct = 0;
    int midiBassHeld = 0;
    float compEnv = 0, compAtk = 0, compRel = 0;

    void push (const Ev& e) { if (events.size() < 4000) { events.push_back (e); std::push_heap (events.begin(), events.end(), later); } }

    float compGain (float env) const
    {
        const float x = 20.f * std::log10 (env + 1.0e-9f), thr = -12, ratio = 4, knee = 30;
        float gr;
        if (2 * (x - thr) < -knee) gr = 0;
        else if (2 * (x - thr) > knee) gr = (thr + (x - thr) / ratio) - x;
        else { const float d = x - thr + knee / 2; gr = (1 / ratio - 1) * d * d / (2 * knee); }
        return std::pow (10.f, gr / 20.f);
    }

    void configure (const Snapshot& S)
    {
        for (int i = 0; i < NT; ++i)
        {
            auto& t = tracks[i]; const auto& p = S.tr[i];
            t.sat.set (p.satType, p.sat);
            const int bits = (p.model == MLM || p.model == MDMX) ? 8 : (p.model == M909 && (i == CHH || i == OHH || i == CYM)) ? 6 : 0;
            t.bits = bits;
            if (t.lastModel != p.model)
            {
                t.lastModel = p.model;
                const double f = p.model == MLM ? 12000 : p.model == MDMX ? 10500 : 20000;
                t.lpOn = f < 20000; t.lp.set (Biquad::LP, f, .5, 0, sr);
            }
        }
        bass.sat.set (S.bass.satType, S.bass.sat);
        masterSat.set (S.satType, S.sat); masterSatR.set (S.satType, S.sat);
    }

    inline float renderTrack (int ti)
    {
        auto& t = tracks[ti];
        float x = 0; bool any = false;
        for (auto& l : t.layers) if (l.active) { x += l.process (rng, sr); any = true; }
        if (! any && t.silent) return 0;
        float y = t.sat.process (x);
        if (t.bits) { const float Lq = (float) (1 << (t.bits - 1)); y = std::round (std::clamp (y, -1.f, 1.f) * Lq) / Lq; }
        if (t.lpOn) y = t.lp.process (y);
        y *= cur->tr[ti].level;
        t.silent = ! any && std::abs (y) < 1.0e-6f && std::abs (t.lastOut) < 1.0e-6f;
        t.lastOut = y;
        return y;
    }

    static float swingOff (float amt, int g, int s, double sd)
    {
        return (amt > 0 && s % (2 * g) == g) ? (float) (amt / 100.0 * g * sd * .5) : 0.f;
    }

    // ---- sequencer step
    void fireStep (int64_t k, int64_t tNom, double sd)
    {
        const auto& S = *cur;
        const int len = std::clamp (S.len, 1, MAXS);
        const int s = (int) (((k % len) + len) % len);
        if (s == 0 && k != firstK)
        {
            if (queued >= 0) { playPat = queued; queued = -1; }
            else if (S.chain) playPat = 1 - playPat;
        }
        const auto& P = S.pat[playPat];
        const int64_t base = tNom + latency;
        push ({ base, EV_POS, playPat, 0, 0, 0, 0, s, {}, 0, 0, false, false });

        drift = std::clamp (drift * .97f + rng.gauss() * .015f, -.15f, .15f);
        const float a = P.acc[s] ? 1.f : .7f;
        if (S.seqDrums)
            for (int ti = 0; ti < NT; ++ti)
            {
                const auto& tp = S.tr[ti];
                if (! P.d[ti][s] || tp.mute || rng.uni() * 100.f >= P.prob[ti][s]) continue;
                const int nr = std::max (1, (int) P.rat[ti][s]);
                const double tv = (double) base + swingOff (tp.swing, tp.swGrid, s, sd) * sr;
                for (int j = 0; j < nr; ++j)
                    trigDrum (ti, a * (j ? .78f : 1.f), s, (int64_t) (tv + j * sd / nr * sr), false);
            }
        if (S.seqBass && midiBassHeld == 0)
        {
            const float hb = S.bass.human / 100.f;
            Ev e {}; e.type = EV_BASS_STEP; e.bs = P.bass[s];
            e.gate = (int) (sd * sr);
            e.amp = std::max (.6f, 1 + rng.gauss() * .08f * hb);
            e.t = std::max (now, (int64_t) (base + (swingOff (S.bass.swing, S.bass.swGrid, s, sd) + rng.gauss() * .004 * hb) * sr));
            push (e);
        }
    }

    // ---- humanize and schedule one drum hit
    void trigDrum (int ti, float a, int s, int64_t t, bool immediate, float extraTune = 0)
    {
        const auto& p = cur->tr[ti];
        struct H { float t, bias, v, p, d, b; };
        static const H HUM[NT] = { { 6, -1, .12f, 8, .06f, .08f }, { 9, 4, .22f, 18, .12f, .25f }, { 7, 2, .18f, 15, .1f, .15f },
                                   { 8, 2, .2f, 20, .1f, .2f }, { 10, 3, .2f, 25, .12f, .15f }, { 7, -2, .25f, 20, .2f, .3f },
                                   { 7, -1, .2f, 20, .25f, .25f }, { 9, 0, .2f, 30, .25f, .35f }, { 6, 0, .15f, 10, .08f, .1f },
                                   { 3, 0, .06f, 4, .05f, 0 } };
        float r = std::pow (2.f, (p.tune + extraTune) / 12.f), D = p.decay, Tn = p.tone, aa = a;
        double tt = (double) t;
        const float h = std::min (1.f, p.human / 100.f);
        if (h > 0)
        {
            const auto& Hh = HUM[ti];
            const float vm = std::max (.25f, 1 + rng.gauss() * Hh.v * h + drift * h);
            aa = a * vm;
            if ((ti == CHH || ti == OHH || ti == CYM) && (s % 2)) aa *= 1 - .18f * h;
            r *= std::pow (2.f, rng.gauss() * Hh.p * h / 1200.f);
            D *= std::max (.3f, 1 + rng.gauss() * Hh.d * h);
            Tn = std::clamp (Tn + rng.gauss() * Hh.b * h + (vm - 1) * .5f, 0.f, 1.f);
            if (! immediate)
            {
                const double off = std::clamp ((double) (rng.gauss() * Hh.t + Hh.bias) * h / 1000.0, -(double) GRID_LATENCY_MS / 1000.0 + .001, .05);
                tt += off * sr;
            }
        }
        Ev e {}; e.t = std::max (now, (int64_t) tt); e.type = EV_DRUM; e.track = ti; e.amp = aa; e.r = r; e.D = D; e.Tn = Tn; e.step = s;
        if (immediate) execute (e); else push (e);
    }

    void playBassStep (const BassStep& st, int64_t t, int gate)
    {
        const auto& B = cur->bass; const auto& e = BassVoice::eng (B.eng);
        if (! st.on)
        {
            if (bass.prevSlide) bass.release (.01f);
            bass.prevSlide = false;
            return;
        }
        const int midiN = 24 + B.key + 12 + st.n + 12 * st.oct + e.oct;
        const float f = 440.f * std::pow (2.f, (midiN - 69) / 12.f);
        bass.note (f, st.acc, bass.prevSlide, t, t + (int64_t) (gate * (st.sl ? 1.6 : e.gate)));
        bass.prevSlide = st.sl;
    }

    void handleMidi (const juce::MidiMessage& m, int64_t t)
    {
        const auto& S = *cur;
        const bool toBass = S.midiMode == MIDI_BASS || (S.midiMode == MIDI_CHANNELS && m.getChannel() == 2);
        if (toBass)
        {
            if (m.isNoteOn())
            {
                Ev e {}; e.t = t; e.type = EV_BASS_NOTE;
                e.freq = (float) juce::MidiMessage::getMidiNoteInHertz (m.getNoteNumber());
                e.acc = m.getFloatVelocity() >= .85f; e.legato = midiBassHeld > 0;
                ++midiBassHeld; push (e);
            }
            else if (m.isNoteOff())
            {
                midiBassHeld = std::max (0, midiBassHeld - 1);
                if (midiBassHeld == 0) { Ev e {}; e.t = t; e.type = EV_BASS_OFF; push (e); }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { midiBassHeld = 0; Ev e {}; e.t = t; e.type = EV_BASS_OFF; push (e); }
            return;
        }
        if (! m.isNoteOn()) return;
        int ti = -1; float tune = 0;
        switch (m.getNoteNumber())
        {
            case 35: ti = SUB; break;
            case 36: ti = KICK; break;
            case 37: ti = RIM; break;
            case 38: case 40: ti = SNARE; break;
            case 39: ti = CLAP; break;
            case 41: ti = TOM; tune = -7; break;
            case 43: ti = TOM; tune = -4; break;
            case 45: ti = TOM; tune = 0; break;
            case 47: ti = TOM; tune = 3; break;
            case 48: ti = TOM; tune = 5; break;
            case 50: ti = TOM; tune = 8; break;
            case 42: case 44: ti = CHH; break;
            case 46: ti = OHH; break;
            case 49: case 51: case 52: case 55: case 57: case 59: ti = CYM; break;
            case 56: ti = COW; break;
            default: break;
        }
        if (ti < 0 || S.tr[ti].mute) return;
        trigDrum (ti, .4f + .6f * m.getFloatVelocity(), 0, t, false, tune);
    }

    void execute (const Ev& e)
    {
        switch (e.type)
        {
            case EV_POS: posStep.store (e.step); posPat.store (e.track); break;
            case EV_DRUM: synth (e.track, e.amp, e.r, e.D, e.Tn); break;
            case EV_BASS_STEP: bass.vel = e.amp; playBassStep (e.bs, e.t, e.gate); bass.vel = 1; break;
            case EV_BASS_NOTE: bass.vel = 1; bass.note (e.freq, e.acc, e.legato, e.t, -1); break;
            case EV_BASS_OFF: bass.release (.012f); break;
            default: break;
        }
    }

    //==========================================================================
    // Voice construction (mirrors the prototype's SY table line by line)
    Track* T_ = nullptr;
    void envStd (Layer& l, float peak, double dec) { l.env.std_ (peak, dec); l.env.begin (sr); }
    void filters (Layer& l, std::initializer_list<FSpec> specs)
    {
        for (auto& f : specs) if (l.nf < 4) l.flt[l.nf++].set (f.t, f.f, f.q, 0, sr);
    }
    void tn (int wave, double f0, double f1, double sw, float peak, double dec, std::initializer_list<FSpec> extra = {})
    {
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::OSC; l.wave = wave; l.f = f0;
        if (f1 > 0 && f1 != f0 && sw > 0) { l.fLeft = std::max<int64_t> (1, (int64_t) (sw * sr)); l.fMult = std::pow (f1 / f0, 1.0 / (double) l.fLeft); }
        filters (l, extra); envStd (l, peak, dec);
    }
    Layer& nz (std::initializer_list<FSpec> specs, float peak, double dec)
    {
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::NOISE; filters (l, specs); envStd (l, peak, dec); return l;
    }
    static constexpr double R808[6] = { 205.3, 304.4, 369.6, 522.7, 540, 800 };
    Layer& metal (double mul, std::initializer_list<FSpec> specs, float peak, double dec)
    {
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::METAL; l.nm = 6; l.mixGain = 1.6f / 6.f;
        for (int k = 0; k < 6; ++k) { l.mfr[k] = R808[k] * mul; l.mph[k] = 0; }
        filters (l, specs); envStd (l, peak, dec); return l;
    }
    void clap (float a, double f, double q, double spread, int n, double tail, double jit = 0)
    {
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::NOISE;
        filters (l, { { Biquad::BP, f, q }, { Biquad::HP, f * .55 } });
        auto& E = l.env; E.clear(); E.set (0, 1.0e-4f);
        for (int i = 0; i < n; ++i)
        {
            const double ti = i * spread + (jit > 0 ? rng.uni() * spread * jit : 0);
            E.set (ti, a); E.exp (ti + spread * .85, a * .12f);
        }
        const double tt = n * spread;
        E.set (tt, a * .85f); E.exp (tt + tail, 1.0e-4f);
        E.begin (sr);
    }
    void cow (int m, double r, double D, float T, float a)
    {
        const bool smp = m == MLM || m == MDMX;
        const double f1 = (smp ? 560 : 540) * r, f2 = (smp ? 850 : 800) * r, dec = (m == M808 ? .32 : m == M606 ? .18 : .26) * D;
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::PAIR; l.nm = 2; l.mixGain = 1; l.mfr[0] = f1; l.mfr[1] = f2;
        filters (l, { { Biquad::BP, (800 + T * 1200) * r, 1.1 } });
        auto& E = l.env; E.clear(); E.set (0, 1.0e-4f); E.exp (.002, a); E.exp (.02, a * .35f); E.exp (dec, 1.0e-4f); E.begin (sr);
    }
    void sample (int ti, float a, double r, double D, float Tn)
    {
        const auto& v = cur->tr[ti].samples;
        if (v.empty()) return;
        int i = (int) (rng.uni() * (float) v.size()); if (i >= (int) v.size()) i = 0;
        if (v.size() > 1 && i == lastSample[ti]) i = (i + 1) % (int) v.size();
        lastSample[ti] = i;
        const auto& sd = v[(size_t) i];
        auto& l = T_->alloc(); l.active = true; l.kind = Layer::SAMPLE; l.smp = sd;
        l.step = r * sd->rate / sr; l.pos = 0; l.t = 0;
        const double lenSec = (double) sd->buf.getNumSamples() / sd->rate / r * std::min (1.0, D);
        l.fadeEnd = (int64_t) (lenSec * sr); l.fadeStart = (int64_t) (lenSec * .7 * sr); l.sAmp = a * 1.2f;
        filters (l, { { Biquad::LP, 600 * std::pow (33.0, (double) Tn), .6 } });
    }
    int lastSample[NT] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

    void synth (int ti, float a, float r, float D, float T)
    {
        T_ = &tracks[ti]; T_->silent = false;
        const int m = cur->tr[ti].model;
        using B = Biquad;
        enum { S_ = Layer::SINE, TR = Layer::TRI };
        if (m == MUSR) { sample (ti, a, r, D, T); return; }
        if (ti == CHH) tracks[OHH].chokeAll (sr);
        switch (ti)
        {
            case KICK:
                if (m == M808) { tn (S_, 64 * r, 49 * r, .04, a, 1.1 * D); nz ({ { B::LP, 900 + T * 2500 } }, a * (.04f + T * .2f), .012); }
                else if (m == M909) { tn (S_, 290 * r, 52 * r, .035, a, .5 * D); tn (TR, 150 * r, 52 * r, .02, a * .3f, .07 * D); nz ({ { B::BP, 3200, .8 } }, a * (.6f + T * 1.2f), .012); }
                else if (m == M606) { tn (S_, 160 * r, 68 * r, .02, a, .26 * D); nz ({ { B::HP, 2500 } }, a * .12f * (.3f + T), .008); }
                else if (m == MLM) { tn (S_, 125 * r, 57 * r, .025, a, .27 * D); nz ({ { B::LP, 300 + T * 400 } }, a * 1.2f, .07 * D); nz ({ { B::BP, 2400, 1 } }, a * .6f * T, .01); }
                else { tn (S_, 175 * r, 54 * r, .03, a, .33 * D); nz ({ { B::BP, 3500, 1 } }, a * (.7f + T * .8f), .014); nz ({ { B::LP, 280 } }, a * .9f, .05 * D); }
                break;
            case SNARE:
                if (m == M808) { tn (S_, 238 * r, 226 * r, .03, a * .55f * (1.2f - T), .13 * D); tn (S_, 476 * r, 460 * r, .03, a * .3f * (1.2f - T), .08 * D); nz ({ { B::HP, 1800 } }, a * (.25f + T * .6f), .18 * D); }
                else if (m == M909) { tn (TR, 230 * r, 185 * r, .04, a * .55f, .15 * D); tn (TR, 360 * r, 310 * r, .04, a * .35f, .1 * D); nz ({ { B::HP, 900 }, { B::LP, 9000 } }, a * (.4f + T * .6f), .24 * D); }
                else if (m == M606) { tn (TR, 310 * r, 290 * r, .02, a * .4f, .07 * D); nz ({ { B::HP, 2500 } }, a * (.5f + T * .4f), .12 * D); }
                else if (m == MLM) { tn (S_, 185 * r, 160 * r, .03, a * .5f, .1 * D); nz ({ { B::BP, 2200, .6 } }, a * (1 + T * .8f), .2 * D); nz ({ { B::HP, 5000 } }, a * .3f, .14 * D); }
                else { tn (TR, 205 * r, 170 * r, .03, a * .6f, .14 * D); nz ({ { B::BP, 1800, .7 } }, a * (1.1f + T * .8f), .26 * D); }
                break;
            case CLAP:
            {
                const double f = (.7 + T * .6) * r;
                if (m == M808) clap (a * 2.2f, 1100 * f, 1.5, .011, 3, .26 * D);
                else if (m == M909) clap (a * 2.2f, 1350 * f, 1.2, .009, 4, .2 * D);
                else if (m == M606) clap (a * 2.f, 1800 * f, 1.6, .008, 3, .12 * D);
                else if (m == MLM) clap (a * 2.4f, 1500 * f, 1.1, .01, 4, .2 * D, .3);
                else clap (a * 2.3f, 1200 * f, 1.2, .012, 3, .3 * D, .2);
                break;
            }
            case RIM:
                if (m == M808) { tn (TR, 1667 * r, 1667 * r, 0, a * .6f, .025 * D, { { B::HP, 400 } }); tn (S_, 455 * r, 455 * r, 0, a * .6f, .03 * D, { { B::HP, 400 } }); }
                else if (m == M909) { nz ({ { B::BP, 2200 * r, 2 } }, a * 1.6f, .03 * D); tn (S_, 520 * r, 500 * r, .01, a * .4f, .025 * D); tn (TR, 1800 * r, 1800 * r, 0, a * .3f, .02 * D); }
                else if (m == M606) { tn (S_, 1700 * r, 1700 * r, 0, a * .6f, .016 * D); nz ({ { B::HP, 4000 } }, a * .3f, .01); }
                else if (m == MLM) { nz ({ { B::BP, 1600 * r, 2 } }, a * 1.8f, .05 * D); tn (S_, 420 * r, 400 * r, .02, a * .5f, .04 * D); }
                else { nz ({ { B::BP, 1900 * r, 1.6 } }, a * 1.8f, .06 * D); tn (S_, 500 * r, 470 * r, .02, a * .5f, .05 * D); }
                break;
            case TOM:
            {
                const double s = .6 + T * .8;
                if (m == M808) { tn (S_, 100 * r * (1 + .15 * s), 100 * r, .1, a, .42 * D); nz ({ { B::LP, 1800 } }, a * .15f, .06 * D); }
                else if (m == M909) { tn (S_, 140 * r * (1 + .55 * s), 140 * r, .08, a, .36 * D); nz ({ { B::BP, 2500, .8 } }, a * .4f, .02); }
                else if (m == M606) { tn (S_, 165 * r * (1 + .25 * s), 165 * r, .05, a, .2 * D); }
                else if (m == MLM) { tn (S_, 120 * r * (1 + .4 * s), 120 * r, .15, a, .45 * D); nz ({ { B::LP, 800 } }, a * .6f, .1 * D); }
                else { tn (S_, 110 * r * (1 + .5 * s), 110 * r, .12, a, .5 * D); nz ({ { B::LP, 1000 } }, a * .6f, .09 * D); }
                break;
            }
            case CHH: case OHH:
            {
                const bool open = ti == OHH; const double mul = r * (.7 + T * .6);
                if (m == M808) metal (mul * 1.4, { { B::BP, 10000 * mul, 1 }, { B::HP, 7000 * mul } }, a * .9f, (open ? .38 : .05) * D);
                else if (m == M909) { nz ({ { B::HP, 7500 * mul } }, a * .7f, (open ? .45 : .06) * D); metal (mul * 1.6, { { B::HP, 8000 * mul } }, a * .25f, (open ? .3 : .04) * D); }
                else if (m == M606) metal (mul * 1.6, { { B::BP, 11500 * mul, 1.2 }, { B::HP, 8500 * mul } }, a * .8f, (open ? .22 : .03) * D);
                else if (m == MLM) nz ({ { B::HP, 6000 * mul } }, a * .75f, (open ? .4 : .05) * D);
                else nz ({ { B::HP, 7000 * mul }, { B::BP, 10000 * mul, .7 } }, a * 1.1f, (open ? .45 : .055) * D);
                break;
            }
            case CYM:
            {
                const double mul = r * (.75 + T * .5);
                if (m == M808) { metal (mul * 1.3, { { B::BP, 8000 * mul, 1 }, { B::HP, 5000 * mul } }, a * .8f, 1.2 * D); metal (mul * .9, { { B::BP, 3500 * mul, 1.2 } }, a * .3f, .5 * D); }
                else if (m == M909) { metal (mul * 1.7, { { B::HP, 5500 * mul } }, a * .4f, 1.4 * D); nz ({ { B::HP, 5000 * mul } }, a * .5f, 1.6 * D); }
                else if (m == M606) metal (mul * 1.5, { { B::HP, 6000 * mul } }, a * .7f, .8 * D);
                else if (m == MLM) nz ({ { B::HP, 4000 * mul } }, a * .7f, 1.8 * D);
                else { nz ({ { B::HP, 4500 * mul } }, a * .6f, 1.5 * D); metal (mul * 1.2, { { B::HP, 5000 * mul } }, a * .25f, .9 * D); }
                break;
            }
            case COW: cow (m, r, D, T, a); break;
            case SUB:
                tn (S_, 44 * r * 1.2, 44 * r, .04, a, 1.2 * D);
                if (m == M909) nz ({ { B::BP, 2500, 1 } }, a * .4f * T, .01);
                break;
            default: break;
        }
    }
};
} // namespace kg
