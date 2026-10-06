#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
namespace P { // parameter ids
    // ---- the hidden effect. Not on the panel: you have to find it. --------
    static const char* ruinOn       = "ruinOn";
    static const char* ruinAmt      = "ruinAmt";
    static const char* ruinRate     = "ruinRate";
    static const char* ruinTone     = "ruinTone";

    static const char* density      = "density";
    static const char* chaos        = "chaos";
    static const char* mix          = "mix";
    static const char* grid         = "grid";
    static const char* swing        = "swing";
    static const char* grooveLock   = "grooveLock";
    static const char* artifacts    = "artifacts";
    static const char* repMin       = "repMin";
    static const char* repMax       = "repMax";
    static const char* sliceMin     = "sliceMin";
    static const char* sliceMax     = "sliceMax";
    static const char* lenMode      = "lenMode";
    static const char* pitchMode    = "pitchMode";
    static const char* pitchPerSkip = "pitchPerSkip";
    static const char* basePitch    = "basePitch";
    static const char* timewarp     = "timewarp";
    static const char* volEnv       = "volEnv";
    static const char* panWalk      = "panWalk";
    static const char* endMode      = "endMode";
    static const char* playMode     = "playMode";
    static const char* motion       = "motion";
    static const char* scTrigger    = "scTrigger";
    static const char* scThresh     = "scThresh";
    static const char* seqOn        = "seqOn";
    static const char* bypass       = "bypass";
    static const char* bpmSync      = "bpmSync";
    static const char* manualBpm    = "manualBpm";
    static juce::String rate (int e) { return "rate_" + juce::String (e); }
    static const char* echoRate     = "echoRate";
    static const char* delayRate    = "delayRate";
    static const char* dubRate      = "dubRate";
    static const char* revBars      = "revBars";
    static juce::String en   (int e) { return "en_"   + juce::String (e); }
    static juce::String amt  (int e) { return "amt_"  + juce::String (e); }
    static juce::String prob (int e) { return "prob_" + juce::String (e); }
}

//==============================================================================
SkipfiendAudioProcessor::SkipfiendAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", makeLayout())
{
    for (int i = 0; i < kSeqSteps; ++i)
    {
        seqEngine[i].store  (i % 4 == 0 ? (int) skf::CDSKIP : -1);
        seqRepeats[i].store (8);
    }
    for (auto& s : logRing) { s.a.store (0); s.b.store (0); }
    for (auto& mapping : ccToParamIndex) mapping.store (-1);
    voices.resize (16);          // polyphonic keys can each want voices at once
    voiceLayer.assign (voices.size(), 0);
    formatManager.registerBasicFormats();
    readAheadThread.startThread();
}

SkipfiendAudioProcessor::~SkipfiendAudioProcessor()
{
    transport.setSource (nullptr);
    readAheadThread.stopThread (2000);
}

//==============================================================================
APVTS::ParameterLayout SkipfiendAudioProcessor::makeLayout()
{
    using FR = juce::NormalisableRange<float>;
    APVTS::ParameterLayout layout;

    auto pf = [&] (const char* id, const juce::String& nm, FR r, float def)
    { layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, nm, r, def)); };
    auto pc = [&] (const char* id, const juce::String& nm, juce::StringArray ch, int def)
    { layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, nm, ch, def)); };
    auto pb = [&] (const char* id, const juce::String& nm, bool def)
    { layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, nm, def)); };
    auto pi = [&] (const char* id, const juce::String& nm, int lo, int hi, int def)
    { layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, nm, lo, hi, def)); };

    pb (P::bypass,    "Bypass",       false);

    pb (P::bpmSync,   "Sync To Host BPM", true);
    pf (P::manualBpm, "Manual BPM",   FR (20.0f, 300.0f), 120.0f);

    pf (P::density,   "Skip Density", FR (0.0f, 1.0f), 0.35f);
    pf (P::chaos,     "Chaos",        FR (0.0f, 1.0f), 0.20f);
    pf (P::mix,       "Mix (Dry/Wet)", FR (0.0f, 1.0f), 1.0f);
    pc (P::grid,      "Loop / Rhythmic Gravity", skf::gridNames(), skf::kDefaultGridIndex);
    pf (P::swing,     "Swing",        FR (0.0f, 1.0f), 0.0f);
    pb (P::grooveLock,"Groove Lock",  false);
    pf (P::artifacts, "Recovery Artifacts", FR (0.0f, 1.0f), 0.40f);

    pi (P::repMin,    "Repeats Min",  2, 64, 3);
    pi (P::repMax,    "Repeats Max",  2, 64, 12);
    pf (P::sliceMin,  "Slice Min ms", FR (1.0f, 500.0f, 0.0f, 0.4f), 30.0f);
    pf (P::sliceMax,  "Slice Max ms", FR (1.0f, 500.0f, 0.0f, 0.4f), 120.0f);
    pc (P::lenMode,   "Slice Length",
        { "Fixed", "Ramp Shorter", "Ramp Longer", "Random" }, 0);
    pc (P::pitchMode, "Pitch / Repeat",
        { "Stable", "Ascending", "Descending", "Chromatic", "Drift" }, 0);
    pf (P::pitchPerSkip, "Pitch Step (semi)", FR (-12.0f, 12.0f), 0.0f);
    pf (P::basePitch,    "Base Pitch (semi)", FR (-24.0f, 24.0f), 0.0f);
    pf (P::timewarp,     "Timewarp",          FR (-1.0f, 1.0f), 0.0f);
    pc (P::volEnv,    "Volume Envelope",
        { "Flat", "Decay", "Swell", "Tremolo", "Ducked" }, 0);
    pc (P::panWalk,   "Pan Walk",
        { "Static", "Alternate", "Random", "Widen" }, 0);
    pc (P::endMode,   "End Behaviour",
        { "Hard Cut", "Tail Out", "Glitch Click", "Seek Noise", "Silence / Resume" }, 3);
    pc (P::playMode,  "Playback Style",
        { "Classic", "Stutter Edit", "Ping-Pong", "Scatter", "Orbit", "Evolve" }, 0);
    pf (P::motion,    "Playback Motion", FR (0.0f, 1.0f), 0.65f);

    // overlay effect rates: how fast each repeats relative to the grid
    const juce::StringArray rateNames { "1x", "2x", "4x", "8x", "16x" };
    pc (P::echoRate,  "Echo Rate",  rateNames, 2);
    pc (P::delayRate, "Delay Rate", rateNames, 1);
    pc (P::dubRate,   "Dub Rate",   rateNames, 1);
    pc (P::revBars,   "Reverse Bars", { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" }, 1);

    pb (P::scTrigger, "Sidechain Trigger", false);
    pf (P::scThresh,  "SC Threshold dB",  FR (-60.0f, 0.0f), -24.0f);
    pb (P::seqOn,     "Skip Language On",  false);

    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const juce::String nm = skf::engineName (e);
        pb (P::en (e).toRawUTF8(),   nm + " On",     e == skf::CDSKIP);
        pf (P::amt (e).toRawUTF8(),  nm + " Amount", FR (0.0f, 1.0f), e == skf::CDSKIP ? 1.0f : 0.7f);
        pf (P::prob (e).toRawUTF8(), nm + " Prob",   FR (0.0f, 1.0f), e == skf::CDSKIP ? 0.85f : 0.5f);
        // per-engine LOOP LENGTH, in bars: 1x is a one-bar loop, 32x is a
        // thirty-two-bar loop. Live-applied, so turning it mid-hold widens or
        // tightens the loop that is already playing.
        pc (P::rate (e).toRawUTF8(), nm + " Loop",
            { "1x", "2x", "3x", "4x", "6x", "8x", "12x", "16x", "24x", "32x" }, 0);
    }

    // RUIN, the hidden effect, deliberately LAST: appending rather than
    // inserting keeps every existing parameter at the index it already had.
    pb (P::ruinOn,   "Ruin On",     false);
    pf (P::ruinAmt,  "Ruin Amount", FR (0.0f, 1.0f), 0.5f);
    pf (P::ruinRate, "Ruin Rate",   FR (0.0f, 1.0f), 0.35f);
    pf (P::ruinTone, "Ruin Tone",   FR (0.0f, 1.0f), 0.5f);

    return layout;
}

float SkipfiendAudioProcessor::cachedParam (const char* id) const
{
    if (auto* a = apvts.getRawParameterValue (id)) return a->load();
    return 0.0f;
}

bool SkipfiendAudioProcessor::isEffectBypassed() const { return cachedParam (P::bypass) > 0.5f; }

//==============================================================================
void SkipfiendAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    // long enough to hold a 32-bar reverse loop at typical tempos, or the whole
    // track when FULL BUFFER is on
    roll.prepare (sampleRate, 2, juce::jmax (64.0, bufferedSeconds.load()));
    chainRoll.prepare (sampleRate, 2, 8.0);   // only needs recent history
    fxRoll.prepare (sampleRate, 2, 64.0);     // REVERSE can reach back 32 bars
    voiceLayer.assign (voices.size(), 0);
    artifacts.prepare (sampleRate);
    limiter.prepare (sampleRate);
    echoLine.prepare (sampleRate);
    delayLine.prepare (sampleRate);
    dubLine.prepare (sampleRate);
    reverser.prepare (sampleRate);
    for (int i = 0; i < NUM_OVERLAYS; ++i) { overlayArmed[i] = false; overlayActive[i] = false; }
    for (auto& v : voices) v.active = false;
    phaseSamples = 0.0;
    globalSample = 0;
    actSmooth = 0.0f;
    playheadInit = false;
    timeSuspended = false;
    srcRmsEnv = wetRmsEnv = 0.0f;
    normGain = 1.0f;
    recordedCount.store (0, std::memory_order_release);
    transport.prepareToPlay (samplesPerBlock, sampleRate);
}

void SkipfiendAudioProcessor::releaseResources()
{
    transport.releaseResources();
}

bool SkipfiendAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto mainOut = l.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (l.getMainInputChannelSet() != mainOut) return false;
    const auto sc = l.getChannelSet (true, 1);
    if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
        return false;
    return true;
}

//==============================================================================
static double gridBeatsFor (int idx) { return skf::gridBeats (idx); }

int SkipfiendAudioProcessor::pickEngineWeighted (juce::Random& r)
{
    double w[skf::NUM_ENGINES]; double sum = 0.0;
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const bool on = cachedParam (P::en (e).toRawUTF8()) > 0.5f;
        w[e] = on ? (double) cachedParam (P::amt (e).toRawUTF8())
                        * (double) cachedParam (P::prob (e).toRawUTF8()) + 1.0e-4 : 0.0;
        sum += w[e];
    }
    if (sum <= 0.0) return -1;
    double x = r.nextDouble() * sum;
    for (int e = 0; e < skf::NUM_ENGINES; ++e) { x -= w[e]; if (x <= 0.0) return e; }
    return skf::CDSKIP;
}

int SkipfiendAudioProcessor::randomEnabledEngine (juce::Random& r)
{
    int on[skf::NUM_ENGINES]; int c = 0;
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
        if (cachedParam (P::en (e).toRawUTF8()) > 0.5f) on[c++] = e;
    return c ? on[r.nextInt (c)] : -1;
}

double SkipfiendAudioProcessor::engineLoopBarsFor (int e) const
{
    static const double kBars[] = { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0 };
    const int idx = juce::jlimit (0, 9, (int) cachedParam (P::rate (e).toRawUTF8()));
    return kBars[idx];
}

void SkipfiendAudioProcessor::applyLiveParamsToVoice (skf::RepeatVoice& v, float chaosAmt)
{
    // A black-key voice owns its own randomised character -- overwriting it from
    // the knobs every block would flatten exactly what makes it random.
    if (! v.p.randomised)
    {
        v.p.pitchMode     = (int) cachedParam (P::pitchMode);
        v.p.basePitchSemi = cachedParam (P::basePitch);
        v.p.pitchPerRep   = cachedParam (P::pitchPerSkip);
        v.p.timewarp      = cachedParam (P::timewarp);
        v.p.volEnv        = (int) cachedParam (P::volEnv);
        v.p.panWalk       = (int) cachedParam (P::panWalk);
        v.p.playMode      = (int) cachedParam (P::playMode);
        v.p.motion        = cachedParam (P::motion);
        v.p.wet           = cachedParam (P::amt (v.engine()).toRawUTF8());

        if (v.engine() == skf::RECSKIP)
            skf::enforceRecordSkipVerbatim (v.p);
    }

    // CHAOS keeps working while you hold: every so often it re-rolls the
    // character of the running loop rather than only affecting the next trigger
    if (chaosAmt > 0.0f && rng.nextFloat() < chaosAmt * 0.03f)
    {
        v.p.pitchMode = rng.nextInt (5);
        v.p.panWalk   = rng.nextInt (4);
        if (v.p.playMode != 0 && rng.nextFloat() < chaosAmt * 0.35f)
            v.p.playMode = 1 + rng.nextInt (5);
        v.p.timewarp  = juce::jlimit (-1.0, 1.0, v.p.timewarp
                            + (rng.nextDouble() * 2.0 - 1.0) * 0.5 * chaosAmt);
    }
}

int SkipfiendAudioProcessor::allocVoiceIndex()
{
    for (int i = 0; i < (int) voices.size(); ++i)
        if (! voices[(size_t) i].isActive()) return i;

    // Nothing free: steal the voice furthest through its burst, but never steal
    // a looping one if a one-shot is available -- looping voices belong to keys
    // that are still held down.
    int best = -1; double bestF = -1.0;
    for (int i = 0; i < (int) voices.size(); ++i)
    {
        auto& v = voices[(size_t) i];
        if (v.p.loopBurst) continue;
        const double f = v.p.repeats > 0 ? (double) v.rIndex / (double) v.p.repeats : 1.0;
        if (f > bestF) { bestF = f; best = i; }
    }
    if (best >= 0) return best;

    for (int i = 0; i < (int) voices.size(); ++i)
    {
        auto& v = voices[(size_t) i];
        const double f = v.p.repeats > 0 ? (double) v.rIndex / (double) v.p.repeats : 1.0;
        if (f > bestF) { bestF = f; best = i; }
    }
    return juce::jmax (0, best);
}

void SkipfiendAudioProcessor::configureEngine (int e, skf::RepeatParams& rp, double bpm, double gridSamples)
{
    const double beatS = 60.0 / juce::jmax (20.0, bpm) * sr;

    int rmn = (int) cachedParam (P::repMin);
    int rmx = (int) cachedParam (P::repMax);
    if (rmn > rmx) std::swap (rmn, rmx);
    rp.repeats       = rng.nextInt (juce::Range<int> (rmn, rmx + 1));
    rp.sliceMinS     = cachedParam (P::sliceMin) * 0.001;
    rp.sliceMaxS     = cachedParam (P::sliceMax) * 0.001;
    if (rp.sliceMinS > rp.sliceMaxS) std::swap (rp.sliceMinS, rp.sliceMaxS);
    rp.lenMode       = (int) cachedParam (P::lenMode);
    rp.pitchMode     = (int) cachedParam (P::pitchMode);
    rp.basePitchSemi = cachedParam (P::basePitch);
    rp.pitchPerRep   = cachedParam (P::pitchPerSkip);
    rp.volEnv        = (int) cachedParam (P::volEnv);
    rp.panWalk       = (int) cachedParam (P::panWalk);
    rp.endMode       = (int) cachedParam (P::endMode);
    rp.playMode      = (int) cachedParam (P::playMode);
    rp.motion        = cachedParam (P::motion);
    rp.timewarp      = cachedParam (P::timewarp);
    rp.wet           = cachedParam (P::amt (e).toRawUTF8());
    rp.flavor = 0; rp.gate = false; rp.driftMsPerRep = 0.0; rp.bufferSilence = 0.0;

    switch (e)
    {
        case skf::CDSKIP:
            rp.sliceMinS = juce::jlimit (0.010, 0.200, rp.sliceMinS);
            rp.sliceMaxS = juce::jlimit (rp.sliceMinS, 0.200, rp.sliceMaxS);
            break;

        case skf::HARDSKIP:
            rp.repeats = 1;
            rp.sliceMinS = rp.sliceMaxS = juce::jlimit (0.05, 1.2, gridSamples / sr * (1.0 + rng.nextInt (4)));
            rp.endMode = 0; // hard cut
            break;

        case skf::STICK:
            rp.sliceMinS = 0.020; rp.sliceMaxS = 0.060;
            rp.repeats   = 60 + rng.nextInt (140);
            rp.driftMsPerRep = (0.08 + rng.nextDouble() * 0.5);
            if (rp.pitchMode == 0) rp.pitchMode = 4; // drift
            rp.pitchPerRep = juce::jmax (0.05, rp.pitchPerRep * 0.15 + 0.05);
            break;

        case skf::BUFFERUR:
            rp.repeats = 2 + rng.nextInt (3);
            rp.sliceMaxS = juce::jlimit (0.10, 0.50, rp.sliceMaxS + 0.15);
            rp.sliceMinS = juce::jmin (rp.sliceMinS, rp.sliceMaxS);
            rp.bufferSilence = 0.40 + rng.nextDouble() * 0.45;
            rp.flavor = 3;
            break;

        case skf::MP3CRPT:
            rp.flavor = 1;
            rp.sliceMinS = juce::jlimit (0.040, 0.220, rp.sliceMinS);
            rp.sliceMaxS = juce::jlimit (rp.sliceMinS, 0.220, rp.sliceMaxS);
            rp.repeats = 2 + rng.nextInt (7);
            break;

        case skf::TAPEDO:
            rp.flavor = 2;
            rp.tapeReverse = rng.nextFloat() < 0.35f;
            rp.repeats = 2 + rng.nextInt (5);
            if (rp.volEnv == 0) rp.volEnv = 4; // ducked
            rp.sliceMinS = juce::jlimit (0.060, 0.320, rp.sliceMinS);
            rp.sliceMaxS = juce::jlimit (rp.sliceMinS, 0.320, rp.sliceMaxS);
            break;

        case skf::RATCHET:
            rp.sliceMinS = beatS / sr / 32.0;
            rp.sliceMaxS = beatS / sr / 8.0;
            rp.repeats   = 6 + rng.nextInt (20);
            if (rp.pitchMode == 0) rp.pitchMode = 1;                 // ascending
            if (rp.pitchPerRep == 0.0) rp.pitchPerRep = 0.5 + rng.nextDouble();
            if (rp.volEnv == 0) rp.volEnv = 1;                       // decay
            rp.timewarp = juce::jlimit (-1.0, 1.0, rp.timewarp + 0.4);
            rp.endMode = 0;
            break;

        case skf::RECSKIP:
            // "record skip": a clean loop, no mangling -- one grid unit of audio
            // repeated verbatim, which is what a needle riding a locked groove does
            rp.sliceMinS = rp.sliceMaxS = juce::jlimit (0.02, 4.0, gridSamples / sr);
            rp.endMode = 0;
            skf::enforceRecordSkipVerbatim (rp);
            break;

        case skf::GATESTUT:
            rp.gate = true;
            // clamped: the grid now reaches 32 bars, but a "gate chop" that long
            // stops being a gate chop (and outruns the rolling buffer)
            rp.sliceMinS = rp.sliceMaxS = juce::jlimit (0.01, 2.0, gridSamples / sr);
            rp.repeats = 4 + rng.nextInt (13);
            rp.pitchMode = 0; rp.endMode = 0;
            break;
        default: break;
    }

    // Groove Lock: bend repeat count so the burst length lands on the grid
    if (cachedParam (P::grooveLock) > 0.5f && e != skf::STICK && gridSamples > 1.0)
    {
        const double avg = (rp.sliceMinS + rp.sliceMaxS) * 0.5 * sr;
        if (avg > 1.0)
        {
            const double burst = rp.repeats * avg;
            const double snapped = juce::jmax (gridSamples, std::round (burst / gridSamples) * gridSamples);
            rp.repeats = juce::jlimit (2, 128, (int) std::round (snapped / avg));
        }
    }
}

int SkipfiendAudioProcessor::fireEngine (int e, long long anchorAbs, double bpm,
                                         double gridSamples, int repOverride, int code,
                                         bool randomiseFeel, double burstCycleSamples,
                                         int burstSlices, int layer)
{
    if (e < 0 || e >= skf::NUM_ENGINES) return -1;

    skf::RepeatParams rp;
    configureEngine (e, rp, bpm, gridSamples);
    if (repOverride > 0) rp.repeats = repOverride;

    if (randomiseFeel)
    {
        // black-key mode: every hit gets its own slice length, pitch behaviour,
        // time-warp, envelope, pan walk and ending -- not just a random engine
        rp.sliceMinS     = 0.010 + rng.nextDouble() * 0.220;
        rp.sliceMaxS     = rp.sliceMinS + rng.nextDouble() * 0.220;
        rp.lenMode       = rng.nextInt (4);
        rp.pitchMode     = rng.nextInt (5);
        rp.pitchPerRep   = (rng.nextDouble() * 2.0 - 1.0) * 7.0;
        rp.basePitchSemi = (rng.nextDouble() * 2.0 - 1.0) * 12.0;
        rp.timewarp      = rng.nextDouble() * 2.0 - 1.0;
        rp.volEnv        = rng.nextInt (5);
        rp.panWalk       = rng.nextInt (4);
        rp.endMode       = rng.nextInt (5);
        rp.playMode      = 1 + rng.nextInt (5);
        rp.motion        = 0.35 + rng.nextDouble() * 0.65;
        rp.wet           = 0.65 + rng.nextDouble() * 0.35;
        rp.randomised    = true;
        if (repOverride <= 0) rp.repeats = 2 + rng.nextInt (14);
    }

    if (burstCycleSamples > 0.0)
    {
        // A held key/button plays a COMPLETE loop before anything else happens on
        // it. Two independent controls shape that loop:
        //   - the engine's RATE knob sets HOW LONG the loop is (1x..32x bars)
        //   - the octave sets how many slices that loop is chopped into, so
        //     higher octaves stutter the same span more finely
        // Applied after the random pass so black keys stay random in character but
        // still play whole, coherent loops.
        const int slices = juce::jlimit (1, 64, burstSlices > 0 ? burstSlices : 1);
        rp.repeats   = slices;
        const double sliceS = (burstCycleSamples / (double) slices) / sr;
        rp.sliceMinS = rp.sliceMaxS = juce::jlimit (0.002, 70.0, sliceS);
        rp.lenMode   = 0;            // fixed-length slices, so the loop is even
        rp.loopBurst = true;         // replay the whole loop, seamlessly

        // A sustained loop must hold its level. Several engines force a decaying
        // or ducked envelope (RATCHET, TAPE DROPOUT) which is right for a one-shot
        // burst but makes every repeat of a held loop quieter than the last.
        rp.volEnv = 0;               // flat

        // per-slice stall depth for BUFFER UNDERRUN inside a loop
        if (rp.bufferSilence > 0.0) rp.bufferSilence = juce::jlimit (0.15, 0.45, rp.bufferSilence);
    }

    const float chaos = cachedParam (P::chaos);
    if (chaos > 0.0f)
    {
        rp.repeats  = juce::jmax (1, (int) std::round (rp.repeats
                        * (1.0 + (rng.nextDouble() * 2.0 - 1.0) * 0.4 * chaos)));
        rp.timewarp = juce::jlimit (-1.0, 1.0, rp.timewarp
                        + (rng.nextDouble() * 2.0 - 1.0) * 0.5 * chaos);
        if (rng.nextFloat() < chaos * 0.25f) rp.pitchMode = rng.nextInt (5);
        if (rng.nextFloat() < chaos * 0.20f) rp.lenMode   = rng.nextInt (4);
    }

    const double beatS  = 60.0 / juce::jmax (20.0, bpm) * sr;
    const double sliceS = rp.sliceMaxS * sr;
    long long anchor;
    switch (e)
    {
        case skf::HARDSKIP:
        {
            static const double iv[4] = { 0.25, 0.75, 1.0, 4.0 };
            const double off = iv[rng.nextInt (4)] * beatS;
            anchor = anchorAbs - (long long) off - (long long) sliceS;
            break;
        }
        case skf::STICK:    anchor = anchorAbs - (long long) (sliceS * 2.0); break;
        case skf::BUFFERUR: anchor = anchorAbs - (long long) sliceS;         break;
        default: anchor = anchorAbs - (long long) (sliceS + rng.nextDouble() * sliceS); break;
    }

    // a looping burst captures the last N bars, the way a DJ loop button does,
    // rather than the small per-engine look-back
    if (burstCycleSamples > 0.0)
    {
        anchor = anchorAbs - (long long) burstCycleSamples;
        // when this loop lets go, the track carries on from the END of the looped
        // region -- which is the moment the loop was struck
        lastLoopResumePos = anchorAbs;
    }

    const long long minA = roll.now() - (long long) roll.len + 4096;
    anchor = juce::jlimit (minA, roll.now() - 8, anchor);

    const int vi = allocVoiceIndex();
    voices[(size_t) vi].start (anchor, rp, e, sr);
    voiceLayer[(size_t) vi] = layer;
    pushFlash (e, e == skf::TAPEDO && rp.tapeReverse);
    logEvent (e, code, rp);
    lastEngineFired.store (e);

    // record for Skip-to-MIDI without allocating on the audio thread
    const double ppq = lastPpq + (double) (anchorAbs - (roll.now())) / beatS;
    const int eventIndex = recordedCount.load (std::memory_order_relaxed);
    if (eventIndex < kMaxRecordedEvents)
    {
        recorded[(size_t) eventIndex] = { juce::jmax (0.0, ppq), e, rp.repeats };
        recordedCount.store (eventIndex + 1, std::memory_order_release);
    }

    float cl = chaosLog.load();
    chaosLog.store (juce::jlimit (0.0f, 1.0f, cl + 0.18f));
    return vi;
}

void SkipfiendAudioProcessor::pushFlash (int engine, bool reverseHint)
{
    // find the stalest flash slot
    int idx = 0; float oldest = -1.0f;
    for (int i = 0; i < (int) flashes.size(); ++i)
    {
        const float a = flashes[i].age.load();
        if (a > oldest) { oldest = a; idx = i; }
    }
    flashes[idx].engine.store (engine);
    flashes[idx].reverse.store (reverseHint);
    flashes[idx].age.store (0.0f);
}

void SkipfiendAudioProcessor::logEvent (int engine, int code, const skf::RepeatParams& rp)
{
    const juce::uint32 a = ((juce::uint32) (engine & 0xff) << 24)
                         | ((juce::uint32) (rp.repeats & 0xffff) << 8)
                         | (juce::uint32) (code & 0xff);

    const int sliceMs = juce::jlimit (0, 4095, (int) std::lround ((rp.sliceMinS + rp.sliceMaxS) * 0.5 * 1000.0));
    const juce::uint32 b = ((juce::uint32) sliceMs & 0xfffu)
                         | ((juce::uint32) (rp.pitchMode & 0x7) << 12)
                         | ((juce::uint32) (rp.lenMode   & 0x7) << 15)
                         | ((juce::uint32) (rp.endMode   & 0x7) << 18)
                         | ((juce::uint32) (rp.flavor    & 0x7) << 21)
                         | ((juce::uint32) (rp.gate ? 1u : 0u)  << 24)
                         | ((juce::uint32) (juce::jlimit (0, 15, (int) std::lround ((rp.timewarp + 1.0) * 7.5))) << 25);

    const int h = logHead.fetch_add (1) & 63;
    logRing[(size_t) h].a.store (a);
    logRing[(size_t) h].b.store (b);
}

//==============================================================================
int SkipfiendAudioProcessor::parameterIndexForId (const juce::String& paramId) const
{
    const auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]))
            if (wp->paramID == paramId)
                return i;
    return -1;
}

juce::String SkipfiendAudioProcessor::parameterIdForIndex (int index) const
{
    const auto& params = getParameters();
    if (index < 0 || index >= params.size())
        return {};
    if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[index]))
        return wp->paramID;
    return {};
}

void SkipfiendAudioProcessor::handleMidiCC (int cc, float v01)
{
    if (cc < 0 || cc >= 128)
        return;

    if (midiLearnActive.load (std::memory_order_acquire))
    {
        const int target = midiLearnTargetIndex.load (std::memory_order_relaxed);
        if (target < 0)
        {
            midiLearnActive.store (false, std::memory_order_release);
            return;
        }

        // One parameter owns at most one CC mapping. Fixed-size atomics keep
        // this learning path deterministic and allocation-free on the audio thread.
        for (auto& mapping : ccToParamIndex)
            if (mapping.load (std::memory_order_relaxed) == target)
                mapping.store (-1, std::memory_order_relaxed);

        ccToParamIndex[(size_t) cc].store (target, std::memory_order_release);
        midiLearnActive.store (false, std::memory_order_release);
        return;
    }

    const int target = ccToParamIndex[(size_t) cc].load (std::memory_order_acquire);
    const auto& params = getParameters();
    if (target >= 0 && target < params.size())
        params[target]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01));
}

void SkipfiendAudioProcessor::startMidiLearn (const juce::String& paramId)
{
    const int index = parameterIndexForId (paramId);
    midiLearnTargetIndex.store (index, std::memory_order_release);
    midiLearnActive.store (index >= 0, std::memory_order_release);
}

juce::String SkipfiendAudioProcessor::getMidiLearnTargetId() const
{
    return parameterIdForIndex (midiLearnTargetIndex.load (std::memory_order_acquire));
}

void SkipfiendAudioProcessor::clearMidiMapping (const juce::String& paramId)
{
    const int index = parameterIndexForId (paramId);
    if (index < 0) return;
    for (auto& mapping : ccToParamIndex)
        if (mapping.load (std::memory_order_relaxed) == index)
            mapping.store (-1, std::memory_order_release);
}

int SkipfiendAudioProcessor::getMappedCcFor (const juce::String& paramId) const
{
    const int index = parameterIndexForId (paramId);
    if (index < 0) return -1;
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParamIndex[(size_t) cc].load (std::memory_order_acquire) == index)
            return cc;
    return -1;
}

//==============================================================================
bool SkipfiendAudioProcessor::isSupportedAudioFile (const juce::File& f)
{
    static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".caf" };
    return exts.contains (f.getFileExtension().toLowerCase());
}

void SkipfiendAudioProcessor::loadSampleFile (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    auto* rawReader = formatManager.createReaderFor (f);
    if (rawReader == nullptr) return;

    // Loading source material must not alter the user's current effect design.
    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (rawReader, true);
    newSource->setLooping (sampleLoop.load());

    const double length = (rawReader->lengthInSamples > 0 && rawReader->sampleRate > 0.0)
        ? (double) rawReader->lengthInSamples / rawReader->sampleRate : 0.0;
    const int nCh = (int) juce::jmin<juce::int64> (2, juce::jmax<juce::int64> (1, rawReader->numChannels));

    transport.stop();
    transport.setSource (newSource.get(), 32768, &readAheadThread, rawReader->sampleRate, nCh);
    readerSource = std::move (newSource);

    // FULL BUFFER: size the capture buffer to hold the entire track, so loops
    // never age out of it and the playhead can lag arbitrarily far behind.
    // Capped at 5 minutes -- stereo float at 48 kHz is ~23 MB/minute.
    if (fullTrackBuffer.load() && length > 1.0)
    {
        const double want = juce::jlimit (64.0, 300.0, length + 5.0);
        if (want > bufferedSeconds.load() + 0.5)
        {
            // resizing the buffer the audio thread reads from: hold the callback
            // lock so we can't pull it out from under a processBlock in flight
            const juce::ScopedLock sl (getCallbackLock());
            roll.prepare (sr, 2, want);
            bufferedSeconds.store (want);
            playheadInit = false;
        }
    }

    { const juce::ScopedLock sl (sampleNameLock); sampleName = f.getFileName(); }
    sampleLengthSecs.store (length);
    sampleLoaded.store (true);
    useSample.store (true);
    transport.setPosition (0.0);
    transport.start();
    samplePlaying.store (true);
}

void SkipfiendAudioProcessor::setSamplePlaying (bool shouldPlay)
{
    if (! sampleLoaded.load()) return;
    if (shouldPlay)
    {
        if (! transport.isPlaying() && transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.001)
            transport.setPosition (0.0);
        transport.start();
    }
    else transport.stop();
    samplePlaying.store (shouldPlay);
}

void SkipfiendAudioProcessor::setSampleLooping (bool shouldLoop)
{
    sampleLoop.store (shouldLoop);
    if (readerSource) readerSource->setLooping (shouldLoop);
}

double SkipfiendAudioProcessor::getSamplePositionSeconds() const
{
    return sampleLoaded.load() ? transport.getCurrentPosition() : 0.0;
}

//==============================================================================
void SkipfiendAudioProcessor::randomizeSkipParams()
{
    juce::Random r;
    r.setSeedRandomly();
    auto setNorm = [this] (const char* id, float norm)
    { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm)); };
    auto setNative = [this] (const char* id, float native)
    { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (native)); };

    int numOn = 0;
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const bool on = r.nextFloat() < 0.42f;
        setNative (P::en (e).toRawUTF8(), on ? 1.0f : 0.0f);
        if (on) ++numOn;
        setNorm (P::amt (e).toRawUTF8(),  0.35f + r.nextFloat() * 0.65f);
        setNorm (P::prob (e).toRawUTF8(), 0.25f + r.nextFloat() * 0.75f);
    }
    if (numOn == 0) setNative (P::en (skf::CDSKIP).toRawUTF8(), 1.0f);

    setNative (P::repMin, (float) (2 + r.nextInt (6)));
    setNative (P::repMax, (float) (8 + r.nextInt (40)));
    setNorm   (P::sliceMin, r.nextFloat() * 0.4f);
    setNorm   (P::sliceMax, 0.2f + r.nextFloat() * 0.6f);
    setNative (P::lenMode,   (float) r.nextInt (4));
    setNative (P::pitchMode, (float) r.nextInt (5));
    setNorm   (P::pitchPerSkip, r.nextFloat());
    setNorm   (P::basePitch,    0.3f + r.nextFloat() * 0.4f);
    setNorm   (P::timewarp,     r.nextFloat());
    setNative (P::volEnv,  (float) r.nextInt (5));
    setNative (P::panWalk, (float) r.nextInt (4));
    setNative (P::endMode, (float) r.nextInt (5));
    setNative (P::playMode, (float) r.nextInt (6));
    setNorm   (P::motion, 0.2f + r.nextFloat() * 0.8f);

    setNorm (P::density,   0.2f + r.nextFloat() * 0.6f);
    setNorm (P::chaos,     r.nextFloat() * 0.7f);
    setNorm (P::swing,     r.nextFloat() * 0.5f);
    setNorm (P::artifacts, 0.2f + r.nextFloat() * 0.6f);
    // keep randomisation in the audibly-active range (1/2 .. 1/32T) -- landing on
    // "32 bars" would leave the effect firing once a phrase, which reads as broken
    setNative (P::grid, (float) (6 + r.nextInt (9)));
}

void SkipfiendAudioProcessor::resetAllToDefaults()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());

    for (int i = 0; i < kSeqSteps; ++i)
    {
        seqEngine[i].store  (i % 4 == 0 ? (int) skf::CDSKIP : -1);
        seqRepeats[i].store (8);
    }

    numHeldKeys.store (0);
    manualTriggerHeld.store (false);
    randomTriggerEngaged.store (false);
    for (int i = 0; i < NUM_OVERLAYS; ++i) overlayHeld[(size_t) i].store (false);
    resetTapTempo();

    // drop any accumulated loop lag and re-join real time
    playheadInit = false;
    timeSuspended = false;
}

void SkipfiendAudioProcessor::setOverlayHeld (int overlayId, bool held)
{
    if (overlayId < 0 || overlayId >= NUM_OVERLAYS) return;
    overlayHeld[(size_t) overlayId].store (held);
}

void SkipfiendAudioProcessor::resetTapTempo()
{
    const juce::ScopedLock sl (tapLock);
    tapTimes.clear();
}

void SkipfiendAudioProcessor::tapTempo()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    double avgMs = 0.0;
    {
        const juce::ScopedLock sl (tapLock);
        // a long pause means you're starting a new count-in
        if (! tapTimes.empty() && nowMs - tapTimes.back() > 2500.0) tapTimes.clear();
        tapTimes.push_back (nowMs);
        if (tapTimes.size() > 8) tapTimes.erase (tapTimes.begin());
        if (tapTimes.size() < 2) return;

        double sum = 0.0;
        for (size_t i = 1; i < tapTimes.size(); ++i) sum += tapTimes[i] - tapTimes[i - 1];
        avgMs = sum / (double) (tapTimes.size() - 1);
    }

    if (avgMs < 150.0 || avgMs > 3000.0) return;          // 20 .. 400 bpm sanity

    const double tapped = juce::jlimit (20.0, 300.0, 60000.0 / avgMs);
    if (auto* p = apvts.getParameter (P::manualBpm))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) tapped));

    // tapping a tempo means you want that tempo, not the host's
    if (auto* s = apvts.getParameter (P::bpmSync))
        s->setValueNotifyingHost (0.0f);
}

void SkipfiendAudioProcessor::setManualTrigger (bool held)
{
    if (held == manualTriggerHeld.load()) return;

    if (auto* mp = apvts.getParameter (P::mix))
    {
        if (held) { savedMixBeforeManualTrigger = mp->getValue(); mp->setValueNotifyingHost (1.0f); }
        else      { mp->setValueNotifyingHost (savedMixBeforeManualTrigger); }
    }
    manualTriggerHeld.store (held);
}

void SkipfiendAudioProcessor::engageRandomTrigger()
{
    if (auto* mp = apvts.getParameter (P::mix))
    {
        if (! randomTriggerEngaged.load()) savedMixBeforeRandomTrigger = mp->getValue();
        randomizeSkipParams();
        mp->setValueNotifyingHost (1.0f);        // slam to full wet
    }
    randomTriggerEngaged.store (true);
}

void SkipfiendAudioProcessor::releaseRandomTrigger()
{
    if (! randomTriggerEngaged.load()) return;
    randomTriggerEngaged.store (false);
    if (auto* mp = apvts.getParameter (P::mix))
        mp->setValueNotifyingHost (savedMixBeforeRandomTrigger);
}

juce::StringArray SkipfiendAudioProcessor::getFactoryPresetNames()
{
    return { "Init / Clean", "CD Skip Classic", "Hard Skip Chop", "Vinyl Stick",
             "Buffer Panic", "Ratchet Fill", "Trip-Hop Tail", "Gate Chopper", "Chaos Storm" };
}

void SkipfiendAudioProcessor::loadFactoryPreset (int index)
{
    auto setNorm = [this] (const char* id, float norm)
    { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm)); };
    auto setNative = [this] (const char* id, float native)
    { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (native)); };
    auto engines = [&] (std::initializer_list<int> on)
    {
        for (int e = 0; e < skf::NUM_ENGINES; ++e)
        {
            bool isOn = false;
            for (int o : on) if (o == e) isOn = true;
            setNative (P::en (e).toRawUTF8(),   isOn ? 1.0f : 0.0f);
            setNorm   (P::amt (e).toRawUTF8(),  0.8f);
            setNorm   (P::prob (e).toRawUTF8(), 0.7f);
        }
    };

    switch (index)
    {
        case 0: // Init / Clean
            engines ({ skf::CDSKIP });
            setNorm ("density", 0.2f); setNorm ("chaos", 0.05f); setNorm ("mix", 1.0f);
            setNative (P::grid, 11.0f); setNorm ("swing", 0.0f); setNative (P::grooveLock, 0.0f);
            setNorm ("artifacts", 0.3f);
            setNative (P::repMin, 3.0f); setNative (P::repMax, 10.0f);
            setNorm ("sliceMin", 0.06f); setNorm ("sliceMax", 0.22f);
            setNative (P::lenMode, 0.0f); setNative (P::pitchMode, 0.0f); setNative (P::volEnv, 0.0f);
            setNative (P::panWalk, 0.0f); setNative (P::endMode, 3.0f);
            setNorm ("pitchPerSkip", 0.5f); setNorm ("basePitch", 0.5f); setNorm ("timewarp", 0.5f);
            break;
        case 1: // CD Skip Classic
            engines ({ skf::CDSKIP, skf::STICK });
            setNorm ("density", 0.45f); setNorm ("chaos", 0.15f);
            setNative (P::grid, 11.0f); setNorm ("artifacts", 0.6f);
            setNative (P::repMin, 3.0f); setNative (P::repMax, 14.0f);
            setNative (P::endMode, 3.0f);
            break;
        case 2: // Hard Skip Chop
            engines ({ skf::HARDSKIP, skf::GATESTUT });
            setNorm ("density", 0.5f); setNorm ("chaos", 0.3f);
            setNative (P::grid, 10.0f); setNative (P::endMode, 0.0f);
            break;
        case 3: // Vinyl Stick
            engines ({ skf::STICK, skf::TAPEDO });
            setNorm ("density", 0.3f); setNorm ("chaos", 0.1f);
            setNative (P::grooveLock, 1.0f); setNorm ("artifacts", 0.7f);
            setNative (P::pitchMode, 4.0f); setNorm ("pitchPerSkip", 0.15f);
            break;
        case 4: // Buffer Panic
            engines ({ skf::BUFFERUR, skf::MP3CRPT });
            setNorm ("density", 0.55f); setNorm ("chaos", 0.4f);
            setNative (P::grid, 11.0f); setNorm ("artifacts", 0.8f);
            break;
        case 5: // Ratchet Fill
            engines ({ skf::RATCHET });
            setNorm ("density", 0.7f); setNorm ("chaos", 0.2f);
            setNative (P::grid, 13.0f); setNative (P::pitchMode, 1.0f); setNorm ("pitchPerSkip", 0.6f);
            setNative (P::endMode, 0.0f);
            break;
        case 6: // Trip-Hop Tail
            engines ({ skf::STICK, skf::CDSKIP });
            setNorm ("density", 0.22f); setNorm ("chaos", 0.15f);
            setNative (P::repMin, 3.0f); setNative (P::repMax, 5.0f); setNative (P::endMode, 1.0f);
            setNative (P::grid, 8.0f);
            break;
        case 7: // Gate Chopper
            engines ({ skf::GATESTUT });
            setNorm ("density", 0.8f); setNorm ("chaos", 0.1f);
            setNative (P::grid, 12.0f); setNative (P::grooveLock, 1.0f);
            break;
        case 8: // Chaos Storm
            engines ({ skf::CDSKIP, skf::HARDSKIP, skf::STICK, skf::BUFFERUR,
                       skf::MP3CRPT, skf::TAPEDO, skf::RATCHET, skf::GATESTUT });
            setNorm ("density", 0.75f); setNorm ("chaos", 0.85f);
            setNorm ("artifacts", 0.7f);
            break;
        default: break;
    }
}

void SkipfiendAudioProcessor::randomizeAll()
{
    if (hasRandomised) resetAllToDefaults();
    randomizeSkipParams();
    hasRandomised = true;
}

//==============================================================================
//  A/B compare
//==============================================================================
void SkipfiendAudioProcessor::storeToSlot (int slot)
{
    if (slot < 0 || slot > 1) return;
    juce::MemoryBlock mb;
    getStateInformation (mb);
    abSlot[slot] = mb;
}

void SkipfiendAudioProcessor::recallSlot (int slot)
{
    if (slot < 0 || slot > 1 || abSlot[slot].getSize() == 0) return;
    setStateInformation (abSlot[slot].getData(), (int) abSlot[slot].getSize());
}

void SkipfiendAudioProcessor::setCurrentSlot (int slot)
{
    if (slot < 0 || slot > 1 || slot == currentSlot) return;
    storeToSlot (currentSlot);          // park whatever we are leaving
    currentSlot = slot;
    if (abSlot[slot].getSize() > 0) recallSlot (slot);
    else                            storeToSlot (slot);   // first visit: B starts as a copy of A
}

void SkipfiendAudioProcessor::copyCurrentSlotToOther()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    abSlot[1 - currentSlot] = mb;
}

//==============================================================================
double SkipfiendAudioProcessor::getExportableSeconds() const noexcept
{
    const double fs = fxRoll.sr();
    if (fs <= 0.0) return 0.0;
    const long long avail = juce::jmin (fxRoll.now(), (long long) fxRoll.len - 8);
    return juce::jmax (0.0, (double) avail / fs);
}

bool SkipfiendAudioProcessor::exportAudioToWav (const juce::File& f, double seconds, int bitDepth)
{
    // Reads the effect-output ring from the message thread while the audio
    // thread keeps writing to it. Worst case the oldest few samples of the
    // export are a block behind -- acceptable for a "print what I just played"
    // convenience, and far cheaper than snapshotting 64 s inside processBlock.
    const double fs = fxRoll.sr();
    if (fs <= 0.0) return false;

    const long long avail = juce::jmin (fxRoll.now(), (long long) fxRoll.len - 8);
    if (avail <= 0) return false;

    long long want = seconds > 0.0 ? (long long) (seconds * fs) : avail;
    want = juce::jlimit<long long> (1, avail, want);
    const long long start = fxRoll.now() - want;

    juce::AudioBuffer<float> out (2, (int) want);
    for (int c = 0; c < 2; ++c)
    {
        auto* d = out.getWritePointer (c);
        for (long long i = 0; i < want; ++i)
            d[(int) i] = fxRoll.readAbs ((double) (start + i), c);
    }

    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (
        wav.createWriterFor (os.get(), fs, 2,
                             (bitDepth == 16 || bitDepth == 32) ? bitDepth : 24, {}, 0));
    if (w == nullptr) return false;
    os.release();                       // the writer owns the stream from here

    return w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
}

void SkipfiendAudioProcessor::savePresetToFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    f.replaceWithData (mb.getData(), mb.getSize());
}

void SkipfiendAudioProcessor::loadPresetFromFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (f.loadFileAsData (mb))
        setStateInformation (mb.getData(), (int) mb.getSize());
}

//==============================================================================
void SkipfiendAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n     = buffer.getNumSamples();
    const int nch   = buffer.getNumChannels();
    // Clamp to the buffer we were actually handed, not just the negotiated bus
    // count: a host (or AudioProcessorGraph) can pass a narrower buffer, and
    // reading channel 1 out of a mono buffer is a crash, not a glitch.
    const int mainCh = juce::jmin (2, getMainBusNumOutputChannels(), buffer.getNumChannels());

    // clear any output channels beyond what we use
    for (int c = mainCh; c < nch; ++c) buffer.clear (c, 0, n);

    if (isEffectBypassed())
    {
        float pk = 0.0f;
        for (int c = 0; c < mainCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
        outPeakL.store (pk); outPeakR.store (pk);
        outGainReduction.store (0.0f);
        activeEngineMask.store (0);
        // bypass passes the live signal, so the playhead re-syncs on the way out
        playheadInit = false;
        timeSuspended = false;
        playheadLagSeconds.store (0.0);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isController()) handleMidiCC (m.getControllerNumber(), m.getControllerValue() / 127.0f);
        }
        return;
    }

    // ---- transport --------------------------------------------------------
    // BPM comes from the host when Sync is on and the host actually reports one;
    // otherwise the manual BPM knob drives it (DJ / standalone use with no DAW clock).
    const bool  wantHostSync = cachedParam (P::bpmSync) > 0.5f;
    const float manualBpmVal = cachedParam (P::manualBpm);
    double bpm = juce::jlimit (20.0f, 300.0f, manualBpmVal);
    bool   gotHostBpm = false;
    bool   playing = false;
    double ppqStart = phaseSamples / juce::jmax (1.0, (60.0 / bpm * sr) / 1.0);
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                if (wantHostSync && *b > 0.0) { bpm = *b; gotHostBpm = true; }
            if (auto p = pos->getPpqPosition())  ppqStart = *p;
            playing = pos->getIsPlaying();
        }
    }
    currentBpm.store (bpm);
    followingHostBpm.store (gotHostBpm);
    const double beatS = 60.0 / juce::jmax (20.0, bpm) * sr;
    if (! playing) ppqStart = phaseSamples / beatS;
    lastPpq = ppqStart;

    const int gridIdx     = (int) cachedParam (P::grid);
    const double gBeats   = gridBeatsFor (gridIdx);
    const double gridS    = gBeats * beatS;
    const float  swing    = cachedParam (P::swing);
    const bool   seqOn    = cachedParam (P::seqOn) > 0.5f;
    const bool   scOn     = cachedParam (P::scTrigger) > 0.5f;
    const float  density  = cachedParam (P::density);
    const float  chaos    = cachedParam (P::chaos);
    const float  mix      = cachedParam (P::mix);
    const float  artAmt   = cachedParam (P::artifacts);

    // ---- test-sample deck: replaces the incoming buffer when armed --------
    if (useSample.load() && sampleLoaded.load())
    {
        juce::AudioBuffer<float> sBuf (juce::jmax (1, mainCh), n);
        juce::AudioSourceChannelInfo info (&sBuf, 0, n);
        transport.getNextAudioBlock (info);
        const float g = sampleGain.load();
        for (int c = 0; c < mainCh; ++c)
        {
            buffer.clear (c, 0, n);
            buffer.addFrom (c, 0, sBuf, juce::jmin (c, sBuf.getNumChannels() - 1), 0, n, g);
        }
        if (samplePlaying.load() && ! transport.isPlaying())
            samplePlaying.store (false);
    }

    // ---- feed the rolling buffer with dry input -------------------------
    {
        juce::AudioBuffer<float> dryIn (buffer.getArrayOfWritePointers(), mainCh, n);
        roll.push (dryIn);
    }
    const long long baseNow = roll.now() - n;

    // ---- MIDI: polyphonic hold-to-perform + always-on CC -> param map -------
    // Every held key runs its own effect simultaneously. Which effect comes from
    // the note; how fast it retriggers comes from the octave (1x/4x/8x/16x/32x);
    // black keys go fully random instead of using a fixed preset.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const bool isNoteOff = m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0);

        if (m.isNoteOn() && m.getVelocity() > 0)
        {
            const int note = m.getNoteNumber();
            int nHeld = numHeldKeys.load();

            // mirror the incoming note into the on-screen data feed
            lastMidiNote.store (note);
            lastMidiVel.store (m.getVelocity());
            lastMidiChan.store (m.getChannel());
            midiNoteOns.fetch_add (1);

            // first key down grabs the mix and slams full wet
            if (nHeld == 0)
                if (auto* mp = apvts.getParameter (P::mix))
                {
                    savedMixBeforeMidiHold = mp->getValue();
                    mp->setValueNotifyingHost (1.0f);
                }

            // already held? retrigger it rather than adding a duplicate
            int slot = -1;
            for (int i = 0; i < nHeld; ++i) if (heldKeys[i].note == note) { slot = i; break; }
            if (slot < 0 && nHeld < kMaxHeldKeys) slot = nHeld++;
            if (slot >= 0)
            {
                static const double kRates[] = { 1.0, 4.0, 8.0, 16.0, 32.0 };
                const int octave = juce::jlimit (0, 4, (note / 12) - 2);
                const int pc     = note % 12;
                const bool black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);

                auto& k = heldKeys[(size_t) slot];
                k.note       = note;
                k.rateMult   = kRates[octave];
                k.randomMode = black;
                k.pressPpq   = ppqStart + (double) juce::jlimit (0, n - 1, meta.samplePosition) / beatS;
                k.voiceIndex = -1;
                k.started    = false;
                // press order = position in the effects chain: the first key
                // mangles the incoming audio, later keys mangle its output
                k.layer      = juce::jmin (slot, 1);
                lastMidiWasBlack.store (black);
                lastMidiOctave.store (octave);

                if (black)
                {
                    // Black key: a completely random failure. Deliberately picks
                    // from ALL engines, not just the enabled ones -- otherwise it
                    // just replays whatever single engine happens to be switched on.
                    k.engine = rng.nextInt (skf::NUM_ENGINES);
                }
                else
                {
                    // white key: the note picks one of the factory effects
                    k.engine = (note / 2 + pc) % skf::NUM_ENGINES;
                }
                numHeldKeys.store (nHeld);
            }
        }
        else if (isNoteOff)
        {
            const int note = m.getNoteNumber();
            int nHeld = numHeldKeys.load();
            for (int i = 0; i < nHeld; ++i)
                if (heldKeys[i].note == note)
                {
                    // release only this key's own voice -- other held keys keep cycling
                    const int vi = heldKeys[(size_t) i].voiceIndex;
                    if (vi >= 0 && vi < (int) voices.size()) voices[(size_t) vi].releaseNow();
                    for (int j = i; j < nHeld - 1; ++j) heldKeys[(size_t) j] = heldKeys[(size_t) j + 1];
                    --nHeld;
                    break;
                }
            numHeldKeys.store (juce::jmax (0, nHeld));

            if (nHeld <= 0)
                if (auto* mp = apvts.getParameter (P::mix))
                    mp->setValueNotifyingHost (savedMixBeforeMidiHold);
        }
        else if (m.isController())
        {
            lastMidiCC.store (m.getControllerNumber());
            lastMidiCCVal.store (m.getControllerValue());
            lastMidiChan.store (m.getChannel());
            midiCCs.fetch_add (1);
            handleMidiCC (m.getControllerNumber(), m.getControllerValue() / 127.0f);
        }
    }

    // ---- the gate: nothing fires unless a key or a trigger button is held ----
    const bool gateOpen = isGateOpen();
    if (gateWasOpen && ! gateOpen)
        for (auto& v : voices) v.releaseNow();   // let go = fade out fast, no click
    gateWasOpen = gateOpen;

    // Everything below schedules against the musical timeline (PPQ), not against
    // the moment a key went down, so every fire lands on the beat grid for the
    // tempo in use -- that's what keeps stacked keys and overlays in sync.
    const double blockBeats = (double) n / beatS;
    const double ppqEnd     = ppqStart + blockBeats;
    const double quantBeats = juce::jmin (gBeats, 1.0);   // never wait more than a beat

    auto fireAtPpq = [&] (double firePpq, int engine, int repOverride, int code,
                          bool randomiseFeel = false, double burstCycleSamples = 0.0,
                          int burstSlices = 0, int layer = 0)
    {
        const double off = juce::jlimit (0.0, (double) juce::jmax (0, n - 1),
                                          (firePpq - ppqStart) * beatS);
        return fireEngine (engine, baseNow + (long long) off, bpm, gridS,
                           repOverride, code, randomiseFeel, burstCycleSamples,
                           burstSlices, layer);
    };

    // ---- held MIDI keys: one complete, self-looping cycle per key -----------
    // A key fires ONE burst that plays all its repeats and then replays itself.
    // Nothing else fires on that key until its cycle is done, so you always hear
    // the full 8x (or 2x, or 32x) of a press. Different keys still run in parallel.
    {
        const int nHeld = numHeldKeys.load();

        // a bar is 4 beats; the loop can't be longer than the audio we actually hold
        const double barSamples = 4.0 * beatS;
        const double maxCycle   = (double) roll.len - 8192.0;

        for (int i = 0; i < nHeld; ++i)
        {
            auto& k = heldKeys[(size_t) i];
            const int slices = juce::jlimit (1, 64, (int) std::lround (k.rateMult));

            // still cycling? retune it live from the knobs, but let it finish.
            // This is what makes a DJ turning the RATE knob widen or tighten the
            // loop that is already playing instead of only the next one.
            if (k.voiceIndex >= 0 && k.voiceIndex < (int) voices.size())
            {
                auto& v = voices[(size_t) k.voiceIndex];
                if (v.isActive() && v.p.loopBurst && ! v.burstFinished())
                {
                    const double cyc = juce::jmin (maxCycle, engineLoopBarsFor (v.engine()) * barSamples);
                    v.p.repeats   = slices;
                    v.p.sliceMinS = v.p.sliceMaxS =
                        juce::jlimit (0.002, 70.0, (cyc / (double) slices) / sr);

                    // Repeat Engine / Master knobs apply to what is already playing
                    applyLiveParamsToVoice (v, chaos);

                    // Hold a loop long enough and the audio it captured falls off
                    // the back of the rolling buffer -- reads then clamp to the
                    // oldest sample and the loop quietly decays to nothing. Grab a
                    // fresh region instead of letting it wither.
                    const long long minSafe = roll.now() - (long long) roll.len + 8192;
                    if (v.sliceStartAbs < minSafe)
                        v.sliceStartAbs = roll.now() - (long long) cyc;
                    continue;
                }
            }

            // the first cycle of a press starts on the grid; a cycle that has run
            // its course restarts immediately so there's no gap mid-hold
            double firePpq = ppqStart;
            if (! k.started)
            {
                const double stepBeats = juce::jmax (0.015625, gBeats);
                const double q = std::ceil (k.pressPpq / stepBeats - 1.0e-9) * stepBeats;
                if (q > ppqEnd) continue;                  // quantise point not reached yet
                firePpq = juce::jmax (q, ppqStart);
            }

            int e = k.engine;
            if (k.randomMode && rng.nextFloat() < 0.7f)
                e = rng.nextInt (skf::NUM_ENGINES);        // black key re-rolls per cycle

            const double cyc = juce::jmin (maxCycle, engineLoopBarsFor (e) * barSamples);
            const int vi = fireAtPpq (firePpq, e, -1, 3, k.randomMode,
                                       cyc, slices, k.layer);
            k.voiceIndex = vi;
            k.started = true;
        }
    }

    // ---- TRIGGER / RANDOM TRIGGER: also one complete, self-looping cycle ----
    const bool buttonHeld = manualTriggerHeld.load() || randomTriggerEngaged.load();
    if (! buttonHeld) { triggerStarted = false; triggerVoiceIndex = -1; }

    if (buttonHeld && ! seqOn && ! scOn)
    {
        bool busy = false;
        if (triggerVoiceIndex >= 0 && triggerVoiceIndex < (int) voices.size())
        {
            auto& v = voices[(size_t) triggerVoiceIndex];
            busy = v.isActive() && v.p.loopBurst && ! v.burstFinished();
            if (busy)
            {
                applyLiveParamsToVoice (v, chaos);
                const double cyc = juce::jmin ((double) roll.len - 8192.0,
                                                engineLoopBarsFor (v.engine()) * 4.0 * beatS);
                v.p.sliceMinS = v.p.sliceMaxS = juce::jlimit (0.002, 70.0, cyc / sr);
                const long long minSafe = roll.now() - (long long) roll.len + 8192;
                if (v.sliceStartAbs < minSafe) v.sliceStartAbs = roll.now() - (long long) cyc;
            }
        }

        if (! busy)
        {
            double firePpq = ppqStart;
            if (! triggerStarted)
            {
                const double q = std::ceil (ppqStart / quantBeats - 1.0e-9) * quantBeats;
                if (q <= ppqEnd) firePpq = juce::jmax (q, ppqStart);
                else firePpq = -1.0;
            }

            if (firePpq >= 0.0)
            {
                int e = pickEngineWeighted (rng);
                if (e < 0) e = skf::CDSKIP;
                if (rng.nextFloat() < chaos * 0.5f)
                { const int r = randomEnabledEngine (rng); if (r >= 0) e = r; }

                // the button uses the chosen engine's own loop length, same as a key
                const double cyc = juce::jmin ((double) roll.len - 8192.0,
                                                engineLoopBarsFor (e) * 4.0 * beatS);
                triggerVoiceIndex = fireAtPpq (firePpq, e, -1, 0, false, cyc, 1, 0);
                triggerStarted = true;
            }
        }
    }

    // ---- Skip Language sequencer, while a button is held --------------------
    // (the sequencer is explicitly step-based, so it stays step-driven)
    if (buttonHeld && seqOn && ! scOn)
    {
        const double posStart = ppqStart / gBeats;
        const double posEnd   = posStart + (double) n / gridS;
        const int k0 = (int) std::ceil  (posStart - 1.0e-9);
        const int k1 = (int) std::floor (posEnd   - 1.0e-9);
        for (int k = k0; k <= k1; ++k)
        {
            double so = (k - posStart) * gridS;
            if ((k & 1) != 0) so += swing * 0.5 * gridS;
            if (so < 0.0 || so >= n) continue;
            const long long stepNow = baseNow + (long long) so;
            const int stepIdx = ((k % kSeqSteps) + kSeqSteps) % kSeqSteps;

            if (seqOn)
            {
                const int se = seqEngine[stepIdx].load();
                if (se >= 0)
                    fireEngine (se, stepNow, bpm, gridS, juce::jmax (2, seqRepeats[stepIdx].load()), 1);
                continue;
            }

            // Density sets how much of the grid gets hit while held; at full
            // density every step fires, so a hold reads as continuous stutter.
            float pr = std::pow (juce::jlimit (0.0f, 1.0f, density), 1.2f);
            pr = juce::jlimit (0.25f, 1.0f, pr + 0.25f);
            if (rng.nextFloat() < pr)
            {
                int e = pickEngineWeighted (rng);
                if (e >= 0)
                {
                    int cd = 0;
                    if (rng.nextFloat() < chaos * 0.5f) { int r = randomEnabledEngine (rng); if (r >= 0) { e = r; cd = 4; } }
                    fireEngine (e, stepNow, bpm, gridS, -1, cd);
                }
            }
        }
    }

    // ---- sidechain transient re-triggering (also gated) -----------------
    if (gateOpen && scOn && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        const float thr = juce::Decibels::decibelsToGain (cachedParam (P::scThresh));
        const float* s0 = scBuf.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float x = std::abs (s0[i]);
            scEnv = juce::jmax (x, scEnv * 0.999f);
            if (scHold > 0) --scHold;
            if (x > thr && x > scPrev * 1.6f && scHold == 0)
            {
                int e = pickEngineWeighted (rng);
                if (e < 0) e = skf::CDSKIP;
                fireEngine (e, baseNow + i, bpm, gridS, -1, 2);
                scHold = (int) (sr * 0.06);
            }
            scPrev = x;
        }
    }

    // ---- overlay effects: arm on press, engage on the next quantise point ----
    // (so ECHO/DELAY/DUB/REVERSE also land on the beat instead of wherever your
    //  finger happened to hit the button)
    {
        const double nextQuant = std::ceil (ppqStart / quantBeats - 1.0e-9) * quantBeats;
        for (int o = 0; o < NUM_OVERLAYS; ++o)
        {
            const bool held = overlayHeld[(size_t) o].load();
            if (held && ! overlayActive[(size_t) o] && ! overlayArmed[(size_t) o])
            {
                overlayArmed[(size_t) o]  = true;
                overlayArmPpq[(size_t) o] = nextQuant;
            }
            if (overlayArmed[(size_t) o] && ppqEnd >= overlayArmPpq[(size_t) o])
            {
                overlayArmed[(size_t) o]  = false;
                overlayActive[(size_t) o] = true;

                if (o == OV_REVERSE)
                {
                    static const double kRevBars[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
                    const int ri = juce::jlimit (0, 5, (int) cachedParam (P::revBars));
                    double win = kRevBars[ri] * 4.0 * beatS;                 // bars -> samples
                    win = juce::jmin (win, (double) fxRoll.len - 8192.0);    // clamp to what we hold
                    reverser.start (fxRoll.now(), win);                      // reverse the EFFECT output
                }
            }
            if (! held && overlayActive[(size_t) o])
            {
                overlayActive[(size_t) o] = false;
                overlayArmed[(size_t) o]  = false;
                if (o == OV_REVERSE) reverser.stop();
            }
        }
    }

    static const double kOvRates[] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
    const double echoDel  = beatS / kOvRates[juce::jlimit (0, 4, (int) cachedParam (P::echoRate))];
    const double delayDel = beatS / kOvRates[juce::jlimit (0, 4, (int) cachedParam (P::delayRate))];
    const double dubDel   = beatS / kOvRates[juce::jlimit (0, 4, (int) cachedParam (P::dubRate))];
    const bool echoFeed  = overlayActive[OV_ECHO];
    const bool delayFeed = overlayActive[OV_DELAY];
    const bool dubFeed   = overlayActive[OV_DUB];

    // ---- RUIN coefficients, recomputed once per block ----------------------
    const bool ruinOn = cachedParam (P::ruinOn) > 0.5f;
    if (ruinOn)
    {
        const float amt  = juce::jlimit (0.0f, 1.0f, cachedParam (P::ruinAmt));
        const float rate = juce::jlimit (0.0f, 1.0f, cachedParam (P::ruinRate));
        const float tone = juce::jlimit (0.0f, 1.0f, cachedParam (P::ruinTone));

        // hold rate: 200Hz at 0 up to the full sample rate at 1
        const double holdHz = 200.0 * std::pow (sr / 200.0, (double) rate);
        ruinStep   = (float) juce::jlimit (0.0, 1.0, holdHz / juce::jmax (1.0, sr));
        // 2 bits of resolution at full amount, 16 at none
        ruinLevels = std::pow (2.0f, juce::jmap (amt, 16.0f, 2.0f)) * 0.5f;
        ruinCoeff  = (float) juce::jlimit (0.002, 0.9, 0.02 + 0.5 * (double) tone);
        ruinTilt   = juce::jmap (tone, 0.0f, 1.0f, 0.15f, 1.6f);
        ruinMix    = juce::jlimit (0.0f, 1.0f, 0.25f + 0.75f * amt);
    }

    // ---- render voices + artifacts, crossfade dry/wet, overlays, limit -----
    float* L = buffer.getWritePointer (0);
    float* R = mainCh > 1 ? buffer.getWritePointer (1) : nullptr;

    float blockPeakL = 0.0f, blockPeakR = 0.0f;

    if (! playheadInit) { playheadPos = baseNow - 1; playheadInit = true; }

    for (int i = 0; i < n; ++i)
    {
        const long long nowS = baseNow + i;

        // the live input, kept for level-matching (L[i] gets overwritten below)
        const float liveL = L[i];
        const float liveR = R ? R[i] : L[i];

        // keep the playhead inside the audio we still hold
        playheadPos = juce::jlimit (nowS - (long long) roll.len + 4096, nowS - 1, playheadPos);

        // The medium plays from the playhead, not from the live input -- this is
        // what lets a loop suspend the track instead of it running on underneath.
        // While time is suspended the playhead is parked, so reading it would hand
        // back the same frozen sample over and over (a DC thump); the loop is the
        // medium during that window, so the base path is silent instead.
        const float dl = timeSuspended ? 0.0f : roll.readAbs ((double) playheadPos, 0);
        const float dr = timeSuspended ? 0.0f : roll.readAbs ((double) playheadPos, 1);

        // ---- stage 1: keys that process the incoming audio ----
        float s1l = 0.0f, s1r = 0.0f;
        bool any1 = false;
        bool loopRunning = false;
        for (size_t vi = 0; vi < voices.size(); ++vi)
            if (voices[vi].isActive() && voiceLayer[vi] == 0)
            { voices[vi].render (roll, &s1l, &s1r); any1 = true; }

        // whatever stage 1 produced (or the dry signal if it's idle) becomes the
        // material the chained keys work on
        const float chainL = any1 ? s1l : dl;
        const float chainR = any1 ? s1r : dr;
        chainRoll.pushSample (chainL, chainR);

        // ---- stage 2: keys layered on top, chewing stage 1's output ----
        float s2l = 0.0f, s2r = 0.0f;
        bool any2 = false;
        for (size_t vi = 0; vi < voices.size(); ++vi)
            if (voices[vi].isActive() && voiceLayer[vi] != 0)
            { voices[vi].render (chainRoll, &s2l, &s2r); any2 = true; }

        float wl = any2 ? s2l : s1l;
        float wr = any2 ? s2r : s1r;
        const bool any = any1 || any2;

        // is a loop holding time still?
        for (size_t vi = 0; vi < voices.size(); ++vi)
            if (voices[vi].isActive() && voices[vi].p.loopBurst) { loopRunning = true; break; }

        if (loopRunning)
        {
            timeSuspended = true;             // park: the track stops advancing
            pendingResumePos = lastLoopResumePos;
        }
        else
        {
            if (timeSuspended)
            {
                // loop let go: carry on from where the looped region ended
                timeSuspended = false;
                playheadPos = juce::jlimit (nowS - (long long) roll.len + 4096,
                                             nowS - 1, pendingResumePos);
            }
            ++playheadPos;
        }

        // ---- level-match the effect to the source -------------------------
        // The engines slice, window and pan their material, which lands well below
        // the level of the track they were cut from -- and drifts as the cycle
        // goes on. Track both levels and scale the wet so a loop plays back at the
        // same loudness as the sample itself.
        const float srcMag = std::abs (0.5f * (liveL + liveR));
        const float wetMag = std::abs (0.5f * (wl + wr));
        srcRmsEnv += 0.0004f * (srcMag - srcRmsEnv);
        // only measure the wet while something is actually sounding, otherwise the
        // gaps between cycles drag the average down and the gain keeps climbing
        if (any) wetRmsEnv += 0.0004f * (wetMag - wetRmsEnv);

        if (any)
        {
            float wantGain = normGain;
            if (wetRmsEnv > 2.0e-4f && srcRmsEnv > 2.0e-4f)
                wantGain = juce::jlimit (0.5f, 4.0f, srcRmsEnv / wetRmsEnv);
            normGain += 0.0002f * (wantGain - normGain);   // ~100 ms, steady not pumping
            wl *= normGain;
            wr *= normGain;
        }

        artifacts.render (&wl, &wr);

        const float target = any ? 1.0f : 0.0f;
        actSmooth += 0.02f * (target - actSmooth);
        const float a = actSmooth * mix;

        float ol  = dl * (1.0f - a) + wl * mix;
        float orr = dr * (1.0f - a) + wr * mix;

        // ---- overlay chain, appended to the END of whatever is playing -------
        // Captured before the overlays so REVERSE chews on the finished effect
        // output (and can't feed back on itself).
        fxRoll.pushSample (ol, orr);

        if (reverser.isRunning())
        {
            float rl = ol, rr = orr;
            reverser.render (fxRoll, &rl, &rr, &ol, &orr);
            ol = rl; orr = rr;
        }

        // each stage feeds the next, so stacking them extends the chain and all
        // four can run at once
        echoLine .process (ol, orr, &ol, &orr, echoDel,  0.45f, 0.70f, 0.45f, echoFeed);
        delayLine.process (ol, orr, &ol, &orr, delayDel, 0.34f, 0.90f, 0.40f, delayFeed);
        dubLine  .process (ol, orr, &ol, &orr, dubDel,   0.72f, 0.22f, 0.45f, dubFeed);

        // ---- RUIN (hidden): sample-and-hold decimation, bit quantisation and
        //      a one-pole tone tilt. Sits last, so it ruins everything above it.
        if (ruinOn)
        {
            ruinPhase += ruinStep;

            if (ruinPhase >= 1.0f)
            {
                ruinPhase -= std::floor (ruinPhase);
                ruinHoldL = ol;
                ruinHoldR = orr;
            }

            float rl = ruinHoldL, rr = ruinHoldR;

            if (ruinLevels > 1.0f)
            {
                rl = std::round (rl * ruinLevels) / ruinLevels;
                rr = std::round (rr * ruinLevels) / ruinLevels;
            }

            ruinLpL += ruinCoeff * (rl - ruinLpL);
            ruinLpR += ruinCoeff * (rr - ruinLpR);
            rl = ruinLpL + (rl - ruinLpL) * ruinTilt;
            rr = ruinLpR + (rr - ruinLpR) * ruinTilt;

            ol  = ol  + (rl - ol)  * ruinMix;
            orr = orr + (rr - orr) * ruinMix;
        }

        limiter.process (ol, orr);

        L[i] = ol;
        if (R) R[i] = orr;

        blockPeakL = juce::jmax (blockPeakL, std::abs (ol));
        blockPeakR = juce::jmax (blockPeakR, R ? std::abs (orr) : std::abs (ol));
    }

    outPeakL.store (blockPeakL);
    outPeakR.store (blockPeakR);
    outGainReduction.store (limiter.gr);

    juce::uint32 mask = 0;
    int liveVoices = 0;
    for (auto& v : voices) if (v.isActive()) { mask |= (1u << (unsigned) v.engine()); ++liveVoices; }
    activeEngineMask.store (mask);

    // ---- telemetry for the rolling data feed ----
    activeVoices.store (liveVoices);
    ppqNow.store (ppqStart);
    lastGridIdx.store (gridIdx);
    blocksProcessed.fetch_add (1);
    // What the ring actually holds, not how long we have been running: now() is
    // the absolute write position and grows forever, but the buffer only keeps
    // its last len samples. Clamped here so every consumer agrees.
    {
        const double elapsed  = (double) roll.now() / juce::jmax (1.0, sr);
        const double capacity = (double) roll.len   / juce::jmax (1.0, sr);
        bufferSeconds.store (juce::jmin (elapsed, capacity));
    }
    playheadLagSeconds.store ((double) (roll.now() - playheadPos) / juce::jmax (1.0, sr));
    {
        float ip = 0.0f;
        for (int c = 0; c < mainCh; ++c) ip = juce::jmax (ip, buffer.getMagnitude (c, 0, n));
        inPeak.store (ip);
    }

    // ---- post: fire recovery artifacts for voices that just ended -------
    for (auto& v : voices)
        if (v.justEnded())
        {
            const int type = (v.p.flavor == 2) ? 3 : (v.p.flavor == 3) ? 2
                            : (v.p.endMode == 3) ? 1 : 0;
            artifacts.trigger (type, artAmt);
        }

    // ---- telemetry ------------------------------------------------------
    bool anyActive = false;
    for (auto& v : voices) anyActive |= v.isActive();
    playJitter.store (chaos * 0.6f + (anyActive ? 0.4f : 0.0f));
    chaosLog.store (chaosLog.load() * 0.9f);

    if (! playing) phaseSamples += n;
    globalSample += n;

}

//==============================================================================
juce::AudioProcessorEditor* SkipfiendAudioProcessor::createEditor()
{
    return new SkipfiendAudioProcessorEditor (*this);
}

void SkipfiendAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    auto seq = juce::ValueTree ("SEQ");
    for (int i = 0; i < kSeqSteps; ++i)
    {
        seq.setProperty ("e" + juce::String (i), seqEngine[i].load(), nullptr);
        seq.setProperty ("r" + juce::String (i), seqRepeats[i].load(), nullptr);
    }
    state.appendChild (seq, nullptr);

    auto midiTree = juce::ValueTree ("MIDILEARN");
    int mappingCount = 0;
    for (int cc = 0; cc < 128; ++cc)
    {
        const int paramIndex = ccToParamIndex[(size_t) cc].load (std::memory_order_acquire);
        const auto pid = parameterIdForIndex (paramIndex);
        if (paramIndex >= 0 && pid.isNotEmpty())
        {
            midiTree.setProperty ("cc" + juce::String (mappingCount), cc, nullptr);
            midiTree.setProperty ("id" + juce::String (mappingCount), pid, nullptr);
            ++mappingCount;
        }
    }
    midiTree.setProperty ("count", mappingCount, nullptr);
    state.appendChild (midiTree, nullptr);

    auto ui = juce::ValueTree ("UI");
    ui.setProperty ("tooltips", tooltipsEnabled.load(), nullptr);
    state.appendChild (ui, nullptr);

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SkipfiendAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
        {
            auto seq = tree.getChildWithName ("SEQ");
            if (seq.isValid())
            {
                for (int i = 0; i < kSeqSteps; ++i)
                {
                    const int engine = (int) seq.getProperty ("e" + juce::String (i), seqEngine[i].load());
                    const int repeats = (int) seq.getProperty ("r" + juce::String (i), 8);
                    seqEngine[i].store  (juce::jlimit (-1, (int) skf::NUM_ENGINES - 1, engine));
                    seqRepeats[i].store (juce::jlimit (1, 128, repeats));
                }
                tree.removeChild (seq, nullptr);
            }

            auto midiTree = tree.getChildWithName ("MIDILEARN");
            if (midiTree.isValid())
            {
                for (auto& mapping : ccToParamIndex) mapping.store (-1, std::memory_order_release);
                const int count = (int) midiTree.getProperty ("count", 0);
                for (int i = 0; i < count; ++i)
                {
                    const int cc = (int) midiTree.getProperty ("cc" + juce::String (i), -1);
                    const juce::String pid = midiTree.getProperty ("id" + juce::String (i), "").toString();
                    const int paramIndex = parameterIndexForId (pid);
                    if (cc >= 0 && cc < 128 && paramIndex >= 0)
                        ccToParamIndex[(size_t) cc].store (paramIndex, std::memory_order_release);
                }
                tree.removeChild (midiTree, nullptr);
            }

            auto ui = tree.getChildWithName ("UI");
            if (ui.isValid())
            {
                tooltipsEnabled.store ((bool) ui.getProperty ("tooltips", true));
                tree.removeChild (ui, nullptr);
            }

            apvts.replaceState (tree);
        }
    }
}

//==============================================================================
static juce::File skipfiendDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("SKIPFIEND");
    d.createDirectory();
    return d;
}

void SkipfiendAudioProcessor::doCapture()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    const auto ts = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto f = skipfiendDir().getChildFile ("capture_" + ts + ".skipfiend");
    f.replaceWithData (mb.getData(), mb.getSize());
}

void SkipfiendAudioProcessor::doMidiExport()
{
    juce::MidiMessageSequence seq;
    const int ticksPerQuarter = 960;
    const int count = juce::jlimit (0, kMaxRecordedEvents,
                                    recordedCount.load (std::memory_order_acquire));
    for (int i = 0; i < count; ++i)
    {
        const auto ev = recorded[(size_t) i];
        const int note = 36 + ev.engine;                       // C1 + engine index
        const double t0 = ev.ppq * ticksPerQuarter;
        const double t1 = t0 + juce::jmax (60.0, ev.repeats * 30.0);
        seq.addEvent (juce::MidiMessage::noteOn  (1, note, (juce::uint8) 100), t0);
        seq.addEvent (juce::MidiMessage::noteOff (1, note),                    t1);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ticksPerQuarter);
    mf.addTrack (seq);

    const auto ts = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto f = skipfiendDir().getChildFile ("skips_" + ts + ".mid");
    if (auto os = f.createOutputStream()) { os->setPosition (0); mf.writeTo (*os); }
}

//==============================================================================
//  Diagnostics: the troubleshooting file, the crash log, and the hard reset.
//  Nothing here runs on the audio thread, and nothing here runs unless asked.
//==============================================================================
juce::File SkipfiendAudioProcessor::getDiagnosticsDir()
{
    auto d = skipfiendDir().getChildFile ("Diagnostics");
    d.createDirectory();
    return d;
}

juce::File SkipfiendAudioProcessor::getCrashLogFile() const
{
    return crashLogFile;
}

juce::String SkipfiendAudioProcessor::buildTroubleshootingReport()
{
    juce::String r;
    r << "SKIPFIEND TROUBLESHOOTING REPORT" << juce::newLine
      << "================================" << juce::newLine
      << "Generated:      " << juce::Time::getCurrentTime().toString (true, true) << juce::newLine
      << "Plugin version: " << SKIPFIEND_VERSION << juce::newLine
      << juce::newLine;

    juce::PluginHostType host;
    r << "HOST / ENVIRONMENT" << juce::newLine
      << "------------------" << juce::newLine
      << "Format:         " << juce::AudioProcessor::getWrapperTypeDescription (wrapperType) << juce::newLine
      << "Host:           " << host.getHostDescription() << juce::newLine
      << "OS:             " << juce::SystemStats::getOperatingSystemName() << juce::newLine
      << "CPU:            " << juce::SystemStats::getCpuModel()
                            << "  (" << juce::SystemStats::getNumCpus() << " cores)" << juce::newLine
      << "RAM:            " << juce::SystemStats::getMemorySizeInMegabytes() << " MB" << juce::newLine
      << juce::newLine;

    r << "AUDIO" << juce::newLine
      << "-----" << juce::newLine
      << "Sample rate:    " << getSampleRate() << " Hz" << juce::newLine
      << "Block size:     " << getBlockSize() << " samples" << juce::newLine
      << "Channels:       in " << getTotalNumInputChannels()
                     << " / out " << getTotalNumOutputChannels() << juce::newLine
      << "Tempo:          " << currentBpm.load()
                            << (followingHostBpm.load() ? " BPM (from host)" : " BPM (manual)") << juce::newLine
      << "Buffered:       " << bufferedSeconds.load() << " s" << juce::newLine
      << "Blocks seen:    " << (unsigned long) blocksProcessed.load() << juce::newLine
      << juce::newLine;

    r << "MIDI" << juce::newLine
      << "----" << juce::newLine
      << "Note ons:       " << (unsigned long) midiNoteOns.load() << juce::newLine
      << "CC messages:    " << (unsigned long) midiCCs.load() << juce::newLine
      << "Held keys:      " << numHeldKeys.load() << juce::newLine;
    {
        int mappings = 0;
        for (int cc = 0; cc < 128; ++cc)
            if (ccToParamIndex[(size_t) cc].load (std::memory_order_acquire) >= 0)
                ++mappings;
        r << "CC mappings:    " << mappings << juce::newLine;

        for (int cc = 0; cc < 128; ++cc)
        {
            const int paramIndex = ccToParamIndex[(size_t) cc].load (std::memory_order_acquire);
            if (paramIndex >= 0)
                r << "                CC " << cc << "  ->  "
                  << parameterIdForIndex (paramIndex) << juce::newLine;
        }
    }
    r << juce::newLine;

    r << "SAMPLE DECK" << juce::newLine
      << "-----------" << juce::newLine
      << "Loaded:         " << (sampleLoaded.load() ? getSampleName() : juce::String ("(none)")) << juce::newLine
      << "Using sample:   " << (useSample.load() ? "yes" : "no") << juce::newLine
      << "Playing / loop: " << (samplePlaying.load() ? "yes" : "no")
                            << " / " << (sampleLoop.load() ? "yes" : "no") << juce::newLine
      << "Length:         " << sampleLengthSecs.load() << " s" << juce::newLine
      << juce::newLine;

    r << "SETTINGS" << juce::newLine
      << "--------" << juce::newLine;

    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            r << wp->paramID.paddedRight (' ', 22) << wp->getCurrentValueAsText() << juce::newLine;

    r << juce::newLine << "SEQUENCER" << juce::newLine << "---------" << juce::newLine;

    for (int i = 0; i < kSeqSteps; ++i)
        r << "step " << juce::String (i + 1).paddedLeft ('0', 2)
          << "  engine " << seqEngine[i].load()
          << "  repeats " << seqRepeats[i].load() << juce::newLine;

    r << juce::newLine << "SELF CHECK" << juce::newLine << "----------" << juce::newLine;
    int warnings = 0;
    const auto warn = [&] (const juce::String& w) { r << "  [!] " << w << juce::newLine; ++warnings; };

    if (getSampleRate() <= 0.0)
        warn ("Sample rate is zero - the host has not prepared the plugin yet.");

    if (getTotalNumInputChannels() == 0)
        warn ("No input channels: an insert effect with no input will be silent.");

    if (isEffectBypassed())
        warn ("BYPASS is on, so the effect is passing audio through untouched.");

    if (blocksProcessed.load() == 0)
        warn ("No audio blocks processed yet - check the track is actually playing.");

    if (! isGateOpen())
        r << "  [i] Gate is closed. The effect only runs while a MIDI note or TRIGGER"
             " is held - this is normal at rest." << juce::newLine;

    if (sampleLoaded.load() && ! useSample.load())
        r << "  [i] A sample is loaded but USE SAMPLE is off, so it is not being heard."
          << juce::newLine;

    if (warnings == 0)
        r << "  No problems detected." << juce::newLine;

    r << juce::newLine << "LICENCE" << juce::newLine << "-------" << juce::newLine
      << "  One seat per user. See LICENCE.txt beside the installed plugin." << juce::newLine
      << "  Support: " << SKIPFIEND_SUPPORT_CONTACT << juce::newLine
      << "  Homepage: " << SKIPFIEND_HOMEPAGE << juce::newLine;

    return r;
}

bool SkipfiendAudioProcessor::exportTroubleshootingFile (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (buildTroubleshootingReport());
}

// The live window: the values that move, rather than the settings that sit still.
juce::String SkipfiendAudioProcessor::getLiveDebugDump()
{
    juce::String r;
    const auto db = [] (float lin)
    {
        return lin > 1.0e-5f ? juce::String (juce::Decibels::gainToDecibels (lin), 1)
                             : juce::String ("-inf");
    };

    r << "TRANSPORT   ppq "    << juce::String (ppqNow.load(), 3)
      << "   bpm "             << juce::String (currentBpm.load(), 2)
      << (followingHostBpm.load() ? " (host)" : " (manual)")
      << "   blocks "          << (unsigned long) blocksProcessed.load() << juce::newLine
      << "LEVELS      in "     << db (inPeak.load())
      << "   outL "            << db (outPeakL.load())
      << "   outR "            << db (outPeakR.load())
      << "   gr "              << juce::String (outGainReduction.load(), 3) << juce::newLine
      << "GATE        "        << (isGateOpen() ? "OPEN" : "closed")
      << "   keys "            << numHeldKeys.load()
      << "   trigger "         << (manualTriggerHeld.load() ? "held" : "-")
      << "   random "          << (randomTriggerEngaged.load() ? "engaged" : "-") << juce::newLine
      << "VOICES      active " << activeVoices.load()
      << "   mask 0x"          << juce::String::toHexString ((int) activeEngineMask.load())
      << "   last engine "     << lastEngineFired.load()
      << "   jitter "          << juce::String (playJitter.load(), 4) << juce::newLine
      << "BUFFER      "        << juce::String (bufferedSeconds.load(), 2) << " s"
      << "   lag "             << juce::String (playheadLagSeconds.load(), 3) << " s"
      << "   grid "            << lastGridIdx.load()
      << "   chaos "           << juce::String (chaosLog.load(), 3) << juce::newLine
      << "MIDI        note "   << lastMidiNote.load()
      << " vel "               << lastMidiVel.load()
      << " ch "                << lastMidiChan.load()
      << "   cc "              << lastMidiCC.load()
      << " val "               << lastMidiCCVal.load()
      << "   oct "             << lastMidiOctave.load() << juce::newLine
      << "OVERLAYS    echo "   << (overlayHeld[OV_ECHO].load()    ? "on " : "-  ")
      << " delay "             << (overlayHeld[OV_DELAY].load()   ? "on " : "-  ")
      << " dub "               << (overlayHeld[OV_DUB].load()     ? "on " : "-  ")
      << " reverse "           << (overlayHeld[OV_REVERSE].load() ? "on"  : "-") << juce::newLine
      << juce::newLine
      << "EVENT RING (newest last)" << juce::newLine;

    const int head = logHead.load();

    for (int i = 0; i < (int) logRing.size(); ++i)
    {
        const int idx = (head + i) % (int) logRing.size();
        const juce::uint32 a = logRing[(size_t) idx].a.load();

        if (a == 0)
            continue;

        r << "  engine " << (int) ((a >> 24) & 0xff)
          << "  repeats " << (int) ((a >> 8) & 0xffff)
          << "  code " << (int) (a & 0xff)
          << "  [" << juce::String::toHexString ((int) logRing[(size_t) idx].b.load()) << "]"
          << juce::newLine;
    }

    return r;
}

// Off on every load, by design: this is a troubleshooting aid, not a feature
// that quietly writes files behind the user's back.
void SkipfiendAudioProcessor::setCrashLogEnabled (bool shouldLog)
{
    crashLogEnabled.store (shouldLog);

    if (! shouldLog)
        return;

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
    crashLogFile = getDiagnosticsDir().getChildFile ("skipfiend_crashlog_" + stamp + ".txt");

    // Lead with a full copy of the troubleshooting report, so this one file
    // tells support everything they need.
    juce::String header;
    header << buildTroubleshootingReport() << juce::newLine
           << "================================================================" << juce::newLine
           << "CRASH LOG STARTED  " << juce::Time::getCurrentTime().toString (true, true) << juce::newLine
           << "================================================================" << juce::newLine;

    crashLogFile.replaceWithText (header);

    static juce::File crashTarget;
    static bool handlerInstalled = false;
    crashTarget = crashLogFile;

    if (! handlerInstalled)
    {
        handlerInstalled = true;
        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            if (crashTarget.existsAsFile())
                crashTarget.appendText (juce::newLine
                                        + "*** CRASH DETECTED "
                                        + juce::Time::getCurrentTime().toString (true, true)
                                        + " ***" + juce::newLine
                                        + juce::SystemStats::getStackBacktrace() + juce::newLine);
        });
    }
}

// The destructive reset. Defaults plus cache, and nothing else: a user's presets
// and exports are their own work and are never touched here.
void SkipfiendAudioProcessor::hardResetAndClearCache()
{
    resetAllToDefaults();

    for (int i = 0; i < kSeqSteps; ++i)
    {
        seqEngine[i].store (-1);
        seqRepeats[i].store (8);
    }

    for (auto& mapping : ccToParamIndex)
        mapping.store (-1, std::memory_order_release);
    midiLearnTargetIndex.store (-1, std::memory_order_release);
    midiLearnActive.store (false, std::memory_order_release);

    abSlot[0].reset();
    abSlot[1].reset();
    currentSlot = 0;
    hasRandomised = false;

    tooltipsEnabled.store (true);
    crashLogEnabled.store (false);

    auto cache = skipfiendDir().getChildFile ("Cache");

    if (cache.isDirectory())
        cache.deleteRecursively();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkipfiendAudioProcessor();
}
