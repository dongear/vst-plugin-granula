#pragma once

#include "GardenProcessor.h"
#include "../../Source/ui/LookAndFeel.h"
#include "../../Source/ui/Widgets.h"

namespace garden::ui
{
namespace col = granula::ui::colours;

// The plant: its shape is the sound. Leaves are partials and glow while notes play.
class PlantView : public juce::Component, private juce::Timer
{
public:
    explicit PlantView (GardenProcessor& p) : proc (p) { startTimerHz (30); }

    void regrow() { growAnim = 0.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 12.0f);

        const auto pp = proc.plantParams();
        const auto spec = buildPlant (pp);
        auto& s = proc.apvts;
        const float water = s.getRawParameterValue (ids::water)->load();
        const float wind = s.getRawParameterValue (ids::wind)->load();
        const float season = s.getRawParameterValue (ids::season)->load();
        const float anim = smooth01 (growAnim);

        const float groundY = r.getY() + r.getHeight() * 0.76f;
        const float cx = r.getCentreX();
        granula::Rng rng ((uint32_t) pp.seed * 7919u + 17u);

        // sun
        {
            const float rad = 10.0f + 24.0f * pp.sun;
            const juce::Point<float> c (r.getRight() - 78.0f, r.getY() + 74.0f);
            g.setColour (col::text.withAlpha (0.08f + 0.1f * pp.sun));
            g.fillEllipse (c.x - rad * 1.8f, c.y - rad * 1.8f, rad * 3.6f, rad * 3.6f);
            g.setColour (col::text.withAlpha (0.35f + 0.5f * pp.sun));
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.5f);
            for (int i = 0; i < 12; ++i)
            {
                const float a = (float) i / 12.0f * juce::MathConstants<float>::twoPi + (float) clockT * 0.1f;
                g.drawLine (c.x + std::cos (a) * rad * 1.3f, c.y + std::sin (a) * rad * 1.3f,
                            c.x + std::cos (a) * rad * (1.5f + 0.4f * pp.sun), c.y + std::sin (a) * rad * (1.5f + 0.4f * pp.sun), 1.0f);
            }
        }

        // ground + roots
        g.setColour (col::track);
        g.drawLine (r.getX() + 20.0f, groundY, r.getRight() - 20.0f, groundY, 1.5f);
        const int nRoots = 2 + (int) std::lround (pp.roots * 9.0f);
        for (int i = 0; i < nRoots; ++i)
        {
            juce::Path root;
            const float a = juce::jmap (rng.uni(), -1.1f, 1.1f);
            const float len = (20.0f + 70.0f * pp.roots * (0.5f + 0.5f * rng.uni())) * anim;
            root.startNewSubPath (cx, groundY);
            root.quadraticTo (cx + a * len * 0.6f, groundY + len * 0.5f, cx + a * len, groundY + len * (0.7f + 0.3f * rng.uni()));
            g.setColour (col::text.withAlpha (0.35f));
            g.strokePath (root, juce::PathStrokeType (1.0f + pp.roots * 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // trunk
        const float sway = wind * 10.0f * (float) std::sin (clockT * 0.9);
        const float height = (r.getHeight() * 0.66f) * (0.32f + 0.68f * pp.growth) * anim;
        const float bendX = pp.bend * r.getWidth() * 0.12f;
        const juce::Point<float> base (cx, groundY), top (cx + bendX * 0.5f + sway, groundY - height);
        const juce::Point<float> ctrl (cx + bendX, groundY - height * 0.5f);
        juce::Path trunk;
        trunk.startNewSubPath (base);
        trunk.quadraticTo (ctrl, top);
        g.setColour (col::text);
        g.strokePath (trunk, juce::PathStrokeType (2.0f + 3.0f * pp.growth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto onCurve = [] (juce::Point<float> a, juce::Point<float> c, juce::Point<float> b, float t)
        {
            const float u = 1.0f - t;
            return juce::Point<float> (u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y);
        };

        // branches
        struct Br { juce::Point<float> a, c, b; };
        std::vector<Br> branches;
        branches.push_back ({ base, ctrl, top });
        for (int b = 1; b < spec.branches; ++b)
        {
            const float at = 0.3f + 0.6f * rng.uni();
            const auto start = onCurve (base, ctrl, top, at);
            const float side = (b % 2 == 0 ? -1.0f : 1.0f);
            const float ang = side * (0.35f + 0.7f * pp.branch * rng.uni());
            const float len = height * (0.3f + 0.35f * rng.uni());
            const juce::Point<float> end (start.x + std::sin (ang) * len + sway * 0.8f, start.y - std::cos (ang) * len * (0.8f - 0.3f * season));
            const juce::Point<float> c (start.x + std::sin (ang) * len * 0.3f + bendX * 0.3f, start.y - len * 0.6f);
            branches.push_back ({ start, c, end });
            juce::Path bp;
            bp.startNewSubPath (start);
            bp.quadraticTo (c, end);
            g.setColour (col::text.withAlpha (0.85f));
            g.strokePath (bp, juce::PathStrokeType (1.2f + 1.8f * pp.growth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // leaves = partials
        for (int k = 0; k < kPartials; ++k)
        {
            const auto& q = spec.p[(size_t) k];
            if (q.amp < 0.01f) continue;
            const auto& br = branches[(size_t) std::clamp (spec.branches > 1 ? q.branchId : 0, 0, (int) branches.size() - 1)];
            const float tpos = 0.3f + 0.7f * q.height;
            auto at = onCurve (br.a, br.c, br.b, tpos);
            const auto next = onCurve (br.a, br.c, br.b, std::min (1.0f, tpos + 0.05f));
            float dir = std::atan2 (next.y - at.y, next.x - at.x);
            const float side = (k % 2 == 0 ? 1.0f : -1.0f);
            const float droop = season * 0.9f;
            dir += side * (1.0f - droop) * 0.9f + droop * 1.2f * side;
            const float size = (7.0f + 36.0f * std::sqrt (q.amp)) * anim * (1.0f - 0.25f * season);
            const float glow = std::min (1.0f, proc.engine.leafGlow[(size_t) k].load() * 6.0f);
            const float flutter = wind * 0.25f * (float) std::sin (clockT * (1.0 + q.wobbleRate) + q.wobblePhase * 6.0);

            juce::Path leaf;
            leaf.addEllipse (0.0f, -size * 0.22f, size, size * 0.44f);
            const auto xf = juce::AffineTransform::rotation (dir + flutter).translated (at.x, at.y);
            g.setColour (col::text.withAlpha (0.12f + 0.75f * glow));
            g.fillPath (leaf, xf);
            g.setColour (col::text.withAlpha (0.55f + 0.45f * glow));
            g.strokePath (leaf, juce::PathStrokeType (1.0f), xf);
        }

        // fallen leaves in autumn / winter
        if (season > 0.5f)
        {
            const int fallen = (int) ((season - 0.5f) * 14.0f);
            for (int i = 0; i < fallen; ++i)
            {
                const float x = cx + juce::jmap (rng.uni(), -1.0f, 1.0f) * r.getWidth() * 0.35f;
                juce::Path leaf;
                leaf.addEllipse (-5.0f, -2.0f, 10.0f, 4.0f);
                g.setColour (col::text.withAlpha (0.3f));
                g.fillPath (leaf, juce::AffineTransform::rotation (rng.uni() * 3.0f).translated (x, groundY - 2.0f));
            }
        }

        // water: drops falling
        const int drops = (int) std::lround (water * 14.0f);
        for (int i = 0; i < drops; ++i)
        {
            const float x = r.getX() + 30.0f + rng.uni() * (r.getWidth() - 60.0f);
            const float speed = 40.0f + 40.0f * rng.uni();
            const float y = r.getY() + std::fmod ((float) clockT * speed + rng.uni() * 400.0f, groundY - r.getY());
            g.setColour (col::text.withAlpha (0.45f));
            g.fillEllipse (x - 1.5f, y - 3.0f, 3.0f, 6.0f);
        }

        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.setColour (col::textDim);
        g.drawText ("SEED #" + juce::String (pp.seed).paddedLeft ('0', 4), r.reduced (14.0f, 10.0f), juce::Justification::topLeft);
    }

private:
    void timerCallback() override
    {
        clockT += 1.0 / 30.0;
        if (growAnim < 1.0f) growAnim = std::min (1.0f, growAnim + 1.0f / 36.0f);
        repaint();
    }

    GardenProcessor& proc;
    double clockT = 0.0;
    float growAnim = 1.0f;
};

class PlayButton : public juce::TextButton
{
public:
    explicit PlayButton (GardenProcessor& p) : juce::TextButton ("PLAY"), proc (p)
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
    GardenProcessor& proc;
    bool down = false;
};
} // namespace garden::ui

class GardenView : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1080, kHeight = 700;
    explicit GardenView (GardenProcessor&);
    ~GardenView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override {}
    void setSeed (int s);

    GardenProcessor& proc;
    granula::ui::GranulaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    common::PresetBar presetBar;
    garden::ui::PlayButton play;
    garden::ui::PlantView plant;
    juce::TextButton grow { "GROW" }, prevSeed { "<" }, nextSeed { ">" };

    using Knob = granula::ui::Knob;
    std::unique_ptr<Knob> growth, branch, bend, leaves, roots, sun, width;
    std::unique_ptr<Knob> water, wind, season, bloom, wither;
    std::unique_ptr<Knob> touch, chorus, space, spaceSize, warmth, out;
    std::unique_ptr<granula::ui::Choice> touchMat, octave;

    juce::Rectangle<int> plantArea, plantPanel, climatePanel, fxPanel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GardenView)
};

class GardenEditor : public juce::AudioProcessorEditor
{
public:
    explicit GardenEditor (GardenProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (granula::ui::colours::bg); }
private:
    GardenView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GardenEditor)
};
