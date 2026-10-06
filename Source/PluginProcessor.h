#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <vector>
#include <map>
#include "Skipfiend.h"

// ============================================================================
//  SKIPFIEND  -  processor
// ============================================================================

class SkipfiendAudioProcessor : public juce::AudioProcessor
{
public:
    SkipfiendAudioProcessor();
    ~SkipfiendAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SKIPFIEND"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "SKIPFIEND"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- editor-facing state -------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kSeqSteps = 16;
    std::atomic<int>  seqEngine  [kSeqSteps];   // -1 = off, else EngineId
    std::atomic<int>  seqRepeats [kSeqSteps];

    // visual telemetry (lock-free-ish, editor polls on timer)
    struct Flash { std::atomic<float> age { 10.0f }; std::atomic<int> engine { 0 }; std::atomic<bool> reverse { false }; };
    std::array<Flash, 24> flashes;
    void pushFlash (int engine, bool reverseHint = false);

    std::atomic<float> playJitter { 0.0f };
    std::atomic<float> chaosLog   { 0.0f };   // rolling "error rate" for the log meter
    std::atomic<int>   lastEngineFired { -1 };
    std::atomic<juce::uint32> activeEngineMask { 0 };   // bit e = engine e has a live voice right now

    // output metering (post-limiter), read by the UI meter/glow
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };
    std::atomic<float> outGainReduction { 0.0f };

    // forensic error-log ring (RT-safe: packed ints, editor formats)
    // a = engine<<24 | repeats<<8 | code
    // b = slice ms | pitchMode | lenMode | endMode | flavor | gate
    struct LogEntry { std::atomic<juce::uint32> a { 0 }, b { 0 }; };
    std::array<LogEntry, 64> logRing;
    std::atomic<int> logHead { 0 };
    void logEvent (int engine, int code, const skf::RepeatParams& rp);

    // ---- rolling telemetry for the on-screen data feed --------------------
    std::atomic<double> ppqNow { 0.0 };
    std::atomic<int>    activeVoices { 0 };
    std::atomic<juce::uint32> blocksProcessed { 0 };
    std::atomic<float>  inPeak { 0.0f };
    std::atomic<int>    lastGridIdx { 0 };
    std::atomic<double> bufferSeconds { 0.0 };
    std::atomic<double> playheadLagSeconds { 0.0 };   // how far behind real time we are

    // ---- live MIDI activity, mirrored into the on-screen data feed ---------
    std::atomic<int> lastMidiNote { -1 }, lastMidiVel { 0 }, lastMidiChan { 0 };
    std::atomic<int> lastMidiCC { -1 }, lastMidiCCVal { 0 };
    std::atomic<juce::uint32> midiNoteOns { 0 }, midiCCs { 0 };
    std::atomic<bool> lastMidiWasBlack { false };
    std::atomic<int>  lastMidiOctave { 0 };

    const skf::RollingBuffer& rolling() const noexcept { return roll; }

    // one-shot file actions. These are called from UI/message-thread callbacks,
    // never from processBlock: state serialization and filesystem I/O are not RT-safe.
    void doCapture();
    void doMidiExport();

    // ---- test-sample deck (drag & drop / Load) --------------------------
    void loadSampleFile (const juce::File& f);
    bool isSampleLoaded() const noexcept   { return sampleLoaded.load(); }
    juce::String getSampleName() const     { const juce::ScopedLock sl (sampleNameLock); return sampleName; }
    void setSamplePlaying (bool shouldPlay);
    bool isSamplePlaying() const noexcept  { return samplePlaying.load(); }
    void setSampleLooping (bool shouldLoop);
    bool isSampleLooping() const noexcept  { return sampleLoop.load(); }
    void setUseSampleSource (bool useIt)   { useSample.store (useIt); }
    bool isUsingSampleSource() const noexcept { return useSample.load(); }
    // Buffer the WHOLE track on load instead of keeping a rolling ~64 s window.
    // Loops then never age out of the buffer and the playhead can lag as far
    // behind as you like. Costs memory proportional to track length.
    void setFullTrackBuffer (bool shouldBufferWholeTrack) { fullTrackBuffer.store (shouldBufferWholeTrack); }
    bool isFullTrackBuffer() const noexcept { return fullTrackBuffer.load(); }
    double getBufferedSeconds() const noexcept { return bufferedSeconds.load(); }

    void setSampleGain (float linGain)     { sampleGain.store (linGain); }
    float getSampleGain() const noexcept   { return sampleGain.load(); }
    double getSampleLengthSeconds() const noexcept { return sampleLengthSecs.load(); }
    double getSamplePositionSeconds() const;
    static bool isSupportedAudioFile (const juce::File& f);

    // ---- MIDI learn -------------------------------------------------------
    void startMidiLearn (const juce::String& paramId);
    void cancelMidiLearn()                 { midiLearnActive.store (false); }
    bool isMidiLearning() const noexcept   { return midiLearnActive.load(); }
    juce::String getMidiLearnTargetId() const;
    void clearMidiMapping (const juce::String& paramId);
    int  getMappedCcFor (const juce::String& paramId) const;

    // ---- randomize / presets ------------------------------------------
    void randomizeSkipParams();
    // The DICE button. Every press after the first wipes the board back to the
    // defaults first, so each roll is a genuinely fresh sound rather than a
    // drift away from the last one.
    void randomizeAll();
    static juce::StringArray getFactoryPresetNames();
    void loadFactoryPreset (int index);
    void savePresetToFile (const juce::File& f);
    void loadPresetFromFile (const juce::File& f);

    // ---- live performance -------------------------------------------------
    // The effect ONLY runs while the gate is open, and the gate is only opened
    // by a held MIDI note, the TRIGGER button, or the RANDOM TRIGGER button.
    void resetAllToDefaults();
    void engageRandomTrigger();     // randomise everything + engage full-wet performance override
    void releaseRandomTrigger();    // release performance override without changing MIX
    void setManualTrigger (bool held);   // the TRIGGER button
    bool isRandomTriggerEngaged() const noexcept { return randomTriggerEngaged.load(); }
    bool isManualTriggerHeld() const noexcept    { return manualTriggerHeld.load(); }
    bool isMidiHoldActive() const noexcept       { return numHeldKeys.load() > 0; }
    int  getNumHeldKeys() const noexcept         { return numHeldKeys.load(); }
    bool isGateOpen() const noexcept
    {
        return numHeldKeys.load() > 0 || manualTriggerHeld.load() || randomTriggerEngaged.load();
    }

    // ---- overlay effects (ECHO / DELAY / DUB / REVERSE), played over the top ----
    enum OverlayId { OV_ECHO = 0, OV_DELAY, OV_DUB, OV_REVERSE, NUM_OVERLAYS };
    void setOverlayHeld (int overlayId, bool held);
    bool isOverlayHeld (int overlayId) const noexcept
    {
        return overlayId >= 0 && overlayId < NUM_OVERLAYS && overlayHeld[overlayId].load();
    }

    // ---- tempo ------------------------------------------------------------
    void tapTempo();                 // call once per tap; sets the manual BPM
    void resetTapTempo();
    double getCurrentBpm() const noexcept        { return currentBpm.load(); }
    bool isFollowingHostBpm() const noexcept     { return followingHostBpm.load(); }

    // ---- bypass -----------------------------------------------------------
    bool isEffectBypassed() const;

    // ---- UI options (travel with the session state) ------------------------
    std::atomic<bool> tooltipsEnabled { true };

    // ---- A/B compare -------------------------------------------------------
    // Two full state snapshots. Switching slots parks the state you are leaving
    // in its own slot, so A/B is always a comparison of two live edits.
    void storeToSlot (int slot);          // 0 = A, 1 = B
    void recallSlot  (int slot);
    void setCurrentSlot (int slot);
    void copyCurrentSlotToOther();        // "A -> B"
    int  getCurrentSlot() const noexcept { return currentSlot; }

    // ---- diagnostics (help > debug) ---------------------------------------
    // The troubleshooting file is a light self-check plus every current setting;
    // the crash log is the heavyweight one and starts with a copy of it.
    juce::String buildTroubleshootingReport();
    bool exportTroubleshootingFile (const juce::File& f);
    juce::String getLiveDebugDump();          // raw state, polled by the debug window
    void setCrashLogEnabled (bool shouldLog); // OFF on every load, by design
    bool isCrashLogEnabled() const noexcept { return crashLogEnabled.load(); }
    juce::File getCrashLogFile() const;
    static juce::File getDiagnosticsDir();
    // Much more destructive than the RESET button: defaults + cache wiped.
    // Never touches Presets or Exports.
    void hardResetAndClearCache();

    // ---- audio export ------------------------------------------------------
    // Writes the tail of the finished effect output to a 24-bit wav.
    // seconds <= 0 exports everything currently held.
    // bitDepth: 16 or 24 for integer wav, 32 for float.
    bool exportAudioToWav (const juce::File& f, double seconds, int bitDepth = 24);
    double getExportableSeconds() const noexcept;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    void configureEngine (int engineId, skf::RepeatParams& rp, double bpm, double gridSamples);
    int  pickEngineWeighted (juce::Random&);
    int  randomEnabledEngine (juce::Random&);
    // returns the index of the voice it started, or -1.
    // burstCycleSamples > 0 makes this a self-looping cycle of exactly that
    // length, chopped into burstSlices pieces.
    int  fireEngine (int engineId, long long anchorAbs, double bpm, double gridSamples,
                     int repOverride, int code, bool randomiseFeel = false,
                     double burstCycleSamples = 0.0, int burstSlices = 0, int layer = 0);
    int  allocVoiceIndex();
    double engineLoopBarsFor (int engineId) const;   // that engine's RATE knob, in bars
    // push the Repeat Engine / Master knobs into a voice that is already running,
    // so turning a knob changes what you are hearing right now
    void applyLiveParamsToVoice (skf::RepeatVoice& v, float chaosAmt);
    void handleMidiCC (int ccNumber, float value01);

    float cachedParam (const char* id) const;

    skf::RollingBuffer roll;
    skf::RollingBuffer chainRoll;   // carries stage-1 output for layered keys
    skf::RollingBuffer fxRoll;      // the finished effect output, for REVERSE to chew on
    std::vector<skf::RepeatVoice> voices;
    std::vector<int> voiceLayer;    // parallel to voices: which chain stage each is on
    skf::RecoveryArtifacts artifacts;
    skf::SafetyLimiter limiter;

    juce::Random rng;
    double sr = 48000.0;
    double phaseSamples = 0.0;      // fallback transport when host isn't playing
    long long globalSample = 0;
    float actSmooth = 0.0f;         // wet-activity smoother for dry/wet crossfade
    // level-matching: keeps the effect playing back as loud as the source
    float srcRmsEnv = 0.0f, wetRmsEnv = 0.0f, normGain = 1.0f;

    // sidechain transient detector
    float scEnv = 0.0f, scPrev = 0.0f;
    int   scHold = 0;

    // recorded trigger events for Skip-to-MIDI. Fixed-capacity storage avoids
    // allocator activity on the audio thread and makes export snapshots safe:
    // an event is fully written before recordedCount is published.
    struct TrigEvt { double ppq = 0.0; int engine = 0; int repeats = 0; };
    static constexpr int kMaxRecordedEvents = 8000;
    std::array<TrigEvt, kMaxRecordedEvents> recorded {};
    std::atomic<int> recordedCount { 0 };
    double lastPpq = 0.0;

    // ---- test-sample deck --------------------------------------------------
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread { "SKIPFIEND sample reader" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::CriticalSection sampleNameLock;
    juce::String sampleName;
    std::atomic<bool>  sampleLoaded  { false };
    std::atomic<bool>  samplePlaying { false };
    std::atomic<bool>  useSample     { false };
    std::atomic<bool>  sampleLoop    { true };
    std::atomic<float> sampleGain    { 1.0f };
    std::atomic<double> sampleLengthSecs { 0.0 };
    std::atomic<bool>   fullTrackBuffer { false };
    std::atomic<double> bufferedSeconds { 64.0 };

    // ---- MIDI learn ---------------------------------------------------
    // Fixed-size, lock-free mapping used directly by processBlock.
    std::atomic<bool> midiLearnActive { false };
    std::atomic<int> midiLearnTargetIndex { -1 };
    std::array<std::atomic<int>, 128> ccToParamIndex {};
    int parameterIndexForId (const juce::String& paramId) const;
    juce::String parameterIdForIndex (int index) const;

    // ---- live performance state ------------------------------------------
    // Polyphonic held keys. Each key runs its own effect at its own rate, so
    // holding three keys plays three effects at once.
    struct HeldKey
    {
        int    note = -1;
        double rateMult = 1.0;      // octave -> 1x / 4x / 8x / 16x / 32x
        bool   randomMode = false;  // black keys: random engine + random feel
        double pressPpq = 0.0;      // used to quantise the first fire
        int    engine = 0;
        int    voiceIndex = -1;     // the voice this key owns while it cycles
        bool   started = false;
        int    layer = 0;           // 0 = processes the dry input, >0 = chained
    };
    static constexpr int kMaxHeldKeys = 12;
    std::array<HeldKey, kMaxHeldKeys> heldKeys;   // audio thread only
    std::atomic<int> numHeldKeys { 0 };

    std::atomic<bool> manualTriggerHeld { false };
    std::atomic<bool> randomTriggerEngaged { false };
    int  triggerVoiceIndex = -1;      // the button's own cycling voice
    bool triggerStarted = false;
    bool gateWasOpen = false;                     // audio thread only, edge detect

    // ---- overlay effects --------------------------------------------------
    std::array<std::atomic<bool>, NUM_OVERLAYS> overlayHeld { };
    std::array<bool, NUM_OVERLAYS> overlayArmed { };       // waiting for the quantise point
    std::array<double, NUM_OVERLAYS> overlayArmPpq { };
    std::array<bool, NUM_OVERLAYS> overlayActive { };      // actually running
    skf::TempoDelay echoLine, delayLine, dubLine;
    skf::ReverseLooper reverser;

    // ---- the playhead ------------------------------------------------------
    // The medium plays from here, not from the live input. While a loop is
    // running the playhead parks, so the track stops advancing underneath the
    // effect; when the loop lets go, playback resumes from the loop's end point
    // and stays that far behind real time -- exactly like a needle riding a
    // locked groove and then carrying on from where it left off.
    long long playheadPos = 0;
    bool      playheadInit = false;
    bool      timeSuspended = false;
    long long pendingResumePos = 0;
    long long lastLoopResumePos = 0;

    // ---- tap tempo --------------------------------------------------------
    juce::CriticalSection tapLock;
    std::vector<double> tapTimes;
    std::atomic<double> currentBpm { 120.0 };     // resolved tempo (host or manual), for the UI
    std::atomic<bool> followingHostBpm { false }; // whether the host is actually supplying it

    // ---- RUIN, the hidden effect (audio thread only) -----------------------
    float ruinPhase = 0.0f, ruinStep = 1.0f;
    float ruinHoldL = 0.0f, ruinHoldR = 0.0f;
    float ruinLpL = 0.0f, ruinLpR = 0.0f;
    float ruinLevels = 0.0f, ruinCoeff = 0.5f, ruinTilt = 1.0f, ruinMix = 0.0f;

    // ---- diagnostics -------------------------------------------------------
    std::atomic<bool> crashLogEnabled { false };
    juce::File crashLogFile;

    // ---- A/B compare + dice state (message thread only) --------------------
    juce::MemoryBlock abSlot[2];
    int  currentSlot = 0;
    bool hasRandomised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkipfiendAudioProcessor)
};
