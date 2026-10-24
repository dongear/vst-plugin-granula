#include "StoneEditor.h"

using namespace stone;
namespace col = granula::ui::colours;

StoneView::StoneView (StoneProcessor& p)
    : proc (p), presetBar (p.presets, lnf), audition (p), trail (p),
      materials (p.apvts, ids::material, 4), exciters (p.apvts, ids::exciter, 4), steps (p.apvts)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    auto knob = [&] (std::unique_ptr<Knob>& k, const std::string& id, const char* name, bool bip = false)
    {
        k = std::make_unique<Knob> (s, id.c_str(), name, col::text, bip);
        addAndMakeVisible (*k);
    };

    addAndMakeVisible (presetBar);
    addAndMakeVisible (audition);

    // trail
    addAndMakeVisible (trail);
    knob (hits, ids::hits, "Hits");
    knob (rate, ids::rate, "Rate");
    knob (accel, ids::accel, "Accel", true);
    knob (jitter, ids::jitter, "Jitter");
    knob (density, ids::density, "Density");
    knob (hitDecay, ids::hitDecay, "Hit Decay");
    knob (drift, ids::drift, "Drift", true);
    knob (spread, ids::spread, "Spread");
    sync = std::make_unique<granula::ui::Toggle> (s, ids::sync, "SYNC", col::text);
    hold = std::make_unique<granula::ui::Toggle> (s, ids::hold, "HOLD ROLL", col::text);
    hold->setTooltip ("Keep rolling while the key is held");
    div = std::make_unique<granula::ui::Choice> (s, ids::div, "Rate");
    addAndMakeVisible (*sync); addAndMakeVisible (*hold); addAndMakeVisible (*div);

    // material + body
    addAndMakeVisible (materials);
    addAndMakeVisible (exciters);
    knob (decay, ids::decay, "Resonance");
    knob (bright, ids::bright, "Bright");
    knob (damp, ids::damp, "Damping");
    knob (pitch, ids::pitch, "Pitch", true);
    knob (inharm, ids::inharm, "Inharm", true);
    knob (hardness, ids::hardness, "Hardness");
    knob (rough, ids::rough, "Rough");
    knob (click, ids::click, "Click");

    // mods
    for (int i = 0; i < 4; ++i)
    {
        auto& m = modRows[(size_t) i];
        m.num.setText (juce::String (i + 1), juce::dontSendNotification);
        m.num.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
        m.num.setColour (juce::Label::textColourId, col::textDim);
        for (int k = 0; k < NumSources; ++k) m.src.addItem (sourceName (k), k + 1);
        for (int k = 0; k < NumDests; ++k) m.dst.addItem (destName (k), k + 1);
        m.amt.setSliderStyle (juce::Slider::LinearHorizontal);
        m.amt.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        m.amt.setColour (juce::Slider::trackColourId, col::text.withAlpha (0.85f));
        m.amt.setColour (juce::Slider::backgroundColourId, col::track);
        m.amt.setColour (juce::Slider::thumbColourId, col::text);
        m.amt.setColour (juce::Slider::textBoxTextColourId, col::text);
        m.amt.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        m.amt.setDoubleClickReturnValue (true, 0.0);
        m.srcA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ids::modSrc (i), m.src);
        m.dstA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ids::modDst (i), m.dst);
        m.amtA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, ids::modAmt (i), m.amt);
        for (juce::Component* c : { (juce::Component*) &m.num, (juce::Component*) &m.src, (juce::Component*) &m.dst, (juce::Component*) &m.amt })
            addAndMakeVisible (c);
    }
    knob (lfoRate, ids::lfoRate, "LFO Rate");
    lfoShape = std::make_unique<granula::ui::Choice> (s, ids::lfoShape, "LFO Shape");
    addAndMakeVisible (*lfoShape);
    addAndMakeVisible (steps);

    // fx
    knob (grit, ids::grit, "Grit");
    knob (verbMix, ids::verbMix, "Space");
    knob (verbSize, ids::verbSize, "Size");
    knob (verbDecay, ids::verbDecay, "Decay");
    knob (out, ids::out, "Out", true);

    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (10);
}

StoneView::~StoneView() { setLookAndFeel (nullptr); }

void StoneView::timerCallback()
{
    const bool syncOn = proc.apvts.getRawParameterValue (ids::sync)->load() > 0.5f;
    const bool holdOn = proc.apvts.getRawParameterValue (ids::hold)->load() > 0.5f;
    rate->setEnabledLook (! syncOn);
    div->setEnabled (syncOn);
    hits->setEnabledLook (! holdOn);
}

void StoneView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // logo: a cracked pebble
    {
        juce::Path stoneShape;
        const float cx = 44.0f, cy = 32.0f;
        for (int i = 0; i <= 9; ++i)
        {
            const float a = (float) i / 9.0f * juce::MathConstants<float>::twoPi;
            const float rr = 15.0f + 3.0f * std::sin (a * 3.0f + 1.0f) + 1.5f * std::cos (a * 5.0f);
            const float x = cx + rr * std::cos (a) * 1.15f, y = cy + rr * std::sin (a) * 0.85f;
            if (i == 0) stoneShape.startNewSubPath (x, y); else stoneShape.lineTo (x, y);
        }
        stoneShape.closeSubPath();
        g.setColour (col::text);
        g.fillPath (stoneShape);
        g.setColour (col::bg);
        juce::Path crack;
        crack.startNewSubPath (cx - 6, cy - 12); crack.lineTo (cx - 1, cy - 3); crack.lineTo (cx - 5, cy + 4); crack.lineTo (cx + 2, cy + 12);
        g.strokePath (crack, juce::PathStrokeType (1.6f));
    }
    g.setColour (col::text);
    g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
    g.drawText ("STONE", 76, 4, 200, 40, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("by artempidor", 77, 36, 200, 18, juce::Justification::centredLeft);

    common::drawPanel (g, trailArea, "TRAIL");
    common::drawPanel (g, matArea, "MATERIAL");
    common::drawPanel (g, bodyArea, "BODY");
    common::drawPanel (g, modArea, "MOVEMENT");
    common::drawPanel (g, fxArea, "SPACE");

    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
    g.drawText ("SOURCE", modRows[0].src.getX(), modArea.getY() + 30, 120, 14, juce::Justification::centredLeft);
    g.drawText ("TARGET", modRows[0].dst.getX(), modArea.getY() + 30, 120, 14, juce::Justification::centredLeft);
    g.drawText ("AMOUNT", modRows[0].amt.getX(), modArea.getY() + 30, 120, 14, juce::Justification::centredLeft);
    g.drawText ("STEPS", steps.getX(), steps.getY() - 16, 100, 14, juce::Justification::centredLeft);
}

void StoneView::resized()
{
    presetBar.setBounds (kWidth / 2 - 190, 16, 380, 32);
    audition.setBounds (presetBar.getRight() + 16, 19, 70, 26);
    out->setBounds (kWidth - 92, 4, 72, 64);
    out->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    out->slider.setPopupDisplayEnabled (true, true, this);

    // TRAIL
    trailArea = { 16, 72, kWidth - 32, 196 };
    trail.setBounds (trailArea.getX() + 14, trailArea.getY() + 34, 470, trailArea.getHeight() - 46);
    {
        const int x0 = trail.getRight() + 16, cell = 86, y0 = trailArea.getY() + 22;
        Knob* ks[] = { hits.get(), rate.get(), accel.get(), jitter.get(), density.get(), hitDecay.get(), drift.get(), spread.get() };
        for (int i = 0; i < 8; ++i) ks[i]->setBounds (x0 + (i % 4) * cell, y0 + (i / 4) * 86, cell, 86);
        const int tx = x0 + 4 * cell + 12, tw = trailArea.getRight() - tx - 14;
        sync->setBounds (tx, y0 + 14, tw, 28);
        div->setBounds (tx, y0 + 50, tw, 44);
        hold->setBounds (tx, y0 + 112, tw, 34);
    }

    // MATERIAL + BODY
    const int y2 = trailArea.getBottom() + 12;
    matArea = { 16, y2, 470, 196 };
    materials.setBounds (matArea.getX() + 12, matArea.getY() + 34, matArea.getWidth() - 24, 76);
    exciters.setBounds (matArea.getX() + 12, materials.getBottom() + 12, matArea.getWidth() - 24, 38);
    bodyArea = { matArea.getRight() + 12, y2, kWidth - 16 - matArea.getRight() - 12, 196 };
    {
        Knob* ks[] = { decay.get(), bright.get(), damp.get(), pitch.get(), hardness.get(), rough.get(), click.get(), inharm.get() };
        const int cell = (bodyArea.getWidth() - 24) / 4;
        for (int i = 0; i < 8; ++i) ks[i]->setBounds (bodyArea.getX() + 12 + (i % 4) * cell, bodyArea.getY() + 24 + (i / 4) * 84, cell, 84);
    }

    // MOVEMENT + SPACE
    const int y3 = matArea.getBottom() + 12;
    modArea = { 16, y3, 700, kHeight - y3 - 14 };
    for (int i = 0; i < 4; ++i)
    {
        auto& m = modRows[(size_t) i];
        const int y = modArea.getY() + 48 + i * 34;
        m.num.setBounds (modArea.getX() + 14, y, 20, 28);
        m.src.setBounds (modArea.getX() + 36, y, 110, 28);
        m.dst.setBounds (modArea.getX() + 152, y, 110, 28);
        m.amt.setBounds (modArea.getX() + 268, y + 2, 156, 24);
    }
    lfoRate->setBounds (modArea.getX() + 430, modArea.getY() + 30, 80, 86);
    lfoShape->setBounds (modArea.getX() + 430, modArea.getY() + 122, 90, 44);
    steps.setBounds (modArea.getX() + 530, modArea.getY() + 48, modArea.getRight() - modArea.getX() - 544, 130);

    fxArea = { modArea.getRight() + 12, y3, kWidth - 16 - modArea.getRight() - 12, modArea.getHeight() };
    {
        Knob* ks[] = { grit.get(), verbMix.get(), verbSize.get(), verbDecay.get() };
        const int cell = (fxArea.getWidth() - 24) / 2;
        for (int i = 0; i < 4; ++i) ks[i]->setBounds (fxArea.getX() + 12 + (i % 2) * cell, fxArea.getY() + 26 + (i / 2) * 86, cell, 86);
    }
}

StoneEditor::StoneEditor (StoneProcessor& p) : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (StoneView::kWidth * 6 / 10, StoneView::kHeight * 6 / 10, StoneView::kWidth * 2, StoneView::kHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) StoneView::kWidth / StoneView::kHeight);
    setSize (StoneView::kWidth * 9 / 10, StoneView::kHeight * 9 / 10);
}

void StoneEditor::resized()
{
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) StoneView::kWidth));
}
