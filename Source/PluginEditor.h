#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "PluginProcessor.h"

//==============================================================================
//  FiendAudio house look & feel.
//  Implements the shared visual identity spec: flat surfaces with subtle depth,
//  270-degree value arcs drawn outside the knob body, 4px-radius controls,
//  8px spacing grid, one re-tintable accent per plugin.
//==============================================================================
class FiendLNF : public juce::LookAndFeel_V4
{
public:
    FiendLNF();
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int,
                       juce::ComboBox&) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>,
                                           juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int, int) override;
};

//==============================================================================
//  CD-player style waveform display with jittering playhead, skip flashes,
//  and an audio-reactive glow tinted by whichever engine is currently firing.
//==============================================================================
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WaveformDisplay (SkipfiendAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    SkipfiendAudioProcessor& proc;
    juce::Path wavePath;
    float jitter = 0.0f, playX = 0.0f, glow = 0.0f;
    juce::StringArray log;      // verbose skip-event log, left column
    juce::StringArray feed;     // continuously rolling telemetry, right column
    int frame = 0;
    int feedTick = 0;
    // The telemetry column only scrolls while something is actually changing;
    // this is a hash of the live values so a silent, idle plugin sits still.
    juce::uint32 feedSig = 0;
    juce::Image ghost;      // phosphor-persistence layer; trail length tracks dry/wet

public:
    std::atomic<bool> dragHighlight { false };
};

//==============================================================================
class SkipfiendAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::FileDragAndDropTarget,
                                       private juce::Timer
{
public:
    explicit SkipfiendAudioProcessorEditor (SkipfiendAudioProcessor&);
    ~SkipfiendAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;
    void filesDropped   (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void paintCanvasContents (juce::Graphics&);
    void layoutCanvasContents();
    void showParamMenu (const juce::String& paramId, juce::Component* c);
    void refreshPresetCombo();
    // ---- MENU dropdown: save / save as / open / export / options ----------
    void showMainMenu();
    void savePreset (bool forceChooser);
    void exportAudioViaChooser (double seconds);
    void showOptions();
    void showDebugPanel();
    void showAbout();
    void applyTooltipMode();
    void setAbSlot (int slot);
    void tryLoadSample (const juce::File& f);
    void loadSampleViaChooser();
    void savePresetViaChooser();
    void loadPresetViaChooser();

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // ---- right-click-safe control subclasses --------------------------
    // JUCE's Slider falls into a value-drag on an unhandled right-click, and
    // Button fires its click callback for ANY mouse button on mouseUp, so a
    // bare right-click for the MIDI-learn menu would also nudge the control.
    // These swallow popup-menu clicks before the base class ever sees them.
    struct Knob : public juce::Slider,
                  private juce::Timer
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit  (const juce::MouseEvent&) override;
        void valueChanged() override;
    private:
        // The drawn position eases towards the real one over ~80ms, so a jump
        // (preset load, MIDI CC, randomise) reads as a move rather than a
        // teleport. The LNF picks this up from the "dispPos" property.
        void timerCallback() override;
        double disp = -1.0;
    };
    struct Combo : public juce::ComboBox
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent&) override;
    };
    struct Toggle : public juce::ToggleButton
    {
        using juce::ToggleButton::ToggleButton;
        std::function<void()> onRightClick;
        float glow = 0.0f;
        juce::Colour glowColour { juce::Colours::transparentBlack };
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void paint (juce::Graphics&) override;
    };
    struct TextBtn : public juce::TextButton,
                     private juce::Timer
    {
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void paint (juce::Graphics&) override;
    private:
        // spec: a momentary press shows a brief 100ms accent flash
        void timerCallback() override;
        float flash = 0.0f;
    };

    // press-and-hold performance button (DJ FX paddle behaviour), with an
    // optional latch mode so it can also be toggled on and left running
    struct MomentaryBtn : public TextBtn
    {
        using TextBtn::TextBtn;
        std::function<void()> onPress, onRelease;
        bool latching = false, latchedOn = false;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
    };

    struct CanvasComp : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void (const juce::MouseEvent&)> onMouseDown;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (onMouseDown) onMouseDown (e); }
    };

    // pulsing outline over whichever control is currently MIDI-learn armed
    struct LearnHighlight : public juce::Component
    {
        float phase = 0.0f;
        LearnHighlight()  { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    };

    struct LevelMeter : public juce::Component
    {
        float l = 0.0f, r = 0.0f, gr = 0.0f;
        // peak hold: 1px line that holds for 1.5s then falls at 20dB/s
        float holdDbL = -60.0f, holdDbR = -60.0f;
        int   holdFramesL = 0, holdFramesR = 0;
        float peakDb = -60.0f;              // numeric readout, top-right
        void update (float newL, float newR, float newGr, double dtSeconds);
        void paint (juce::Graphics&) override;
    };

    struct HelpOverlay : public juce::Component
    {
        juce::TextEditor text;
        TextBtn debugBtn { "DEBUG..." };     // include.md: reached from help
        TextBtn closeBtn { "CLOSE  [ESC]" };
        HelpOverlay();
        void resized() override;
        void paint (juce::Graphics&) override;
    };

    // The last thing the plugin gained, and the heaviest: a live view of the
    // raw internals, plus the two files support will ask for.
    struct DebugOverlay : public juce::Component
    {
        juce::Label    title;
        juce::TextEditor dump;
        Toggle  crashLogBtn   { "CREATE LOG FILE ON CRASH" };
        TextBtn troubleshootBtn { "EXPORT TROUBLESHOOTING FILE" };
        TextBtn hardResetBtn  { "RESET ALL SETTINGS TO DEFAULT" };
        TextBtn openFolderBtn { "OPEN DIAGNOSTICS FOLDER" };
        juce::Label note;
        TextBtn closeBtn { "CLOSE  [ESC]" };
        DebugOverlay();
        void resized() override;
        void paint (juce::Graphics&) override;
    };

    // Reached from MENU > Options. Holds the preferences that are not part of
    // the sound: tooltips, MIDI mappings, and the standalone audio/MIDI device
    // setup (when hosted, the host owns the devices and that row says so).
    struct OptionsOverlay : public juce::Component
    {
        juce::Label  title;
        Toggle       tooltipsBtn { "HOVER TOOLTIPS" };
        TextBtn      deviceBtn   { "AUDIO / MIDI DEVICE SETUP..." };
        juce::Label  deviceNote;
        TextBtn      clearMidiBtn { "CLEAR ALL MIDI MAPPINGS" };
        juce::Label  midiNote;
        TextBtn      closeBtn { "CLOSE  [ESC]" };
        OptionsOverlay();
        void resized() override;
        void paint (juce::Graphics&) override;
        // Options is a handful of switches, so it gets a compact centred card
        // rather than a full-window sheet with a void under the controls.
        juce::Rectangle<int> cardBounds() const;
    };

    Knob&  addKnob  (const juce::String& paramId, const juce::String& label, const juce::String& tip);
    Combo& addCombo (const juce::String& paramId, juce::StringArray items, const juce::String& tip);
    void   registerParamControl (const juce::String& paramId, juce::Component& c, juce::Label* l);
    void   applyMidiLearnTooltip (juce::Component& c, const juce::String& paramId, const juce::String& baseTip);

    SkipfiendAudioProcessor& proc;
    FiendLNF lnf;
    juce::TooltipWindow tooltipWindow { nullptr, 500 };

    CanvasComp canvas;
    static constexpr int kCanvasW = 1200;
    static constexpr int kPerfRowH = 60;            // live performance strip under the title
    static constexpr int kOverlayRowH = 66;         // ECHO / DELAY / DUB / REVERSE strip
    static constexpr int kCanvasH = 956 + kPerfRowH + kOverlayRowH;

    WaveformDisplay wave;

    juce::OwnedArray<Knob>          knobs;
    juce::OwnedArray<juce::Label>   labels;
    juce::OwnedArray<Combo>         combos;
    juce::OwnedArray<Toggle>        toggles;
    juce::OwnedArray<SA> sAtt;
    juce::OwnedArray<BA> bAtt;
    juce::OwnedArray<CA> cAtt;

    // engine strip
    juce::OwnedArray<Toggle> engBtn;
    std::array<float, skf::NUM_ENGINES> engineGlow {};

    // skip-language sequencer
    struct SeqCell : juce::Component, juce::SettableTooltipClient
    {
        std::function<void (int)> onEdit;
        int engine = -1, repeats = 8;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    };
    juce::OwnedArray<SeqCell> seqCells;

    TextBtn captureBtn { "CAPTURE" }, midiExportBtn { "SKIP->MIDI" };

    // ---- header strip: menu / presets / A-B / bypass / help / dice / meter ----
    TextBtn menuBtn   { "MENU" };
    Toggle  bypassBtn { "BYPASS" };
    TextBtn helpBtn   { "HELP" };
    TextBtn randomBtn { "DICE" };
    TextBtn abBtn     { "A" };
    TextBtn abCopyBtn { "A>B" };
    Combo   presetCombo;
    TextBtn presetSaveBtn { "SAVE" };   // quick access; MENU has the full save/open set
    TextBtn presetLoadBtn { "LOAD" };
    LevelMeter meter;

    // the preset file SAVE writes to; empty until a Save As / Open happens
    juce::File currentPresetFile;

    // ---- live performance row -------------------------------------------
    Toggle       bpmSyncBtn { "SYNC" };
    TextBtn      tapBtn { "TAP" };
    Knob         bpmKnob;
    juce::Label  bpmLabel;

    // ---- overlay effects strip (ECHO / DELAY / DUB / REVERSE) ------------
    MomentaryBtn echoBtn    { "ECHO" };
    MomentaryBtn delayBtn   { "DELAY" };
    MomentaryBtn dubBtn     { "DUB" };
    MomentaryBtn reverseBtn { "REVERSE" };
    Combo        echoRateBox, delayRateBox, dubRateBox, revBarsBox;
    TextBtn      resetBtn { "RESET" };
    MomentaryBtn triggerBtn { "TRIGGER" };
    MomentaryBtn randomTriggerBtn { "RANDOM TRIGGER" };
    Toggle       latchBtn { "LATCH" };
    Knob         mixKnob;                    // big red dry/wet, lives in the header
    juce::Label  mixLabel;

    // ---- sample deck ----------------------------------------------------
    juce::Label sampleNameLabel { {}, "NO SAMPLE LOADED  -  DRAG A .WAV HERE, OR USE LOAD" };
    TextBtn loadSampleBtn   { "LOAD" };
    Toggle  playSampleBtn   { "PLAY" };
    Toggle  loopSampleBtn   { "LOOP" };
    Toggle  sourceSampleBtn { "USE SAMPLE" };
    Toggle  fullBufferBtn   { "FULL BUFFER" };
    Knob    sampleGainKnob;
    juce::Label sampleGainLabel { {}, "SMP GAIN" };

    // ---- MIDI learn ------------------------------------------------------
    juce::Label statusLabel;
    LearnHighlight learnHighlight;
    std::map<juce::String, juce::Component*> componentForParamId;
    std::map<juce::String, juce::Label*>     labelForParamId;
    std::map<juce::String, juce::String>     baseLabelText;

    HelpOverlay help;
    OptionsOverlay options;
    DebugOverlay debugPanel;
    int debugTick = 0;          // the debug dump refreshes at 4Hz, not 20

    // The status line is rewritten every tick with the gate hint, which would
    // wipe a one-off message before anyone could read it. flashStatus() holds
    // a message for a few seconds and the tick text yields to it.
    void flashStatus (const juce::String& message, int seconds = 4);
    int statusHoldTicks = 0;

    // The host can replace the whole state behind the editor's back (preset
    // recall, undo, session load), so the UI re-syncs to it rather than
    // assuming it is the only thing that changes these.
    bool lastTooltipsApplied = true;

    // Export quality, chosen in MENU > Export audio and remembered for the session.
    int exportBitDepth = 24;

    // ---- RUIN: the hidden tab. It opens only when the brand notch in the
    //      very top-left corner is clicked, and closes itself again. ---------
    CanvasComp  secretPanel;
    Toggle      ruinOnBtn { "RUIN" };
    Knob        ruinAmtKnob, ruinRateKnob, ruinToneKnob;
    juce::Label ruinAmtLabel, ruinRateLabel, ruinToneLabel, ruinTitle;
    TextBtn     secretCloseBtn { "X" };
    void toggleSecretPanel();

    // ---- signature LED (top-left): dark grey at -inf, white approaching 0dB,
    //      and red for as long as the output is over 0dB ----------------------
    float ledLevel = 0.0f;      // 0..1, tracks output level
    bool  ledClip  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkipfiendAudioProcessorEditor)
};
