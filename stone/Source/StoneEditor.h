#pragma once

#include "StoneProcessor.h"
#include "../../Source/ui/LookAndFeel.h"
#include "../../Source/ui/Widgets.h"

namespace stone::ui
{
namespace col = granula::ui::colours;
using APVTS = juce::AudioProcessorValueTreeState;

// A row/grid of buttons acting as a radio selector for a choice parameter.
class ChoiceButtons : public juce::Component, private juce::Timer
{
public:
    ChoiceButtons (APVTS& s, const char* id, int columns) : param (s.getParameter (id)), cols (columns)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
            for (int i = 0; i < c->choices.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (c->choices[i].toUpperCase()));
                b->setColour (juce::TextButton::buttonOnColourId, col::text);
                b->onClick = [this, i]
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                    param->endChangeGesture();
                    sync();
                };
                addAndMakeVisible (b);
            }
        sync();
        startTimerHz (10);
    }

    void resized() override
    {
        const int rows = (buttons.size() + cols - 1) / cols;
        const float w = (float) getWidth() / (float) cols, h = (float) getHeight() / (float) rows;
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (juce::Rectangle<float> ((float) (i % cols) * w, (float) (i / cols) * h, w, h).reduced (3.0f).toNearestInt());
    }

private:
    void sync()
    {
        const int v = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == v, juce::dontSendNotification);
    }
    void timerCallback() override { sync(); }

    juce::RangedAudioParameter* param;
    int cols;
    juce::OwnedArray<juce::TextButton> buttons;
};

// Planned trail (deterministic preview) + live strikes across the stereo field.
class TrailView : public juce::Component, private juce::Timer
{
public:
    explicit TrailView (StoneProcessor& p) : proc (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 10.0f);

        auto field = r.reduced (14.0f, 10.0f);
        auto live = field.removeFromTop (34.0f);
        field.removeFromTop (8.0f);

        // live stereo field
        g.setColour (col::track);
        g.drawHorizontalLine ((int) live.getCentreY(), live.getX(), live.getRight());
        g.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
        g.setColour (col::textDim);
        g.drawText ("L", live.withWidth (12), juce::Justification::centredLeft);
        g.drawText ("R", live.withTrimmedLeft (live.getWidth() - 12), juce::Justification::centredRight);
        const double now = proc.engine.now();
        for (auto& f : proc.engine.flashes)
        {
            const double t = f.time.load();
            if (t < 0.0 || now - t > 1.2) continue;
            const float age = (float) ((now - t) / 1.2), lv = std::min (1.0f, f.level.load());
            const float x = live.getCentreX() + f.pan.load() * (live.getWidth() * 0.5f - 14.0f);
            const float rad = 3.0f + 9.0f * lv * (1.0f - age);
            g.setColour (col::text.withAlpha (0.15f * (1.0f - age)));
            g.fillEllipse (x - rad * 2.0f, live.getCentreY() - rad * 2.0f, rad * 4.0f, rad * 4.0f);
            g.setColour (col::text.withAlpha (1.0f - age));
            g.fillEllipse (x - rad * 0.5f, live.getCentreY() - rad * 0.5f, rad, rad);
        }

        // planned trail: time on x, level on y
        const auto p = proc.currentParams();
        std::vector<std::pair<double, float>> hits;
        granula::Rng rng (1234);
        double t = 0.0, gap = p.intervalSec;
        const int total = p.hold ? 24 : std::max (1, p.hits);
        for (int k = 0; k < total; ++k)
        {
            const bool sounds = k == 0 || rng.uni() <= p.density;
            if (sounds) hits.push_back ({ t, std::pow (p.hitDecay, (float) k) });
            t += std::max (0.004, gap * (1.0 + p.jitter * 0.9 * rng.bi()));
            gap = std::clamp (gap * std::pow (2.0, -0.25 * p.accel), 0.006, 4.0);
        }
        const double span = std::max (0.25, t);
        g.setColour (col::track);
        g.drawHorizontalLine ((int) field.getBottom(), field.getX(), field.getRight());
        for (auto& [ht, lv] : hits)
        {
            const float x = field.getX() + (float) (ht / span) * field.getWidth();
            const float y = field.getBottom() - lv * (field.getHeight() - 6.0f);
            g.setColour (col::text.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, y, field.getBottom());
            g.setColour (col::text);
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }
        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.setColour (col::textDim);
        g.drawText (p.hold ? "ROLLS WHILE HELD" : juce::String ((int) hits.size()) + " HITS  /  " + juce::String (span, 2) + " s",
                    field.withHeight (14.0f), juce::Justification::topRight);
    }

private:
    void timerCallback() override { repaint(); }
    StoneProcessor& proc;
};

// 8 step values, click / drag to draw.
class StepView : public juce::Component, private juce::Timer
{
public:
    explicit StepView (APVTS& s)
    {
        for (int i = 0; i < 8; ++i) params[(size_t) i] = s.getParameter (ids::step (i));
        startTimerHz (15);
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 8.0f);
        const float w = r.getWidth() / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float v = params[(size_t) i]->getValue();
            auto bar = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (3.0f, 6.0f);
            g.setColour (col::track);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (col::text);
            g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() * (1.0f - v)), 3.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 7, (int) (e.position.x / ((float) getWidth() / 8.0f)));
        const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - 6.0f) / ((float) getHeight() - 12.0f));
        params[(size_t) i]->setValueNotifyingHost (v);
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    std::array<juce::RangedAudioParameter*, 8> params {};
};

// "Hold to play" button for auditioning without a keyboard.
class AuditionButton : public juce::TextButton
{
public:
    explicit AuditionButton (StoneProcessor& p) : juce::TextButton ("PLAY"), proc (p)
    {
        setColour (juce::TextButton::buttonOnColourId, col::text);
        setTooltip ("Plays C3 while held");
    }
    void buttonStateChanged() override
    {
        const bool d = isDown();
        if (d != down) { down = d; proc.previewNote (d); setToggleState (d, juce::dontSendNotification); }
    }
private:
    StoneProcessor& proc;
    bool down = false;
};
} // namespace stone::ui

class StoneView : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1080, kHeight = 704;
    explicit StoneView (StoneProcessor&);
    ~StoneView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    StoneProcessor& proc;
    granula::ui::GranulaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    common::PresetBar presetBar;
    stone::ui::AuditionButton audition;

    using Knob = granula::ui::Knob;
    stone::ui::TrailView trail;
    std::unique_ptr<Knob> hits, rate, accel, jitter, density, hitDecay, drift, spread;
    std::unique_ptr<granula::ui::Toggle> sync, hold;
    std::unique_ptr<granula::ui::Choice> div;

    stone::ui::ChoiceButtons materials, exciters;
    std::unique_ptr<Knob> decay, bright, damp, pitch, inharm, hardness, rough, click;

    struct ModRow
    {
        juce::Label num;
        juce::ComboBox src, dst;
        juce::Slider amt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcA, dstA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtA;
    };
    std::array<ModRow, 4> modRows;
    std::unique_ptr<Knob> lfoRate;
    std::unique_ptr<granula::ui::Choice> lfoShape;
    stone::ui::StepView steps;

    std::unique_ptr<Knob> grit, verbMix, verbSize, verbDecay, out;

    juce::Rectangle<int> trailArea, matArea, bodyArea, modArea, fxArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoneView)
};

class StoneEditor : public juce::AudioProcessorEditor
{
public:
    explicit StoneEditor (StoneProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (granula::ui::colours::bg); }
private:
    StoneView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoneEditor)
};
