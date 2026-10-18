#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "../dsp/GranularEngine.h"

namespace granula::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

// Rotary knob with a caption above and value read-out below.
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const char* paramId, const juce::String& caption, juce::Colour accent, bool bipolar = false)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        addAndMakeVisible (slider);

        label.setText (caption.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        label.setColour (juce::Label::textColourId, colours::textDim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }

    // The value box is created before we're attached to the themed parent;
    // rebuild it once we are so it picks up the custom look-and-feel.
    void parentHierarchyChanged() override { slider.sendLookAndFeelChange(); }

    void setEnabledLook (bool on)
    {
        slider.setEnabled (on);
        label.setAlpha (on ? 1.0f : 0.4f);
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Pill-shaped toggle bound to a bool parameter.
class Toggle : public juce::TextButton
{
public:
    Toggle (APVTS& state, const char* paramId, const juce::String& caption, juce::Colour accent)
        : juce::TextButton (caption)
    {
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, accent);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Round power switch used in section headers.
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton (APVTS& state, const char* paramId, juce::Colour c) : accent (c)
    {
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
        setTooltip ("On / Off");
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 18.0f);
        const bool on = getToggleState();
        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
        }
        g.setColour (on ? accent : colours::track.brighter (over ? 0.3f : 0.0f));
        g.fillEllipse (r);

        juce::Path icon;
        const auto c = r.getCentre();
        icon.addCentredArc (c.x, c.y, 4.5f, 4.5f, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
        icon.startNewSubPath (c.x, c.y - 6.0f);
        icon.lineTo (c.x, c.y - 1.5f);
        g.setColour (on ? colours::bg : colours::textDim);
        g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    juce::Colour accent;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Caption + combo box bound to a choice parameter.
class Choice : public juce::Component
{
public:
    Choice (APVTS& state, const char* paramId, const juce::String& caption)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);
        addAndMakeVisible (box);
        label.setText (caption.toUpperCase(), juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        label.setColour (juce::Label::textColourId, colours::textDim);
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        box.setBounds (r.removeFromTop (26));
    }

    juce::ComboBox box;
    juce::Label label;

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

// XY pad: X = grain size, Y = density. Drag anywhere to move the handle.
class XYPad : public juce::Component, private juce::Timer
{
public:
    XYPad (APVTS& state, const char* xId, const char* yId, const char* syncId)
        : xParam (state.getParameter (xId)), yParam (state.getParameter (yId)),
          syncValue (state.getRawParameterValue (syncId))
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (r, 10.0f);

        // Grid
        g.setColour (colours::track.withAlpha (0.55f));
        for (int i = 1; i < 6; ++i)
        {
            const float fx = r.getX() + r.getWidth() * (float) i / 6.0f;
            const float fy = r.getY() + r.getHeight() * (float) i / 6.0f;
            g.drawVerticalLine ((int) fx, r.getY() + 6, r.getBottom() - 6);
            g.drawHorizontalLine ((int) fy, r.getX() + 6, r.getRight() - 6);
        }

        const auto h = handlePos();
        const auto accent = colours::grain;

        // Crosshair
        g.setColour (accent.withAlpha (0.25f));
        g.drawVerticalLine ((int) h.x, r.getY() + 4, r.getBottom() - 4);
        g.drawHorizontalLine ((int) h.y, r.getX() + 4, r.getRight() - 4);

        // Glow + handle
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (accent.withAlpha (0.06f * (float) (5 - i)));
            const float rr = 10.0f + (float) i * 6.0f;
            g.fillEllipse (h.x - rr, h.y - rr, rr * 2, rr * 2);
        }
        g.setColour (accent);
        g.fillEllipse (h.x - 9, h.y - 9, 18, 18);
        g.setColour (colours::bg);
        g.fillEllipse (h.x - 3.5f, h.y - 3.5f, 7, 7);

        // Axis captions
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.setColour (colours::textDim);
        g.drawText ("SIZE  " + xParam->getCurrentValueAsText(), r.reduced (10, 8).removeFromBottom (14),
                    juce::Justification::centredRight);
        const bool synced = syncValue->load() > 0.5f;
        g.drawText (synced ? juce::String ("DENSITY  (SYNC)") : "DENSITY  " + yParam->getCurrentValueAsText(),
                    r.reduced (10, 8).removeFromTop (14), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        xParam->beginChangeGesture();
        yParam->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto r = padArea();
        const float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float ny = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight());
        xParam->setValueNotifyingHost (nx);
        yParam->setValueNotifyingHost (ny);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        xParam->endChangeGesture();
        yParam->endChangeGesture();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        xParam->setValueNotifyingHost (xParam->getDefaultValue());
        yParam->setValueNotifyingHost (yParam->getDefaultValue());
    }

private:
    juce::Rectangle<float> padArea() const { return getLocalBounds().toFloat().reduced (16.0f); }

    juce::Point<float> handlePos() const
    {
        const auto r = padArea();
        return { r.getX() + xParam->getValue() * r.getWidth(), r.getBottom() - yParam->getValue() * r.getHeight() };
    }

    void timerCallback() override
    {
        const float x = xParam->getValue(), y = yParam->getValue();
        const float s = syncValue->load();
        if (! juce::exactlyEqual (x, lastX) || ! juce::exactlyEqual (y, lastY) || ! juce::exactlyEqual (s, lastSync)) { lastX = x; lastY = y; lastSync = s; repaint(); }
    }

    juce::RangedAudioParameter* xParam;
    juce::RangedAudioParameter* yParam;
    std::atomic<float>* syncValue;
    float lastX = -1, lastY = -1, lastSync = -1;
};

// Live view of the grain cloud: x = how far back in time, y = pitch.
class GrainCloud : public juce::Component, private juce::Timer
{
public:
    explicit GrainCloud (granula::GranularEngine& e) : engine (e) { startTimerHz (40); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (r, 10.0f);

        const auto area = r.reduced (12.0f, 10.0f);
        g.setColour (colours::track.withAlpha (0.6f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        for (int i = 0; i <= 6; ++i)
        {
            const float x = area.getRight() - area.getWidth() * (float) i / 6.0f;
            g.drawVerticalLine ((int) x, area.getCentreY() - 3, area.getCentreY() + 3);
        }

        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.setColour (colours::textDim.withAlpha (0.8f));
        g.drawText ("NOW", area.withTrimmedTop (area.getHeight() - 12), juce::Justification::bottomRight);
        g.drawText ("-3 s", area.withTrimmedTop (area.getHeight() - 12), juce::Justification::bottomLeft);
        g.drawText ("+24 st", area, juce::Justification::topLeft);

        // Input level as a soft bar on the right edge.
        const float lvl = juce::jlimit (0.0f, 1.0f, smoothedLevel);
        g.setColour (colours::grain.withAlpha (0.5f));
        g.fillRoundedRectangle (r.getRight() - 6.0f, r.getBottom() - 6.0f - lvl * (r.getHeight() - 12.0f), 3.0f,
                                lvl * (r.getHeight() - 12.0f), 1.5f);

        for (auto& v : engine.vis)
        {
            const float amp = v.amp.load (std::memory_order_relaxed);
            if (amp <= 0.001f) continue;
            const float pos = v.pos.load (std::memory_order_relaxed);
            const float pitch = juce::jlimit (-24.0f, 24.0f, v.pitch.load (std::memory_order_relaxed));
            const float size = v.size.load (std::memory_order_relaxed);
            const float x = area.getRight() - pos * area.getWidth();
            const float y = area.getCentreY() - pitch / 24.0f * (area.getHeight() * 0.5f - 6.0f);
            const float rad = 2.5f + std::sqrt (size) * 11.0f;
            g.setColour (colours::grain.withAlpha (0.15f * amp));
            g.fillEllipse (x - rad * 1.8f, y - rad * 1.8f, rad * 3.6f, rad * 3.6f);
            g.setColour (colours::grain.withAlpha (0.25f + 0.75f * amp));
            g.fillEllipse (x - rad * 0.5f, y - rad * 0.5f, rad, rad);
        }
    }

private:
    void timerCallback() override
    {
        const float lvl = engine.inputLevel.load (std::memory_order_relaxed);
        smoothedLevel = juce::jmax (lvl, smoothedLevel * 0.85f);
        repaint();
    }

    granula::GranularEngine& engine;
    float smoothedLevel = 0.0f;
};

} // namespace granula::ui
