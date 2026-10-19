#pragma once

#include "InterferenceProcessor.h"
#include "../../Source/ui/LookAndFeel.h"
#include "../../Source/ui/Widgets.h"

namespace interference::ui
{
namespace col = granula::ui::colours;

// Live spectrum: input (grey fill) vs output (white line), with the in-key notes drawn as lines.
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (InterferenceProcessor& p) : proc (p), fft (kOrder)
    {
        window.resize (kSize);
        for (int i = 0; i < kSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / kSize);
        specIn.assign (kSize / 2, -120.0f);
        specOut.assign (kSize / 2, -120.0f);
        ringIn.assign (kSize, 0.0f);
        ringOut.assign (kSize, 0.0f);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 10.0f);
        const auto a = r.reduced (14.0f, 12.0f).withTrimmedBottom (14.0f);

        // in-key note lines
        const uint16_t mask = proc.getActiveMask();
        const int root = (int) proc.apvts.getRawParameterValue (ids::root)->load();
        for (int m = 24; m <= 124; ++m)
        {
            if (! ((mask >> (m % 12)) & 1)) continue;
            const float x = xFor ((float) noteFreq (m), a);
            if (x < a.getX() || x > a.getRight()) continue;
            const bool isRoot = (m % 12) == root && ! proc.isFollowingMidi();
            g.setColour (col::text.withAlpha (isRoot ? 0.22f : 0.08f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }

        // frequency labels
        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.setColour (col::textDim);
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (f, a);
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x - 20.0f, a.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
        }

        auto curve = [&] (const std::vector<float>& spec, bool closed)
        {
            juce::Path p;
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            const int cols = (int) a.getWidth();
            for (int c = 0; c <= cols; c += 2)
            {
                const float f0 = fFor (a.getX() + (float) c, a), f1 = fFor (a.getX() + (float) c + 2.0f, a);
                int b0 = (int) (f0 / sr * kSize), b1 = std::max (b0 + 1, (int) (f1 / sr * kSize));
                b0 = juce::jlimit (1, kSize / 2 - 1, b0); b1 = juce::jlimit (b0 + 1, kSize / 2, b1);
                float v = -120.0f;
                for (int b = b0; b < b1; ++b) v = std::max (v, spec[(size_t) b]);
                const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, v), -90.0f, 0.0f, a.getBottom(), a.getY());
                if (c == 0) p.startNewSubPath (a.getX(), closed ? a.getBottom() : y);
                if (c == 0 && closed) p.lineTo (a.getX(), y); else p.lineTo (a.getX() + (float) c, y);
            }
            if (closed) { p.lineTo (a.getRight(), a.getBottom()); p.closeSubPath(); }
            return p;
        };

        g.setColour (col::text.withAlpha (0.13f));
        g.fillPath (curve (specIn, true));
        g.setColour (col::text);
        g.strokePath (curve (specOut, false), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText ("IN", a.getX(), a.getY(), 60, 14, juce::Justification::left);
        g.setColour (col::text);
        g.drawText ("OUT", a.getX() + 26, a.getY(), 60, 14, juce::Justification::left);
    }

private:
    static constexpr int kOrder = 12, kSize = 1 << kOrder;

    static float xFor (float f, juce::Rectangle<float> a)
    {
        return a.getX() + a.getWidth() * (std::log (f / 30.0f) / std::log (18000.0f / 30.0f));
    }
    static float fFor (float x, juce::Rectangle<float> a)
    {
        return 30.0f * std::pow (18000.0f / 30.0f, (x - a.getX()) / a.getWidth());
    }

    void timerCallback() override
    {
        auto& fifo = proc.displayFifo;
        const int ready = fifo.getNumReady();
        if (ready > 0)
        {
            const auto scope = fifo.read (ready);
            auto take = [this] (int start, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    ringIn[(size_t) ringPos] = proc.displayIn[(size_t) (start + i)];
                    ringOut[(size_t) ringPos] = proc.displayOut[(size_t) (start + i)];
                    ringPos = (ringPos + 1) % kSize;
                }
            };
            take (scope.startIndex1, scope.blockSize1);
            take (scope.startIndex2, scope.blockSize2);
        }
        analyse (ringIn, specIn);
        analyse (ringOut, specOut);
        repaint();
    }

    void analyse (const std::vector<float>& ring, std::vector<float>& spec)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < kSize; ++i) work[(size_t) i] = ring[(size_t) ((ringPos + i) % kSize)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int b = 0; b < kSize / 2; ++b)
        {
            const float dbv = juce::Decibels::gainToDecibels (work[(size_t) b] / (kSize * 0.25f), -120.0f);
            spec[(size_t) b] = std::max (dbv, spec[(size_t) b] - 1.8f);
        }
    }

    InterferenceProcessor& proc;
    juce::dsp::FFT fft;
    std::vector<float> window, specIn, specOut, ringIn, ringOut;
    std::vector<float> work = std::vector<float> (kSize * 2);
    int ringPos = 0;
};

// One-octave keyboard: lit keys are in the key. Click to add/remove notes.
class KeyboardView : public juce::Component, private juce::Timer
{
public:
    explicit KeyboardView (InterferenceProcessor& p) : proc (p) { startTimerHz (15); }

    void paint (juce::Graphics& g) override
    {
        const uint16_t mask = proc.getActiveMask();
        const bool midi = proc.isFollowingMidi();
        const int root = (int) proc.apvts.getRawParameterValue (ids::root)->load();
        const bool custom = (int) proc.apvts.getRawParameterValue (ids::scale)->load() >= customScaleIndex();

        for (int pass = 0; pass < 2; ++pass)
            for (int pc = 0; pc < 12; ++pc)
            {
                if (isBlack (pc) != (pass == 1)) continue;
                const auto r = keyRect (pc);
                const bool on = (mask >> pc) & 1;
                if (! isBlack (pc))
                {
                    g.setColour (on ? col::text : col::panelHi);
                    g.fillRoundedRectangle (r.reduced (1.5f), 5.0f);
                    g.setColour (on ? col::bg : col::textDim);
                }
                else
                {
                    g.setColour (col::bg);
                    g.fillRoundedRectangle (r.expanded (2.0f, 0.0f), 4.0f);
                    g.setColour (on ? col::text.withAlpha (0.85f) : col::track);
                    g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
                    g.setColour (on ? col::bg : col::textDim);
                }
                g.setFont (juce::FontOptions (isBlack (pc) ? 9.5f : 11.0f).withStyle ("Bold"));
                g.drawText (noteNames()[pc], r.withTrimmedTop (r.getHeight() - 18.0f), juce::Justification::centred);
                if (pc == root && ! custom && ! midi)
                {
                    g.fillEllipse (r.getCentreX() - 3.0f, r.getY() + 8.0f, 6.0f, 6.0f);
                }
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int pass = 1; pass >= 0; --pass) // black keys sit on top
            for (int pc = 0; pc < 12; ++pc)
                if (isBlack (pc) == (pass == 1) && keyRect (pc).contains (e.position))
                {
                    proc.toggleNoteFromUI (pc);
                    repaint();
                    return;
                }
    }

private:
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    juce::Rectangle<float> keyRect (int pc) const
    {
        static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const float w = (float) getWidth() / 7.0f, h = (float) getHeight();
        if (! isBlack (pc)) return { whiteIndex[pc] * w, 0.0f, w, h };
        const float bw = w * 0.62f;
        return { (whiteIndex[pc] + 1) * w - bw * 0.5f, 0.0f, bw, h * 0.6f };
    }

    void timerCallback() override
    {
        const auto m = proc.getActiveMask();
        if (m != lastMask) { lastMask = m; repaint(); }
    }

    InterferenceProcessor& proc;
    uint16_t lastMask = 0xFFFF;
};
// Shows the sung note, how far off it is, and where TUNE is pulling it.
class TuneMeter : public juce::Component, private juce::Timer
{
public:
    explicit TuneMeter (InterferenceProcessor& p) : proc (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 10.0f);
        const bool on = proc.isTuneActive();
        auto& t = proc.getTuner();
        const float pitch = t.detectedPitch.load(), target = t.targetPitch.load(), shift = t.appliedShift.load();

        auto text = r.reduced (14.0f, 8.0f);
        auto nameArea = text.removeFromLeft (110.0f);
        if (! on || pitch < 0.0f)
        {
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
            g.drawText ("--", nameArea, juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (on ? "waiting for a voice..." : "TUNE is off", text, juce::Justification::centredLeft);
            return;
        }

        const int tn = (int) std::lround (target);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (28.0f).withStyle ("Bold"));
        g.drawText (noteNames()[((tn % 12) + 12) % 12] + juce::String (tn / 12 - 1), nameArea, juce::Justification::centredLeft);

        // cents scale: -100 .. +100 around the target note
        const auto bar = text.withSizeKeepingCentre (text.getWidth(), 34.0f);
        const float cx = bar.getCentreX();
        g.setColour (col::track);
        g.fillRoundedRectangle (bar.withSizeKeepingCentre (bar.getWidth(), 4.0f), 2.0f);
        for (int c = -100; c <= 100; c += 25)
        {
            const float x = cx + (float) c / 100.0f * bar.getWidth() * 0.5f;
            g.fillRect (x - 0.5f, bar.getCentreY() - (c == 0 ? 12.0f : 5.0f), 1.0f, c == 0 ? 24.0f : 10.0f);
        }
        const float inCents = juce::jlimit (-100.0f, 100.0f, (pitch - target) * 100.0f);
        const float outCents = juce::jlimit (-100.0f, 100.0f, (pitch + shift - target) * 100.0f);
        const float xi = cx + inCents / 100.0f * bar.getWidth() * 0.5f;
        const float xo = cx + outCents / 100.0f * bar.getWidth() * 0.5f;
        g.setColour (col::textDim);
        g.drawEllipse (xi - 6.0f, bar.getCentreY() - 6.0f, 12.0f, 12.0f, 1.5f);
        g.setColour (col::text);
        g.fillEllipse (xo - 5.0f, bar.getCentreY() - 5.0f, 10.0f, 10.0f);

        g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
        g.setColour (col::textDim);
        g.drawText ("IN " + juce::String (juce::roundToInt (inCents)) + " ct", bar.withTrimmedTop (24.0f).withHeight (14.0f).translated (0, 12), juce::Justification::centredLeft);
        g.setColour (col::text);
        g.drawText ("OUT " + juce::String (juce::roundToInt (outCents)) + " ct", bar.withTrimmedTop (24.0f).withHeight (14.0f).translated (0, 12), juce::Justification::centredRight);
    }

private:
    void timerCallback() override { repaint(); }
    InterferenceProcessor& proc;
};
} // namespace interference::ui

class InterferenceView : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1000, kHeight = 770;
    explicit InterferenceView (InterferenceProcessor&);
    ~InterferenceView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresets();
    void presetChosen (int id);
    void stepPreset (int dir);
    void showSaveDialog();
    void panel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    InterferenceProcessor& proc;
    granula::ui::GranulaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };
    juce::TextButton zeroLat { "ZERO LATENCY" }, hq { "HQ" };
    juce::Array<juce::File> userFiles;
    int shownProgram = -1, shownChange = -1;

    interference::ui::SpectrumView spectrum;
    interference::ui::KeyboardView keyboard;
    juce::ComboBox rootBox, scaleBox;
    std::unique_ptr<granula::ui::Toggle> midiToggle;
    juce::Label keyLabel, modeInfo;

    using Knob = granula::ui::Knob;
    std::unique_ptr<Knob> color, snap, focus, gate, trans, low, high, mix, out;
    std::unique_ptr<Knob> tSpeed, tHumanize, tAmount, tFormant;
    std::unique_ptr<granula::ui::PowerButton> tuneOn;
    interference::ui::TuneMeter tuneMeter;
    juce::Rectangle<int> spectrumArea, keyArea, tuneArea, controlArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterferenceView)
};

class InterferenceEditor : public juce::AudioProcessorEditor
{
public:
    explicit InterferenceEditor (InterferenceProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (granula::ui::colours::bg); }

private:
    InterferenceView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterferenceEditor)
};
