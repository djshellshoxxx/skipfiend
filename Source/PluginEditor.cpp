#include "PluginEditor.h"

namespace col
{
    // === VISUAL IDENTITY SPEC (theme.md) -- FiendAudio house palette ===
    // Neutrals, layout and control shapes are shared by every plugin; only one
    // accent may be re-tinted per plugin for its own identity.
    static const juce::Colour bg      { 0xff0e1116 };   // background base
    static const juce::Colour panel   { 0xff171b22 };   // panel surface
    static const juce::Colour line    { 0xff2a303a };   // panel edge / bevel
    static const juce::Colour accent  { 0xffe8532a };   // primary accent, burnt orange
    static const juce::Colour accent2 { 0xff4fb6c4 };   // secondary accent, muted teal
    static const juce::Colour text    { 0xffe6e8ec };   // text primary
    static const juce::Colour dim     { 0xff8a929e };   // text muted
    static const juce::Colour green   { 0xff7bc96f };   // success / signal-on
    static const juce::Colour yellow  { 0xfff2c14e };   // warning / clip
    static const juce::Colour red     { 0xffe0403a };   // clip
    static const juce::Colour shadow  { 0x8c000000 };   // rgba(0,0,0,0.55)
    static const juce::Colour knobTop { 0xff232833 };   // knob body gradient, top
    static const juce::Colour knobBot { 0xff14181f };   // knob body gradient, bottom
}

// The spec asks for Inter / JetBrains Mono, which are not guaranteed to be
// installed on an end user's machine, so walk the fallback chain and take the
// first family that actually exists before dropping to the system default.
static juce::String pickTypeface (const juce::StringArray& candidates, bool mono)
{
    static const juce::StringArray installed = juce::Font::findAllTypefaceNames();

    for (const auto& name : candidates)
        if (installed.contains (name))
            return name;

    return mono ? juce::Font::getDefaultMonospacedFontName()
                : juce::Font::getDefaultSansSerifFontName();
}

// Primary UI font: labels at weight 500, section headers at 600. JUCE only
// exposes plain/bold, so "bold" stands in for the heavier weight.
static juce::Font uiFont (float h, bool semibold = false)
{
    static const juce::String family = pickTypeface ({ "Inter", "Space Grotesk",
                                                       "Segoe UI", "Helvetica Neue" }, false);
    return juce::Font (juce::FontOptions (family, h,
                                          semibold ? juce::Font::bold : juce::Font::plain));
}

// Small uppercase control label: 11px, letter-spacing +0.08em.
static juce::Font labelFont (float h = 11.0f, bool semibold = false)
{
    auto f = uiFont (h, semibold);
    f.setExtraKerningFactor (0.08f);
    return f;
}

// Numeric readouts: tabular mono, no letter-spacing.
static juce::Font monoFont (float h, bool bold = false)
{
    static const juce::String family = pickTypeface ({ "JetBrains Mono", "IBM Plex Mono",
                                                       "Cascadia Mono", "Consolas" }, true);
    return juce::Font (juce::FontOptions (family, h,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// The scrolling telemetry column is deliberately off-palette: a light,
// low-contrast green that reads as background texture, never as a control.
static const juce::Colour kFeedGreen { 0xffa6e3a1 };

// The editor's own repaint rate. The meter's 1.5s peak hold and 20dB/s fall are
// derived from this, so the two must not drift apart.
static constexpr int kUiRefreshHz = 20;

static juce::String formatTime (double secs)
{
    if (secs < 0.0 || std::isnan (secs)) secs = 0.0;
    const int m = (int) (secs / 60.0);
    const int s = (int) secs % 60;
    return juce::String::formatted ("%d:%02d", m, s);
}

//==============================================================================
static const char* kManualText = R"MANUAL(
SKIPFIEND  --  playback failure unit
========================================

QUICK START
-----------
SKIPFIEND is an insert effect: it needs audio flowing through it to do
anything. In a DAW, put it on a track and press play. Outside a DAW (or to
audition it right now), use the SAMPLE DECK under the waveform display:

  1. Drag a .wav / .aiff / .flac / .ogg / .mp3 file onto the waveform
     display (anywhere in the plugin window works too), OR click LOAD.
  2. The sample loads, starts looping, and "USE SAMPLE" switches on
     automatically -- the effect now hears the sample instead of your
     live/host input.
  3. Use PLAY / LOOP to control playback, and the small knob next to the
     deck to set the sample's level going into the engine.
  4. Flip "USE SAMPLE" off to go back to hearing your live/host input.

The sample deck is a testing convenience only -- the loaded file is not
saved with your project/preset.

WHAT IT DOES
------------
SKIPFIEND keeps a rolling buffer of at least the last 64 seconds of whatever
is passing through
it and treats that buffer as a physical medium being abused: a bumped CD, a
scratched disc, a dying drive, a corrupted MP3, a skipping DAT, a stuck
cassette. Nine parallel "engines" model different real failures; whichever
one fires grabs a slice of that rolling buffer and retriggers it through a
shared Repeat Engine that can pitch it, warp its speed, shape its volume,
walk it around the stereo field, and decide how the burst ends.

THE NINE ENGINES
-----------------
  CD SKIP           laser slip: a short phase-aligned loop, seek click on
                    recovery.
  HARD SKIP         scratched disc losing its place: the playhead jumps
                    back mid-phrase and hard-cuts.
  STICK             stuck groove: a tiny slice loops near-forever, drifting
                    slowly out of tune.
  BUFFER UNDERRUN   streaming rebuffer: most of the burst is muted, then
                    resumes on the tail.
  MP3 CORRUPT       wrong decode: bitcrush, pre-echo, ringing phantom tones.
  TAPE DROPOUT      tape edge damage: level sag, wow, hiss on recovery.
  RATCHET           accelerating retrigger burst, ascending pitch, decay.
  GATE STUTTER      hard grid-synced gate chop -- the "gator": clicky,
                    cut-up on/off chopping within every slice.
  RECORD SKIP       needle in a locked groove: one grid unit of audio repeated
                    verbatim, with no pitching, warping or shaping at all --
                    the cleanest of the nine, and the most obviously "stuck".

Each engine has ON / AMOUNT / PROBABILITY. When a skip is due, the trigger
system randomly weights between the enabled engines by AMOUNT x PROBABILITY.

THE REPEAT ENGINE (under every engine)
---------------------------------------
Per burst: slice length (fixed / ramping / random), pitch behaviour
(stable / ascending / descending / chromatic / drifting) with a pitch step
and base pitch, a Timewarp that speeds up or slows down the burst over its
length, a volume envelope, a pan-walk pattern, and an End Behaviour deciding
whether the burst hard-cuts, fades, clicks, hisses, or mutes-and-resumes.

HOW TRIGGERING WORKS  (important)
----------------------------------
The effect ONLY runs while you hold something down. Nothing fires on its
own, ever. There are exactly two ways to make it play:

  1. Hold a MIDI key. Each key plays one effect for as long as it's held --
     hold two or three keys and you get two or three effects at once.
  2. Hold the TRIGGER button. Runs the effect at full wet while held,
     stops on release. RANDOM TRIGGER does the same but re-randomises
     every engine first.

Everything is QUANTISED to the tempo in use: a press schedules onto the
next beat-grid point rather than firing wherever your finger landed, so
stacked keys and overlays all lock together.

THE PLAYHEAD: LOOPING SUSPENDS THE TRACK
-----------------------------------------
SKIPFIEND plays from a playhead running through its buffer, not straight
from the live input -- and a loop PARKS that playhead.

While you hold a loop the track stops advancing underneath you. When you
let go, playback continues FROM THE END OF THE LOOPED REGION -- the point
where you struck the loop -- rather than jumping ahead to wherever the
track would have got to. Nothing in between is skipped; it is pushed later,
exactly like a needle riding a locked groove and then carrying on.

So after looping, output runs BEHIND real time by however long you held,
and stays there. That is deliberate: it is what makes the loop feel like
part of the record rather than a filter over it. The rolling telemetry on
the right shows how far behind the playhead is, and RESET drops the
accumulated lag and re-joins real time. The buffer holds ~64 s, which is
the most lag that can build up.

FULL CYCLES
------------
A press plays a COMPLETE cycle before anything else happens on that press.
The rate is how many subdivisions make up the cycle: an 8x press plays all
eight slices and then loops the whole thing again, seamlessly. A 2x press
plays both halves; a 32x press plays all thirty-two. You always hear the
full loop of what you pressed -- it is never cut off partway by the next
trigger.

Keys still run IN PARALLEL: each held key owns its own cycle, so three keys
give three complete cycles at once, each at its own rate. Releasing one key
stops only that key's cycle. The first cycle of a press starts on the grid;
once a cycle has run its course it restarts immediately, so there is no gap
mid-hold.

THE KEYBOARD MAP
-----------------
  White keys   each play a different effect.
  Black keys   play a completely random effect, with random attack and
               random probability per hit -- every stab is different.
  Octave       sets the speed: going up the keyboard retriggers faster,
               1x / 4x / 8x / 16x / 32x as you climb octaves.

OVERLAY FX  (bottom strip)
---------------------------
Four hold-to-play effects that ride on top of the main engine. Each has a
rate selector, and like everything else they engage on the next quantised
beat point.

  ECHO      tempo-synced echo, medium feedback. Rate sets the division
            (1x / 2x / 4x / 8x / 16x of a beat).
  DELAY     cleaner, longer repeats, less feedback.
  DUB       dark, high-feedback dub repeats that ring out past release.
  REVERSE   loops the last N bars BACKWARDS while held; the selector sets
            the window (1, 2, 4, 8, 16 or 32 bars).

Echo/Delay/Dub keep ringing out after release rather than cutting dead;
Reverse crossfades back to the normal signal. The reverse window is limited
by the ~64 s the rolling buffer holds, so 32 bars at very slow tempos will
clamp to what has actually been captured.

TEMPO AND SYNC
---------------
  SYNC   follows the host tempo, and lights up ONLY when the host is
         actually supplying one -- so you can tell at a glance whether
         you're really locked to the DAW.
  TAP    tap it in time to set the tempo by hand. Tapping switches SYNC
         off, since you're explicitly choosing the tempo.
  BPM    sets the tempo directly when not synced. The readout shows the
         tempo in use and whether it is HOST or MAN.

Everything else -- Skip Density, Chaos, the Skip Language sequencer, the
Sidechain trigger -- no longer STARTS anything. They shape what happens
while you're holding. Density sets how much of the grid gets hit during a
hold (at full density a hold reads as continuous stutter); with SKIP
LANGUAGE on, the 16-step sequencer decides which engine fires per step for
the duration of the hold.

Releasing always fades out over a few milliseconds rather than cutting
abruptly, so holds can be stabbed rhythmically without clicking.

LIVE / DJ PERFORMANCE
----------------------
The strip across the top is built to be played mid-set:

  SYNC + BPM       SYNC follows the host tempo. With SYNC off (or running
                   standalone with no DAW clock) the BPM knob sets tempo by
                   hand. The readout shows the tempo in use and whether it
                   came from the HOST or is MANual.
  DRY/WET (red)    Fully left = clean signal, fully right = effect only.
                   The one knob to grab when you want the effect in or out;
                   it's red so you can find it without looking.
  TRIGGER          Hold to run the effect at full wet; release to stop.
                   This and a held MIDI key are the only things that make
                   the effect play at all.
  RANDOM TRIGGER   The same, but re-randomises every engine first, so each
                   stab is a different failure. LATCH makes both trigger
                   buttons toggle per click instead of hold-to-perform.
  RESET            Every parameter back to default. This also happens
                   automatically when you load a new sample/track.

MIDI HOLD-TO-PERFORM
---------------------
MIDI triggering is always live -- there is no mode to switch on. Holding a
key selects a factory preset (note number cycles through them) and plays it
at FULL WET for as long as the key is held, re-triggering every grid step.
Releasing restores your dry/wet.
Different keys give different effects, so a pad controller becomes a bank
of performance FX. Last-note priority: a second key switches presets
rather than layering.

LOOP / GRID LENGTH
-------------------
Rhythmic Gravity spans 32 bars down to a 1/32 slice: 32, 16, 8, 4, 2 and 1
bar, then 1/2, 1/4, 1/8, 1/8T, dotted 1/8, 1/16, 1/16T, 1/32, 1/32T. Long
settings give sparse phrase-scale retriggers, short ones give fine stutter
and gate chops -- on a two-second one-shot or a full-length track alike.

MASTER SECTION
--------------
  SKIP DENSITY     how often new skips fire.
  CHAOS            0 = grid-locked and deterministic; higher values let
                   engines, timing and repeat parameters swap and drift
                   unpredictably.
  MIX              dry/wet balance.
  RHYTHMIC GRAVITY the grid skips snap to, with SWING pulling the off-steps
                   and GROOVE LOCK bending repeat counts to land exactly on
                   the grid.
  ARTIFACTS        the seek-click / laser-hunt / buffer-hiss / tape-stop
                   noise played between skips.
  SIDECHAIN TRIG   fires skips from transients on the Sidechain bus instead
                   of the grid (enable the Sidechain input in your host).
  MIDI MODE        incoming MIDI notes trigger engines directly -- note
                   number mod 8 picks the engine, velocity sets the repeat
                   count.
  SKIP LANGUAGE    the 16-step sequencer at the bottom drives triggering
                   instead of the density/chaos dice. Click a cell to cycle
                   its engine (including off), mouse-wheel to set its
                   repeat count.

BYPASS / DICE / PRESETS
------------------------
BYPASS passes audio through untouched. DICE randomises the engines and
repeat/master parameters for instant new territory. The PRESET menu loads a
factory starting point; SAVE / LOAD... store and recall your own full
settings as .skipfiend files.

MIDI LEARN
----------
Right-click ANY knob, toggle, or dropdown to map it to a MIDI CC:
  - "MIDI Learn..."      arms learning for that control -- move a knob or
                          fader on your MIDI controller and it's mapped.
  - "Clear MIDI Mapping" removes an existing mapping.
  - "Reset to Default"   puts the control back to its default value.
A mapped control shows its CC number appended to its label. Mappings are
saved with your project/preset.

CAPTURE / SKIP-TO-MIDI
------------------------
CAPTURE writes the full current state to Documents/SKIPFIEND/ for later
reference. SKIP->MIDI writes the actual retrigger pattern just played to a
.mid file (note 36 + engine index per fire) so you can reuse the rhythm in
a sampler.

METERING
--------
The two bars on the header show post-limiter output level: teal down low,
orange through the nominal range, amber above -9 dB and red above -3 dB.
A 1px line rides on each bar's highest recent peak, holds for 1.5 seconds
and then falls away; the number at the top right is that peak in dB.
The round LED at the very top left tracks the same output: unlit grey at
silence, brightening to white as you approach 0 dB, and latching red for
as long as the output is over 0 dB. A thin red flash across the top of the
meter shows the safety limiter catching a peak. The waveform display's
border glows in the colour of whichever engine is currently firing,
brighter as the output gets louder.

KEYBOARD
--------
  Esc    close this help window, or the options page.

)MANUAL"
// MSVC caps a single string literal at 16KB; adjacent literals concatenate.
R"MANUAL(THE GUI AT A GLANCE
--------------------
  Header strip   plugin name and version, MENU, A / A>B compare, DICE
                 (randomise), BYPASS, HELP, and the output meter + LED.
  Performance    SYNC / TAP / BPM, then RESET, TRIGGER and RANDOM TRIGGER.
                 The effect only runs while a gate is open: a held MIDI
                 note, TRIGGER, or RANDOM TRIGGER.
  Display        the rolling buffer, the jittering playhead, skip flashes,
                 and the scrolling telemetry column on the right.
  Sample deck    drag a file in (or LOAD) to audition without a DAW.
  Skip engines   eight failure models, each with ON / AMOUNT / PROBABILITY.
  Repeat engine  what every retrigger does: slice, pitch, warp, envelope,
                 pan walk and how the burst ends.
  Master         density, chaos and rhythmic gravity.
  Skip language  16-step sequencer: click sets the engine, wheel sets repeats.
  Overlay FX     ECHO / DELAY / DUB / REVERSE, held over the top.

Every knob: drag vertically to change it, hold Shift for coarse moves and
Ctrl (Cmd on macOS) for fine trim. Double-click resets it to default.
Right-click any control for MIDI learn, reset to default, or type an exact
value. Hover any control for a tooltip explaining it (tooltips can be
turned off in MENU > Options).

PRESETS, SAVING AND LOADING
----------------------------
The preset box on the performance row holds the factory bank. SAVE and LOAD
next to it, and MENU > Save / Save as / Open, read and write .skipfiend
files. Plain "Save" writes back to the file you last saved or opened; the
first save asks where to put it.

Preset files belong in:
  Windows   Documents\SKIPFIEND\Presets
  macOS     ~/Documents/SKIPFIEND/Presets
MENU > "Open preset folder in Explorer" takes you straight there. If a
preset does not show up, check it has the .skipfiend extension and is in
that folder, not a sub-folder, then reopen the plugin.

A / B COMPARE
--------------
A and B are two complete, independent snapshots. Switching slots parks the
state you are leaving in the slot you came from, so A/B always compares two
live edits rather than an edit against a stale copy. A>B copies whatever is
in the current slot over the other one.

RESET AND RANDOMIZE
--------------------
RESET puts every control back to its default. DICE randomises everything;
each press after the first wipes the board back to defaults first, so every
roll is a fresh sound rather than a drift away from the last one.

EXPORTING AUDIO
----------------
MENU > Export audio writes the finished effect output (the last 8 or 30
seconds, or everything currently buffered) to a 24-bit WAV. When it lands,
a dialog tells you the file name, the folder, the length and the quality.
Exports default to Documents/SKIPFIEND/Exports.

INSTALLING AND UNINSTALLING BY HAND
------------------------------------
If the installer did not work, copy the files yourself:

  Windows VST3   C:\Program Files\Common Files\VST3\SKIPFIEND.vst3
  macOS VST3     /Library/Audio/Plug-Ins/VST3/SKIPFIEND.vst3
  Standalone     anywhere you like; it needs no registration.

To uninstall, delete that .vst3 bundle, then delete Documents/SKIPFIEND if
you also want your presets, exports and logs gone. Rescan your plugins in
the DAW afterwards.

TROUBLESHOOTING
----------------
The plugin does not appear in my DAW
  Rescan the plugin folder. Confirm the .vst3 is in the path above and that
  your DAW is the same architecture (64-bit). Some hosts keep a blocklist of
  plugins that failed a previous scan -- clear it and rescan.

I hear nothing
  SKIPFIEND is an insert effect and only makes sound while a gate is open.
  Hold a MIDI note, or hold TRIGGER. Check BYPASS is off and the mix knob is
  not fully dry. With no host transport, load a sample in the deck and press
  PLAY with "USE SAMPLE" on.

It sounds thin, or nothing changes when I turn a knob
  Most Repeat Engine controls only apply to bursts that are already running.
  Hold the gate open and keep turning -- the change is audible on the voices
  currently cycling.

My presets are not showing up
  See PRESETS above for the exact folder.

Something is badly wrong
  MENU > About has the support address. Include your DAW, operating system,
  and the version number shown in the header and the bottom-right corner.

DEBUG AND SUPPORT FILES
------------------------
The DEBUG button at the bottom of this window opens a live view of the
plugin's internals -- transport, levels, gate, voices, buffer, MIDI and the
event ring -- updating as you play. It also holds three things:

  EXPORT TROUBLESHOOTING FILE   A snapshot. It runs a light self-check, then
    saves every current setting, your audio and MIDI configuration, the
    version and licence, and which host you are running in. Start here for
    small problems -- it is often enough to spot a misconfiguration.

  CREATE LOG FILE ON CRASH      The heavy one, and off every time the plugin
    loads. Switching it on starts a file named for the moment you enabled it,
    opening with a full copy of the troubleshooting report. If the plugin goes
    down hard, the crash is appended to that same file. Leave it on, reproduce
    the crash, then send the file.

  RESET ALL SETTINGS TO DEFAULT  A hard reset, much more destructive than the
    RESET button on the panel: every setting, every MIDI mapping, both A/B
    slots and the cache folder. Your saved presets and exports are NOT
    touched. Use it when the plugin is misbehaving rather than mis-tuned.

Both files are written to Documents/SKIPFIEND/Diagnostics, and the debug
window has a button that opens that folder. If SKIPFIEND keeps crashing,
send BOTH files to support with a description of what you were doing.

VERSION, LICENCE AND LINKS
---------------------------
  Version   see the header strip and the bottom-right of the window.
  Licence   one seat per user. The full text is LICENCE.txt, next to the
            installed plugin.
  Homepage  )MANUAL" SKIPFIEND_HOMEPAGE R"MANUAL(
  Source    )MANUAL" SKIPFIEND_GITHUB R"MANUAL(
  Support   )MANUAL" SKIPFIEND_SUPPORT_EMAIL R"MANUAL(
)MANUAL";

//==============================================================================
FiendLNF::FiendLNF()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::dim);
    setColour (juce::ComboBox::backgroundColourId, col::bg);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.25f));
    setColour (juce::TextButton::buttonColourId, col::bg);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::panel);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
    setColour (juce::ScrollBar::thumbColourId, col::line);
}

juce::Font FiendLNF::getLabelFont (juce::Label&) { return labelFont (11.0f); }
juce::Font FiendLNF::getTextButtonFont (juce::TextButton&, int) { return labelFont (11.0f, true); }
juce::Font FiendLNF::getComboBoxFont (juce::ComboBox&) { return labelFont (11.0f); }
juce::Font FiendLNF::getPopupMenuFont() { return uiFont (12.0f); }

void FiendLNF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    // The Knob eases a display position towards the real one; use it when present
    // so a jumped value reads as a move rather than a teleport.
    if (auto* d = s.getProperties().getVarPointer ("dispPos"))
        pos = juce::jlimit (0.0f, 1.0f, (float) (double) *d);

    const bool hovered = (bool) s.getProperties().getWithDefault ("hover", false);
    const float ang = a0 + pos * (a1 - a0);

    // Body: radial gradient top-to-bottom, 1px inner stroke. The value arc is
    // drawn OUTSIDE this, with a 4px gap, so the body stays clean.
    const float body = juce::jmax (4.0f, r - 7.0f);

    g.setColour (col::shadow);
    g.fillEllipse (c.x - body, c.y - body + 2.0f, body * 2.0f, body * 2.0f);

    juce::ColourGradient grad (col::knobTop.brighter (hovered ? 0.08f : 0.0f),
                               c.x, c.y - body,
                               col::knobBot, c.x, c.y + body, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f);
    g.setColour (col::line);
    g.drawEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f, 1.0f);

    const float arcR = body + 4.0f;          // 4px gap outside the body

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (col::line.withAlpha (0.6f));
    g.strokePath (track, juce::PathStrokeType (3.0f));

    // a knob can request its own accent colour (the dry/wet knob is red so it
    // reads instantly as "this is the one that turns the effect on and off")
    juce::Colour arc = col::accent;
    if (auto* v = s.getProperties().getVarPointer ("accentColour"))
        arc = juce::Colour ((juce::uint32) (int) *v);

    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, ang, true);
    if (hovered)
        arc = arc.brighter (0.08f);      // hover lifts the control ~8%

    g.setColour (s.isEnabled() ? arc : col::dim);
    g.strokePath (val, juce::PathStrokeType (3.0f));

    // centre dot: accent while the value is live, muted at rest
    g.setColour (s.isEnabled() ? arc : col::dim);
    g.fillEllipse (c.x - 2.0f, c.y - 2.0f, 4.0f, 4.0f);

    // 2px indicator from centre to rim, rounded cap
    juce::Point<float> tip (c.x + std::cos (ang - juce::MathConstants<float>::halfPi) * (body - 2.0f),
                            c.y + std::sin (ang - juce::MathConstants<float>::halfPi) * (body - 2.0f));
    juce::Path ind;
    ind.startNewSubPath (c);
    ind.lineTo (tip);
    g.strokePath (ind, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void FiendLNF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                     bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? col::accent.withAlpha (0.25f) : over ? col::line : col::panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (down ? col::accent : col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void FiendLNF::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? col::accent.withAlpha (0.15f) : (over ? col::line : col::panel));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? col::accent : col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? col::accent : col::dim);
    g.setFont (monoFont (10.0f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2),
                      juce::Justification::centredLeft, 2);
}

// Fader: 4px track, accent fill, 16x24 thumb carrying the knob body gradient.
void FiendLNF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float minPos, float maxPos,
                                 juce::Slider::SliderStyle style, juce::Slider& s)
{
    juce::ignoreUnused (minPos, maxPos);
    const bool vertical = (style == juce::Slider::LinearVertical
                        || style == juce::Slider::LinearBarVertical);

    juce::Colour fill = col::accent;
    if (auto* v = s.getProperties().getVarPointer ("accentColour"))
        fill = juce::Colour ((juce::uint32) (int) *v);
    if (! s.isEnabled())
        fill = col::dim;

    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = vertical ? area.withSizeKeepingCentre (4.0f, area.getHeight())
                          : area.withSizeKeepingCentre (area.getWidth(), 4.0f);

    g.setColour (col::line);
    g.fillRoundedRectangle (track, 2.0f);

    auto filled = track;
    if (vertical) filled = filled.withTop (pos);        // fills upward from the bottom
    else          filled = filled.withRight (pos);
    g.setColour (fill);
    g.fillRoundedRectangle (filled, 2.0f);

    // 16x24 thumb, long axis across the travel direction
    const float tw = vertical ? 24.0f : 16.0f;
    const float th = vertical ? 16.0f : 24.0f;
    juce::Rectangle<float> thumb (tw, th);
    thumb.setCentre (vertical ? track.getCentreX() : pos,
                     vertical ? pos : track.getCentreY());

    g.setColour (col::shadow);
    g.fillRoundedRectangle (thumb.translated (0.0f, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (col::knobTop, thumb.getCentreX(), thumb.getY(),
                                             col::knobBot, thumb.getCentreX(), thumb.getBottom(),
                                             false));
    g.fillRoundedRectangle (thumb, 4.0f);
    g.setColour (fill);
    g.drawRoundedRectangle (thumb.reduced (0.5f), 4.0f, 1.0f);
}

// Off state is muted text; hover/press lifts it to the accent.
void FiendLNF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    juce::Colour c = down ? col::accent
                          : (over ? col::text : col::dim);
    if (! b.isEnabled())
        c = col::dim.withAlpha (0.5f);

    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2),
                      juce::Justification::centred, 2);
}

// Tooltips are dark pills that stay inside the window.
juce::Rectangle<int> FiendLNF::getTooltipBounds (const juce::String& tip, juce::Point<int> pos,
                                                 juce::Rectangle<int> parentArea)
{
    juce::AttributedString attributed;
    attributed.setText (tip);
    attributed.setFont (monoFont (11.0f));

    juce::TextLayout layout;
    layout.createLayout (attributed, (float) juce::jmax (80, parentArea.getWidth() - 40));

    const int w = juce::jlimit (60, parentArea.getWidth(), (int) std::ceil (layout.getWidth()) + 20);
    const int h = (int) std::ceil (layout.getHeight()) + 12;

    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 6)  : pos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void FiendLNF::drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h);

    g.setColour (col::shadow);
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 6.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    g.setColour (col::dim);
    g.setFont (monoFont (11.0f));
    g.drawFittedText (tip, r.reduced (10.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 6);
}

void FiendLNF::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                             juce::ComboBox& cb)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (col::panel);   g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line);    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (col::accent);
    juce::Path tri;
    tri.addTriangle (w - 14.0f, h * 0.4f, w - 6.0f, h * 0.4f, w - 10.0f, h * 0.62f);
    g.fillPath (tri);
    juce::ignoreUnused (cb);
}

//==============================================================================
WaveformDisplay::WaveformDisplay (SkipfiendAudioProcessor& p) : proc (p)
{
    startTimerHz (30);
}

void WaveformDisplay::timerCallback()
{
    ++frame;
    jitter = proc.playJitter.load();
    glow   = juce::jlimit (0.0f, 1.0f, 0.6f * (proc.outPeakL.load() + proc.outPeakR.load()));

    // age the skip flashes
    for (auto& f : proc.flashes)
    {
        float a = f.age.load();
        if (a < 5.0f) f.age.store (a + 1.0f / 30.0f);
    }

    // ---- verbose skip-event log, filling the full height of the display ----
    const int maxLines = juce::jmax (4, (getHeight() - 34) / 12);
    const int head = proc.logHead.load();
    log.clearQuick();
    static const char* codes[]  = { "GRID", "SEQ", "SIDECHAIN", "MIDI", "CHAOS" };
    static const char* pitchN[] = { "STABLE", "ASC", "DESC", "CHROM", "DRIFT" };
    static const char* lenN[]   = { "FIXED", "RAMP-", "RAMP+", "RAND" };
    static const char* endN[]   = { "CUT", "TAIL", "CLICK", "SEEK", "MUTE" };
    static const char* flavN[]  = { "", " MP3", " TAPE", " UNDERRUN" };

    for (int i = maxLines - 1; i >= 0; --i)
    {
        const auto& entry = proc.logRing[(size_t) ((head - 1 - i) & 63)];
        const juce::uint32 a = entry.a.load();
        const juce::uint32 b = entry.b.load();
        if (a == 0) continue;

        const int e   = (int) ((a >> 24) & 0xff);
        const int rep = (int) ((a >> 8) & 0xffff);
        const int cd  = (int) (a & 0xff);

        const int sliceMs = (int) (b & 0xfffu);
        const int pm      = (int) ((b >> 12) & 0x7);
        const int lm      = (int) ((b >> 15) & 0x7);
        const int em      = (int) ((b >> 18) & 0x7);
        const int fl      = (int) ((b >> 21) & 0x7);
        const bool gate   = ((b >> 24) & 0x1) != 0;
        const int warpQ   = (int) ((b >> 25) & 0xf);
        const float warp  = warpQ / 7.5f - 1.0f;

        log.add (juce::String::formatted ("ERR 0x%04X ", a & 0xffff)
                 + juce::String (skf::engineName (e)).paddedRight (' ', 16)
                 + "x" + juce::String (rep).paddedRight (' ', 4)
                 + juce::String (sliceMs).paddedLeft (' ', 4) + "ms "
                 + juce::String (pitchN[juce::jlimit (0, 4, pm)]).paddedRight (' ', 7)
                 + juce::String (lenN[juce::jlimit (0, 3, lm)]).paddedRight (' ', 6)
                 + "END:" + juce::String (endN[juce::jlimit (0, 4, em)]).paddedRight (' ', 6)
                 + "WARP" + juce::String (warp, 2).paddedLeft (' ', 6) + " "
                 + (gate ? "GATED " : "")
                 + juce::String (flavN[juce::jlimit (0, 3, fl)]).trim().paddedRight (' ', 9)
                 + "[" + juce::String (codes[juce::jlimit (0, 4, cd)]) + "]");
    }

    // ---- continuously rolling telemetry on the right ----
    // real values, cycling so the column never stalls even when nothing fires
    const juce::uint32 sig =
          (juce::uint32) proc.midiNoteOns.load() * 2654435761u
        ^ (juce::uint32) proc.midiCCs.load() * 40503u
        ^ (juce::uint32) ((proc.lastEngineFired.load() + 2) * 97)
        ^ (juce::uint32) (proc.activeVoices.load() * 131)
        ^ (juce::uint32) proc.activeEngineMask.load()
        ^ (juce::uint32) (juce::jlimit (0.0f, 1.0f, proc.outPeakL.load()) * 400.0f)
        ^ (juce::uint32) (juce::jlimit (0.0f, 1.0f, proc.inPeak.load())   * 400.0f);

    const bool feedChanged = (sig != feedSig);
    feedSig = sig;

    if (feedChanged && ++feedTick % 3 == 0)
    {
        const int which = (feedTick / 3) % 11;
        const auto db = [] (float lin)
        {
            return lin > 1.0e-5f ? juce::String (juce::Decibels::gainToDecibels (lin), 1) + "dB"
                                 : juce::String ("-inf  ");
        };
        juce::String line;
        switch (which)
        {
            case 0: line = "OUT L " + db (proc.outPeakL.load()) + "  R " + db (proc.outPeakR.load()); break;
            case 1: line = "IN  " + db (proc.inPeak.load()) + "   GR " + db (1.0f - proc.outGainReduction.load()); break;
            case 2: line = "VOICES " + juce::String (proc.activeVoices.load()) + "/16   KEYS "
                            + juce::String (proc.getNumHeldKeys()); break;
            case 3: line = "BPM " + juce::String (proc.getCurrentBpm(), 2)
                            + (proc.isFollowingHostBpm() ? " HOST" : " MAN"); break;
            case 4: line = "PPQ " + juce::String (proc.ppqNow.load(), 3); break;
            case 5: line = juce::String::formatted ("MASK 0x%03X  BLK %lu",
                              (unsigned) proc.activeEngineMask.load(),
                              (unsigned long) proc.blocksProcessed.load()); break;
            case 6: line = "BUF " + juce::String (proc.bufferSeconds.load(), 1) + "s  GRID "
                            + skf::gridNames()[juce::jlimit (0, skf::kNumGrids - 1, proc.lastGridIdx.load())]; break;
            case 7: line = "PLAYHEAD -" + juce::String (proc.playheadLagSeconds.load(), 2) + "s behind"; break;
            case 8:
            {
                const int nn = proc.lastMidiNote.load();
                if (nn < 0) { line = "MIDI IN  --  no notes yet"; break; }
                static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                line = "NOTE " + juce::String (names[nn % 12]) + juce::String (nn / 12 - 1)
                        + " (" + juce::String (nn) + ") v" + juce::String (proc.lastMidiVel.load())
                        + (proc.lastMidiWasBlack.load() ? " RND" : " FIX");
                break;
            }
            case 9:
            {
                const int cc = proc.lastMidiCC.load();
                line = cc < 0 ? "CC  --  none"
                              : "CC " + juce::String (cc) + " = " + juce::String (proc.lastMidiCCVal.load())
                                + "  ch" + juce::String (proc.lastMidiChan.load());
                break;
            }
            default: line = juce::String::formatted ("MIDI n:%lu cc:%lu  OCT %d",
                              (unsigned long) proc.midiNoteOns.load(),
                              (unsigned long) proc.midiCCs.load(),
                              proc.lastMidiOctave.load()); break;
        }
        feed.add (line);
        while (feed.size() > maxLines) feed.remove (0);
    }

    repaint();
}

void WaveformDisplay::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
        ghost = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRect (b);

    // audio-reactive glow border, tinted by whichever engine is currently firing
    const juce::uint32 mask = proc.activeEngineMask.load();
    const int dominant = mask != 0 ? proc.lastEngineFired.load() : -1;
    if (dominant >= 0 && glow > 0.02f)
    {
        const auto ec = skf::engineColour (dominant);
        for (int ring = 4; ring >= 1; --ring)
        {
            g.setColour (ec.withAlpha (juce::jlimit (0.0f, 0.4f, glow * 0.16f * (float) ring)));
            g.drawRect (b.reduced ((float) ring), (float) ring);
        }
    }

    g.setColour (col::line);
    g.drawRect (b, 1.0f);

    // grid ticks like a CD transport readout
    g.setColour (col::line.withAlpha (0.5f));
    for (int i = 1; i < 16; ++i)
    {
        const float x = b.getX() + b.getWidth() * i / 16.0f;
        g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
    }

    const auto& rb = proc.rolling();
    const int W = (int) b.getWidth();

    // ---- phosphor-persistence ("ghosting") layer -------------------------
    // The moving elements are drawn into a layer that only partly fades each
    // frame, leaving a light trail. How long that trail lingers tracks the
    // dry/wet knob: dry = almost no ghosting, wet = a longer smear.
    if (! ghost.isValid() || ghost.getWidth() != getWidth() || ghost.getHeight() != getHeight())
        ghost = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);

    float mixAmt = 1.0f;
    if (auto* mv = proc.apvts.getRawParameterValue ("mix")) mixAmt = mv->load();
    ghost.multiplyAllAlphas (juce::jmap (juce::jlimit (0.0f, 1.0f, mixAmt), 0.0f, 1.0f, 0.42f, 0.82f));

    {
        juce::Graphics gg (ghost);

        // waveform of the rolling buffer (last ~2.2 s)
        const double now = (double) rb.now();
        const double span = rb.sr() * 2.2;
        juce::Path wave;
        const float midY = b.getCentreY();
        const float amp = b.getHeight() * 0.42f;
        for (int px = 0; px < W; ++px)
        {
            const double a = now - span + (span * px) / juce::jmax (1, W);
            const float s = 0.5f * (rb.readAbs (a, 0) + rb.readAbs (a, 1));
            const float y = midY - juce::jlimit (-1.0f, 1.0f, s) * amp;
            if (px == 0) wave.startNewSubPath (b.getX() + px, y);
            else         wave.lineTo (b.getX() + px, y);
        }
        gg.setColour ((dominant >= 0 ? skf::engineColour (dominant) : col::accent).withAlpha (0.85f));
        gg.strokePath (wave, juce::PathStrokeType (1.0f));

        // jittering playhead
        const float jx = b.getRight() - 3.0f + (jitter > 0.01f
                            ? (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 10.0f * jitter : 0.0f);
        gg.setColour (col::text);
        gg.drawVerticalLine ((int) jx, b.getY(), b.getBottom());

        // frame-drop flashes for recent skips (+ a reverse-flicker glyph for TAPE DROPOUT)
        for (size_t i = 0; i < proc.flashes.size(); ++i)
        {
            const float age = proc.flashes[i].age.load();
            if (age > 0.5f) continue;
            const int e = proc.flashes[i].engine.load();
            const float x = b.getX() + (float) ((i * 53) % juce::jmax (1, W - 8));
            gg.setColour (skf::engineColour (e).withAlpha (juce::jmax (0.0f, 0.55f - age)));
            gg.fillRect (x, b.getY(), 5.0f, b.getHeight());

            if (proc.flashes[i].reverse.load() && age < 0.4f)
            {
                gg.setColour (skf::engineColour (e).withAlpha (juce::jmax (0.0f, 0.9f - age * 2.0f)));
                gg.setFont (monoFont (9.0f, true));
                gg.drawText ("<<REV", (int) x - 8, (int) b.getY() + 2, 50, 12, juce::Justification::left);
            }
        }
    }

    g.drawImageAt (ghost, 0, 0);

    // forensic error log, full height down the left
    g.setFont (monoFont (10.0f));
    for (int i = 0; i < log.size(); ++i)
    {
        const bool newest = (i == log.size() - 1);
        // older entries fade out down the column
        const float fade = juce::jmap ((float) i, 0.0f, juce::jmax (1.0f, (float) log.size() - 1),
                                        0.35f, 0.95f);
        g.setColour (newest ? col::red : col::dim.withAlpha (fade));
        g.drawText (log[i], (int) b.getX() + 8, (int) b.getY() + 6 + i * 12,
                    (int) b.getWidth() - 16, 12, juce::Justification::left);
    }

    // continuously rolling telemetry, full height down the right
    g.setFont (monoFont (10.0f));
    for (int i = 0; i < feed.size(); ++i)
    {
        // The top two and bottom two lines fade away entirely, so the column
        // reads as a window onto a stream rather than a fixed block of text.
        const int n = feed.size();
        float fade = 0.55f;

        // The edge fade only makes sense once the column has actually filled.
        // While it is still building after a reset, blanking the first and last
        // two lines would hide the whole feed.
        if (n >= 8)
        {
            if      (i == 0 || i == n - 1) fade = 0.0f;
            else if (i == 1 || i == n - 2) fade = 0.22f;
        }

        if (fade <= 0.0f)
            continue;

        g.setColour (kFeedGreen.withAlpha (fade));
        g.drawText (feed[i], (int) b.getRight() - 268, (int) b.getY() + 6 + i * 12,
                    260, 12, juce::Justification::right);
    }

    g.setColour (col::dim);
    g.setFont (monoFont (9.0f));
    // now() is the absolute write position, i.e. everything that has ever gone
    // through - but the ring only holds len samples, so cap the readout at the
    // real capacity rather than claiming to have captured hours of audio.
    {
        const double sr = juce::jmax (1.0, rb.sr());
        const double elapsed  = rb.now() / sr;
        const double capacity = (double) rb.len / sr;
        const double held     = juce::jmin (elapsed, capacity);

        g.drawText ("ROLLING BUFFER  //  " + juce::String (held, 1) + "s CAPTURED",
                    (int) b.getX() + 8, (int) b.getBottom() - 16, 400, 12,
                    juce::Justification::left);
    }

    // drag & drop invitation overlay
    if (dragHighlight.load())
    {
        g.setColour (col::accent.withAlpha (0.12f));
        g.fillRect (b);
        g.setColour (col::accent);
        g.drawRect (b.reduced (3.0f), 2.0f);
        g.setFont (monoFont (16.0f, true));
        g.drawText ("DROP TO LOAD TEST SAMPLE", b, juce::Justification::centred);
    }
}

//==============================================================================
//  right-click-safe control subclasses
//==============================================================================
void SkipfiendAudioProcessorEditor::Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    juce::Slider::mouseDown (e);
}

void SkipfiendAudioProcessorEditor::Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // Vertical drag. Shift coarsens, Ctrl/Cmd gives the fine-trim pass.
    setSliderSnapsToMousePosition (false);
    setMouseDragSensitivity (e.mods.isShiftDown()   ? 110
                           : e.mods.isCommandDown() ? 1500
                                                    : 420);
    juce::Slider::mouseDrag (e);
}

void SkipfiendAudioProcessorEditor::Knob::mouseEnter (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    getProperties().set ("hover", true);
    repaint();
    juce::Slider::mouseEnter (e);
}

void SkipfiendAudioProcessorEditor::Knob::mouseExit (const juce::MouseEvent& e)
{
    getProperties().set ("hover", false);
    repaint();
    juce::Slider::mouseExit (e);
}

void SkipfiendAudioProcessorEditor::Knob::valueChanged()
{
    // Kick the easing timer so the pointer travels to the new value over ~80ms
    // instead of teleporting (preset load, MIDI CC, randomise).
    if (! isTimerRunning())
        startTimerHz (60);
}

void SkipfiendAudioProcessorEditor::Knob::timerCallback()
{
    const double target = valueToProportionOfLength (getValue());

    if (disp < 0.0)
        disp = target;

    // ~80ms ease-out at 60Hz.
    disp += (target - disp) * 0.25;

    if (std::abs (target - disp) < 0.0005)
    {
        disp = target;
        stopTimer();
    }

    getProperties().set ("dispPos", disp);
    repaint();
}

void SkipfiendAudioProcessorEditor::Combo::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    juce::ComboBox::mouseDown (e);
}

void SkipfiendAudioProcessorEditor::Toggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    juce::ToggleButton::mouseDown (e);
}

void SkipfiendAudioProcessorEditor::Toggle::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    juce::ToggleButton::mouseUp (e);
}

void SkipfiendAudioProcessorEditor::Toggle::paint (juce::Graphics& g)
{
    juce::ToggleButton::paint (g);
    if (glow > 0.01f)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (glowColour.withAlpha (juce::jlimit (0.0f, 0.85f, glow)));
        g.drawRect (r, 2.0f);
    }
}

void SkipfiendAudioProcessorEditor::TextBtn::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;

    flash = 1.0f;
    startTimerHz (60);
    juce::TextButton::mouseDown (e);
}

void SkipfiendAudioProcessorEditor::TextBtn::paint (juce::Graphics& g)
{
    juce::TextButton::paint (g);

    if (flash > 0.01f)
    {
        g.setColour (col::accent.withAlpha (0.55f * flash));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }
}

void SkipfiendAudioProcessorEditor::TextBtn::timerCallback()
{
    // ~100ms from full to nothing at 60Hz
    flash -= 1.0f / 6.0f;

    if (flash <= 0.0f)
    {
        flash = 0.0f;
        stopTimer();
    }

    repaint();
}

void SkipfiendAudioProcessorEditor::TextBtn::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    juce::TextButton::mouseUp (e);
}

void SkipfiendAudioProcessorEditor::MomentaryBtn::mouseDown (const juce::MouseEvent& e)
{
    TextBtn::mouseDown (e);
    if (e.mods.isPopupMenu()) return;

    if (latching)
    {
        latchedOn = ! latchedOn;
        if (latchedOn) { if (onPress) onPress(); }
        else           { if (onRelease) onRelease(); }
    }
    else if (onPress) onPress();

    repaint();
}

void SkipfiendAudioProcessorEditor::MomentaryBtn::mouseUp (const juce::MouseEvent& e)
{
    const bool wasPopup = e.mods.isPopupMenu();
    TextBtn::mouseUp (e);
    if (wasPopup) return;

    if (! latching && onRelease) onRelease();
    repaint();
}

//==============================================================================
void SkipfiendAudioProcessorEditor::LearnHighlight::paint (juce::Graphics& g)
{
    const float a = 0.4f + 0.4f * std::sin (phase);
    g.setColour (col::accent.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
    g.drawRect (getLocalBounds().toFloat().reduced (1.0f), 2.5f);
}

// Peak hold: catch the peak, hold it 1.5s, then let it fall at 20dB/s.
void SkipfiendAudioProcessorEditor::LevelMeter::update (float newL, float newR, float newGr,
                                                        double dtSeconds)
{
    l = newL; r = newR; gr = newGr;

    const auto toDb = [] (float lin)
    {
        return juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, lin));
    };

    const int   elapsedMs = (int) (dtSeconds * 1000.0);
    const float fallDb    = 20.0f * (float) dtSeconds;

    const auto advance = [&] (float db, float& holdDb, int& holdMs)
    {
        if (db >= holdDb)
        {
            holdDb = db;
            holdMs = 1500;
        }
        else
        {
            holdMs -= elapsedMs;

            if (holdMs <= 0)
            {
                holdMs = 0;
                holdDb = juce::jmax (-60.0f, holdDb - fallDb);
            }
        }
    };

    advance (toDb (newL), holdDbL, holdFramesL);
    advance (toDb (newR), holdDbR, holdFramesR);

    peakDb = juce::jmax (holdDbL, holdDbR);
}

void SkipfiendAudioProcessorEditor::LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRect (b);
    g.setColour (col::line);
    g.drawRect (b, 1.0f);

    auto drawBar = [&] (juce::Rectangle<float> r, float lin)
    {
        const float db = juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, lin));
        const float t = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        auto filled = r.withY (r.getBottom() - r.getHeight() * t).withHeight (r.getHeight() * t);

        // Spec: a gradient up the bar - teal low, orange through the nominal
        // range, amber near clip, red at clip. The gradient is anchored to the
        // WHOLE bar, not the filled part, so a given height always means the
        // same level rather than the colour sliding around as the level moves.
        juce::ColourGradient grad (col::accent2.withAlpha (0.9f), r.getX(), r.getBottom(),
                                   col::red.withAlpha (0.9f),     r.getX(), r.getY(), false);
        grad.addColour (0.50, col::accent.withAlpha (0.9f));   // -24dB, nominal
        grad.addColour (0.81, col::yellow.withAlpha (0.9f));   //  -9dB, near clip
        grad.addColour (0.94, col::red.withAlpha (0.9f));      //  -3dB, clip

        g.setGradientFill (grad);
        g.fillRect (filled);
    };

    // 1px peak-hold line riding above the bar
    auto drawHold = [&] (juce::Rectangle<float> rect, float db)
    {
        if (db <= -59.0f) return;
        const float t = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        const float yy = rect.getBottom() - rect.getHeight() * t;
        g.setColour (db > -3.0f ? col::red : col::text);
        g.fillRect (rect.getX(), yy, rect.getWidth(), 1.0f);
    };

    auto barsArea = b.reduced (3.0f);
    const float gap = 3.0f;
    const float w = (barsArea.getWidth() - gap) * 0.5f;
    auto leftBar = barsArea.removeFromLeft (w);
    barsArea.removeFromLeft (gap);
    drawBar (leftBar, l);
    drawBar (barsArea, r);
    drawHold (leftBar, holdDbL);
    drawHold (barsArea, holdDbR);

    // Numeric peak readout, top-right. It sits on top of the bar, which can be
    // bright amber or red, so give it a dark backing rather than muted grey on
    // top of a light fill.
    {
        const auto text = peakDb <= -59.0f ? juce::String ("-INF")
                                           : juce::String (peakDb, 1);
        auto slot = b.reduced (3.0f, 2.0f).removeFromTop (11.0f).removeFromRight (30.0f);

        g.setColour (col::bg.withAlpha (0.78f));
        g.fillRoundedRectangle (slot, 2.0f);

        g.setColour (peakDb > -3.0f ? col::red : col::text);
        g.setFont (monoFont (9.0f));
        g.drawText (text, slot.reduced (2.0f, 0.0f), juce::Justification::centredRight, false);
    }

    if (gr > 0.02f)
    {
        g.setColour (col::red.withAlpha (juce::jlimit (0.0f, 1.0f, gr * 2.0f)));
        g.fillRect (b.removeFromTop (3.0f));
    }
}

//==============================================================================
SkipfiendAudioProcessorEditor::HelpOverlay::HelpOverlay()
{
    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setFont (monoFont (13.0f));
    text.setColour (juce::TextEditor::backgroundColourId, col::panel);
    text.setColour (juce::TextEditor::textColourId, col::text);
    text.setColour (juce::TextEditor::outlineColourId, col::line);
    text.setText (kManualText, false);
    addAndMakeVisible (text);
    addAndMakeVisible (debugBtn);
    addAndMakeVisible (closeBtn);
    setVisible (false);
}

void SkipfiendAudioProcessorEditor::HelpOverlay::resized()
{
    auto b = getLocalBounds().reduced (60);
    auto row = b.removeFromBottom (28);
    closeBtn.setBounds (row.removeFromRight (160));
    row.removeFromRight (8);
    debugBtn.setBounds (row.removeFromRight (120));
    b.removeFromBottom (8);
    text.setBounds (b);
}

void SkipfiendAudioProcessorEditor::HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.75f));
    auto b = getLocalBounds().reduced (56).toFloat();
    g.setColour (col::bg);
    g.fillRect (b);
    g.setColour (col::accent);
    g.drawRect (b, 1.5f);
}

//==============================================================================
SkipfiendAudioProcessorEditor::OptionsOverlay::OptionsOverlay()
{
    title.setText ("OPTIONS", juce::dontSendNotification);
    title.setFont (labelFont (14.0f, true));
    title.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (title);

    addAndMakeVisible (tooltipsBtn);

    deviceBtn.setTooltip ("Choose the audio device and MIDI inputs (standalone only).");
    addAndMakeVisible (deviceBtn);

    deviceNote.setFont (monoFont (10.0f));
    deviceNote.setColour (juce::Label::textColourId, col::dim);
    deviceNote.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (deviceNote);

    clearMidiBtn.setTooltip ("Forget every learned MIDI CC mapping.");
    addAndMakeVisible (clearMidiBtn);

    midiNote.setFont (monoFont (10.0f));
    midiNote.setColour (juce::Label::textColourId, col::dim);
    midiNote.setJustificationType (juce::Justification::topLeft);
    midiNote.setText ("Right-click any control to learn a CC, reset it, or type an exact value.",
                      juce::dontSendNotification);
    addAndMakeVisible (midiNote);

    addAndMakeVisible (closeBtn);
    setVisible (false);
}

juce::Rectangle<int> SkipfiendAudioProcessorEditor::OptionsOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (600, getWidth() - 64),
                                                   juce::jmin (380, getHeight() - 64));
}

void SkipfiendAudioProcessorEditor::OptionsOverlay::resized()
{
    auto b = cardBounds().reduced (24);

    title.setBounds (b.removeFromTop (22));
    b.removeFromTop (16);

    closeBtn.setBounds (b.removeFromBottom (28).removeFromRight (160));
    b.removeFromBottom (16);

    tooltipsBtn.setBounds (b.removeFromTop (28).removeFromLeft (220));
    b.removeFromTop (16);

    deviceBtn.setBounds (b.removeFromTop (28).removeFromLeft (280));
    b.removeFromTop (4);
    deviceNote.setBounds (b.removeFromTop (34));
    b.removeFromTop (12);

    clearMidiBtn.setBounds (b.removeFromTop (28).removeFromLeft (280));
    b.removeFromTop (4);
    midiNote.setBounds (b.removeFromTop (34));
}

SkipfiendAudioProcessorEditor::DebugOverlay::DebugOverlay()
{
    title.setText ("DEBUG", juce::dontSendNotification);
    title.setFont (labelFont (14.0f, true));
    title.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (title);

    dump.setMultiLine (true, false);
    dump.setReadOnly (true);
    dump.setScrollbarsShown (true);
    dump.setCaretVisible (false);
    dump.setFont (monoFont (11.0f));
    dump.setColour (juce::TextEditor::backgroundColourId, col::panel);
    dump.setColour (juce::TextEditor::textColourId, col::green);
    dump.setColour (juce::TextEditor::outlineColourId, col::line);
    addAndMakeVisible (dump);

    crashLogBtn.setTooltip ("Start a log file now and keep writing to it. If the plugin "
                            "crashes, the reason lands in the same file. Off every time "
                            "the plugin loads.");
    addAndMakeVisible (crashLogBtn);

    troubleshootBtn.setTooltip ("Run a light self-check and save it with every current "
                                "setting, for you or for support.");
    addAndMakeVisible (troubleshootBtn);

    hardResetBtn.setTooltip ("Hard reset: defaults, mappings and cache all wiped. Your "
                             "saved presets and exports are left alone.");
    addAndMakeVisible (hardResetBtn);

    openFolderBtn.setTooltip ("Open the folder these files are written to.");
    addAndMakeVisible (openFolderBtn);

    note.setFont (monoFont (10.0f));
    note.setColour (juce::Label::textColourId, col::dim);
    note.setJustificationType (juce::Justification::topLeft);
    note.setText (
        "TROUBLESHOOTING FILE  a snapshot: a light self-check, every setting, your audio "
        "and MIDI configuration, the version and licence, and what host you are in. Start "
        "here for small problems.\n"
        "CRASH LOG  the heavy one. It opens with a copy of the troubleshooting file and "
        "keeps writing; if the plugin goes down, the crash lands in it. Leave it on, "
        "reproduce the crash, then send the file.\n"
        "Both are written to Documents/SKIPFIEND/Diagnostics. If SKIPFIEND keeps crashing, "
        "send BOTH to " SKIPFIEND_SUPPORT_EMAIL " with a description of what you did.",
        juce::dontSendNotification);
    addAndMakeVisible (note);

    addAndMakeVisible (closeBtn);
    setVisible (false);
}

void SkipfiendAudioProcessorEditor::DebugOverlay::resized()
{
    auto b = getLocalBounds().reduced (76, 72);

    title.setBounds (b.removeFromTop (22));
    b.removeFromTop (8);

    auto buttons = b.removeFromTop (28);
    crashLogBtn.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (8);
    troubleshootBtn.setBounds (buttons.removeFromLeft (240));
    buttons.removeFromLeft (8);
    hardResetBtn.setBounds (buttons.removeFromLeft (250));
    buttons.removeFromLeft (8);
    openFolderBtn.setBounds (buttons.removeFromLeft (210));

    b.removeFromTop (10);
    note.setBounds (b.removeFromBottom (66));
    b.removeFromBottom (8);

    auto row = b.removeFromBottom (28);
    closeBtn.setBounds (row.removeFromRight (160));
    b.removeFromBottom (8);

    dump.setBounds (b);
}

void SkipfiendAudioProcessorEditor::DebugOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.75f));

    auto b = getLocalBounds().reduced (56).toFloat();
    g.setColour (col::bg);
    g.fillRect (b);
    g.setColour (col::accent);
    g.drawRect (b, 1.5f);
}

void SkipfiendAudioProcessorEditor::OptionsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.75f));

    auto b = cardBounds().toFloat();
    g.setColour (col::shadow);
    g.fillRoundedRectangle (b.translated (0.0f, 3.0f), 6.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (col::accent);
    g.drawRoundedRectangle (b.reduced (0.75f), 6.0f, 1.5f);
}

//==============================================================================
void SkipfiendAudioProcessorEditor::SeqCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::bg);
    g.fillRect (r);
    if (engine >= 0)
    {
        g.setColour (skf::engineColour (engine).withAlpha (0.85f));
        g.fillRect (r.removeFromTop (r.getHeight() * 0.62f).reduced (2.0f));
        g.setColour (col::text);
        g.setFont (monoFont (10.0f, true));
        g.drawText ("x" + juce::String (repeats), getLocalBounds().removeFromBottom (16),
                    juce::Justification::centred);
        g.setColour (col::bg);
        g.setFont (monoFont (9.0f, true));
        g.drawText (juce::String (skf::engineName (engine)).substring (0, 3),
                    getLocalBounds().removeFromTop ((int) (getHeight() * 0.62f)),
                    juce::Justification::centred);
    }
    g.setColour (col::line);
    g.drawRect (getLocalBounds().toFloat(), 1.0f);
}

void SkipfiendAudioProcessorEditor::SeqCell::mouseDown (const juce::MouseEvent&)
{
    engine = (engine + 2) % (skf::NUM_ENGINES + 1) - 1;   // -1..7 cycle
    if (onEdit) onEdit (0);
    repaint();
}

void SkipfiendAudioProcessorEditor::SeqCell::mouseWheelMove (const juce::MouseEvent&,
                                                             const juce::MouseWheelDetails& w)
{
    repeats = juce::jlimit (2, 40, repeats + (w.deltaY > 0 ? 1 : -1));
    if (onEdit) onEdit (0);
    repaint();
}

//==============================================================================
void SkipfiendAudioProcessorEditor::registerParamControl (const juce::String& id, juce::Component& c, juce::Label* l)
{
    componentForParamId[id] = &c;
    if (l != nullptr) { labelForParamId[id] = l; baseLabelText[id] = l->getText(); }
}

void SkipfiendAudioProcessorEditor::applyMidiLearnTooltip (juce::Component& c, const juce::String&, const juce::String& baseTip)
{
    if (auto* tt = dynamic_cast<juce::SettableTooltipClient*> (&c))
        tt->setTooltip (baseTip.isNotEmpty() ? baseTip + "  (right-click to map MIDI)"
                                              : "Right-click to map MIDI.");
}

SkipfiendAudioProcessorEditor::Knob& SkipfiendAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& text, const juce::String& tip)
{
    auto* s = new Knob();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // Spec: a 270-degree sweep. JUCE defaults to 288, which reads subtly wrong
    // next to the value arc drawn around it.
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                            juce::MathConstants<float>::pi * 2.75f, true);
    s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s->setLookAndFeel (&lnf);
    s->onRightClick = [this, id, s] { showParamMenu (id, s); };
    canvas.addAndMakeVisible (s);
    knobs.add (s);
    sAtt.add (new SA (proc.apvts, id, *s));

    // Spec: double-click resets to default. The attachment has just given the
    // slider its real range, so the normalised default converts correctly here.
    if (auto* param = proc.apvts.getParameter (id))
        s->setDoubleClickReturnValue (true, s->proportionOfLengthToValue (param->getDefaultValue()));

    auto* l = new juce::Label ({}, text);
    l->setJustificationType (juce::Justification::centred);
    l->setFont (monoFont (9.0f));
    l->setColour (juce::Label::textColourId, col::dim);
    canvas.addAndMakeVisible (l);
    labels.add (l);

    applyMidiLearnTooltip (*s, id, tip);
    registerParamControl (id, *s, l);
    return *s;
}

SkipfiendAudioProcessorEditor::Combo& SkipfiendAudioProcessorEditor::addCombo (const juce::String& id, juce::StringArray items, const juce::String& tip)
{
    auto* c = new Combo();
    c->setLookAndFeel (&lnf);
    c->addItemList (items, 1);
    c->onRightClick = [this, id, c] { showParamMenu (id, c); };
    canvas.addAndMakeVisible (c);
    combos.add (c);
    cAtt.add (new CA (proc.apvts, id, *c));

    applyMidiLearnTooltip (*c, id, tip);
    registerParamControl (id, *c, nullptr);
    return *c;
}

//==============================================================================
SkipfiendAudioProcessorEditor::SkipfiendAudioProcessorEditor (SkipfiendAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    canvas.onPaint = [this] (juce::Graphics& g) { paintCanvasContents (g); };
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (wave);

    static const char* kEngineTip[skf::NUM_ENGINES] = {
        "Laser slip: short phase-aligned loop, seek click on recovery.",
        "Scratched disc losing its place: playhead jumps back mid-phrase, hard cuts.",
        "Stuck groove: tiny slice loops near-forever with slow pitch drift.",
        "Streaming rebuffer: most of the burst is muted, resumes on the tail.",
        "Wrong decode: bitcrush + pre-echo + ringing phantom frequencies.",
        "Tape edge damage: level sag, wow, and hiss on recovery.",
        "Accelerating retrigger burst, ascending pitch, decay envelope.",
        "Hard grid-synced gate chop -- the \"gator\": clicky, cut-up texture.",
        "Needle stuck in a locked groove: a clean, unmangled loop of one grid unit."
    };
    static_assert (sizeof (kEngineTip) / sizeof (kEngineTip[0]) == skf::NUM_ENGINES,
                   "engine tooltip table must cover every engine");

    // ---- engine strip ----
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const juce::String id = "en_" + juce::String (e);
        auto* t = new Toggle (skf::engineName (e));
        t->setLookAndFeel (&lnf);
        t->glowColour = skf::engineColour (e);
        t->onRightClick = [this, id, t] { showParamMenu (id, t); };
        canvas.addAndMakeVisible (t);
        engBtn.add (t);
        bAtt.add (new BA (proc.apvts, id, *t));
        applyMidiLearnTooltip (*t, id, kEngineTip[e]);
        registerParamControl (id, *t, nullptr);

        addKnob ("amt_"  + juce::String (e), "AMT", "How loud this engine's skips are relative to the others.");
        addKnob ("prob_" + juce::String (e), "PRB", "How often this engine is picked, weighted against the others enabled.");
        addKnob ("rate_" + juce::String (e), "RATE",
                 "Loop LENGTH for this effect: 1x is a one-bar loop, 32x a thirty-two-bar loop. "
                 "Turning it widens or tightens the loop that is already playing, in real time. "
                 "(The played octave decides how finely that loop is chopped.)");
    }

    // ---- repeat engine ----
    addKnob ("repMin",       "REPEATS MIN",  "Lower bound on how many times a burst repeats before it resolves.");
    addKnob ("repMax",       "REPEATS MAX",  "Upper bound on how many times a burst repeats before it resolves.");
    addKnob ("sliceMin",     "SLICE MIN",    "Shortest slice grabbed from the rolling buffer, in milliseconds.");
    addKnob ("sliceMax",     "SLICE MAX",    "Longest slice grabbed from the rolling buffer, in milliseconds.");
    addKnob ("pitchPerSkip", "PITCH STEP",   "Semitones applied per repeat in Ascending/Descending/Chromatic/Drift.");
    addKnob ("basePitch",    "BASE PITCH",   "Constant pitch offset applied to every repeat, in semitones.");
    addKnob ("timewarp",     "TIMEWARP",     "Speeds up (right) or slows down (left) playback rate over the burst.");
    addKnob ("motion",       "MOTION",       "Depth of the selected playback style: direction flips, fragment jumps, stereo travel and speed movement.");
    addCombo ("lenMode",   { "Fixed", "Ramp Shorter", "Ramp Longer", "Random" },
              "How slice length changes across the burst.");
    addCombo ("pitchMode", { "Stable", "Ascending", "Descending", "Chromatic", "Drift" },
              "How pitch changes across the burst.");
    addCombo ("volEnv",    { "Flat", "Decay", "Swell", "Tremolo", "Ducked" },
              "Volume shape applied across the burst.");
    addCombo ("panWalk",   { "Static", "Alternate", "Random", "Widen" },
              "Stereo placement across the burst.");
    addCombo ("endMode",   { "Hard Cut", "Tail Out", "Glitch Click", "Seek Noise", "Silence / Resume" },
              "What happens when the burst finishes.");
    addCombo ("playMode",  { "Classic", "Stutter Edit", "Ping-Pong", "Scatter", "Orbit", "Evolve" },
              "Changes how repeats move through the captured audio. Classic keeps the original straight playback; the other modes add phrase-level motion.");

    // ---- master ---- (MIX lives up in the performance row instead)
    addKnob ("density",  "SKIP DENSITY", "How often the plugin fires new skips.");
    addKnob ("chaos",    "CHAOS",        "0 = grid-locked. Higher = engines/timing/params drift and swap unpredictably.");
    addKnob ("swing",    "SWING",        "Delays every other grid step for a swung feel.");
    addKnob ("artifacts","ARTIFACTS",    "Amount of seek-click / laser-hunt / hiss / tape-stop noise between skips.");
    addKnob ("scThresh", "SC THRESH",    "Level the sidechain input must cross to fire a skip.");
    addCombo ("grid", skf::gridNames(),
              "Loop length / Rhythmic Gravity: 32 bars down to a 1/32 slice.");

    struct ToggleDef { const char* id; const char* name; const char* tip; };
    static const ToggleDef masterToggles[] = {
        { "grooveLock", "GROOVE LOCK",    "Bends the repeat count so every burst's length lands on the grid." },
        { "scTrigger",  "SIDECHAIN TRIG", "While held, drive the re-triggering from Sidechain-bus transients instead of the grid." },
        { "seqOn",      "SKIP LANGUAGE",  "While held, the 16-step sequencer below decides which engine fires per step." },
    };
    for (auto& td : masterToggles)
    {
        const juce::String id (td.id);
        auto* t = new Toggle (td.name);
        t->setLookAndFeel (&lnf);
        t->onRightClick = [this, id, t] { showParamMenu (id, t); };
        canvas.addAndMakeVisible (t);
        toggles.add (t);
        bAtt.add (new BA (proc.apvts, id, *t));
        applyMidiLearnTooltip (*t, id, td.tip);
        registerParamControl (id, *t, nullptr);
    }

    // ---- skip-language sequencer ----
    for (int i = 0; i < SkipfiendAudioProcessor::kSeqSteps; ++i)
    {
        auto* cell = new SeqCell();
        cell->engine  = proc.seqEngine[i].load();
        cell->repeats = proc.seqRepeats[i].load();
        cell->setTooltip ("Click: cycle engine (incl. off). Mouse-wheel: set repeat count.");
        cell->onEdit  = [this, i, cell] (int)
        {
            proc.seqEngine[i].store (cell->engine);
            proc.seqRepeats[i].store (cell->repeats);
        };
        canvas.addAndMakeVisible (cell);
        seqCells.add (cell);
    }

    // ---- footer ----
    captureBtn.setLookAndFeel (&lnf);
    midiExportBtn.setLookAndFeel (&lnf);
    captureBtn.setTooltip ("Write the current full state to Documents/SKIPFIEND for later reference.");
    midiExportBtn.setTooltip ("Export the retrigger pattern just played as a .mid file.");
    canvas.addAndMakeVisible (captureBtn);
    canvas.addAndMakeVisible (midiExportBtn);
    captureBtn.onClick    = [this] { proc.doCapture(); };
    midiExportBtn.onClick = [this] { proc.doMidiExport(); };

    // ---- header: bypass / help / dice / presets / meter ----
    bypassBtn.setLookAndFeel (&lnf);
    bypassBtn.onRightClick = [this] { showParamMenu ("bypass", &bypassBtn); };
    canvas.addAndMakeVisible (bypassBtn);
    bAtt.add (new BA (proc.apvts, "bypass", bypassBtn));
    applyMidiLearnTooltip (bypassBtn, "bypass", "Bypasses the effect entirely -- audio passes through untouched.");
    registerParamControl ("bypass", bypassBtn, nullptr);

    menuBtn.setLookAndFeel (&lnf);
    menuBtn.setTooltip ("Save / save as / open presets, export audio, options and about.");
    menuBtn.onClick = [this] { showMainMenu(); };
    canvas.addAndMakeVisible (menuBtn);

    abBtn.setLookAndFeel (&lnf);
    abBtn.setTooltip ("A/B compare: switch slots. The slot you leave keeps its own edit.");
    abBtn.onClick = [this] { setAbSlot (proc.getCurrentSlot() == 0 ? 1 : 0); };
    canvas.addAndMakeVisible (abBtn);

    abCopyBtn.setLookAndFeel (&lnf);
    abCopyBtn.setTooltip ("Copy the current slot over the other one.");
    abCopyBtn.onClick = [this] { proc.copyCurrentSlotToOther(); };
    canvas.addAndMakeVisible (abCopyBtn);

    helpBtn.setLookAndFeel (&lnf);
    helpBtn.setTooltip ("Open the manual.");
    helpBtn.onClick = [this] { help.setVisible (! help.isVisible()); if (help.isVisible()) { help.toFront (true); grabKeyboardFocus(); } };
    canvas.addAndMakeVisible (helpBtn);

    randomBtn.setLookAndFeel (&lnf);
    randomBtn.setTooltip ("Randomise the skip engines and repeat/master parameters.");
    randomBtn.onClick = [this] { proc.randomizeSkipParams(); };
    canvas.addAndMakeVisible (randomBtn);

    presetCombo.setLookAndFeel (&lnf);
    presetCombo.setTooltip ("Load a factory preset.");
    refreshPresetCombo();
    presetCombo.onChange = [this]
    {
        const int idx = presetCombo.getSelectedItemIndex();
        if (idx >= 0) proc.loadFactoryPreset (idx);
    };
    canvas.addAndMakeVisible (presetCombo);

    presetSaveBtn.setLookAndFeel (&lnf);
    presetSaveBtn.setTooltip ("Save the current settings as a .skipfiend preset file.");
    presetSaveBtn.onClick = [this] { savePresetViaChooser(); };
    canvas.addAndMakeVisible (presetSaveBtn);

    presetLoadBtn.setLookAndFeel (&lnf);
    presetLoadBtn.setTooltip ("Load settings from a .skipfiend preset file.");
    presetLoadBtn.onClick = [this] { loadPresetViaChooser(); };
    canvas.addAndMakeVisible (presetLoadBtn);

    canvas.addAndMakeVisible (meter);

    // ---- live performance row -------------------------------------------
    bpmSyncBtn.setLookAndFeel (&lnf);
    bpmSyncBtn.onRightClick = [this] { showParamMenu ("bpmSync", &bpmSyncBtn); };
    canvas.addAndMakeVisible (bpmSyncBtn);
    bAtt.add (new BA (proc.apvts, "bpmSync", bpmSyncBtn));
    applyMidiLearnTooltip (bpmSyncBtn, "bpmSync",
        "Follow the host's tempo. Turn off (or run standalone) to use the BPM knob instead.");
    registerParamControl ("bpmSync", bpmSyncBtn, nullptr);

    bpmKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    bpmKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    bpmKnob.setLookAndFeel (&lnf);
    bpmKnob.onRightClick = [this] { showParamMenu ("manualBpm", &bpmKnob); };
    canvas.addAndMakeVisible (bpmKnob);
    sAtt.add (new SA (proc.apvts, "manualBpm", bpmKnob));
    applyMidiLearnTooltip (bpmKnob, "manualBpm", "Tempo used when not synced to a host.");
    bpmLabel.setFont (monoFont (9.0f));
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setColour (juce::Label::textColourId, col::dim);
    canvas.addAndMakeVisible (bpmLabel);
    registerParamControl ("manualBpm", bpmKnob, nullptr);

    tapBtn.setLookAndFeel (&lnf);
    tapBtn.setTooltip ("Tap four times in time to set the BPM the effects lock to. "
                       "Tapping switches SYNC off, since you're setting the tempo by hand.");
    tapBtn.onClick = [this] { proc.tapTempo(); };
    canvas.addAndMakeVisible (tapBtn);

    resetBtn.setLookAndFeel (&lnf);
    resetBtn.setTooltip ("Reset every parameter back to its default.");
    resetBtn.onClick = [this] { proc.resetAllToDefaults(); };
    canvas.addAndMakeVisible (resetBtn);

    // TRIGGER is the primary way to play the effect by hand: it only runs while held
    triggerBtn.setLookAndFeel (&lnf);
    triggerBtn.setTooltip ("Hold to run the effect at full wet. Release to stop. "
                           "The effect only ever runs while this is held or a MIDI key is down.");
    triggerBtn.onPress   = [this] { proc.setManualTrigger (true); };
    triggerBtn.onRelease = [this] { proc.setManualTrigger (false); };
    canvas.addAndMakeVisible (triggerBtn);

    randomTriggerBtn.setLookAndFeel (&lnf);
    randomTriggerBtn.setTooltip ("Hold to randomise every engine and run at full wet. "
                                 "Release to stop. Turn on LATCH to toggle instead of hold.");
    randomTriggerBtn.onPress   = [this] { proc.engageRandomTrigger(); };
    randomTriggerBtn.onRelease = [this] { proc.releaseRandomTrigger(); };
    canvas.addAndMakeVisible (randomTriggerBtn);

    latchBtn.setLookAndFeel (&lnf);
    latchBtn.setClickingTogglesState (true);
    latchBtn.setTooltip ("Make RANDOM TRIGGER latch on/off per click instead of hold-to-perform.");
    latchBtn.onClick = [this]
    {
        const bool on = latchBtn.getToggleState();
        randomTriggerBtn.latching = on;
        triggerBtn.latching = on;
        if (! on && randomTriggerBtn.latchedOn)
        {
            randomTriggerBtn.latchedOn = false;
            proc.releaseRandomTrigger();
        }
        if (! on && triggerBtn.latchedOn)
        {
            triggerBtn.latchedOn = false;
            proc.setManualTrigger (false);
        }
    };
    canvas.addAndMakeVisible (latchBtn);

    // big red dry/wet -- the one control a DJ grabs mid-set
    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    mixKnob.setLookAndFeel (&lnf);
    mixKnob.getProperties().set ("accentColour", (int) col::red.getARGB());
    mixKnob.onRightClick = [this] { showParamMenu ("mix", &mixKnob); };
    canvas.addAndMakeVisible (mixKnob);
    sAtt.add (new SA (proc.apvts, "mix", mixKnob));
    applyMidiLearnTooltip (mixKnob, "mix",
        "DRY/WET. Fully left = effect off (clean signal), fully right = effect only.");
    mixLabel.setText ("DRY / WET", juce::dontSendNotification);
    mixLabel.setFont (monoFont (9.0f, true));
    mixLabel.setJustificationType (juce::Justification::centred);
    mixLabel.setColour (juce::Label::textColourId, col::red);
    canvas.addAndMakeVisible (mixLabel);
    registerParamControl ("mix", mixKnob, nullptr);

    // ---- sample deck ----
    sampleNameLabel.setFont (monoFont (10.0f));
    sampleNameLabel.setColour (juce::Label::textColourId, col::dim);
    canvas.addAndMakeVisible (sampleNameLabel);

    loadSampleBtn.setLookAndFeel (&lnf);
    loadSampleBtn.setTooltip ("Open a file browser to load a test sample.");
    loadSampleBtn.onClick = [this] { loadSampleViaChooser(); };
    canvas.addAndMakeVisible (loadSampleBtn);

    playSampleBtn.setLookAndFeel (&lnf);
    playSampleBtn.setClickingTogglesState (true);
    playSampleBtn.setTooltip ("Play or stop the loaded test sample.");
    playSampleBtn.onClick = [this] { proc.setSamplePlaying (playSampleBtn.getToggleState()); };
    canvas.addAndMakeVisible (playSampleBtn);

    loopSampleBtn.setLookAndFeel (&lnf);
    loopSampleBtn.setClickingTogglesState (true);
    loopSampleBtn.setToggleState (true, juce::dontSendNotification);
    loopSampleBtn.setTooltip ("Loop the test sample when it reaches the end.");
    loopSampleBtn.onClick = [this] { proc.setSampleLooping (loopSampleBtn.getToggleState()); };
    canvas.addAndMakeVisible (loopSampleBtn);

    sourceSampleBtn.setLookAndFeel (&lnf);
    sourceSampleBtn.setClickingTogglesState (true);
    sourceSampleBtn.setTooltip ("Feed the effect from the test sample instead of live/host input.");
    sourceSampleBtn.onClick = [this] { proc.setUseSampleSource (sourceSampleBtn.getToggleState()); };
    canvas.addAndMakeVisible (sourceSampleBtn);

    fullBufferBtn.setLookAndFeel (&lnf);
    fullBufferBtn.setClickingTogglesState (true);
    fullBufferBtn.setTooltip ("Buffer the WHOLE track on load instead of a rolling ~64s window. "
                              "Loops then never age out and the playhead can lag as far behind as you like. "
                              "Uses memory in proportion to track length (capped at 5 minutes). "
                              "Switch on BEFORE loading the track.");
    fullBufferBtn.onClick = [this] { proc.setFullTrackBuffer (fullBufferBtn.getToggleState()); };
    canvas.addAndMakeVisible (fullBufferBtn);

    sampleGainKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sampleGainKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sampleGainKnob.setLookAndFeel (&lnf);
    sampleGainKnob.setRange (0.0, 2.0, 0.001);
    sampleGainKnob.setValue (1.0, juce::dontSendNotification);
    sampleGainKnob.setTooltip ("Level of the test sample feeding into the effect.");
    sampleGainKnob.onValueChange = [this] { proc.setSampleGain ((float) sampleGainKnob.getValue()); };
    canvas.addAndMakeVisible (sampleGainKnob);
    sampleGainLabel.setFont (monoFont (9.0f));
    sampleGainLabel.setJustificationType (juce::Justification::centred);
    sampleGainLabel.setColour (juce::Label::textColourId, col::dim);
    canvas.addAndMakeVisible (sampleGainLabel);

    // ---- overlay effects strip: played over the top of the main effect ----
    {
        struct OvDef { MomentaryBtn* btn; Combo* box; const char* rateId; int id; const char* tip; };
        const OvDef defs[] = {
            { &echoBtn,    &echoRateBox,  "echoRate",  SkipfiendAudioProcessor::OV_ECHO,
              "Hold for tempo-synced echo over the top of the main effect. Repeats ring out after release." },
            { &delayBtn,   &delayRateBox, "delayRate", SkipfiendAudioProcessor::OV_DELAY,
              "Hold for a cleaner tempo-synced delay over the top. Repeats ring out after release." },
            { &dubBtn,     &dubRateBox,   "dubRate",   SkipfiendAudioProcessor::OV_DUB,
              "Hold for dark, high-feedback dub repeats. Rings out long after release." },
            { &reverseBtn, &revBarsBox,   "revBars",   SkipfiendAudioProcessor::OV_REVERSE,
              "Hold to loop the last N bars backwards. The knob sets how many bars (1 to 32)." },
        };

        for (auto& d : defs)
        {
            const int id = d.id;
            d.btn->setLookAndFeel (&lnf);
            d.btn->setTooltip (d.tip);
            d.btn->onPress   = [this, id] { proc.setOverlayHeld (id, true); };
            d.btn->onRelease = [this, id] { proc.setOverlayHeld (id, false); };
            canvas.addAndMakeVisible (*d.btn);

            const juce::String rid (d.rateId);
            d.box->setLookAndFeel (&lnf);
            if (auto* rateParam = proc.apvts.getParameter (rid))
                d.box->addItemList (rateParam->getAllValueStrings(), 1);
            d.box->onRightClick = [this, rid, box = d.box] { showParamMenu (rid, box); };
            canvas.addAndMakeVisible (*d.box);
            cAtt.add (new CA (proc.apvts, rid, *d.box));
            applyMidiLearnTooltip (*d.box, rid, d.tip);
            registerParamControl (rid, *d.box, nullptr);
        }
    }

    // ---- MIDI-learn status banner + highlight ----
    statusLabel.setFont (monoFont (10.0f));
    statusLabel.setColour (juce::Label::textColourId, col::dim);
    canvas.addAndMakeVisible (statusLabel);
    canvas.addChildComponent (learnHighlight);

    // ---- help overlay (top-level, unaffected by canvas scaling) ----
    // Named so they can be found and driven by tests and accessibility tools.
    help.setName ("help");
    options.setName ("options");
    debugPanel.setName ("debug");
    secretPanel.setName ("secret");

    addChildComponent (help);
    help.closeBtn.setLookAndFeel (&lnf);
    help.closeBtn.onClick = [this] { help.setVisible (false); };

    addChildComponent (options);
    options.title.setLookAndFeel (&lnf);
    options.tooltipsBtn.setLookAndFeel (&lnf);
    options.tooltipsBtn.onClick = [this]
    {
        proc.tooltipsEnabled.store (options.tooltipsBtn.getToggleState());
        applyTooltipMode();
    };
    options.deviceBtn.setLookAndFeel (&lnf);
    options.clearMidiBtn.setLookAndFeel (&lnf);
    options.clearMidiBtn.onClick = [this]
    {
        for (const auto& entry : componentForParamId)
            proc.clearMidiMapping (entry.first);

        flashStatus ("MIDI MAPPINGS CLEARED");
    };
    options.closeBtn.setLookAndFeel (&lnf);
    options.closeBtn.onClick = [this] { options.setVisible (false); };

    help.debugBtn.setLookAndFeel (&lnf);
    help.debugBtn.setTooltip ("Open the debug window: live internals, the crash log switch, "
                              "and the troubleshooting export.");
    help.debugBtn.onClick = [this] { showDebugPanel(); };

    addChildComponent (debugPanel);
    debugPanel.title.setLookAndFeel (&lnf);
    debugPanel.dump.setLookAndFeel (&lnf);
    debugPanel.crashLogBtn.setLookAndFeel (&lnf);
    debugPanel.troubleshootBtn.setLookAndFeel (&lnf);
    debugPanel.hardResetBtn.setLookAndFeel (&lnf);
    debugPanel.openFolderBtn.setLookAndFeel (&lnf);
    debugPanel.closeBtn.setLookAndFeel (&lnf);

    debugPanel.crashLogBtn.onClick = [this]
    {
        const bool on = debugPanel.crashLogBtn.getToggleState();
        proc.setCrashLogEnabled (on);

        if (on)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Crash logging on",
                "Writing to:\n" + proc.getCrashLogFile().getFullPathName()
                    + "\n\nLeave this on, reproduce the crash, then send that file to "
                      SKIPFIEND_SUPPORT_EMAIL " along with the troubleshooting file.",
                "OK");
    };

    debugPanel.troubleshootBtn.onClick = [this]
    {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
        auto f = SkipfiendAudioProcessor::getDiagnosticsDir()
                     .getChildFile ("skipfiend_troubleshooting_" + stamp + ".txt");

        if (proc.exportTroubleshootingFile (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Troubleshooting file saved",
                "Saved to:\n" + f.getFullPathName()
                    + "\n\nThis is the light diagnostic: your settings, audio and MIDI "
                      "setup, version and licence, and which host you are in. Send it to "
                      "support with a description of the problem.",
                "OK");
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Could not save",
                "Failed to write:\n" + f.getFullPathName(), "OK");
    };

    debugPanel.hardResetBtn.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
            "Reset all settings to default?",
            "This is much more destructive than the RESET button. It wipes every "
            "setting, every MIDI mapping, both A/B slots and the cache folder.\n\n"
            "Your saved presets and exports are NOT touched.\n\nThis cannot be undone.",
            "RESET EVERYTHING", "CANCEL", nullptr,
            juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 0)
                    return;

                proc.hardResetAndClearCache();
                debugPanel.crashLogBtn.setToggleState (false, juce::dontSendNotification);
                options.tooltipsBtn.setToggleState (true, juce::dontSendNotification);
                applyTooltipMode();
                setAbSlot (0);
                flashStatus ("HARD RESET - SETTINGS AND CACHE CLEARED", 6);
            }));
    };

    debugPanel.openFolderBtn.onClick = [this]
    {
        SkipfiendAudioProcessor::getDiagnosticsDir().revealToUser();
    };

    debugPanel.closeBtn.onClick = [this] { debugPanel.setVisible (false); };

    // ---- the hidden effect ------------------------------------------------
    secretPanel.onPaint = [this] (juce::Graphics& g)
    {
        auto r = secretPanel.getLocalBounds().toFloat();
        g.setColour (col::shadow);
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 4.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::accent2);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    };
    canvas.addChildComponent (secretPanel);

    ruinTitle.setText ("RUIN  //  hidden", juce::dontSendNotification);
    ruinTitle.setFont (labelFont (10.0f, true));
    ruinTitle.setColour (juce::Label::textColourId, col::accent2);
    secretPanel.addAndMakeVisible (ruinTitle);

    const juce::String secretTip = "SECRET HIDDEN EFFECT -- you found RUIN. ";

    ruinOnBtn.setLookAndFeel (&lnf);
    ruinOnBtn.setTooltip (secretTip + "Switches the hidden destroyer on. It sits after "
                                      "everything else, so it ruins the finished output.");
    ruinOnBtn.onRightClick = [this] { showParamMenu ("ruinOn", &ruinOnBtn); };
    secretPanel.addAndMakeVisible (ruinOnBtn);
    bAtt.add (new BA (proc.apvts, "ruinOn", ruinOnBtn));
    registerParamControl ("ruinOn", ruinOnBtn, nullptr);

    struct { Knob* k; juce::Label* l; const char* id; const char* name; const char* tip; }
    ruinCtl[] =
    {
        { &ruinAmtKnob,  &ruinAmtLabel,  "ruinAmt",  "AMOUNT",
          "how hard it bites: bit depth collapses from 16 bits down to 2." },
        { &ruinRateKnob, &ruinRateLabel, "ruinRate", "RATE",
          "sample-and-hold rate, from a 200Hz grind up to barely touched." },
        { &ruinToneKnob, &ruinToneLabel, "ruinTone", "TONE",
          "tilts the wreckage from dull and muffled to thin and brittle." },
    };

    for (auto& rc : ruinCtl)
    {
        rc.k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        rc.k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
        rc.k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        rc.k->setLookAndFeel (&lnf);
        rc.k->getProperties().set ("accentColour", (int) col::accent2.getARGB());
        const juce::String id (rc.id);
        rc.k->onRightClick = [this, id, rc] { showParamMenu (id, rc.k); };
        secretPanel.addAndMakeVisible (rc.k);
        sAtt.add (new SA (proc.apvts, id, *rc.k));

        if (auto* param = proc.apvts.getParameter (id))
            rc.k->setDoubleClickReturnValue (true,
                rc.k->proportionOfLengthToValue (param->getDefaultValue()));

        rc.l->setText (rc.name, juce::dontSendNotification);
        rc.l->setJustificationType (juce::Justification::centred);
        rc.l->setFont (monoFont (9.0f));
        rc.l->setColour (juce::Label::textColourId, col::dim);
        secretPanel.addAndMakeVisible (rc.l);

        applyMidiLearnTooltip (*rc.k, id, secretTip + rc.tip);
        registerParamControl (id, *rc.k, rc.l);
    }

    secretCloseBtn.setLookAndFeel (&lnf);
    secretCloseBtn.setTooltip ("Close the hidden effect. RUIN keeps running if you left "
                               "it switched on.");
    secretCloseBtn.onClick = [this] { secretPanel.setVisible (false); };
    secretPanel.addAndMakeVisible (secretCloseBtn);

    // The notch in the very top-left corner is the way in.
    canvas.onMouseDown = [this] (const juce::MouseEvent& e)
    {
        if (e.x < 14 && e.y < 14 && (e.x + e.y) < 16)
            toggleSecretPanel();
    };

    setAbSlot (proc.getCurrentSlot());
    applyTooltipMode();

    canvas.setSize (kCanvasW, kCanvasH);
    layoutCanvasContents();

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kCanvasW / (double) kCanvasH);
        c->setSizeLimits (kCanvasW / 2, kCanvasH / 2, kCanvasW * 2, kCanvasH * 2);
    }
    setSize (kCanvasW, kCanvasH);

    startTimerHz (kUiRefreshHz);
}

SkipfiendAudioProcessorEditor::~SkipfiendAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    for (auto* s : knobs)  s->setLookAndFeel (nullptr);
    for (auto* c : combos) c->setLookAndFeel (nullptr);
    for (auto* t : engBtn) t->setLookAndFeel (nullptr);
    for (auto* t : toggles) t->setLookAndFeel (nullptr);
    captureBtn.setLookAndFeel (nullptr);
    midiExportBtn.setLookAndFeel (nullptr);
    bypassBtn.setLookAndFeel (nullptr);
    helpBtn.setLookAndFeel (nullptr);
    randomBtn.setLookAndFeel (nullptr);
    presetCombo.setLookAndFeel (nullptr);
    presetSaveBtn.setLookAndFeel (nullptr);
    presetLoadBtn.setLookAndFeel (nullptr);
    loadSampleBtn.setLookAndFeel (nullptr);
    playSampleBtn.setLookAndFeel (nullptr);
    loopSampleBtn.setLookAndFeel (nullptr);
    sourceSampleBtn.setLookAndFeel (nullptr);
    fullBufferBtn.setLookAndFeel (nullptr);
    sampleGainKnob.setLookAndFeel (nullptr);
    bpmSyncBtn.setLookAndFeel (nullptr);
    tapBtn.setLookAndFeel (nullptr);
    bpmKnob.setLookAndFeel (nullptr);
    echoBtn.setLookAndFeel (nullptr);
    delayBtn.setLookAndFeel (nullptr);
    dubBtn.setLookAndFeel (nullptr);
    reverseBtn.setLookAndFeel (nullptr);
    echoRateBox.setLookAndFeel (nullptr);
    delayRateBox.setLookAndFeel (nullptr);
    dubRateBox.setLookAndFeel (nullptr);
    revBarsBox.setLookAndFeel (nullptr);
    resetBtn.setLookAndFeel (nullptr);
    triggerBtn.setLookAndFeel (nullptr);
    randomTriggerBtn.setLookAndFeel (nullptr);
    latchBtn.setLookAndFeel (nullptr);
    mixKnob.setLookAndFeel (nullptr);
    help.closeBtn.setLookAndFeel (nullptr);
    menuBtn.setLookAndFeel (nullptr);
    abBtn.setLookAndFeel (nullptr);
    abCopyBtn.setLookAndFeel (nullptr);
    options.title.setLookAndFeel (nullptr);
    options.tooltipsBtn.setLookAndFeel (nullptr);
    options.deviceBtn.setLookAndFeel (nullptr);
    options.clearMidiBtn.setLookAndFeel (nullptr);
    options.closeBtn.setLookAndFeel (nullptr);
    help.debugBtn.setLookAndFeel (nullptr);
    debugPanel.title.setLookAndFeel (nullptr);
    debugPanel.dump.setLookAndFeel (nullptr);
    debugPanel.crashLogBtn.setLookAndFeel (nullptr);
    debugPanel.troubleshootBtn.setLookAndFeel (nullptr);
    debugPanel.hardResetBtn.setLookAndFeel (nullptr);
    debugPanel.openFolderBtn.setLookAndFeel (nullptr);
    debugPanel.closeBtn.setLookAndFeel (nullptr);
    ruinOnBtn.setLookAndFeel (nullptr);
    ruinAmtKnob.setLookAndFeel (nullptr);
    ruinRateKnob.setLookAndFeel (nullptr);
    ruinToneKnob.setLookAndFeel (nullptr);
    secretCloseBtn.setLookAndFeel (nullptr);
}

//==============================================================================
bool SkipfiendAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && debugPanel.isVisible()) { debugPanel.setVisible (false); return true; }
    if (k == juce::KeyPress::escapeKey && options.isVisible()) { options.setVisible (false); return true; }
    if (k == juce::KeyPress::escapeKey && help.isVisible()) { help.setVisible (false); return true; }
    return false;
}

bool SkipfiendAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (SkipfiendAudioProcessor::isSupportedAudioFile (juce::File (f))) return true;
    return false;
}

void SkipfiendAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    wave.dragHighlight.store (true);
}

void SkipfiendAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    wave.dragHighlight.store (false);
}

void SkipfiendAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    wave.dragHighlight.store (false);
    for (auto& f : files)
    {
        juce::File file (f);
        if (SkipfiendAudioProcessor::isSupportedAudioFile (file)) { tryLoadSample (file); break; }
    }
}

void SkipfiendAudioProcessorEditor::tryLoadSample (const juce::File& f)
{
    proc.loadSampleFile (f);
    sourceSampleBtn.setToggleState (proc.isUsingSampleSource(), juce::dontSendNotification);
    playSampleBtn.setToggleState (proc.isSamplePlaying(), juce::dontSendNotification);
}

void SkipfiendAudioProcessorEditor::loadSampleViaChooser()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Load a test sample...", juce::File(),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile()) tryLoadSample (file);
        });
}

void SkipfiendAudioProcessorEditor::savePresetViaChooser()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("SKIPFIEND").getChildFile ("Presets");
    dir.createDirectory();
    auto chooser = std::make_shared<juce::FileChooser> ("Save preset...",
        dir.getChildFile ("Preset.skipfiend"), "*.skipfiend");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File())
            {
                currentPresetFile = file.withFileExtension ("skipfiend");
                proc.savePresetToFile (currentPresetFile);
            }
        });
}

void SkipfiendAudioProcessorEditor::loadPresetViaChooser()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("SKIPFIEND").getChildFile ("Presets");
    auto chooser = std::make_shared<juce::FileChooser> ("Load preset...", dir, "*.skipfiend");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                currentPresetFile = file;     // plain Save now writes back here
                proc.loadPresetFromFile (file);
            }
        });
}

// ---------------------------------------------------------------------------
//  MENU dropdown: save / save as / open / export / options / about
// ---------------------------------------------------------------------------
static juce::File skipfiendUserDir (const juce::String& sub)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("SKIPFIEND").getChildFile (sub);
    dir.createDirectory();
    return dir;
}

void SkipfiendAudioProcessorEditor::showMainMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);

    const bool haveFile = (currentPresetFile != juce::File());
    // Always enabled: with no file yet, Save falls through to Save As rather
    // than presenting a dead menu item.
    m.addItem (1, haveFile ? "Save  (" + currentPresetFile.getFileName() + ")" : "Save...");
    m.addItem (2, "Save as...");
    m.addItem (3, "Open preset...");
    m.addSeparator();

    const double exportable = proc.getExportableSeconds();
    juce::PopupMenu exportMenu;
    exportMenu.setLookAndFeel (&lnf);
    exportMenu.addItem (10, "Last 8 seconds",  exportable > 0.25);
    exportMenu.addItem (11, "Last 30 seconds", exportable > 0.25);
    exportMenu.addItem (12, "Everything buffered ("
                             + juce::String (exportable, 1) + "s)", exportable > 0.25);
    exportMenu.addSeparator();
    exportMenu.addSectionHeader ("Quality");
    exportMenu.addItem (20, "16-bit WAV",       true, exportBitDepth == 16);
    exportMenu.addItem (21, "24-bit WAV",       true, exportBitDepth == 24);
    exportMenu.addItem (22, "32-bit float WAV", true, exportBitDepth == 32);
    m.addSubMenu ("Export audio to .wav", exportMenu, exportable > 0.25);
    m.addSeparator();

    m.addItem (4, "Open preset folder in Explorer");
    m.addItem (5, "Options...");
    m.addSeparator();
    m.addItem (6, "About SKIPFIEND");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn),
        [this] (int result)
        {
            switch (result)
            {
                case 1:  savePreset (false); break;
                case 2:  savePreset (true);  break;
                case 3:  loadPresetViaChooser(); break;
                case 4:  skipfiendUserDir ("Presets").revealToUser(); break;
                case 5:  showOptions(); break;
                case 6:  showAbout();   break;
                case 10: exportAudioViaChooser (8.0);  break;
                case 11: exportAudioViaChooser (30.0); break;
                case 12: exportAudioViaChooser (0.0);  break;
                // Quality is remembered, then the user picks a length.
                case 20: exportBitDepth = 16; showMainMenu(); break;
                case 21: exportBitDepth = 24; showMainMenu(); break;
                case 22: exportBitDepth = 32; showMainMenu(); break;
                default: break;
            }
        });
}

// Save straight back to the file this session came from; fall back to Save As
// the first time, when there is nowhere to write yet.
void SkipfiendAudioProcessorEditor::savePreset (bool forceChooser)
{
    if (forceChooser || currentPresetFile == juce::File())
    {
        savePresetViaChooser();
        return;
    }

    proc.savePresetToFile (currentPresetFile);
    flashStatus ("SAVED  " + currentPresetFile.getFileName());
}

void SkipfiendAudioProcessorEditor::exportAudioViaChooser (double seconds)
{
    auto dir = skipfiendUserDir ("Exports");
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");

    auto chooser = std::make_shared<juce::FileChooser> ("Export audio...",
        dir.getChildFile ("SKIPFIEND_" + stamp + ".wav"), "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser, seconds] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File())
                return;

            file = file.withFileExtension ("wav");

            if (proc.exportAudioToWav (file, seconds, exportBitDepth))
            {
                const double len = juce::jmax (0.0, seconds > 0.0 ? juce::jmin (seconds, proc.getExportableSeconds())
                                                                  : proc.getExportableSeconds());
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    "Export complete",
                    "Wrote " + file.getFileName() + "\n\n"
                    "Location:  " + file.getParentDirectory().getFullPathName() + "\n"
                    "Length:    " + juce::String (len, 2) + " seconds\n"
                    "Quality:   " + juce::String (exportBitDepth)
                        + (exportBitDepth == 32 ? "-bit float WAV, " : "-bit WAV, ")
                        + juce::String (proc.getSampleRate() / 1000.0, 1) + " kHz",
                    "OK");
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Export failed",
                    "Could not write to:\n" + file.getFullPathName()
                        + "\n\nCheck that the folder exists and is writable.",
                    "OK");
            }
        });
}

void SkipfiendAudioProcessorEditor::showOptions()
{
    options.tooltipsBtn.setToggleState (proc.tooltipsEnabled.load(), juce::dontSendNotification);

    const bool standalone = juce::JUCEApplicationBase::isStandaloneApp();
    options.deviceBtn.setEnabled (standalone);
    options.deviceNote.setText (standalone
        ? "Standalone: audio device and MIDI inputs are chosen in the app's own "
          "settings panel (the gear icon in the window's title bar)."
        : "Running as a plugin: the host owns the audio device and MIDI routing, "
          "so set those up in your DAW's preferences.",
        juce::dontSendNotification);

    options.setVisible (true);
    options.toFront (true);
    grabKeyboardFocus();
}

void SkipfiendAudioProcessorEditor::flashStatus (const juce::String& message, int seconds)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusHoldTicks = juce::jmax (1, seconds * kUiRefreshHz);
}

void SkipfiendAudioProcessorEditor::toggleSecretPanel()
{
    const bool show = ! secretPanel.isVisible();
    secretPanel.setVisible (show);

    if (show)
    {
        secretPanel.toFront (false);
        flashStatus ("RUIN", 3);
    }
}

void SkipfiendAudioProcessorEditor::showDebugPanel()
{
    debugPanel.crashLogBtn.setToggleState (proc.isCrashLogEnabled(), juce::dontSendNotification);
    debugPanel.dump.setText (proc.getLiveDebugDump(), false);
    debugPanel.setVisible (true);
    debugPanel.toFront (true);
    grabKeyboardFocus();
}

void SkipfiendAudioProcessorEditor::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
        "SKIPFIEND " SKIPFIEND_VERSION,
        "SKIPFIEND  -  playback failure unit\n"
        "Version " SKIPFIEND_VERSION "\n"
        "(c) FiendAudio\n\n"
        "Licence:   one seat per user, see LICENCE.txt in the install folder.\n"
        "Homepage:  " SKIPFIEND_HOMEPAGE "\n"
        "Source:    " SKIPFIEND_GITHUB "\n"
        "Support:   " SKIPFIEND_SUPPORT_EMAIL "\n\n"
        "Press HELP for the full manual, troubleshooting and install notes.",
        "OK");
}

void SkipfiendAudioProcessorEditor::applyTooltipMode()
{
    lastTooltipsApplied = proc.tooltipsEnabled.load();
    // Spec: tooltips appear after 400ms hover. Off means never.
    tooltipWindow.setMillisecondsBeforeTipAppears (proc.tooltipsEnabled.load() ? 400 : 0x3fffffff);
}

void SkipfiendAudioProcessorEditor::setAbSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    proc.setCurrentSlot (slot);
    abBtn.setButtonText (slot == 0 ? "A" : "B");
    abCopyBtn.setButtonText (slot == 0 ? "A>B" : "B>A");
    refreshPresetCombo();
}

void SkipfiendAudioProcessorEditor::refreshPresetCombo()
{
    presetCombo.clear (juce::dontSendNotification);
    presetCombo.addItemList (SkipfiendAudioProcessor::getFactoryPresetNames(), 1);
}

void SkipfiendAudioProcessorEditor::showParamMenu (const juce::String& paramId, juce::Component* c)
{
    auto* param = proc.apvts.getParameter (paramId);
    if (param == nullptr) return;

    const bool learningThis = proc.isMidiLearning() && proc.getMidiLearnTargetId() == paramId;
    const int mappedCc = proc.getMappedCcFor (paramId);

    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader (param->getName (64));
    if (learningThis)
        m.addItem (1, "Cancel MIDI Learn");
    else
        m.addItem (1, mappedCc >= 0 ? "Re-learn MIDI CC (currently CC " + juce::String (mappedCc) + ")"
                                    : "MIDI Learn...");
    m.addItem (2, "Clear MIDI Mapping", mappedCc >= 0);
    m.addSeparator();
    m.addItem (3, "Reset to Default");
    m.addItem (4, "Type a Value...");

    juce::Component::SafePointer<SkipfiendAudioProcessorEditor> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (c),
        [safeThis, paramId, param, learningThis] (int result)
        {
            if (safeThis == nullptr) return;
            if (result == 1)
            {
                if (learningThis) safeThis->proc.cancelMidiLearn();
                else               safeThis->proc.startMidiLearn (paramId);
            }
            else if (result == 2) safeThis->proc.clearMidiMapping (paramId);
            else if (result == 3) param->setValueNotifyingHost (param->getDefaultValue());
            else if (result == 4)
            {
                // Type an exact value, as include.md asks for. The window owns
                // itself and is deleted when it is dismissed.
                auto* w = new juce::AlertWindow (param->getName (64),
                                                 "Enter a value for this control.",
                                                 juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("value", param->getCurrentValueAsText(), "Value:");
                w->addButton ("SET",    1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                w->enterModalState (true,
                    juce::ModalCallbackFunction::create ([w, param] (int choice)
                    {
                        if (choice != 1)
                            return;

                        const auto typed = w->getTextEditorContents ("value").trim();

                        if (typed.isNotEmpty())
                            param->setValueNotifyingHost (
                                juce::jlimit (0.0f, 1.0f, param->getValueForText (typed)));
                    }), true);
            }
        });
}

//==============================================================================
void SkipfiendAudioProcessorEditor::timerCallback()
{
    // ---- output meter ----
    const float atk = 0.6f, rel = 0.12f;
    auto smooth = [] (float cur, float target, float a, float r)
    { return target > cur ? cur + a * (target - cur) : cur + r * (target - cur); };
    meter.l  = smooth (meter.l,  proc.outPeakL.load(),        atk, rel);
    meter.r  = smooth (meter.r,  proc.outPeakR.load(),        atk, rel);
    meter.gr = smooth (meter.gr, proc.outGainReduction.load(),atk, rel);
    meter.update (meter.l, meter.r, meter.gr, 1.0 / (double) kUiRefreshHz);
    meter.repaint();

    // ---- re-sync to state the host may have replaced underneath us ----
    if (const bool tips = proc.tooltipsEnabled.load(); tips != lastTooltipsApplied)
    {
        lastTooltipsApplied = tips;
        options.tooltipsBtn.setToggleState (tips, juce::dontSendNotification);
        applyTooltipMode();
    }

    if (abBtn.getButtonText() != (proc.getCurrentSlot() == 0 ? "A" : "B"))
        setAbSlot (proc.getCurrentSlot());

    // ---- debug window: stream the raw internals while it is open ----
    // Rebuilding the whole dump fights the scrollbar and allocates a large
    // string, so do it a few times a second rather than every frame.
    if (debugPanel.isVisible() && ++debugTick % 5 == 0)
    {
        const int caret = debugPanel.dump.getCaretPosition();
        debugPanel.dump.setText (proc.getLiveDebugDump(), false);
        debugPanel.dump.setCaretPosition (caret);
    }

    // ---- signature LED ----
    const float outPk = juce::jmax (proc.outPeakL.load(), proc.outPeakR.load());
    const float outDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, outPk));
    ledClip = (outDb >= 0.0f);
    // -60dB..0dB maps to unlit..full; rises instantly, falls back gently
    const float ledTarget = juce::jlimit (0.0f, 1.0f, (outDb + 60.0f) / 60.0f);
    ledLevel = ledTarget > ledLevel ? ledTarget : ledLevel + 0.15f * (ledTarget - ledLevel);

    // ---- per-engine glow on the engine strip ----
    const juce::uint32 mask = proc.activeEngineMask.load();
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const bool active = ((mask >> e) & 1u) != 0;
        engineGlow[e] = active ? 1.0f : engineGlow[e] * 0.85f;
        if (e < engBtn.size())
        {
            engBtn[e]->glow = engineGlow[e];
            if (engineGlow[e] > 0.01f) engBtn[e]->repaint();
        }
    }

    // ---- label row: the control's name, its live value while you are on it,
    //      and any learned CC. Spec: the value readout only shows on hover/drag.
    for (auto& kv : labelForParamId)
    {
        const int cc = proc.getMappedCcFor (kv.first);
        const auto base = baseLabelText.count (kv.first) ? baseLabelText[kv.first] : kv.second->getText();

        juce::String shown = base;

        auto comp = componentForParamId.find (kv.first);

        if (comp != componentForParamId.end())
            if (auto* slider = dynamic_cast<juce::Slider*> (comp->second))
                if (slider->isMouseOverOrDragging() || slider->isMouseButtonDown())
                    shown = slider->getTextFromValue (slider->getValue());

        kv.second->setText (cc >= 0 ? shown + " *" + juce::String (cc) : shown,
                            juce::dontSendNotification);
        kv.second->setColour (juce::Label::textColourId,
                              shown == base ? col::dim : col::text);
    }

    // ---- MIDI-learn highlight + status banner ----
    if (proc.isMidiLearning())
    {
        const auto target = proc.getMidiLearnTargetId();
        auto it = componentForParamId.find (target);
        if (it != componentForParamId.end())
        {
            // Controls inside the hidden panel are not direct children of the
            // canvas, so convert into the canvas space the highlight lives in.
            auto* comp = it->second;
            auto bounds = canvas.getLocalArea (comp->getParentComponent(),
                                               comp->getBounds()).expanded (2);
            learnHighlight.setBounds (bounds);
            learnHighlight.toFront (false);
            learnHighlight.setVisible (true);
            learnHighlight.phase += 0.35f;
            learnHighlight.repaint();
        }
        juce::String nm = target;
        if (auto* p = proc.apvts.getParameter (target)) nm = p->getName (64);
        statusLabel.setText ("MIDI LEARN ARMED for \"" + nm + "\" -- move a MIDI CC controller now  (right-click it again to cancel)",
                              juce::dontSendNotification);
    }
    else
    {
        learnHighlight.setVisible (false);
        // status text is set below, based on whether the trigger gate is open
    }

    // ---- sample deck sync ----
    if (proc.isSampleLoaded())
    {
        sampleNameLabel.setText (proc.getSampleName() + "   ["
            + formatTime (proc.getSamplePositionSeconds()) + " / " + formatTime (proc.getSampleLengthSeconds()) + "]",
            juce::dontSendNotification);
    }
    playSampleBtn.setToggleState (proc.isSamplePlaying(), juce::dontSendNotification);
    sourceSampleBtn.setToggleState (proc.isUsingSampleSource(), juce::dontSendNotification);

    bypassBtn.setToggleState (proc.isEffectBypassed(), juce::dontSendNotification);

    // ---- live performance readouts ----
    bpmLabel.setText (juce::String (proc.getCurrentBpm(), 1)
                        + (proc.isFollowingHostBpm() ? " HOST" : " MAN"),
                      juce::dontSendNotification);

    // SYNC lights up only when the host is actually supplying the tempo, not
    // merely when the button is switched on
    const bool synced = proc.isFollowingHostBpm();
    bpmSyncBtn.glowColour = col::accent;
    bpmSyncBtn.glow = synced ? 1.0f : 0.0f;
    bpmSyncBtn.repaint();

    // overlay buttons glow while actually engaged
    {
        MomentaryBtn* ovBtns[] = { &echoBtn, &delayBtn, &dubBtn, &reverseBtn };
        for (int i = 0; i < 4; ++i) ovBtns[i]->repaint();
    }

    if (statusHoldTicks > 0)
    {
        --statusHoldTicks;          // a one-off message owns the line for now
    }
    else if (proc.isGateOpen())
    {
        statusLabel.setText (proc.isMidiHoldActive()
                                ? "TRIGGERING -- MIDI key held, preset at full wet. Release the key to stop."
                                : (proc.isRandomTriggerEngaged()
                                       ? "TRIGGERING -- RANDOM TRIGGER held at full wet. Release to stop."
                                       : "TRIGGERING -- TRIGGER held at full wet. Release to stop."),
                             juce::dontSendNotification);
    }
    else if (! proc.isMidiLearning())
    {
        statusLabel.setText ("Hold TRIGGER or a MIDI key to run the effect -- it only plays while held.",
                             juce::dontSendNotification);
    }
}

//==============================================================================
void SkipfiendAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // spec: 6px radius on the main window, drawn as an edge over the canvas
    g.setColour (col::line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void SkipfiendAudioProcessorEditor::paintCanvasContents (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // ---- plugin header: 32px strip in the panel surface, 1px separator under it
    g.setColour (col::panel);
    g.fillRect (0, 0, canvas.getWidth(), 32);
    g.setColour (col::line);
    g.drawHorizontalLine (32, 0.0f, (float) canvas.getWidth());

    // ---- output LED: unlit grey -> white as the output approaches 0dB, and
    //      red the whole time it is over. This is the house animation marker.
    {
        const juce::Rectangle<float> led (16.0f, 11.0f, 10.0f, 10.0f);
        const juce::Colour unlit { 0xff3a4049 };
        const juce::Colour lit = ledClip ? col::red
                                         : unlit.interpolatedWith (juce::Colours::white, ledLevel);

        if (ledLevel > 0.02f || ledClip)
        {
            g.setColour (lit.withAlpha (0.28f));
            g.fillEllipse (led.expanded (4.0f));     // soft bloom
        }

        g.setColour (lit);
        g.fillEllipse (led);
        g.setColour (col::line);
        g.drawEllipse (led, 1.0f);
    }

    g.setColour (col::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("SKIPFIEND", 34, 6, 220, 20, juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (monoFont (10.0f));
    g.drawText ("// playback failure unit  v" SKIPFIEND_VERSION,
                150, 9, 260, 16, juce::Justification::left);

    // ---- brand mark: 2px accent notch across the top-left corner, 12px at 45deg
    g.setColour (col::accent);
    g.drawLine (0.0f, 12.0f, 12.0f, 0.0f, 2.0f);

    auto section = [&] (const juce::String& t, int y)
    {
        g.setColour (col::accent);
        g.fillRect (16, y + 1, 2, 12);              // section marker bar
        g.setColour (col::text);
        g.setFont (labelFont (10.0f, true));
        g.drawText (t, 24, y, 700, 14, juce::Justification::left);
        g.setColour (col::line);
        g.drawHorizontalLine (y + 16, 16.0f, (float) canvas.getWidth() - 16.0f);
    };
    // performance strip backing, so the live controls read as one block
    {
        auto strip = juce::Rectangle<float> (16.0f, 40.0f,
                                             (float) canvas.getWidth() - 32.0f,
                                             (float) kPerfRowH - 8.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (strip, 4.0f);     // spec: 4px on inner panels
        g.setColour (col::line);
        g.drawRoundedRectangle (strip, 4.0f, 1.0f);
    }

    const int O = kPerfRowH;   // everything below the new strip shifts down by this
    section ("SAMPLE DECK  [drag a .wav onto the display above, or LOAD]", 314 + O);
    section ("SKIP ENGINES  [" + juce::String (skf::NUM_ENGINES) + " PARALLEL]", 404 + O);
    section ("REPEAT ENGINE  [PER-RETRIGGER MODULATION]", 538 + O);
    section ("MASTER  //  DENSITY / CHAOS / RHYTHMIC GRAVITY", 660 + O);
    section ("SKIP LANGUAGE  [click = engine, wheel = repeats]", 782 + O);
    section ("OVERLAY FX  [hold to play over the top - rate knob sets the sync division]", 888 + O);

    // ---- footer: version, bottom-right, 9px muted mono
    g.setColour (col::dim);
    g.setFont (monoFont (9.0f));
    g.drawText ("v" SKIPFIEND_VERSION,
                canvas.getWidth() - 96, canvas.getHeight() - 28, 80, 12,
                juce::Justification::right);
}

//==============================================================================
void SkipfiendAudioProcessorEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) kCanvasW,
                                     (float) getHeight() / (float) kCanvasH);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    help.setBounds (getLocalBounds());
    options.setBounds (getLocalBounds());
    debugPanel.setBounds (getLocalBounds());
}

void SkipfiendAudioProcessorEditor::layoutCanvasContents()
{
    // Knob diameters from the visual identity spec.
    constexpr int kKnobSmall = 36, kKnobDefault = 48, kKnobLarge = 64;
    juce::ignoreUnused (kKnobSmall, kKnobDefault);

    const int M = 16;
    const int W = kCanvasW - M * 2;
    const int O = kPerfRowH;   // vertical shift applied below the performance strip

    wave.setBounds (M, 40 + O, W, 250);

    // ---- title-row controls (top-right) ----
    meter.setBounds (kCanvasW - M - 60, 6, 60, 30);
    helpBtn.setBounds (kCanvasW - M - 130, 6, 60, 20);
    bypassBtn.setBounds (kCanvasW - M - 200, 6, 62, 20);
    randomBtn.setBounds (kCanvasW - M - 270, 6, 62, 20);
    abCopyBtn.setBounds (kCanvasW - M - 340, 6, 62, 20);
    abBtn.setBounds     (kCanvasW - M - 382, 6, 34, 20);
    menuBtn.setBounds   (kCanvasW - M - 452, 6, 62, 20);

    // ---- live performance strip: laid out with a running cursor so the
    //      controls physically cannot overlap each other ----
    {
        const int rowY = 40;
        const int gap  = 8;
        int x = M + gap;

        auto place = [&] (juce::Component& c, int w, int h)
        {
            c.setBounds (x, rowY + (kPerfRowH - 8 - h) / 2, w, h);
            x += w + gap;
        };

        place (bpmSyncBtn, 54, 20);
        place (tapBtn, 40, 20);
        bpmKnob.setBounds (x, rowY + 4, 34, 34);
        bpmLabel.setBounds (x - 8, rowY + 38, 50, 10);
        x += 34 + gap + 8;

        place (presetCombo, 150, 18);
        place (presetSaveBtn, 54, 18);
        place (presetLoadBtn, 62, 18);

        x += 10;
        place (resetBtn, 58, 20);
        place (triggerBtn, 108, 30);
        place (randomTriggerBtn, 132, 30);
        place (latchBtn, 52, 20);

        // dry/wet sits at the far right of the strip, big and red
        const int mixSize = 44;
        mixKnob.setBounds (kCanvasW - M - gap - mixSize, rowY + 2, mixSize, mixSize);
        mixLabel.setBounds (kCanvasW - M - gap - mixSize - 22, rowY + kPerfRowH - 22, mixSize + 44, 10);
    }

    // ---- sample deck row ----
    {
        const int y = 346 + O;
        sourceSampleBtn.setBounds (M, y, 110, 22);
        playSampleBtn.setBounds (M + 114, y, 60, 22);
        loopSampleBtn.setBounds (M + 178, y, 60, 22);
        loadSampleBtn.setBounds (M + 242, y, 60, 22);
        fullBufferBtn.setBounds (M + 306, y, 92, 22);
        sampleGainKnob.setBounds (M + 404, y - 6, 34, 34);
        sampleGainLabel.setBounds (M + 396, y + 24, 50, 10);
        sampleNameLabel.setBounds (M + 456, y, W - 456, 22);
    }

    // engine strip: one column per engine, toggle + AMT / PRB / RATE
    const int colW = W / skf::NUM_ENGINES;
    for (int e = 0; e < skf::NUM_ENGINES; ++e)
    {
        const int x = M + e * colW;
        engBtn[e]->setBounds (x + 2, 430 + O, colW - 4, 20);
        const int kw = (colW - 8) / 3;
        for (int j = 0; j < 3; ++j)
        {
            const int kx = x + 2 + j * (kw + 2);
            knobs[e * 3 + j]->setBounds (kx, 454 + O, kw, kw + 4);
            labels[e * 3 + j]->setBounds (kx, 454 + O + kw + 2, kw, 12);
        }
    }

    // The engine strip owns the first two knobs per engine, so everything after
    // it is indexed relative to that -- not a hardcoded offset that silently
    // collides the moment an engine is added.
    const int kEngineKnobs = skf::NUM_ENGINES * 3;   // AMT / PRB / RATE per engine
    const int kRepeatKnob0 = kEngineKnobs;
    const int kMasterKnob0 = kRepeatKnob0 + 8;

    // repeat engine row: 8 knobs + 6 combos across 14 columns
    {
        const int cols = 14;
        const int cw = W / cols;
        const int y = 558 + O;
        // Spec caps a knob at 64px (the "large" size); columns are wider than
        // that here, so centre the knob in its column rather than filling it.
        const int ks = juce::jmin (kKnobLarge, cw - 6);

        for (int i = 0; i < 8; ++i)
        {
            const int kx = M + i * cw + (cw - ks) / 2;
            knobs[kRepeatKnob0 + i]->setBounds (kx, y, ks, ks);
            labels[kRepeatKnob0 + i]->setBounds (M + i * cw - 6, y + ks + 2, cw + 12, 12);
        }
        for (int i = 0; i < 6; ++i)
            combos[i]->setBounds (M + (8 + i) * cw + 3, y + 26, cw - 6, 22);
    }

    // master row: 5 knobs + grid combo + 4 toggles across 10 columns
    {
        const int y = 680 + O;
        const int cols = 10;
        const int cw = W / cols;
        const int ks = juce::jmin (kKnobLarge, cw - 8);

        for (int i = 0; i < 5; ++i)
        {
            const int kx = M + i * cw + (cw - ks) / 2;
            knobs[kMasterKnob0 + i]->setBounds (kx, y, ks, ks);
            labels[kMasterKnob0 + i]->setBounds (M + i * cw - 6, y + ks + 2, cw + 12, 12);
        }
        combos[6]->setBounds (M + 5 * cw + 4, y + 20, cw - 8, 22);
        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds (M + (6 + i) * cw + 2, y + 6, cw - 6, 22);
    }

    // sequencer strip
    {
        const int n = SkipfiendAudioProcessor::kSeqSteps;
        const int cw = W / n;
        for (int i = 0; i < n; ++i)
            seqCells[i]->setBounds (M + i * cw, 802 + O, cw - 2, 62);
    }

    // ---- overlay effects strip, bottom of the window ----
    {
        const int y  = 908 + O;
        const int cw = W / 4;
        MomentaryBtn* btns[] = { &echoBtn, &delayBtn, &dubBtn, &reverseBtn };
        Combo*        boxes[] = { &echoRateBox, &delayRateBox, &dubRateBox, &revBarsBox };
        for (int i = 0; i < 4; ++i)
        {
            const int x = M + i * cw;
            btns[i]->setBounds (x + 4, y, cw - 80, 30);
            boxes[i]->setBounds (x + cw - 72, y + 4, 66, 22);
        }
    }

    // the hidden tab, tucked under the notch that opens it
    {
        secretPanel.setBounds (16, 36, 300, 74);
        auto inner = secretPanel.getLocalBounds().reduced (8, 6);

        auto top = inner.removeFromTop (14);
        secretCloseBtn.setBounds (top.removeFromRight (18));
        ruinTitle.setBounds (top);

        inner.removeFromTop (2);
        ruinOnBtn.setBounds (inner.removeFromLeft (58).withHeight (22));

        Knob*        ks[] = { &ruinAmtKnob, &ruinRateKnob, &ruinToneKnob };
        juce::Label* ls[] = { &ruinAmtLabel, &ruinRateLabel, &ruinToneLabel };

        for (int i = 0; i < 3; ++i)
        {
            auto cell = inner.removeFromLeft (60);
            ks[i]->setBounds (cell.removeFromTop (36).withSizeKeepingCentre (36, 36));
            ls[i]->setBounds (cell.removeFromTop (12));
        }
    }

    // footer
    const int footY = 916 + O + kOverlayRowH - 20;
    captureBtn.setBounds (kCanvasW - M - 220, footY, 100, 20);
    midiExportBtn.setBounds (kCanvasW - M - 112, footY, 96, 20);
    statusLabel.setBounds (M, footY, kCanvasW - M * 2 - 232, 20);
}
