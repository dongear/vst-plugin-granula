#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace granula::ui
{

// Sample display for the instrument: waveform, play position, spray zone,
// voice playheads and live grains. Click/drag sets the position; when empty,
// a click opens a file browser. Files can be dropped anywhere on the plugin.
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (GranulaProcessor& p)
        : proc (p),
          posParam (p.apvts.getParameter (ids::samplePos)),
          sprayValue (p.apvts.getRawParameterValue (ids::sampleSpray))
    {
        shownVersion = proc.getSampleVersion();
        sampleCache = proc.getSample();
        setMouseCursor (sampleCache ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
        startTimerHz (40);
    }

    std::function<void()> onBrowse;

    void setDragHighlight (bool on) { if (highlight != on) { highlight = on; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (r, 10.0f);

        const auto area = r.reduced (12.0f, 10.0f);
        const auto err = proc.getSampleError();

        if (sampleCache == nullptr)
        {
            juce::Path border;
            border.addRoundedRectangle (r.reduced (6.0f), 8.0f);
            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, border, dashes, 2);
            g.setColour ((highlight ? colours::grain : colours::track.brighter (0.2f)));
            g.fillPath (dashed);

            g.setColour (highlight ? colours::grain : colours::text);
            g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
            g.drawText (proc.isLoadingSample() ? "Loading..." : "Drop an audio file here  -  or click to browse",
                        area.withTrimmedBottom (area.getHeight() * 0.35f), juce::Justification::centred);
            g.setColour (err.isNotEmpty() ? colours::tape : colours::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (err.isNotEmpty() ? err : "WAV  AIFF  MP3  FLAC  OGG  M4A  -  then play notes",
                        area.withTrimmedTop (area.getHeight() * 0.55f), juce::Justification::centred);
            return;
        }

        // Spray zone around the position
        const float pos = posParam->convertFrom0to1 (posParam->getValue());
        const float spray = sprayValue->load();
        const float px = area.getX() + pos * area.getWidth();
        const float sw = spray * area.getWidth();
        g.setColour (colours::grain.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (px - sw, area.getY(), sw * 2.0f, area.getHeight()).getIntersection (area));

        // Waveform
        const auto& peaks = sampleCache->peaks;
        const int bins = (int) peaks.size() / 2;
        const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        float maxAbs = 0.001f;
        for (float v : peaks) maxAbs = std::max (maxAbs, std::abs (v));
        const float scaleY = half / maxAbs * 0.95f;
        const int cols = (int) area.getWidth();
        juce::Path wave;
        for (int x = 0; x < cols; ++x)
        {
            const int b = juce::jlimit (0, bins - 1, x * bins / std::max (1, cols));
            const float lo = peaks[(size_t) b * 2], hi = peaks[(size_t) b * 2 + 1];
            wave.addRectangle (area.getX() + (float) x, mid - hi * scaleY, 1.0f, std::max (1.0f, (hi - lo) * scaleY));
        }
        g.setColour (colours::textDim.withAlpha (highlight ? 0.3f : 0.55f));
        g.fillPath (wave);

        // Grains
        for (auto& v : proc.getSampler().vis)
        {
            const float amp = v.amp.load (std::memory_order_relaxed);
            if (amp <= 0.001f) continue;
            const float x = area.getX() + v.pos.load (std::memory_order_relaxed) * area.getWidth();
            const float pitch = juce::jlimit (-36.0f, 36.0f, v.pitch.load (std::memory_order_relaxed));
            const float y = mid - pitch / 36.0f * (half - 6.0f);
            const float rad = 2.5f + std::sqrt (v.size.load (std::memory_order_relaxed)) * 10.0f;
            g.setColour (colours::grain.withAlpha (0.15f * amp));
            g.fillEllipse (x - rad * 1.6f, y - rad * 1.6f, rad * 3.2f, rad * 3.2f);
            g.setColour (colours::grain.withAlpha (0.3f + 0.7f * amp));
            g.fillEllipse (x - rad * 0.45f, y - rad * 0.45f, rad * 0.9f, rad * 0.9f);
        }

        // Voice playheads (move when Scan != 0)
        g.setColour (colours::text.withAlpha (0.5f));
        for (auto& vp : proc.getSampler().voicePos)
        {
            const float v = vp.load (std::memory_order_relaxed);
            if (v >= 0.0f) g.drawVerticalLine ((int) (area.getX() + v * area.getWidth()), area.getY(), area.getBottom());
        }

        // Position marker
        g.setColour (colours::grain);
        g.fillRect (px - 1.0f, area.getY() - 4.0f, 2.0f, area.getHeight() + 8.0f);
        juce::Path tri;
        tri.addTriangle (px - 5.0f, r.getY() + 2.0f, px + 5.0f, r.getY() + 2.0f, px, r.getY() + 8.0f);
        g.fillPath (tri);

        // File name + length
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.setColour (colours::text.withAlpha (0.85f));
        const double secs = (double) sampleCache->length() / sampleCache->sampleRate;
        g.drawText (juce::String (sampleCache->name) + "   " + juce::String (secs, 1) + " s",
                    area.withTrimmedTop (area.getHeight() - 14.0f), juce::Justification::bottomLeft);

        if (highlight)
        {
            g.setColour (colours::grain.withAlpha (0.12f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (colours::grain);
            g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
            g.drawText ("Drop to replace sample", r, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sampleCache == nullptr) { if (onBrowse) onBrowse(); return; }
        dragging = true;
        posParam->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto area = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        const float v = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        posParam->setValueNotifyingHost (posParam->convertTo0to1 (v));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) posParam->endChangeGesture();
        dragging = false;
    }

private:
    void timerCallback() override
    {
        if (proc.getSampleVersion() != shownVersion)
        {
            shownVersion = proc.getSampleVersion();
            sampleCache = proc.getSample();
            setMouseCursor (sampleCache ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
        }
        repaint();
    }

    GranulaProcessor& proc;
    juce::RangedAudioParameter* posParam;
    std::atomic<float>* sprayValue;
    std::shared_ptr<const granula::SampleData> sampleCache;
    int shownVersion = -1;
    bool highlight = false, dragging = false;
};

// Holds a note while pressed so a sample can be auditioned without MIDI.
class PreviewButton : public juce::TextButton
{
public:
    explicit PreviewButton (GranulaProcessor& p) : juce::TextButton ("HOLD TO PLAY"), proc (p)
    {
        setColour (juce::TextButton::buttonOnColourId, colours::grain);
        setTooltip ("Plays C3 while held");
    }

    void buttonStateChanged() override
    {
        const bool down = isDown();
        if (down != playing) { playing = down; proc.previewNote (60, down); setToggleState (down, juce::dontSendNotification); }
    }

private:
    GranulaProcessor& proc;
    bool playing = false;
};

} // namespace granula::ui
