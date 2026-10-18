#pragma once

#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

// All controls live in MainView, laid out at a fixed design size and scaled
// as a whole when the plugin window is resized.
class MainView : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1080, kHeight = 652;

    explicit MainView (GranulaProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour accent);

    GranulaProcessor& proc;
    granula::ui::GranulaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    using Knob = granula::ui::Knob;
    using Toggle = granula::ui::Toggle;
    using Choice = granula::ui::Choice;
    using Power = granula::ui::PowerButton;

    // Header
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    std::unique_ptr<Knob> outKnob;

    // Grain
    granula::ui::XYPad xyPad;
    granula::ui::GrainCloud cloud;
    std::unique_ptr<Knob> size, density, position, spray, pitch, pitchRand, detune, spread, reverse, shape, feedback, grainMix;
    std::unique_ptr<Choice> scale, grainDiv;
    std::unique_ptr<Toggle> grainSync, freeze;

    // Tape
    std::unique_ptr<Power> tapeOn;
    std::unique_ptr<Knob> tapeDrive, tapeWow, tapeFlutter, tapeAge, tapeHiss;

    // Delay
    std::unique_ptr<Power> delayOn;
    std::unique_ptr<Knob> delayTime, delayFb, delayTone, delayMix;
    std::unique_ptr<Toggle> delaySync, delayPing;
    std::unique_ptr<Choice> delayDiv;

    // Reverb
    std::unique_ptr<Power> revOn;
    std::unique_ptr<Knob> revSize, revDecay, revDamp, revPre, revWidth, revMix;

    juce::Rectangle<int> grainArea, tapeArea, delayArea, revArea;
    int shownProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

class GranulaEditor : public juce::AudioProcessorEditor
{
public:
    explicit GranulaEditor (GranulaProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (granula::ui::colours::bg); }

private:
    MainView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranulaEditor)
};
