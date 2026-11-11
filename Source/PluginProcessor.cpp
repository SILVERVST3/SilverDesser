#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
static String freqToText (float hz)
{
    if (hz >= 1000.0f) return String (hz / 1000.0f, 2) + " kHz";
    return String (roundToInt (hz)) + " Hz";
}

static float textToFreq (const String& t)
{
    const String s = t.trim().toLowerCase();
    float v = s.getFloatValue();
    if (s.contains ("k")) v *= 1000.0f;
    return v;
}

AudioProcessorValueTreeState::ParameterLayout SilverDesserProcessor::createLayout()
{
    using P = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;

    auto dbText = [] (int dec) { return [dec] (float x, int) { return String (x, dec) + " dB"; }; };
    auto numFromText = [] (const String& t) { return t.getFloatValue(); };

    v.push_back (std::make_unique<P> (ParameterID { ids::inTrim, 1 }, "Input Trim",
                 NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                 A().withStringFromValueFunction (dbText (1)).withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<P> (ParameterID { ids::outTrim, 1 }, "Output Trim",
                 NormalisableRange<float> (-12.0f, 0.0f, 0.1f), 0.0f,
                 A().withStringFromValueFunction (dbText (1)).withValueFromStringFunction (numFromText)));

    {
        NormalisableRange<float> r (20.0f, 20000.0f, 0.01f);
        r.setSkewForCentre (1000.0f);
        v.push_back (std::make_unique<P> (ParameterID { ids::freq, 1 }, "Frequency", r, 7000.0f,
                     A().withStringFromValueFunction ([] (float x, int) { return freqToText (x); })
                        .withValueFromStringFunction ([] (const String& t) { return textToFreq (t); })));
    }

    v.push_back (std::make_unique<P> (ParameterID { ids::width, 1 }, "Width",
                 NormalisableRange<float> (0.2f, 10.0f, 0.01f), 1.4f,
                 A().withStringFromValueFunction ([] (float x, int) { return String (x, 2); })
                    .withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<P> (ParameterID { ids::slope, 1 }, "Slope/Q",
                 NormalisableRange<float> (12.0f, 72.0f, 1.0f), 72.0f,
                 A().withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + " dB"; })
                    .withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::listen, 1 }, "Listen",
                 StringArray { "Mix", "Inside", "Outside" }, 0));

    v.push_back (std::make_unique<P> (ParameterID { ids::threshold, 1 }, "Threshold",
                 NormalisableRange<float> (-84.0f, 0.0f, 0.1f), -18.0f,
                 A().withStringFromValueFunction (dbText (1)).withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::autoTrack, 1 }, "Level Tracking Auto", true));

    {
        NormalisableRange<float> r (0.1f, 10.0f, 0.1f);
        r.setSkewForCentre (1.0f);
        v.push_back (std::make_unique<P> (ParameterID { ids::damping, 1 }, "Damping", r, 1.0f,
                     A().withStringFromValueFunction ([] (float x, int) { return String (x, 1) + " s"; })
                        .withValueFromStringFunction (numFromText)));
    }

    v.push_back (std::make_unique<P> (ParameterID { ids::ratio, 1 }, "Ratio",
                 NormalisableRange<float> (-22.5f, 45.0f, 0.1f), 26.5f,
                 A().withStringFromValueFunction ([] (float x, int) { return String (x, 1) + " " + String::charToString ((juce_wchar) 0x00B0); })
                    .withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<P> (ParameterID { ids::makeup, 1 }, "Make-up",
                 NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f,
                 A().withStringFromValueFunction (dbText (1)).withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<P> (ParameterID { ids::wet, 1 }, "Wet/Dry",
                 NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                 A().withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + " %"; })
                    .withValueFromStringFunction (numFromText)));

    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::knee, 1 }, "Soft-Knee",
                 StringArray { "0 dB", "5 dB", "10 dB", "15 dB", "20 dB" }, 0));

    {
        NormalisableRange<float> r (0.01f, 52.0f, 0.01f);
        r.setSkewForCentre (2.0f);
        v.push_back (std::make_unique<P> (ParameterID { ids::attack, 1 }, "Attack", r, 5.2f,
                     A().withStringFromValueFunction ([] (float x, int) { return String (x, 2) + " ms"; })
                        .withValueFromStringFunction (numFromText)));
    }
    {
        NormalisableRange<float> r (0.01f, 30.0f, 0.01f);
        r.setSkewForCentre (0.5f);
        v.push_back (std::make_unique<P> (ParameterID { ids::hold, 1 }, "Hold", r, 0.01f,
                     A().withStringFromValueFunction ([] (float x, int) { return String (x, 2) + " s"; })
                        .withValueFromStringFunction (numFromText)));
    }
    {
        NormalisableRange<float> r (0.01f, 3.11f, 0.001f);
        r.setSkewForCentre (0.3f);
        v.push_back (std::make_unique<P> (ParameterID { ids::release, 1 }, "Release", r, 0.127f,
                     A().withStringFromValueFunction ([] (float x, int) { return String (x, 3) + " s"; })
                        .withValueFromStringFunction (numFromText)));
    }

    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::trigWide, 1 },  "Trigger Wide", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::audioWide, 1 }, "Audio Wide", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::effectIn, 1 },  "Effect In", true));

    return { v.begin(), v.end() };
}

//==============================================================================
SilverDesserProcessor::SilverDesserProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SilverDesser", createLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pInTrim = raw (ids::inTrim);   pOutTrim = raw (ids::outTrim);
    pFreq = raw (ids::freq);       pWidth = raw (ids::width);     pSlope = raw (ids::slope);
    pListen = raw (ids::listen);   pThr = raw (ids::threshold);   pAuto = raw (ids::autoTrack);
    pDamp = raw (ids::damping);    pRatio = raw (ids::ratio);     pMakeup = raw (ids::makeup);
    pWet = raw (ids::wet);         pKnee = raw (ids::knee);       pAtt = raw (ids::attack);
    pHold = raw (ids::hold);       pRel = raw (ids::release);     pTrig = raw (ids::trigWide);
    pAudio = raw (ids::audioWide); pFx = raw (ids::effectIn);
}

bool SilverDesserProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return out == l.getMainInputChannelSet();
}

silver::DesserParams SilverDesserProcessor::readParams() const
{
    silver::DesserParams p;
    p.inTrimDb    = pInTrim->load();
    p.outTrimDb   = pOutTrim->load();
    p.freq        = pFreq->load();
    p.width       = pWidth->load();
    p.slopeDb     = pSlope->load();
    p.listen      = (int) std::lround (pListen->load());
    p.thresholdDb = pThr->load();
    p.autoTrack   = pAuto->load() > 0.5f;
    p.dampingS    = pDamp->load();
    p.ratioDeg    = pRatio->load();
    p.makeupDb    = pMakeup->load();
    p.wet         = pWet->load() * 0.01f;
    p.kneeDb      = 5.0f * (float) std::lround (pKnee->load());
    p.attackMs    = pAtt->load();
    p.holdS       = pHold->load();
    p.releaseS    = pRel->load();
    p.trigWide    = pTrig->load() > 0.5f;
    p.audioWide   = pAudio->load() > 0.5f;
    p.effectIn    = pFx->load() > 0.5f;
    return p;
}

void SilverDesserProcessor::prepareToPlay (double sampleRate, int)
{
    numChannelsPrepared = jmax (1, jmin (2, getTotalNumInputChannels()));
    currentSampleRate = sampleRate;

    core.prepare (sampleRate, numChannelsPrepared);
    core.setParams (readParams(), true);
    setLatencySamples (core.getLatencySamples());
}

void SilverDesserProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int nCh = jmin (buffer.getNumChannels(), 2);
    const int nSmp = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, nSmp);

    core.setParams (readParams());
    core.process (buffer.getArrayOfWritePointers(), nCh, nSmp);

    // medidores (a interface le e zera)
    auto maxStore = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
    };

    maxStore (meterInPeak,  core.getPeakIn());
    maxStore (meterOutPeak, core.getPeakOut());
    maxStore (meterGrDb,    core.getGrDb());
    trackLevelDb.store (core.getTrackDb());
}

//==============================================================================
void SilverDesserProcessor::switchAB (int slot)
{
    if (slot == activeSlot || slot < 0 || slot > 1) return;

    slotState[activeSlot] = apvts.copyState();

    if (! slotState[slot].isValid())
        slotState[slot] = apvts.copyState();

    activeSlot = slot;
    apvts.replaceState (slotState[slot].createCopy());
}

void SilverDesserProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("advanced", advancedVisible, nullptr);
    state.setProperty ("editorWidth", editorWidth, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SilverDesserProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto vt = ValueTree::fromXml (*xml);
        if (vt.isValid())
        {
            advancedVisible = (bool) vt.getProperty ("advanced", true);
            editorWidth = (int) vt.getProperty ("editorWidth", 1000);
            apvts.replaceState (vt);
        }
    }
}

AudioProcessorEditor* SilverDesserProcessor::createEditor()
{
    return new SilverDesserEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilverDesserProcessor();
}
