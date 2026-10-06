#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>   // for the per-engine display colours
#include <cmath>

// ============================================================================
//  SKIPFIEND  -  playback-failure stutter/skip DSP core (header-only)
//  Engines: CD SKIP / HARD SKIP / STICK / BUFFER UNDERRUN / MP3 CORRUPT /
//           TAPE DROPOUT / RATCHET / GATE STUTTER
// ============================================================================

namespace skf
{

enum EngineId { CDSKIP=0, HARDSKIP, STICK, BUFFERUR, MP3CRPT, TAPEDO, RATCHET, GATESTUT,
                RECSKIP, NUM_ENGINES };

inline const char* engineName (int e)
{
    static const char* n[NUM_ENGINES] =
        { "CD SKIP", "HARD SKIP", "STICK", "BUFFER UNDERRUN",
          "MP3 CORRUPT", "TAPE DROPOUT", "RATCHET", "GATE STUTTER", "RECORD SKIP" };
    return (e >= 0 && e < NUM_ENGINES) ? n[e] : "?";
}

inline juce::Colour engineColour (int e)
{
    // Per-engine identity colours, drawn from the house palette (theme.md)
    // rather than the saturated primaries this started with: the accent orange
    // and teal, the warning amber and success green, plus muted neighbours that
    // stay in the same flat, slightly desaturated world.
    static const juce::uint32 c[NUM_ENGINES] =
        { 0xffe8532a,   // CD SKIP         primary accent
          0xfff2764e,   // HARD SKIP       lighter orange
          0xfff2c14e,   // STICK           warning amber
          0xff4fb6c4,   // BUFFER UNDERRUN secondary accent, teal
          0xff9b7fd4,   // MP3 CORRUPT     muted violet
          0xffc98a5e,   // TAPE DROPOUT    warm tan
          0xffe05a7f,   // RATCHET         muted rose
          0xffe6e8ec,   // GATE STUTTER    primary text
          0xff7bc96f }; // RECORD SKIP     success green
    return juce::Colour (c[juce::jlimit (0, NUM_ENGINES - 1, e)]);
}

// ----------------------------------------------------------------------------
//  Loop / grid lengths: 32 bars down to a 1/32 slice (4/4 assumed, 1 bar = 4 beats).
//  Single source of truth shared by the parameter layout and the UI combo box.
// ----------------------------------------------------------------------------
inline juce::StringArray gridNames()
{
    return { "32 bars", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
             "1/2", "1/4", "1/8", "1/8T", "1/8.", "1/16", "1/16T", "1/32", "1/32T" };
}

enum { kNumGrids = 15, kDefaultGridIndex = 11 };   // default = 1/16

inline double gridBeats (int idx)
{
    switch (idx)
    {
        case 0:  return 128.0;      // 32 bars
        case 1:  return 64.0;       // 16 bars
        case 2:  return 32.0;       // 8 bars
        case 3:  return 16.0;       // 4 bars
        case 4:  return 8.0;        // 2 bars
        case 5:  return 4.0;        // 1 bar
        case 6:  return 2.0;        // 1/2
        case 7:  return 1.0;        // 1/4
        case 8:  return 0.5;        // 1/8
        case 9:  return 1.0 / 3.0;  // 1/8T
        case 10: return 0.75;       // dotted 1/8
        case 11: return 0.25;       // 1/16
        case 12: return 1.0 / 6.0;  // 1/16T
        case 13: return 0.125;      // 1/32
        case 14: return 1.0 / 12.0; // 1/32T
        default: return 0.25;
    }
}

// ----------------------------------------------------------------------------
//  Rolling buffer  -  last N seconds of input as an abusable playback medium.
//  Indexed by absolute sample count; linear-interpolated fractional reads.
// ----------------------------------------------------------------------------
struct RollingBuffer
{
    void prepare (double sampleRate, int numCh, double seconds = 22.0)
    {
        fs    = sampleRate;
        ch    = juce::jlimit (1, 2, numCh);
        len   = juce::nextPowerOfTwo ((int) std::ceil (sampleRate * seconds));
        mask  = len - 1;
        buf.setSize (2, len);
        buf.clear();
        writeIdx = 0;
        total    = 0;
    }

    void push (const juce::AudioBuffer<float>& in)
    {
        const int n  = in.getNumSamples();
        const int ic = in.getNumChannels();
        for (int c = 0; c < 2; ++c)
        {
            const float* src = in.getReadPointer (juce::jmin (c, ic - 1));
            float* dst = buf.getWritePointer (c);
            int w = writeIdx;
            for (int i = 0; i < n; ++i) { dst[w] = src[i]; w = (w + 1) & mask; }
        }
        writeIdx = (writeIdx + n) & mask;
        total   += n;
    }

    int ringPos (long long a) const
    {
        long long m = ((long long) writeIdx + (a - total)) % (long long) len;
        if (m < 0) m += len;
        return (int) m;
    }

    float readAbs (double a, int c) const
    {
        if (total < 8) return 0.0f;
        const double lo = (double) juce::jmax<long long> (0, total - len + 8);
        const double hi = (double) (total - 3);
        a = juce::jlimit (lo, hi, a);
        const long long i0 = (long long) std::floor (a);
        const float f = (float) (a - (double) i0);
        const float* d = buf.getReadPointer (juce::jlimit (0, 1, c));
        const float s0 = d[ringPos (i0)];
        const float s1 = d[ringPos (i0 + 1)];
        return s0 + (s1 - s0) * f;
    }

    // single-sample write, used to feed the effect-chain buffer that layered
    // keys read from
    void pushSample (float l, float r) noexcept
    {
        buf.getWritePointer (0)[writeIdx] = l;
        buf.getWritePointer (1)[writeIdx] = r;
        writeIdx = (writeIdx + 1) & mask;
        ++total;
    }

    long long now() const noexcept { return total; }
    double    sr()  const noexcept { return fs; }

    juce::AudioBuffer<float> buf;
    double fs = 48000.0;
    int ch = 2, len = 0, mask = 0, writeIdx = 0;
    long long total = 0;
};

// ----------------------------------------------------------------------------
//  Per-retrigger modulation parameters (the Repeat Engine).
// ----------------------------------------------------------------------------
struct RepeatParams
{
    int    repeats       = 8;
    double sliceMinS     = 0.030;
    double sliceMaxS     = 0.120;
    int    lenMode       = 0;   // 0 fixed(mid) 1 ramp-shorter 2 ramp-longer 3 random
    int    pitchMode     = 0;   // 0 stable 1 asc 2 desc 3 chromatic 4 drift
    double basePitchSemi = 0.0;
    double pitchPerRep   = 0.0; // semitone step applied each repeat
    int    volEnv        = 0;   // 0 flat 1 decay 2 swell 3 tremolo 4 ducked
    int    panWalk       = 0;   // 0 static 1 alt 2 random 3 widen
    int    endMode       = 0;   // 0 hardcut 1 tail-out 2 glitch-click 3 seek-noise 4 silence-resume
    int    playMode      = 0;   // 0 classic 1 stutter-edit 2 ping-pong 3 scatter 4 orbit 5 evolve
    double motion        = 0.65; // 0..1 intensity of the selected playback choreography
    double timewarp      = 0.0; // -1..1  >0 accelerate burst, <0 decelerate
    double wet           = 1.0;
    double driftMsPerRep = 0.0; // STICK drift
    int    flavor        = 0;   // 0 none 1 mp3 2 tape 3 buffer-underrun
    bool   gate          = false;
    double bufferSilence = 0.0; // 0..1 fraction of burst muted (BUFFER UNDERRUN)
    bool   loopBurst     = false; // when the burst ends, start it again from the top
    bool   randomised    = false; // black-key voice: don't overwrite its random character
};

// ----------------------------------------------------------------------------
//  A single retrigger voice.
// ----------------------------------------------------------------------------
struct RepeatVoice
{
    void start (long long startAbs, const RepeatParams& pp, int eng, double sampleRate)
    {
        p = pp; engineId = eng; fs = sampleRate;
        sliceStartAbs = startAbs;
        pos = 0.0; rIndex = 0;
        active = true; tailing = false; tailGain = 1.0; tailRate = 0.9992;
        burstsPlayed = 0;
        curLen = sliceLenFor (0);
        wowPhase = 0.0;
        scatterOffset = 0.0;
        endedThisPass = false;
    }

    bool  isActive()  const noexcept { return active; }
    int   engine()    const noexcept { return engineId; }
    // a looping burst is "busy" until it's released; a one-shot until it ends
    bool  burstFinished() const noexcept { return ! active || tailing; }
    int   cyclesPlayed()  const noexcept { return burstsPlayed; }
    bool  justEnded() noexcept { bool b = endedThisPass; endedThisPass = false; return b; }

    // Gate released (key/button let go): fade out fast instead of clicking off.
    void releaseNow() noexcept
    {
        if (! active || tailing) return;
        tailing  = true;
        tailGain = 1.0;
        tailRate = 0.975;        // ~7 ms at 48 kHz
    }

    double sliceLenFor (int i)
    {
        const double mn = juce::jmax (2.0, p.sliceMinS * fs);
        const double mx = juce::jmax (mn, p.sliceMaxS * fs);
        const double t  = (p.repeats > 1) ? (double) i / (double) (p.repeats - 1) : 0.0;
        switch (p.lenMode)
        {
            case 1: return juce::jmax (2.0, mx + (mn - mx) * t);           // ramp shorter
            case 2: return juce::jmax (2.0, mn + (mx - mn) * t);           // ramp longer
            case 3: return mn + (mx - mn) * rng.nextDouble();              // random
            default: return (mn + mx) * 0.5;                              // fixed
        }
    }

    double pitchRatioFor (int i)
    {
        double semi = p.basePitchSemi;
        switch (p.pitchMode)
        {
            case 1: semi += p.pitchPerRep * i;          break;
            case 2: semi -= p.pitchPerRep * i;          break;
            case 3: semi += p.pitchPerRep * (i % 12);   break;
            case 4: semi += (rng.nextDouble() * 2.0 - 1.0) * p.pitchPerRep; break;
            default: break;
        }
        return std::pow (2.0, semi / 12.0);
    }

    double timewarpRate (int i)
    {
        if (std::abs (p.timewarp) < 1.0e-4) return 1.0;
        const double t = (p.repeats > 1) ? (double) i / (double) (p.repeats - 1) : 0.0;
        const double a = p.timewarp;
        const double f = (a >= 0.0) ? (1.0 + a * 3.0 * t)      // up to 4x
                                    : (1.0 + a * 0.8 * t);     // down to ~0.2x
        return juce::jmax (0.1, f);
    }

    float burstEnv (double gT, double intra)
    {
        switch (p.volEnv)
        {
            case 1: return (float) std::pow (juce::jmax (0.0, 1.0 - gT), 1.5);
            case 2: return (float) std::pow (juce::jlimit (0.0, 1.0, gT), 1.3);
            case 3: return 0.5f + 0.5f * (float) std::sin (gT * juce::MathConstants<double>::twoPi * 6.0);
            case 4: return (float) (1.0 - 0.85 * std::pow (std::sin (intra * juce::MathConstants<double>::pi), 2.0));
            default: return 1.0f;
        }
    }

    void panFor (int i, float& gl, float& gr)
    {
        double pan = 0.0;
        switch (p.panWalk)
        {
            case 1: pan = (i % 2 == 0) ? -0.7 : 0.7; break;
            case 2: pan = rng.nextDouble() * 2.0 - 1.0; break;
            case 3: pan = juce::jlimit (-1.0, 1.0, (double) i / juce::jmax (1, p.repeats - 1) * 2.0 - 1.0); break;
            default: pan = 0.0; break;
        }

        const double depth = juce::jlimit (0.0, 1.0, p.motion);
        if (p.playMode == 1)
            pan = juce::jlimit (-1.0, 1.0, pan + ((i & 1) ? 0.55 : -0.55) * depth);
        else if (p.playMode == 4 || p.playMode == 5)
            pan = juce::jlimit (-1.0, 1.0, pan + std::sin ((double) i * 1.35) * 0.85 * depth);

        const double a = (pan * 0.5 + 0.5) * juce::MathConstants<double>::halfPi;
        gl = (float) std::cos (a);
        gr = (float) std::sin (a);
    }

    void colorize (float& l, float& r, double intra)
    {
        if (p.flavor == 1) // MP3 CORRUPT: bitcrush + pre-echo + ring
        {
            const float q = 40.0f;
            l = std::round (l * q) / q;
            r = std::round (r * q) / q;
            l += 0.35f * preL; r += 0.35f * preR;         // pre-echo / phantom
            const float ring = 0.25f * (float) std::sin (intra * juce::MathConstants<double>::twoPi * 90.0);
            l += ring * preL; r += ring * preR;
            preL = l; preR = r;
        }
        else if (p.flavor == 2) // TAPE DROPOUT: wow + hiss + level sag
        {
            wowPhase += juce::MathConstants<double>::twoPi * 5.2 / fs;
            const float sag = 0.78f + 0.22f * (float) std::sin (wowPhase);
            l *= sag; r *= sag;
            l += (rng.nextFloat() - 0.5f) * 0.012f;
            r += (rng.nextFloat() - 0.5f) * 0.012f;
        }
    }

    // render one stereo sample, adding wet*mix into out pointers
    void render (const RollingBuffer& rb, float* outL, float* outR)
    {
        if (! active) return;

        if (tailing)
        {
            const float g = (float) tailGain;
            *outL += lastL * g * (float) p.wet;
            *outR += lastR * g * (float) p.wet;
            tailGain *= tailRate;
            if (tailGain < 5.0e-4) { active = false; endedThisPass = true; }
            return;
        }

        const int i = rIndex;
        const double depth = juce::jlimit (0.0, 1.0, p.motion);
        double direction = 1.0;
        double localPos = pos;
        double phraseOffset = 0.0;
        double choreographyRate = 1.0;

        // Playback personalities sit above the individual glitch engines. The
        // classic mode is bit-for-bit compatible with the old one-direction
        // loop; the other modes introduce repeat-to-repeat movement instead of
        // merely throwing more random values at the same gesture.
        switch (p.playMode)
        {
            case 1: // STUTTER EDIT: an 8-step phrase of flips, skips and speed accents
            {
                const int step = i & 7;
                if (step == 2 || step == 5) direction = -1.0;
                phraseOffset = ((step == 3) ? 0.55 : (step == 6) ? -0.35 : 0.0) * curLen * depth;
                choreographyRate = 1.0 + ((step == 1 || step == 6) ? 0.75 * depth
                                          : (step == 4) ? -0.35 * depth : 0.0);
                break;
            }
            case 2: // PING-PONG: alternate playback direction every repeat
                direction = (i & 1) ? -1.0 : 1.0;
                break;
            case 3: // SCATTER: each repeat reads a different nearby fragment
                phraseOffset = scatterOffset;
                break;
            case 4: // ORBIT: smooth speed breathing; stereo orbit is applied in panFor()
                choreographyRate = 1.0 + std::sin (((double) i + pos / juce::jmax (1.0, curLen))
                                      * juce::MathConstants<double>::halfPi) * 0.45 * depth;
                break;
            case 5: // EVOLVE: controlled combination of direction, offsets and rate movement
            {
                const int step = i % 6;
                direction = (step == 2 || step == 5) ? -1.0 : 1.0;
                phraseOffset = ((double) step - 2.5) * curLen * 0.14 * depth;
                choreographyRate = 1.0 + std::sin ((double) step * 1.7) * 0.55 * depth;
                break;
            }
            default: break;
        }

        if (direction < 0.0)
            localPos = juce::jmax (0.0, curLen - 1.0 - pos);

        const double rate = pitchRatioFor (i) * timewarpRate (i)
                          * juce::jlimit (0.20, 4.0, choreographyRate);
        const double rd   = (double) sliceStartAbs + localPos + phraseOffset;

        float sL = rb.readAbs (rd, 0);
        float sR = rb.readAbs (rd, 1);

        const double intra = (curLen > 0.0) ? pos / curLen : 0.0;
        double gT = (p.repeats > 0) ? ((double) i + intra) / (double) p.repeats : 0.0;
        float env = burstEnv (gT, intra);

        // the gate chop ducks hard but never all the way to silence -- a dead gap
        // in the middle of a held loop reads as the effect dropping out
        if (p.gate) env *= (intra < 0.5 ? 1.0f : 0.12f);

        // BUFFER UNDERRUN. A real rebuffer STALLS the transport, it doesn't mute
        // it, so instead of gapping the audio we drag playback down to a crawl --
        // a time-stretch/pitch-bend that keeps output flowing the whole way
        // through. Measured per SLICE on a looping cycle; against whole-cycle
        // progress it would swallow the first half of a multi-bar loop.
        double rateScale = 1.0;
        if (p.bufferSilence > 0.0)
        {
            const double prog = p.loopBurst ? intra : gT;
            if (prog < p.bufferSilence)
            {
                // ease into the stall so it sounds like the drive struggling
                const double d = juce::jlimit (0.0, 1.0, prog / juce::jmax (1.0e-6, p.bufferSilence));
                rateScale = 0.18 + 0.72 * d;
                env *= (float) (0.55 + 0.45 * d);
            }
        }

        // micro window to kill slice-edge clicks (except when glitch-click end wanted)
        float win = 1.0f;
        const double fN = juce::jmin (48.0, curLen * 0.12);
        if (fN > 1.0)
        {
            if (pos < fN)              win = (float) (pos / fN);
            else if (pos > curLen - fN) win = (float) ((curLen - pos) / fN);
        }
        win = juce::jlimit (0.0f, 1.0f, win);

        colorize (sL, sR, intra);

        float gl, gr; panFor (i, gl, gr);
        const float wl = sL * env * win;
        const float wr = sR * env * win;
        const float mono = 0.5f * (wl + wr);
        const float ol = wl * 0.4f + mono * 0.6f * gl * 1.41421356f;
        const float orr = wr * 0.4f + mono * 0.6f * gr * 1.41421356f;

        lastL = ol; lastR = orr;
        *outL += ol * (float) p.wet;
        *outR += orr * (float) p.wet;

        pos += rate * rateScale;
        while (pos >= curLen && active && ! tailing)
        {
            pos -= curLen;
            if (p.driftMsPerRep != 0.0)
                sliceStartAbs += (long long) std::llround (p.driftMsPerRep * fs * 0.001);
            ++rIndex;

            if (p.playMode == 3)
            {
                const double spread = juce::jmax (2.0, curLen * 2.5) * juce::jlimit (0.0, 1.0, p.motion);
                scatterOffset = (rng.nextDouble() * 2.0 - 1.0) * spread;
            }

            if (rIndex >= p.repeats)
            {
                if (p.loopBurst)
                {
                    // A held key plays its whole cycle, then immediately starts the
                    // cycle again -- seamless, and you always hear the full loop.
                    rIndex = 0;
                    curLen = sliceLenFor (0);
                    scatterOffset = 0.0;
                    ++burstsPlayed;
                }
                else
                {
                    switch (p.endMode)
                    {
                        case 1: tailing = true; tailGain = 1.0; break;        // tail-out
                        default: active = false; endedThisPass = true; break; // hard / click / seek / silence
                    }
                }
            }
            else
            {
                curLen = sliceLenFor (rIndex);
            }
        }
    }

    RepeatParams p;
    juce::Random rng;
    bool  active = false, tailing = false, endedThisPass = false;
    int   engineId = 0, rIndex = 0, burstsPlayed = 0;
    long long sliceStartAbs = 0;
    double pos = 0.0, curLen = 0.0, fs = 48000.0, tailGain = 1.0, tailRate = 0.9992, wowPhase = 0.0;
    double scatterOffset = 0.0;
    float lastL = 0.0f, lastR = 0.0f, preL = 0.0f, preR = 0.0f;
};

// ----------------------------------------------------------------------------
//  Recovery Artifacts  -  the noise that plays between skips.
// ----------------------------------------------------------------------------
struct RecoveryArtifacts
{
    void prepare (double sr) { fs = sr; env = 0.0; hpf = 0.0f; }

    void trigger (int type, double amt)  // 0 seek-click 1 laser-hunt 2 buffer-hiss 3 tape-stop
    {
        if (amt <= 0.001) return;
        kind = type;
        env  = juce::jlimit (0.0, 1.0, amt);
        life = (type == 3) ? (int) (fs * 0.25) : (type == 0) ? (int) (fs * 0.010)
                                                             : (int) (fs * 0.12);
        n = 0; wob = 0.0;
    }

    void render (float* l, float* r)
    {
        if (env <= 0.0 || n >= life) return;
        const double t = (double) n / (double) life;
        float s = 0.0f;
        switch (kind)
        {
            case 0: s = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::pow (1.0 - t, 6.0); break; // tick
            case 1: s = (float) std::sin (n * 0.7) * (float) (1.0 - t) * 0.5f
                        + (rng.nextFloat() - 0.5f) * 0.3f * (float) (1.0 - t); break;            // laser hunt
            case 2: s = (rng.nextFloat() - 0.5f) * 0.5f * (float) (1.0 - t); break;              // buffer hiss
            case 3: wob += juce::MathConstants<double>::twoPi * (30.0 * (1.0 - t)) / fs;
                    s = (float) std::sin (wob) * (float) std::pow (1.0 - t, 2.0) * 0.6f; break;   // tape stop
        }
        // gentle HP so clicks stay crisp not thumpy
        hpf += 0.2f * (s - hpf);
        const float o = (s - hpf) * (float) env * 0.9f;
        *l += o; *r += o;
        if (++n >= life) env = 0.0;
    }

    juce::Random rng;
    double fs = 48000.0, env = 0.0, wob = 0.0;
    int kind = 0, life = 0, n = 0;
    float hpf = 0.0f;
};

// ----------------------------------------------------------------------------
//  TempoDelay  -  the shared engine behind ECHO / DELAY / DUB. Tempo-synced,
//  fed only while its button is held, but always processed so the tail rings
//  out naturally after release instead of chopping off.
// ----------------------------------------------------------------------------
struct TempoDelay
{
    void prepare (double sampleRate, double maxSeconds = 5.0)
    {
        fs   = sampleRate;
        len  = juce::nextPowerOfTwo ((int) std::ceil (sampleRate * maxSeconds));
        mask = len - 1;
        buf.setSize (2, len);
        buf.clear();
        w = 0;
        lpL = lpR = 0.0f;
        hpL = hpR = 0.0f;
    }

    void reset()
    {
        buf.clear();
        lpL = lpR = 0.0f;
        hpL = hpR = 0.0f;
    }

    float readAt (int ch, double delaySamples) const
    {
        const double rp = (double) w - juce::jlimit (1.0, (double) (len - 4), delaySamples);
        double ip = rp;
        while (ip < 0.0) ip += (double) len;
        const int i0 = ((int) ip) & mask;
        const int i1 = (i0 + 1) & mask;
        const float f = (float) (ip - std::floor (ip));
        const float* d = buf.getReadPointer (juce::jlimit (0, 1, ch));
        return d[i0] + (d[i1] - d[i0]) * f;
    }

    // feed: whether new input is being injected (button held)
    // tone: 0 = dark/dubby feedback path, 1 = clean repeats
    void process (float inL, float inR, float* outL, float* outR,
                  double delaySamples, float feedback, float tone, float wetAmt, bool feed)
    {
        const float dL = readAt (0, delaySamples);
        const float dR = readAt (1, delaySamples);

        // damp the feedback path; darker = more dub
        const float a = juce::jlimit (0.02f, 0.99f, 0.12f + tone * 0.85f);
        lpL += a * (dL - lpL);
        lpR += a * (dR - lpR);

        // gentle high-pass so long feedback doesn't build mud
        hpL += 0.002f * (lpL - hpL);
        hpR += 0.002f * (lpR - hpR);
        const float fbL = lpL - hpL;
        const float fbR = lpR - hpR;

        float wL = feed ? inL : 0.0f;
        float wR = feed ? inR : 0.0f;
        wL += fbL * feedback;
        wR += fbR * feedback;

        // keep the feedback loop from running away
        wL = SafetyLimiterClip (wL);
        wR = SafetyLimiterClip (wR);

        buf.getWritePointer (0)[w] = wL;
        buf.getWritePointer (1)[w] = wR;
        w = (w + 1) & mask;

        *outL += dL * wetAmt;
        *outR += dR * wetAmt;
    }

    static float SafetyLimiterClip (float x)
    {
        if (x > 1.2f)  return 1.2f;
        if (x < -1.2f) return -1.2f;
        return x;
    }

    bool isRinging() const noexcept { return std::abs (lpL) + std::abs (lpR) > 1.0e-5f; }

    juce::AudioBuffer<float> buf;
    double fs = 48000.0;
    int len = 0, mask = 0, w = 0;
    float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f;
};

// ----------------------------------------------------------------------------
//  ReverseLooper  -  while held, plays a window of the rolling buffer backwards
//  and loops it. Window length is set in bars (1 .. 32).
// ----------------------------------------------------------------------------
struct ReverseLooper
{
    void prepare (double sampleRate) { fs = sampleRate; gain = 0.0f; running = false; }

    void start (long long anchorAbs, double windowSamples)
    {
        anchor = anchorAbs;
        window = juce::jmax (256.0, windowSamples);
        pos    = 0.0;
        running = true;
    }

    void stop() { running = false; }

    // returns true while it still has something to contribute (incl. its fade-out)
    bool render (const RollingBuffer& rb, float* outL, float* outR, float* dryL, float* dryR)
    {
        const float target = running ? 1.0f : 0.0f;
        gain += 0.002f * (target - gain);          // ~10 ms crossfade, no clicks
        if (gain < 1.0e-4f && ! running) return false;

        const double readAbs = (double) anchor - pos;
        const float sL = rb.readAbs (readAbs, 0);
        const float sR = rb.readAbs (readAbs, 1);

        // crossfade against the dry signal so engaging/releasing is seamless
        *outL = *dryL * (1.0f - gain) + sL * gain;
        *outR = *dryR * (1.0f - gain) + sR * gain;

        pos += 1.0;
        if (pos >= window) pos = 0.0;
        return true;
    }

    bool isRunning() const noexcept { return running || gain > 1.0e-4f; }

    double fs = 48000.0, pos = 0.0, window = 48000.0;
    long long anchor = 0;
    float gain = 0.0f;
    bool running = false;
};

// ----------------------------------------------------------------------------
//  SafetyLimiter  -  gentle output ceiling so 8 stacked voices + repeat gain
//  can never slam a DAW's next plugin. No lookahead (keeps latency at 0),
//  soft-knee above the ceiling rather than a hard clip.
// ----------------------------------------------------------------------------
struct SafetyLimiter
{
    void prepare (double sampleRate) { fs = sampleRate; juce::ignoreUnused (fs); gr = 0.0f; }

    static float softClip (float x, float ceiling)
    {
        const float a = std::abs (x);
        if (a <= ceiling) return x;
        const float over = a - ceiling;
        const float comp = ceiling + over / (1.0f + over * 6.0f);
        return (x < 0.0f) ? -comp : comp;
    }

    // returns the instantaneous gain reduction applied (0 = none), for metering
    float process (float& l, float& r, float ceilingDb = -0.3f)
    {
        const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        const float before = juce::jmax (std::abs (l), std::abs (r));
        l = softClip (l, ceiling);
        r = softClip (r, ceiling);
        const float after = juce::jmax (std::abs (l), std::abs (r));
        const float inst = before > 1.0e-6f ? juce::jmax (0.0f, 1.0f - after / before) : 0.0f;
        gr = juce::jmax (inst, gr * 0.995f);
        return gr;
    }

    double fs = 48000.0;
    float gr = 0.0f;
};

} // namespace skf
