/*
    SKIPFIEND headless test harness.

    Drives the processor without a host or an audio device: every engine, every
    repeat-engine mode, presets, randomise, A/B, hard reset, state round-trips
    and the export paths, checking after each block that nothing has gone NaN,
    infinite or wildly out of range.

    Not shipped. Build with -DSKIPFIEND_BUILD_TESTS=ON and run the exe; it
    prints a PASS/FAIL line per case and exits non-zero if anything failed.
*/

#include <atomic>
#include <set>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    int checksRun = 0, checksFailed = 0;
    juce::StringArray failures;

    void check (bool condition, const juce::String& what)
    {
        ++checksRun;

        if (! condition)
        {
            ++checksFailed;
            failures.add (what);
            std::cout << "  FAIL  " << what << std::endl;
        }
    }

    // The core invariant: whatever the settings, the output stays finite and
    // inside a sane range. A limiter sits last, so anything past +4 is a bug.
    bool bufferIsSane (const juce::AudioBuffer<float>& b, float limit = 4.0f)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i]) || std::abs (d[i]) > limit)
                    return false;
        }

        return true;
    }

    void fillNoise (juce::AudioBuffer<float>& b, juce::Random& r)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = (r.nextFloat() * 2.0f - 1.0f) * 0.5f;
        }
    }

    // Run some audio through, optionally holding a MIDI note so the gate opens.
    bool runBlocks (SkipfiendAudioProcessor& p, int numBlocks, int blockSize,
                    juce::Random& r, int heldNote = -1)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        bool sane = true;

        for (int i = 0; i < numBlocks; ++i)
        {
            fillNoise (buf, r);
            juce::MidiBuffer midi;

            if (heldNote >= 0 && i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, heldNote, (juce::uint8) 100), 0);

            if (heldNote >= 0 && i == numBlocks - 1)
                midi.addEvent (juce::MidiMessage::noteOff (1, heldNote), 0);

            p.processBlock (buf, midi);
            sane = sane && bufferIsSane (buf);
        }

        return sane;
    }

    // img.isValid() is true for a completely blank image, so it proves nothing
    // about painting. Require real, varied, opaque content instead.
    bool imageHasContent (const juce::Image& img)
    {
        if (! img.isValid() || img.getWidth() < 4 || img.getHeight() < 4)
            return false;

        std::set<juce::uint32> seen;
        int opaque = 0, sampled = 0;

        for (int y = 0; y < img.getHeight(); y += 7)
            for (int x = 0; x < img.getWidth(); x += 7)
            {
                const auto px = img.getPixelAt (x, y);
                ++sampled;

                if (px.getAlpha() > 200)
                    ++opaque;

                seen.insert (px.getARGB());
            }

        // mostly opaque, and not one flat colour
        return sampled > 0 && opaque > sampled / 2 && seen.size() > 8;
    }

    juce::String stateToString (SkipfiendAudioProcessor& p)
    {
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        return juce::String::toHexString (mb.getData(), (int) mb.getSize(), 0);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Random r (0x5C19F1ED);

    std::cout << "SKIPFIEND headless tests" << std::endl
              << "========================" << std::endl;

    std::unique_ptr<SkipfiendAudioProcessor> p (
        dynamic_cast<SkipfiendAudioProcessor*> (createPluginFilter()));

    check (p != nullptr, "createPluginFilter returns a SkipfiendAudioProcessor");

    if (p == nullptr)
        return 1;

    // ---- 1. every sample rate / block size combination ---------------------
    std::cout << "[1] sample rates x block sizes" << std::endl;
    const double rates[]  = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int    blocks[] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

    for (double sr : rates)
        for (int bs : blocks)
        {
            p->prepareToPlay (sr, bs);
            check (runBlocks (*p, 8, bs, r, 60),
                   "audio stays finite at " + juce::String (sr, 0) + "Hz / "
                       + juce::String (bs) + " samples");
        }

    p->prepareToPlay (48000.0, 512);

    // ---- regression: high playback rates may cross multiple slice edges -----
    std::cout << "[1b] repeat voice high-rate boundary handling" << std::endl;
    {
        skf::RollingBuffer rb;
        rb.prepare (1000.0, 2, 1.0);
        juce::AudioBuffer<float> src (2, 64);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < src.getNumSamples(); ++i)
                src.setSample (ch, i, (float) i / 64.0f);
        rb.push (src);

        skf::RepeatParams rp;
        rp.repeats = 8;
        rp.sliceMinS = rp.sliceMaxS = 0.002; // 2 samples at 1 kHz
        rp.basePitchSemi = 24.0;             // 4x read rate
        rp.timewarp = 1.0;
        skf::RepeatVoice v;
        v.start (rb.now() - 16, rp, skf::CDSKIP, 1000.0);

        float l = 0.0f, rr = 0.0f;
        v.render (rb, &l, &rr);
        check (! v.isActive() || v.pos < v.curLen,
               "high-rate render consumes every crossed slice boundary");
    }

    // ---- regression: random pan is random per repeat, not per sample --------
    std::cout << "[1c] repeat-stable random pan" << std::endl;
    {
        skf::RepeatVoice v;
        v.p.panWalk = 2;
        float l1 = 0.0f, r1 = 0.0f, l2 = 0.0f, r2 = 0.0f;
        v.panFor (3, l1, r1);
        v.panFor (3, l2, r2);
        check (std::abs (l1 - l2) < 1.0e-7f && std::abs (r1 - r2) < 1.0e-7f,
               "Random Pan Walk holds one position for the duration of a repeat");
    }

    // ---- 2. each engine alone, then all together ---------------------------
    std::cout << "[2] engines" << std::endl;
    {
        auto setEngine = [&] (int e, bool on)
        {
            if (auto* param = p->apvts.getParameter ("en" + juce::String (e)))
                param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        };

        for (int e = 0; e < skf::NUM_ENGINES; ++e)
        {
            p->resetAllToDefaults();

            for (int other = 0; other < skf::NUM_ENGINES; ++other)
                setEngine (other, other == e);

            check (runBlocks (*p, 24, 512, r, 60),
                   "engine " + juce::String (e) + " (" + skf::engineName (e) + ") alone");
        }

        p->resetAllToDefaults();

        for (int e = 0; e < skf::NUM_ENGINES; ++e)
            setEngine (e, true);

        check (runBlocks (*p, 48, 512, r, 60), "all engines at once");
    }

    // ---- 3. every discrete mode of the repeat engine ------------------------
    std::cout << "[3] repeat engine modes" << std::endl;
    {
        const char* modeIds[] = { "lenMode", "pitchMode", "volEnv", "panWalk", "endMode", "playMode", "grid" };

        for (const char* id : modeIds)
        {
            auto* param = p->apvts.getParameter (id);

            if (param == nullptr)
            {
                check (false, juce::String ("parameter ") + id + " exists");
                continue;
            }

            const int steps = param->getNumSteps();
            bool sane = true;

            for (int stepIdx = 0; stepIdx < juce::jmin (steps, 32); ++stepIdx)
            {
                param->setValueNotifyingHost (steps > 1 ? (float) stepIdx / (float) (steps - 1) : 0.0f);
                sane = sane && runBlocks (*p, 8, 512, r, 60);
            }

            check (sane, juce::String ("every value of ") + id);
            if (juce::String (id) == "playMode")
                check (param->getNumSteps() >= 5, "playMode exposes multiple playback personalities");
            param->setValueNotifyingHost (param->getDefaultValue());
        }
    }

    // ---- 4. extremes: every continuous parameter at both ends --------------
    std::cout << "[4] parameter extremes" << std::endl;
    {
        bool sane = true;

        for (auto* param : p->getParameters())
        {
            const float original = param->getValue();

            for (float v : { 0.0f, 1.0f })
            {
                param->setValueNotifyingHost (v);
                sane = sane && runBlocks (*p, 4, 512, r, 60);
            }

            param->setValueNotifyingHost (original);
        }

        check (sane, "every parameter at both extremes");
        p->resetAllToDefaults();
    }

    // ---- 5. the gate: closed, button, MIDI, random trigger, overlays -------
    std::cout << "[5] gate and overlays" << std::endl;
    {
        check (! p->isGateOpen(), "gate starts closed");
        check (runBlocks (*p, 16, 512, r), "silence path with the gate closed");

        p->setManualTrigger (true);
        check (p->isGateOpen(), "TRIGGER opens the gate");
        check (runBlocks (*p, 24, 512, r), "audio with TRIGGER held");
        p->setManualTrigger (false);
        check (! p->isGateOpen(), "releasing TRIGGER closes the gate");

        p->engageRandomTrigger();
        check (p->isRandomTriggerEngaged(), "RANDOM TRIGGER engages");
        check (runBlocks (*p, 24, 512, r), "audio with RANDOM TRIGGER held");
        p->releaseRandomTrigger();
        check (! p->isGateOpen(), "releasing RANDOM TRIGGER closes the gate");

        for (int ov = 0; ov < SkipfiendAudioProcessor::NUM_OVERLAYS; ++ov)
        {
            p->setManualTrigger (true);
            p->setOverlayHeld (ov, true);
            const bool ok = runBlocks (*p, 24, 512, r, 60);
            p->setOverlayHeld (ov, false);
            p->setManualTrigger (false);
            check (ok, "overlay " + juce::String (ov) + " held");
        }

        // all four overlays stacked on top of a held key
        p->setManualTrigger (true);

        for (int ov = 0; ov < SkipfiendAudioProcessor::NUM_OVERLAYS; ++ov)
            p->setOverlayHeld (ov, true);

        check (runBlocks (*p, 32, 512, r, 60), "all four overlays at once");

        for (int ov = 0; ov < SkipfiendAudioProcessor::NUM_OVERLAYS; ++ov)
            p->setOverlayHeld (ov, false);

        p->setManualTrigger (false);
    }

    // ---- 6. polyphony: stacked held keys -----------------------------------
    std::cout << "[6] polyphony" << std::endl;
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        bool sane = true;

        for (int note = 48; note <= 72; ++note)
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            fillNoise (buf, r);
            p->processBlock (buf, midi);
            sane = sane && bufferIsSane (buf);
        }

        check (sane, "25 stacked held keys (over the 12-key limit on purpose)");

        for (int note = 48; note <= 72; ++note)
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            fillNoise (buf, r);
            p->processBlock (buf, midi);
            sane = sane && bufferIsSane (buf);
        }

        check (sane, "releasing all of them");
        check (! p->isGateOpen(), "gate closes once every key is released");
    }

    // ---- 7. presets --------------------------------------------------------
    std::cout << "[7] presets" << std::endl;
    {
        const auto names = SkipfiendAudioProcessor::getFactoryPresetNames();
        check (names.size() > 0, "factory bank is not empty");

        bool sane = true;

        for (int i = 0; i < names.size(); ++i)
        {
            p->loadFactoryPreset (i);
            sane = sane && runBlocks (*p, 16, 512, r, 60);
        }

        check (sane, juce::String (names.size()) + " factory presets all play");

        // and every preset survives a save/load round trip
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("skipfiend_test.skipfiend");
        bool roundTrip = true;

        for (int i = 0; i < names.size(); ++i)
        {
            p->loadFactoryPreset (i);
            const auto before = stateToString (*p);
            p->savePresetToFile (tmp);
            p->resetAllToDefaults();
            p->loadPresetFromFile (tmp);
            roundTrip = roundTrip && (stateToString (*p) == before);
        }

        check (roundTrip, "every preset survives save -> reset -> load unchanged");
        tmp.deleteFile();
    }

    // ---- 8. randomise, repeatedly ------------------------------------------
    std::cout << "[8] randomise" << std::endl;
    {
        bool sane = true;

        for (int i = 0; i < 40; ++i)
        {
            p->randomizeAll();
            sane = sane && runBlocks (*p, 8, 512, r, 60);
        }

        check (sane, "40 consecutive DICE presses stay finite");
        check (p->apvts.getParameter ("playMode") != nullptr
                   && p->apvts.getParameter ("motion") != nullptr,
               "DICE-capable state includes playback style and motion controls");

        // the hidden effect must not be exposed by randomising
        auto* ruin = p->apvts.getParameter ("ruinOn");
        check (ruin != nullptr && ruin->getValue() < 0.5f,
               "DICE leaves the hidden RUIN effect switched off");
    }

    // ---- 9. state round trip, A/B, hard reset ------------------------------
    std::cout << "[9] state, A/B, hard reset" << std::endl;
    {
        p->randomizeAll();
        const auto before = stateToString (*p);
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        p->resetAllToDefaults();
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        check (stateToString (*p) == before, "getState -> setState is lossless");

        p->storeToSlot (0);
        p->randomizeAll();
        const auto bState = stateToString (*p);
        p->setCurrentSlot (1);
        p->setCurrentSlot (0);
        p->setCurrentSlot (1);
        check (stateToString (*p) == bState, "A/B keeps each slot's own edit");
        check (runBlocks (*p, 16, 512, r, 60), "audio after A/B switching");

        p->hardResetAndClearCache();
        check (runBlocks (*p, 16, 512, r, 60), "audio after a hard reset");
        check (p->getMappedCcFor ("mix") < 0, "hard reset clears MIDI mappings");
    }

    // ---- 10. the hidden effect ---------------------------------------------
    std::cout << "[10] hidden effect" << std::endl;
    {
        p->resetAllToDefaults();

        if (auto* on = p->apvts.getParameter ("ruinOn"))
        {
            on->setValueNotifyingHost (1.0f);
            bool sane = true;

            for (float amt : { 0.0f, 0.5f, 1.0f })
                for (float rate : { 0.0f, 0.5f, 1.0f })
                    for (float tone : { 0.0f, 0.5f, 1.0f })
                    {
                        p->apvts.getParameter ("ruinAmt")->setValueNotifyingHost (amt);
                        p->apvts.getParameter ("ruinRate")->setValueNotifyingHost (rate);
                        p->apvts.getParameter ("ruinTone")->setValueNotifyingHost (tone);
                        sane = sane && runBlocks (*p, 6, 512, r, 60);
                    }

            check (sane, "RUIN stays finite across all 27 corner settings");
            on->setValueNotifyingHost (0.0f);
        }
        else
        {
            check (false, "ruinOn parameter exists");
        }
    }

    // ---- 11. MIDI learn ----------------------------------------------------
    std::cout << "[11] midi learn" << std::endl;
    {
        p->hardResetAndClearCache();
        p->startMidiLearn ("mix");
        check (p->isMidiLearning(), "learn arms");

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
        fillNoise (buf, r);
        p->processBlock (buf, midi);

        check (! p->isMidiLearning(), "learn disarms once a CC arrives");
        check (p->getMappedCcFor ("mix") == 74, "the CC is mapped to the parameter");
        check (bufferIsSane (buf), "audio is sane through a learn");

        p->clearMidiMapping ("mix");
        check (p->getMappedCcFor ("mix") < 0, "mapping clears");
    }

    // ---- 12. export --------------------------------------------------------
    std::cout << "[12] export" << std::endl;
    {
        p->resetAllToDefaults();
        runBlocks (*p, 200, 512, r, 60);        // put something in the buffer

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);

        for (int depth : { 16, 24, 32 })
        {
            auto wav = dir.getChildFile ("skipfiend_test_" + juce::String (depth) + ".wav");
            const bool wrote = p->exportAudioToWav (wav, 1.0, depth);
            check (wrote && wav.existsAsFile() && wav.getSize() > 1000,
                   juce::String (depth) + "-bit wav export writes a real file");

            juce::AudioFormatManager fm;
            fm.registerBasicFormats();

            if (std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
                reader != nullptr)
                check ((int) reader->bitsPerSample == depth,
                       juce::String (depth) + "-bit wav really is " + juce::String (depth) + "-bit");
            else
                check (false, juce::String (depth) + "-bit wav is readable back");

            wav.deleteFile();
        }

        const auto report = p->buildTroubleshootingReport();
        check (report.contains ("SKIPFIEND TROUBLESHOOTING REPORT")
                   && report.contains ("SELF CHECK")
                   && report.contains ("SETTINGS"),
               "troubleshooting report has its sections");
        check (report.length() > 1000, "troubleshooting report is substantial");

        // the brand strings come from CMake; make sure they actually reach the
        // generated text rather than merely compiling
        check (report.contains (SKIPFIEND_SUPPORT_EMAIL),
               "report carries the support address from CMake");
        check (report.contains (SKIPFIEND_HOMEPAGE),
               "report carries the homepage from CMake");
        check (report.contains (SKIPFIEND_VERSION),
               "report carries the version from CMake");
    }

    // ---- 12b. the sample deck, against a real file -------------------------
    std::cout << "[12b] sample deck" << std::endl;
    {
        // The repo ships a test wav next to the sources; skip gracefully if the
        // harness is run from somewhere that cannot see it.
        juce::File wav (juce::File::getCurrentWorkingDirectory().getChildFile ("03.wav"));

        if (! wav.existsAsFile())
            wav = juce::File (__FILE__).getParentDirectory().getParentDirectory()
                      .getChildFile ("03.wav");

        if (wav.existsAsFile())
        {
            check (SkipfiendAudioProcessor::isSupportedAudioFile (wav), "03.wav is recognised");
            check (! SkipfiendAudioProcessor::isSupportedAudioFile (
                       juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("nope.txt")),
                   "a .txt is rejected");

            // Loading source material must not wipe the sound the user just designed.
            if (auto* motion = p->apvts.getParameter ("motion"))
                motion->setValueNotifyingHost (0.91f);
            if (auto* mode = p->apvts.getParameter ("playMode"))
                mode->setValueNotifyingHost (mode->convertTo0to1 (5.0f));
            const float motionBeforeLoad = p->apvts.getParameter ("motion")->getValue();
            const float modeBeforeLoad = p->apvts.getParameter ("playMode")->getValue();

            p->loadSampleFile (wav);
            check (std::abs (p->apvts.getParameter ("motion")->getValue() - motionBeforeLoad) < 1.0e-6f
                       && std::abs (p->apvts.getParameter ("playMode")->getValue() - modeBeforeLoad) < 1.0e-6f,
                   "loading a sample preserves the current effect design");
            check (p->isSampleLoaded(), "sample loads");
            check (p->getSampleName().isNotEmpty(), "sample reports a name");
            check (p->getSampleLengthSeconds() > 0.0, "sample reports a length");

            p->setUseSampleSource (true);
            p->setSampleLooping (true);
            p->setSamplePlaying (true);
            check (p->isSamplePlaying(), "sample plays");

            bool sane = runBlocks (*p, 60, 512, r, 60);
            check (sane, "audio from the sample deck stays finite");

            // gain extremes while it plays
            for (float g : { 0.0f, 1.0f, 4.0f })
            {
                p->setSampleGain (g);
                sane = sane && runBlocks (*p, 12, 512, r, 60);
            }

            check (sane, "sample deck at gain 0, 1 and 4");
            p->setSampleGain (1.0f);

            // full-track buffering on and off, mid-playback
            p->setFullTrackBuffer (true);
            check (runBlocks (*p, 24, 512, r, 60), "FULL BUFFER on, mid-playback");
            p->setFullTrackBuffer (false);
            check (runBlocks (*p, 24, 512, r, 60), "FULL BUFFER off again");

            p->setSamplePlaying (false);
            p->setUseSampleSource (false);
            check (runBlocks (*p, 12, 512, r, 60), "back to the live input");
        }
        else
        {
            std::cout << "  (skipped: 03.wav not found)" << std::endl;
        }

        // a file that is not audio at all must not load or crash
        auto bogus = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("skipfiend_not_audio.wav");
        bogus.replaceWithText ("this is definitely not a wav file");
        p->loadSampleFile (bogus);
        check (runBlocks (*p, 8, 512, r, 60), "a corrupt file does not take the audio thread down");
        bogus.deleteFile();
    }

    // ---- 13. lifecycle abuse -----------------------------------------------
    std::cout << "[13] lifecycle" << std::endl;
    {
        bool sane = true;

        for (int i = 0; i < 5; ++i)
        {
            p->releaseResources();
            p->prepareToPlay (44100.0 + i * 4000.0, 64 << (i % 4));
            sane = sane && runBlocks (*p, 8, 64 << (i % 4), r, 60);
        }

        check (sane, "repeated releaseResources / prepareToPlay cycles");

        // processing with the gate open across a prepare boundary
        p->setManualTrigger (true);
        runBlocks (*p, 4, 512, r);
        p->prepareToPlay (48000.0, 512);
        check (runBlocks (*p, 8, 512, r), "prepareToPlay while the gate is held open");
        p->setManualTrigger (false);
    }

    // ---- 14. hostile input -------------------------------------------------
    //  Everything here is something a host, a user or a corrupted file can
    //  actually do. None of it may crash or poison the audio.
    std::cout << "[14] hostile input" << std::endl;
    {
        p->resetAllToDefaults();

        // random bytes where a state blob should be
        {
            juce::MemoryBlock junk;
            junk.setSize (512);

            for (size_t i = 0; i < junk.getSize(); ++i)
                junk[(int) i] = (char) r.nextInt (256);

            p->setStateInformation (junk.getData(), (int) junk.getSize());
            check (runBlocks (*p, 8, 512, r, 60), "random bytes as state");
        }

        // a valid state, truncated part-way through
        {
            juce::MemoryBlock good;
            p->resetAllToDefaults();
            p->getStateInformation (good);

            for (double fraction : { 0.1, 0.5, 0.9 })
            {
                const int cut = juce::jmax (1, (int) (good.getSize() * fraction));
                p->setStateInformation (good.getData(), cut);
                check (runBlocks (*p, 6, 512, r, 60),
                       "state truncated to " + juce::String ((int) (fraction * 100)) + "%");
            }

            // and the whole thing again, which must still restore cleanly
            p->setStateInformation (good.getData(), (int) good.getSize());
            check (runBlocks (*p, 6, 512, r, 60), "full state after truncated ones");
        }

        // zero-length state
        p->setStateInformation (nullptr, 0);
        check (runBlocks (*p, 6, 512, r, 60), "empty state");

        // preset files that are not presets
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);

            auto notPreset = dir.getChildFile ("skipfiend_not_a_preset.skipfiend");
            notPreset.replaceWithText ("<<<this is not xml at all>>>");
            p->loadPresetFromFile (notPreset);
            check (runBlocks (*p, 6, 512, r, 60), "a garbage preset file");
            notPreset.deleteFile();

            auto emptyPreset = dir.getChildFile ("skipfiend_empty.skipfiend");
            emptyPreset.replaceWithText ("");
            p->loadPresetFromFile (emptyPreset);
            check (runBlocks (*p, 6, 512, r, 60), "an empty preset file");
            emptyPreset.deleteFile();

            p->loadPresetFromFile (dir.getChildFile ("skipfiend_does_not_exist.skipfiend"));
            check (runBlocks (*p, 6, 512, r, 60), "a preset file that does not exist");
        }

        // factory preset indices outside the bank
        {
            const int n = SkipfiendAudioProcessor::getFactoryPresetNames().size();

            for (int idx : { -1, n, n + 50, 99999 })
                p->loadFactoryPreset (idx);

            check (runBlocks (*p, 8, 512, r, 60), "out-of-range factory preset indices");
        }

        // export somewhere that cannot be written
        {
            juce::File bad ("Z:/definitely/not/a/real/path/skipfiend.wav");
            check (! p->exportAudioToWav (bad, 1.0, 24), "export to an unwritable path fails cleanly");
            check (runBlocks (*p, 6, 512, r, 60), "audio survives a failed export");
        }

        // A/B hammered, and slots outside the valid range
        {
            for (int i = 0; i < 30; ++i)
                p->setCurrentSlot (i % 2);

            for (int bad : { -1, 2, 77 })
                p->setCurrentSlot (bad);

            check (p->getCurrentSlot() == 1 || p->getCurrentSlot() == 0,
                   "A/B slot stays valid after out-of-range requests");
            check (runBlocks (*p, 8, 512, r, 60), "audio after hammering A/B");
        }

        // tap tempo, including nonsense tapping
        {
            p->resetTapTempo();

            for (int i = 0; i < 8; ++i)
                p->tapTempo();

            const double bpm = p->getCurrentBpm();
            check (bpm > 10.0 && bpm < 1000.0,
                   "tap tempo yields a sane BPM (" + juce::String (bpm, 1) + ")");
            check (runBlocks (*p, 8, 512, r, 60), "audio after tap tempo");
            p->resetTapTempo();
        }

        // a flood of MIDI, including malformed-ish extremes
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;

            for (int i = 0; i < 200; ++i)
            {
                const int pos = r.nextInt (512);
                switch (r.nextInt (4))
                {
                    case 0: midi.addEvent (juce::MidiMessage::noteOn (1 + r.nextInt (16),
                                               r.nextInt (128), (juce::uint8) r.nextInt (128)), pos); break;
                    case 1: midi.addEvent (juce::MidiMessage::noteOff (1 + r.nextInt (16),
                                               r.nextInt (128)), pos); break;
                    case 2: midi.addEvent (juce::MidiMessage::controllerEvent (1 + r.nextInt (16),
                                               r.nextInt (128), r.nextInt (128)), pos); break;
                    default: midi.addEvent (juce::MidiMessage::pitchWheel (1, r.nextInt (16384)), pos); break;
                }
            }

            fillNoise (buf, r);
            p->processBlock (buf, midi);
            check (bufferIsSane (buf), "a 200-message MIDI flood in one block");

            // and all-notes-off must actually close the gate
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::allNotesOff (1), 0);

            for (int ch = 1; ch <= 16; ++ch)
                off.addEvent (juce::MidiMessage::allNotesOff (ch), 0);

            fillNoise (buf, r);
            p->processBlock (buf, off);
            check (bufferIsSane (buf), "all-notes-off is handled");
        }

        // silence in, silence out: the effect must not invent noise from nothing
        {
            p->resetAllToDefaults();
            juce::AudioBuffer<float> buf (2, 512);

            // The plugin declares a 2s tail, so delay/reverse lines are allowed
            // to ring out. What matters is that they DECAY to nothing and that
            // nothing manufactures noise from silence.
            float early = 0.0f, late = 0.0f;

            for (int i = 0; i < 400; ++i)        // ~4.3s at 48k/512
            {
                buf.clear();
                juce::MidiBuffer midi;
                p->processBlock (buf, midi);
                const float mag = buf.getMagnitude (0, buf.getNumSamples());

                if (i < 10)   early = juce::jmax (early, mag);
                if (i >= 380) late  = juce::jmax (late,  mag);
            }

            std::cout << "  tail decay: " << early << " -> " << late << std::endl;
            check (early > late, "the tail actually decays rather than sitting flat");
            check (late < 1.0e-3f,
                   "silence in, silence out once tails decay (early "
                       + juce::String (early, 6) + " -> late " + juce::String (late, 6) + ")");
        }

        // a buffer larger than anything prepareToPlay was told about
        {
            p->prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> big (2, 4096);
            fillNoise (big, r);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            p->processBlock (big, midi);
            check (bufferIsSane (big), "a block 16x larger than the prepared size");
            p->prepareToPlay (48000.0, 512);
        }
    }

    // ---- 15. bus layouts ---------------------------------------------------
    //  Hosts probe these before they will load the plugin at all. The plugin
    //  declares main in, main out and an optional sidechain in, so every layout
    //  has to carry all three buses.
    std::cout << "[15] bus layouts" << std::endl;
    {
        using Set = juce::AudioChannelSet;

        auto supported = [&p] (const Set& mainIn, const Set& mainOut, const Set& sidechain)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (mainIn);
            layout.inputBuses.add (sidechain);
            layout.outputBuses.add (mainOut);
            return p->checkBusesLayoutSupported (layout);
        };

        check (supported (Set::stereo(), Set::stereo(), Set::disabled()),
               "stereo in / stereo out");
        check (supported (Set::mono(), Set::mono(), Set::disabled()),
               "mono in / mono out");
        check (supported (Set::stereo(), Set::stereo(), Set::stereo()),
               "stereo + stereo sidechain");
        check (supported (Set::stereo(), Set::stereo(), Set::mono()),
               "stereo + mono sidechain");

        // layouts the plugin should refuse rather than accept and misbehave on
        check (! supported (Set::stereo(), Set::create5point1(), Set::disabled()),
               "a surround output is refused");
        check (! supported (Set::mono(), Set::stereo(), Set::disabled()),
               "mismatched in/out widths are refused");
        check (! supported (Set::stereo(), Set::stereo(), Set::create5point1()),
               "a surround sidechain is refused");

        check (p->getBusCount (true) >= 1, "at least one input bus");
        check (p->getBusCount (false) >= 1, "at least one output bus");

        // Actually negotiate mono and run it - the layout least likely to have
        // been exercised by hand.
        {
            juce::AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (Set::mono());
            mono.inputBuses.add (Set::disabled());
            mono.outputBuses.add (Set::mono());

            const bool applied = p->setBusesLayout (mono);
            check (applied, "the mono layout can actually be applied");

            if (applied)
            {
                p->prepareToPlay (48000.0, 512);
                juce::AudioBuffer<float> monoBuf (1, 512);
                bool sane = true;

                for (int i = 0; i < 24; ++i)
                {
                    fillNoise (monoBuf, r);
                    juce::MidiBuffer midi;

                    if (i == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

                    p->processBlock (monoBuf, midi);
                    sane = sane && bufferIsSane (monoBuf);
                }

                check (sane, "running in mono stays finite");
            }

            // and a buffer narrower than the negotiated layout must not crash
            juce::AudioProcessor::BusesLayout stereo;
            stereo.inputBuses.add (Set::stereo());
            stereo.inputBuses.add (Set::disabled());
            stereo.outputBuses.add (Set::stereo());
            p->setBusesLayout (stereo);
            p->prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> narrow (1, 512);
            fillNoise (narrow, r);
            juce::MidiBuffer midi;
            p->processBlock (narrow, midi);
            check (bufferIsSane (narrow), "a mono buffer through a stereo layout does not crash");
        }
    }

    // ---- 16. the real race -------------------------------------------------
    //  In a host, the message thread mutates state (preset recall, automation,
    //  undo, the editor) while the audio thread is mid-processBlock. Nothing so
    //  far has exercised that overlap. This drives both at once.
    std::cout << "[16] message thread vs audio thread" << std::endl;
    {
        p->prepareToPlay (48000.0, 512);
        p->resetAllToDefaults();

        std::atomic<bool> stop { false };
        std::atomic<int>  mutations { 0 };

        // Stands in for the message thread: everything a host or the editor can
        // do to the processor while audio is running.
        std::thread messageThread ([&]
        {
            juce::Random mr (0xBEEF);
            juce::MemoryBlock snapshot;
            p->getStateInformation (snapshot);

            const int presetCount = SkipfiendAudioProcessor::getFactoryPresetNames().size();

            while (! stop.load())
            {
                switch (mr.nextInt (10))
                {
                    case 0: p->randomizeAll(); break;
                    case 1: p->loadFactoryPreset (mr.nextInt (juce::jmax (1, presetCount))); break;
                    case 2: p->resetAllToDefaults(); break;
                    case 3: p->setStateInformation (snapshot.getData(), (int) snapshot.getSize()); break;
                    case 4: { juce::MemoryBlock mb; p->getStateInformation (mb); } break;
                    case 5: p->setCurrentSlot (mr.nextInt (2)); break;
                    case 6: p->startMidiLearn ("mix"); p->cancelMidiLearn(); break;
                    case 7: p->setManualTrigger (mr.nextBool()); break;
                    case 8: p->setOverlayHeld (mr.nextInt (SkipfiendAudioProcessor::NUM_OVERLAYS),
                                               mr.nextBool()); break;
                    default:
                        if (auto* param = p->apvts.getParameter ("mix"))
                            param->setValueNotifyingHost (mr.nextFloat());
                        break;
                }

                ++mutations;
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        });

        // Meanwhile, the audio thread keeps its deadline.
        juce::AudioBuffer<float> buf (2, 512);
        bool sane = true;
        int blocksRun = 0;

        const auto until = juce::Time::getMillisecondCounter() + 3000;

        while (juce::Time::getMillisecondCounter() < until)
        {
            fillNoise (buf, r);
            juce::MidiBuffer midi;

            if ((blocksRun % 16) == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (blocksRun % 24),
                                                          (juce::uint8) 100), 0);

            if ((blocksRun % 16) == 8)
                midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (blocksRun % 24)), 0);

            p->processBlock (buf, midi);
            sane = sane && bufferIsSane (buf);
            ++blocksRun;
        }

        stop.store (true);
        messageThread.join();

        std::cout << "  " << blocksRun << " blocks against " << mutations.load()
                  << " concurrent state changes" << std::endl;

        check (sane, "audio stays finite while state is mutated concurrently");
        check (blocksRun > 100, "the audio thread kept running throughout");
        check (mutations.load() > 100, "the message thread really was hammering it");

        // and the processor is still usable afterwards
        p->setManualTrigger (false);

        for (int ov = 0; ov < SkipfiendAudioProcessor::NUM_OVERLAYS; ++ov)
            p->setOverlayHeld (ov, false);

        p->resetAllToDefaults();
        check (runBlocks (*p, 16, 512, r, 60), "processor is healthy after the race");
    }

    // ---- 17. the editor ----------------------------------------------------
    //  No window and no display: build the component, lay it out and render it
    //  into an offscreen image. That runs every paint path - the look and feel,
    //  the section headers, the LED, the meter, the overlays - so a crash or a
    //  bad assumption in drawing code surfaces here rather than in a DAW.
    std::cout << "[17] editor" << std::endl;
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        check (ed != nullptr, "editor constructs");

        if (ed != nullptr)
            ed->setVisible (true);       // a host does this; JUCE paints nothing otherwise

        // Is it "the second editor" or "after cycling"? Make a second one right
        // now, while the first is still alive, and probe both.
        {
            std::unique_ptr<juce::AudioProcessorEditor> probeEd (p->createEditor());

            if (probeEd != nullptr)
            {
                probeEd->setVisible (true);
                juce::Image i2 (juce::Image::ARGB, probeEd->getWidth(), probeEd->getHeight(), true);
                juce::Graphics g2 (i2);
                probeEd->paintEntireComponent (g2, true);
                std::cout << "  2nd editor (1st still alive): content="
                          << (int) imageHasContent (i2) << std::endl;
            }
        }

        {
            juce::Image i1 (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
            juce::Graphics g1 (i1);
            ed->paintEntireComponent (g1, true);
            std::cout << "  1st editor: content=" << (int) imageHasContent (i1) << std::endl;
        }

        if (ed != nullptr)
        {
            const int w = ed->getWidth(), h = ed->getHeight();
            check (w > 0 && h > 0, "editor has a size ("
                                       + juce::String (w) + "x" + juce::String (h) + ")");

            auto render = [&ed] (int rw, int rh)
            {
                juce::Image img (juce::Image::ARGB, juce::jmax (1, rw), juce::jmax (1, rh), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
                return img;
            };

            // at its natural size, and saved to disk so the rendered UI can
            // actually be looked at rather than only asserted about
            {
                auto img = render (w, h);
                check (imageHasContent (img), "editor paints at its natural size");

                auto shot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("skipfiend_ui.png");
                shot.deleteFile();

                if (auto out = shot.createOutputStream())
                {
                    juce::PNGImageFormat png;

                    if (png.writeImageToStream (img, *out))
                        std::cout << "  rendered UI -> " << shot.getFullPathName() << std::endl;
                }
            }

            // The brand mark is a deliberately subtle 2px diagonal notch in the
            // very top-left corner, too small to confirm by eye in a full
            // screenshot - so check the pixels directly.
            {
                auto img = render (w, h);
                const juce::Colour accent (0xffe8532a);
                int hits = 0;

                for (int d = 1; d <= 11; ++d)
                {
                    for (int jitter = -1; jitter <= 1; ++jitter)
                    {
                        const int x = d + jitter, y = 12 - d;

                        if (x < 0 || y < 0 || x >= img.getWidth() || y >= img.getHeight())
                            continue;

                        const auto px = img.getPixelAt (x, y);

                        // close to the accent hue and clearly warmer than the
                        // near-black panel behind it
                        if (px.getRed() > 120 && px.getRed() > px.getBlue() + 40)
                            ++hits;
                    }
                }

                check (hits >= 6, "the 2px accent notch is actually drawn in the top-left corner ("
                                      + juce::String (hits) + " accent pixels on the diagonal)");
                juce::ignoreUnused (accent);
            }

            // and while being resized, which re-runs the whole layout
            bool resized = true;

            for (auto scale : { 0.5, 0.75, 1.0, 1.5 })
            {
                ed->setSize ((int) (w * scale), (int) (h * scale));
                auto img = render (ed->getWidth(), ed->getHeight());
                resized = resized && imageHasContent (img);
            }

            check (resized, "editor lays out and paints at 50/75/100/150%");
            ed->setSize (w, h);

            // painting while audio is running and the gate is open, so the
            // meter, LED, telemetry feed and engine glows all have live values
            {
                p->setManualTrigger (true);
                runBlocks (*p, 20, 512, r, 60);
                auto img = render (w, h);
                check (imageHasContent (img), "editor paints with live audio and the gate open");
                p->setManualTrigger (false);
            }

            // painting after a hard reset, where every cached UI value is stale
            {
                p->hardResetAndClearCache();
                auto img = render (w, h);
                check (imageHasContent (img), "editor paints straight after a hard reset");
            }

            // repeated create/destroy - the pattern a user makes by opening and
            // closing the plugin window, and where dangling look-and-feel
            // pointers or leaked listeners show up
            ed.reset();

            bool cycled = true;

            for (int i = 0; i < 5; ++i)
            {
                std::unique_ptr<juce::AudioProcessorEditor> e2 (p->createEditor());
                cycled = cycled && (e2 != nullptr);

                if (e2 != nullptr)
                {
                    e2->setVisible (true);
                    juce::Image img (juce::Image::ARGB, 400, 400, true);
                    juce::Graphics g (img);
                    e2->paintEntireComponent (g, true);
                }

                runBlocks (*p, 4, 512, r, 60);
            }

            check (cycled, "editor survives 5 open/close cycles with audio running");

            // ---- live UI states ------------------------------------------
            //  Everything above rendered a static, idle editor. These states
            //  need the editor's 20Hz timer to actually run, which is what
            //  drives the meter, the LED, the engine glows and the telemetry
            //  feed - none of which any test has exercised until now.
            {
                std::unique_ptr<juce::AudioProcessorEditor> live (p->createEditor());
                live->setVisible (true);

                auto settle = [&live] (int ms)
                {
                    // dispatch pending messages so juce::Timer callbacks fire
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
                };

                auto capture = [&live, &settle] (const juce::String& name, int ms)
                {
                    settle (ms);

                    juce::Image img (juce::Image::ARGB,
                                     juce::jmax (1, live->getWidth()),
                                     juce::jmax (1, live->getHeight()), true);
                    {
                        juce::Graphics warm (img);
                        live->paintEntireComponent (warm, true);
                    }
                    juce::Graphics g (img);
                    live->paintEntireComponent (g, true);

                    auto shot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("skipfiend_ui_" + name + ".png");
                    shot.deleteFile();

                    if (auto out = shot.createOutputStream())
                    {
                        juce::PNGImageFormat png;

                        if (png.writeImageToStream (img, *out))
                            std::cout << "  rendered " << name << " -> "
                                      << shot.getFullPathName() << std::endl;
                    }

                    return img;
                };

                // 1. a sample loaded into the deck
                juce::File wav (juce::File::getCurrentWorkingDirectory().getChildFile ("03.wav"));

                if (! wav.existsAsFile())
                    wav = juce::File (__FILE__).getParentDirectory().getParentDirectory()
                              .getChildFile ("03.wav");

                if (wav.existsAsFile())
                {
                    p->loadSampleFile (wav);
                    p->setUseSampleSource (true);
                    p->setSamplePlaying (true);
                    runBlocks (*p, 40, 512, r, -1);
                    check (imageHasContent (capture ("sample", 250)),
                           "editor paints with a sample loaded and playing");
                }

                // 2. gate open, engines firing, audio loud enough to light the
                //    meter and the LED
                {
                    if (auto* mix = p->apvts.getParameter ("mix"))
                        mix->setValueNotifyingHost (1.0f);

                    p->setManualTrigger (true);

                    juce::AudioBuffer<float> loud (2, 512);

                    // Interleave audio with message-loop pumping. The telemetry
                    // column deliberately only scrolls when values change, so
                    // processing a burst and THEN idling leaves it empty -
                    // the opposite of how a host behaves.
                    for (int pass = 0; pass < 30; ++pass)
                    {
                        for (int i = 0; i < 6; ++i)
                        {
                            for (int c = 0; c < loud.getNumChannels(); ++c)
                            {
                                auto* d = loud.getWritePointer (c);

                                for (int n = 0; n < loud.getNumSamples(); ++n)
                                    d[n] = 0.9f * std::sin ((float) ((pass * 6 + i) * 512 + n) * 0.02f);
                            }

                            juce::MidiBuffer midi;

                            if (pass == 0 && i == 0)
                                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 120), 0);

                            p->processBlock (loud, midi);
                        }

                        settle (40);
                    }

                    auto playing = capture ("playing", 60);
                    check (imageHasContent (playing),
                           "editor paints with the gate open and engines firing");

                    // The meter is ~60x30 in a 1200px-wide shot, far too small
                    // to judge the gradient by eye. Crop and magnify it.
                    {
                        auto area = juce::Rectangle<int> (playing.getWidth() - 110, 0, 110, 46)
                                        .getIntersection (playing.getBounds());

                        if (! area.isEmpty())
                        {
                            auto crop = playing.getClippedImage (area);
                            auto big  = crop.rescaled (area.getWidth() * 5, area.getHeight() * 5,
                                                       juce::Graphics::lowResamplingQuality);

                            auto shot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                            .getChildFile ("skipfiend_ui_meter.png");
                            shot.deleteFile();

                            if (auto out = shot.createOutputStream())
                            {
                                juce::PNGImageFormat png;

                                if (png.writeImageToStream (big, *out))
                                    std::cout << "  rendered meter -> "
                                              << shot.getFullPathName() << std::endl;
                            }
                        }
                    }

                    p->setManualTrigger (false);
                    juce::MidiBuffer off;
                    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                    juce::AudioBuffer<float> b2 (2, 512);
                    b2.clear();
                    p->processBlock (b2, off);
                }

                // 3. the window scaled down, where the canvas transform applies
                {
                    const int w0 = live->getWidth(), h0 = live->getHeight();
                    live->setSize ((int) (w0 * 0.6), (int) (h0 * 0.6));
                    check (imageHasContent (capture ("scaled", 150)),
                           "editor paints scaled to 60%");
                    live->setSize (w0, h0);
                }

                // 4. MIDI-learn highlight over a control INSIDE the hidden
                //    panel - the nested case whose coordinate conversion was
                //    wrong, and which has never actually been looked at.
                {
                    std::function<juce::Component*(juce::Component*, const juce::String&)> find;
                    find = [&find] (juce::Component* root, const juce::String& n) -> juce::Component*
                    {
                        if (root == nullptr)
                            return nullptr;

                        if (root->getName() == n)
                            return root;

                        for (auto* c : root->getChildren())
                            if (auto* f = find (c, n))
                                return f;

                        return nullptr;
                    };

                    if (auto* secret = find (live.get(), "secret"))
                    {
                        secret->setVisible (true);
                        secret->toFront (false);
                        live->resized();
                    }

                    p->startMidiLearn ("ruinAmt");
                    check (p->isMidiLearning(), "learn armed on a nested control");
                    check (imageHasContent (capture ("learn", 200)),
                           "editor paints with the learn highlight armed");
                    p->cancelMidiLearn();
                }

                p->setUseSampleSource (false);
                p->setSamplePlaying (false);
            }

            // Reveal each overlay and render it. These are full-window panels
            // that are invisible by default, so nothing above has drawn them.
            {
                std::unique_ptr<juce::AudioProcessorEditor> e3 (p->createEditor());
                e3->setVisible (true);

                {
                    juce::Image probe (juce::Image::ARGB, e3->getWidth(), e3->getHeight(), true);
                    juce::Graphics pg (probe);
                    e3->paintEntireComponent (pg, true);
                    std::cout << "  e3 bare render: size " << e3->getWidth() << "x" << e3->getHeight()
                              << " visible=" << (int) e3->isVisible()
                              << " content=" << (int) imageHasContent (probe)
                              << " centre=" << probe.getPixelAt (e3->getWidth()/2, e3->getHeight()/2)
                                                    .toDisplayString (true) << std::endl;
                }

                std::function<juce::Component*(juce::Component*, const juce::String&)> findByName;
                findByName = [&findByName] (juce::Component* root,
                                            const juce::String& name) -> juce::Component*
                {
                    if (root == nullptr)
                        return nullptr;

                    if (root->getName() == name)
                        return root;

                    for (auto* child : root->getChildren())
                        if (auto* found = findByName (child, name))
                            return found;

                    return nullptr;
                };

                for (const auto* name : { "help", "options", "debug", "secret" })
                {
                    auto* panel = findByName (e3.get(), name);
                    check (panel != nullptr, juce::String ("overlay '") + name + "' exists");

                    if (panel == nullptr)
                        continue;

                    panel->setVisible (true);
                    panel->toFront (false);
                    e3->resized();

                    juce::Image img (juce::Image::ARGB,
                                     juce::jmax (1, e3->getWidth()),
                                     juce::jmax (1, e3->getHeight()), true);

                    // The very first paint of a freshly built editor comes back
                    // empty (JUCE has not run its initial layout/cache pass
                    // yet), so paint once to settle it, then capture.
                    {
                        juce::Graphics warm (img);
                        e3->paintEntireComponent (warm, true);
                    }

                    juce::Graphics g (img);
                    e3->paintEntireComponent (g, true);
                    check (imageHasContent (img), juce::String ("overlay '") + name + "' paints");

                    auto shot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile (juce::String ("skipfiend_ui_") + name + ".png");
                    shot.deleteFile();

                    if (auto out = shot.createOutputStream())
                    {
                        juce::PNGImageFormat png;

                        if (png.writeImageToStream (img, *out))
                            std::cout << "  rendered " << name << " -> "
                                      << shot.getFullPathName() << std::endl;
                    }

                    panel->setVisible (false);
                }
            }
        }

        check (runBlocks (*p, 16, 512, r, 60), "processor is healthy after the editor closed");
    }

    std::cout << std::endl
              << "========================" << std::endl
              << checksRun - checksFailed << " / " << checksRun << " checks passed"
              << std::endl;

    if (checksFailed > 0)
    {
        std::cout << std::endl << "FAILURES:" << std::endl;

        for (const auto& f : failures)
            std::cout << "  - " << f << std::endl;
    }

    return checksFailed == 0 ? 0 : 1;
}
