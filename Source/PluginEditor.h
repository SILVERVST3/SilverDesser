#pragma once

#include "PluginProcessor.h"
#include "Components.h"
#include "SpectrumDisplay.h"

class SilverDesserEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit SilverDesserEditor (SilverDesserProcessor&);
    ~SilverDesserEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==========================================================================
    struct Canvas : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    };

    struct BarButton : public juce::Component
    {
        std::function<void()> onClick;
        bool advanced = true;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
    };

    //==========================================================================
    void timerCallback() override;
    void paintCanvas (juce::Graphics&);
    void layoutChildren();
    void setAdvanced (bool shouldBeAdvanced, bool resizeWindow);
    void updateAB();

    static constexpr int baseW = 1256;
    static constexpr int baseHAdvanced = 712;
    static constexpr int baseHEasy = 628;
    int baseH() const { return advanced ? baseHAdvanced : baseHEasy; }

    SilverDesserProcessor& proc;
    bool advanced = true;

    juce::TooltipWindow tooltipWindow { this, 650 };
    Canvas canvas;

    SpectrumDisplay display;

    MeterBar inMeter { false }, outMeter { true };
    MeterScale inScale, outScale;
    GRMeter grMeter;
    RatioFader ratioFader;

    ValueBox inTrim, outTrim, width, slope, freq, makeup, wet, ratioBox, knee, attack, hold, release, damping;
    ParamButton effectIn, autoTrack, trigWide, audioWide;

    PushBtn listenMix, listenInside, listenOutside;
    juce::ParameterAttachment listenAttach;

    PushBtn abA, abB;
    BarButton advBar;

    // ballistics
    float inLevel = -100.0f, outLevel = -100.0f, outHold = -100.0f, outHoldT = 0.0f;
    float grLevel = 0.0f, grHold = 0.0f, grHoldT = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverDesserEditor)
};
