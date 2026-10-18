#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace granula::ui
{
namespace colours
{
inline const juce::Colour bg       { 0xff0b0b0b };
inline const juce::Colour panel    { 0xff151515 };
inline const juce::Colour panelHi  { 0xff1f1f1f };
inline const juce::Colour track    { 0xff2e2e2e };
inline const juce::Colour text     { 0xfff4f4f4 };
inline const juce::Colour textDim  { 0xff8a8a8a };
// Monochrome: every section uses white as its accent.
inline const juce::Colour grain    { 0xffffffff };
inline const juce::Colour tape     { 0xffffffff };
inline const juce::Colour delay    { 0xffffffff };
inline const juce::Colour reverb   { 0xffffffff };
} // namespace colours

class GranulaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GranulaLookAndFeel()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, grain.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderOutlineColourId, track);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, panelHi);
        setColour (juce::ComboBox::outlineColourId, track);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, panelHi);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, grain.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::textColourOffId, textDim);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::TextEditor::backgroundColourId, panelHi);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, grain.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, grain);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = juce::jmax (2.5f, radius * 0.13f);
        const float arcR = radius - lineW * 0.5f;
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::track.withMultipliedAlpha (alpha));
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent.withMultipliedAlpha (alpha));
            g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // Knob body
        const float bodyR = arcR - lineW * 1.3f;
        juce::ColourGradient grad (colours::panelHi.brighter (0.15f), centre.x, centre.y - bodyR,
                                   colours::panel.darker (0.3f), centre.x, centre.y + bodyR, false);
        g.setGradientFill (grad);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer
        const auto tip = centre.getPointOnCircumference (bodyR * 0.82f, angle);
        const auto base = centre.getPointOnCircumference (bodyR * 0.35f, angle);
        g.setColour ((s.isMouseOverOrDragging() ? accent : colours::text).withMultipliedAlpha (alpha));
        g.drawLine ({ base, tip }, juce::jmax (2.0f, lineW * 0.6f));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (12.0f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        const float alpha = b.isEnabled() ? 1.0f : 0.35f;
        if (b.getToggleState())
        {
            g.setColour (accent.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        }
        else
        {
            g.setColour (colours::panelHi.brighter (over ? 0.12f : 0.0f).brighter (down ? 0.1f : 0.0f).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (colours::track.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, h * 0.5f)).withStyle ("Bold"));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        const float alpha = box.isEnabled() ? 1.0f : 0.35f;
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.5f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (alpha));
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (14.0f)); }
};

} // namespace granula::ui
