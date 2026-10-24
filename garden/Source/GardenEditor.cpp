#include "GardenEditor.h"

using namespace garden;
namespace col = granula::ui::colours;

GardenView::GardenView (GardenProcessor& p) : proc (p), presetBar (p.presets, lnf), play (p), plant (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    auto knob = [&] (std::unique_ptr<Knob>& k, const char* id, const char* name, bool bip = false)
    {
        k = std::make_unique<Knob> (s, id, name, col::text, bip);
        addAndMakeVisible (*k);
    };

    addAndMakeVisible (presetBar);
    addAndMakeVisible (play);
    addAndMakeVisible (plant);

    grow.setColour (juce::TextButton::buttonOnColourId, col::text);
    grow.setTooltip ("Grow a new plant (new random seed)");
    grow.onClick = [this] { setSeed (1 + (int) (juce::Random::getSystemRandom().nextInt (9999))); };
    prevSeed.onClick = [this] { setSeed ((int) proc.apvts.getRawParameterValue (ids::seed)->load() - 1); };
    nextSeed.onClick = [this] { setSeed ((int) proc.apvts.getRawParameterValue (ids::seed)->load() + 1); };
    for (auto* b : { &grow, &prevSeed, &nextSeed }) addAndMakeVisible (*b);

    knob (growth, ids::growth, "Growth");
    knob (branch, ids::branch, "Branch");
    knob (bend, ids::bend, "Bend", true);
    knob (leaves, ids::leaves, "Leaves");
    knob (roots, ids::roots, "Roots");
    knob (sun, ids::sun, "Sun");
    knob (width, ids::width, "Width");
    growth->slider.setTooltip ("From a simple sprout (pure tone) to a full bush (rich sound)");
    branch->slider.setTooltip ("Branches shape the tone into organic clusters, like a voice or a hollow body");
    bend->slider.setTooltip ("Right: bell / stone overtones. Left: soft, hollow");
    leaves->slider.setTooltip ("Shimmer on top");
    roots->slider.setTooltip ("Body and sub underneath");
    sun->slider.setTooltip ("Brightness");

    knob (water, ids::water, "Water");
    knob (wind, ids::wind, "Wind");
    knob (season, ids::season, "Season");
    knob (bloom, ids::bloom, "Bloom");
    knob (wither, ids::wither, "Wither");
    water->slider.setTooltip ("Breath and air inside the tone");
    wind->slider.setTooltip ("Living movement: every leaf drifts a little (mod wheel adds more)");
    season->slider.setTooltip ("Spring: leaves open one by one, bright and lasting. Autumn: tops fade, notes wilt");
    bloom->slider.setTooltip ("How long a note takes to open (attack)");
    wither->slider.setTooltip ("How long it takes to fade after release");
    octave = std::make_unique<granula::ui::Choice> (s, ids::octave, "Octave");
    addAndMakeVisible (*octave);

    knob (touch, ids::touch, "Touch");
    touch->slider.setTooltip ("A soft organic knock when a note starts");
    touchMat = std::make_unique<granula::ui::Choice> (s, ids::touchMat, "Touch of");
    addAndMakeVisible (*touchMat);
    knob (chorus, ids::chorus, "Chorus");
    knob (space, ids::space, "Space");
    knob (spaceSize, ids::spaceSize, "Size");
    knob (warmth, ids::warmth, "Warmth");
    knob (out, ids::out, "Out", true);

    setSize (kWidth, kHeight);
}

GardenView::~GardenView() { setLookAndFeel (nullptr); }

void GardenView::setSeed (int sd)
{
    if (auto* prm = proc.apvts.getParameter (ids::seed))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (1, 9999, sd)));
        prm->endChangeGesture();
    }
    plant.regrow();
}

void GardenView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // logo: a sprout
    {
        juce::Path stem, l1, l2;
        stem.startNewSubPath (40, 56); stem.quadraticTo (38, 40, 42, 26);
        l1.addEllipse (0, -5, 18, 10);
        l2.addEllipse (0, -4, 15, 8);
        g.setColour (col::text);
        g.strokePath (stem, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (l1, juce::AffineTransform::rotation (-0.7f).translated (42, 30));
        g.fillPath (l2, juce::AffineTransform::rotation (3.6f).translated (41, 38));
    }
    g.setColour (col::text);
    g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
    g.drawText ("GARDEN", 66, 4, 220, 40, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("by artempidor", 67, 36, 200, 18, juce::Justification::centredLeft);

    common::drawPanel (g, plantArea, "PLANT");
    common::drawPanel (g, plantPanel, "SHAPE");
    common::drawPanel (g, climatePanel, "CLIMATE");
    common::drawPanel (g, fxPanel, "TOUCH  &  AIR");
}

void GardenView::resized()
{
    presetBar.setBounds (kWidth / 2 - 150, 16, 380, 32);
    play.setBounds (presetBar.getRight() + 16, 19, 70, 26);
    out->setBounds (kWidth - 92, 4, 72, 64);
    out->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    out->slider.setPopupDisplayEnabled (true, true, this);

    plantArea = { 16, 72, 500, kHeight - 72 - 14 };
    plant.setBounds (plantArea.getX() + 12, plantArea.getY() + 34, plantArea.getWidth() - 24, plantArea.getHeight() - 92);
    grow.setBounds (plantArea.getX() + 12, plant.getBottom() + 12, 200, 34);
    nextSeed.setBounds (plantArea.getRight() - 12 - 34, plant.getBottom() + 14, 34, 30);
    prevSeed.setBounds (nextSeed.getX() - 40, plant.getBottom() + 14, 34, 30);

    const int rx = plantArea.getRight() + 12, rw = kWidth - 16 - rx;
    plantPanel = { rx, 72, rw, 210 };
    {
        Knob* ks[] = { growth.get(), branch.get(), bend.get(), leaves.get(), roots.get(), sun.get(), width.get() };
        const int cell = (rw - 24) / 4;
        for (int i = 0; i < 7; ++i) ks[i]->setBounds (rx + 12 + (i % 4) * cell, plantPanel.getY() + 26 + (i / 4) * 90, cell, 90);
    }
    climatePanel = { rx, plantPanel.getBottom() + 12, rw, 210 };
    {
        Knob* ks[] = { water.get(), wind.get(), season.get(), bloom.get(), wither.get() };
        const int cell = (rw - 24) / 4;
        for (int i = 0; i < 5; ++i) ks[i]->setBounds (rx + 12 + (i % 4) * cell, climatePanel.getY() + 26 + (i / 4) * 90, cell, 90);
        octave->setBounds (rx + 12 + cell + 24, climatePanel.getY() + 26 + 90 + 20, cell - 20, 46);
    }
    fxPanel = { rx, climatePanel.getBottom() + 12, rw, kHeight - climatePanel.getBottom() - 26 };
    {
        const int cell = (rw - 24) / 5;
        Knob* ks[] = { touch.get(), chorus.get(), space.get(), spaceSize.get(), warmth.get() };
        for (int i = 0; i < 5; ++i) ks[i]->setBounds (rx + 12 + i * cell, fxPanel.getY() + 26, cell, 90);
        touchMat->setBounds (rx + 20, fxPanel.getY() + 26 + 92, cell * 2 - 12, 46);
    }
}

GardenEditor::GardenEditor (GardenProcessor& p) : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (GardenView::kWidth * 6 / 10, GardenView::kHeight * 6 / 10, GardenView::kWidth * 2, GardenView::kHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) GardenView::kWidth / GardenView::kHeight);
    setSize (GardenView::kWidth * 9 / 10, GardenView::kHeight * 9 / 10);
}

void GardenEditor::resized()
{
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) GardenView::kWidth));
}
