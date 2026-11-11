#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DesserDSP.h"

namespace ids
{
    inline const char* inTrim    = "inTrim";
    inline const char* outTrim   = "outTrim";
    inline const char* freq      = "freq";
    inline const char* width     = "width";
    inline const char* slope     = "slope";
    inline const char* listen    = "listen";
    inline const char* threshold = "threshold";
    inline const char* autoTrack = "autoTrack";
    inline const char* damping   = "damping";
    inline const char* ratio     = "ratio";
    inline const char* makeup    = "makeup";
    inline const char* wet       = "wet";
    inline const char* knee      = "knee";
    inline const char* attack    = "attack";
    inline const char* hold      = "hold";
    inline const char* release   = "release";
    inline const char* trigWide  = "trigWide";
    inline const char* audioWide = "audioWide";
    inline const char* effectIn  = "effectIn";
}

class SilverDesserProcessor : public juce::AudioProcessor
{
public:
    SilverDesserProcessor();
    ~SilverDesserProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SilverDesser"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // ---- dados para a interface (escritos no audio thread) ----
    std::atomic<float> meterInPeak  { 0.0f };   // pico linear desde a ultima leitura
    std::atomic<float> meterOutPeak { 0.0f };
    std::atomic<float> meterGrDb    { 0.0f };   // maxima reducao desde a ultima leitura
    std::atomic<float> trackLevelDb { 0.0f };   // nivel medio rastreado (Level Tracking)
    std::atomic<double> currentSampleRate { 48000.0 };

    int copyScope (bool bandpass, float* dst, int num) const { return core.copyScope (bandpass, dst, num); }

    // ---- A/B ----
    void switchAB (int slot);
    int getActiveSlot() const { return activeSlot; }

    // preferencias de interface (salvas no estado)
    bool advancedVisible = true;
    int  editorWidth = 1000;

private:
    silver::DesserParams readParams() const;

    silver::DesserCore core;
    int numChannelsPrepared = 2;

    juce::ValueTree slotState[2];
    int activeSlot = 0;

    std::atomic<float>* pInTrim = nullptr; std::atomic<float>* pOutTrim = nullptr;
    std::atomic<float>* pFreq = nullptr;   std::atomic<float>* pWidth = nullptr;  std::atomic<float>* pSlope = nullptr;
    std::atomic<float>* pListen = nullptr; std::atomic<float>* pThr = nullptr;    std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pDamp = nullptr;   std::atomic<float>* pRatio = nullptr;  std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pWet = nullptr;    std::atomic<float>* pKnee = nullptr;   std::atomic<float>* pAtt = nullptr;
    std::atomic<float>* pHold = nullptr;   std::atomic<float>* pRel = nullptr;    std::atomic<float>* pTrig = nullptr;
    std::atomic<float>* pAudio = nullptr;  std::atomic<float>* pFx = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverDesserProcessor)
};
