#pragma once

#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"
#include "ui/WaveformView.h"

// All controls live in MainView, laid out at a fixed design size and scaled
// as a whole when the plugin window is resized.
class MainView : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::TextDragAndDropTarget,
                 private juce::Timer
{
public:
    static constexpr int kWidth = 1080, kHeight = 652;

    explicit MainView (GranulaProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drag & drop of samples and preset files
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    // Some hosts hand over dragged files as text (a path or file:// URL).
    bool isInterestedInTextDrag (const juce::String& text) override;
    void textDragEnter (const juce::String& text, int x, int y) override;
    void textDragExit (const juce::String& text) override;
    void textDropped (const juce::String& text, int x, int y) override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void rebuildPresetMenu();
    void presetChosen (int id);
    void stepPreset (int dir);
    void showSaveDialog();
    void browseForSample();
    void applyMode();
    void layoutGrain();
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour accent);

    GranulaProcessor& proc;
    bool inputMode = false;
    granula::ui::GranulaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    using Knob = granula::ui::Knob;
    using Toggle = granula::ui::Toggle;
    using Choice = granula::ui::Choice;
    using Power = granula::ui::PowerButton;

    // Header
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };
    juce::Array<juce::File> userFiles;
    int shownChange = -1;
    std::unique_ptr<Knob> outKnob;

    // Grain
    granula::ui::XYPad xyPad;
    std::unique_ptr<granula::ui::GrainCloud> cloud;        // INPUT mode
    std::unique_ptr<granula::ui::WaveformView> waveform;   // SAMPLE mode
    std::unique_ptr<juce::TextButton> loadButton;          // SAMPLE mode
    std::unique_ptr<granula::ui::PreviewButton> preview;   // SAMPLE mode
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton modeSample { "SAMPLE" }, modeInput { "INPUT" };
    juce::Label inputHint;

    std::unique_ptr<Knob> size, density, pitch, pitchRand, detune, spread, reverse, shape;
    std::unique_ptr<Knob> positionS, sprayS, scan, attack, release;      // SAMPLE mode
    std::unique_ptr<Knob> positionI, sprayI, feedback, grainMix;          // INPUT mode
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
