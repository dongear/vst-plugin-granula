#include "PluginEditor.h"

using namespace granula;
namespace col = granula::ui::colours;

MainView::MainView (GranulaProcessor& p)
    : proc (p),
      xyPad (p.apvts, ids::size, ids::density, ids::sync),
      cloud (p.getGranular())
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    auto knob = [&] (std::unique_ptr<Knob>& k, const char* id, const char* name, juce::Colour c, bool bip = false)
    {
        k = std::make_unique<Knob> (s, id, name, c, bip);
        addAndMakeVisible (*k);
    };

    // Header
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
        shownProgram = idx;
    };
    presetBox.setColour (juce::ComboBox::backgroundColourId, col::panel);
    addAndMakeVisible (presetBox);

    auto step = [this] (int dir)
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + dir + n) % n);
        refreshPresetBox();
    };
    prevPreset.onClick = [step] { step (-1); };
    nextPreset.onClick = [step] { step (1); };
    for (auto* b : { &prevPreset, &nextPreset })
    {
        b->setColour (juce::TextButton::buttonOnColourId, col::grain);
        addAndMakeVisible (*b);
    }
    knob (outKnob, ids::outGain, "Output", col::text, true);

    // Grain
    addAndMakeVisible (xyPad);
    addAndMakeVisible (cloud);
    knob (size, ids::size, "Size", col::grain);
    knob (density, ids::density, "Density", col::grain);
    knob (position, ids::position, "Position", col::grain);
    knob (spray, ids::spray, "Spray", col::grain);
    knob (pitch, ids::pitch, "Pitch", col::grain, true);
    knob (pitchRand, ids::pitchRand, "Pitch Rnd", col::grain);
    knob (detune, ids::detune, "Detune", col::grain);
    knob (spread, ids::spread, "Spread", col::grain);
    knob (reverse, ids::reverse, "Reverse", col::grain);
    knob (shape, ids::shape, "Shape", col::grain);
    knob (feedback, ids::feedback, "Feedback", col::grain);
    knob (grainMix, ids::grainMix, "Mix", col::grain);

    scale = std::make_unique<Choice> (s, ids::scale, "Scale");
    grainDiv = std::make_unique<Choice> (s, ids::division, "Rate");
    grainSync = std::make_unique<Toggle> (s, ids::sync, "SYNC", col::grain);
    freeze = std::make_unique<Toggle> (s, ids::freeze, "FREEZE", col::grain);
    freeze->setTooltip ("Stop recording new audio and keep granulating what is in the buffer");
    for (juce::Component* c : { (juce::Component*) scale.get(), (juce::Component*) grainDiv.get(),
                                (juce::Component*) grainSync.get(), (juce::Component*) freeze.get() })
        addAndMakeVisible (c);

    // Tape
    tapeOn = std::make_unique<Power> (s, ids::tapeOn, col::tape);
    addAndMakeVisible (*tapeOn);
    knob (tapeDrive, ids::tapeDrive, "Drive", col::tape);
    knob (tapeWow, ids::tapeWow, "Wow", col::tape);
    knob (tapeFlutter, ids::tapeFlutter, "Flutter", col::tape);
    knob (tapeAge, ids::tapeAge, "Age", col::tape);
    knob (tapeHiss, ids::tapeHiss, "Hiss", col::tape);

    // Delay
    delayOn = std::make_unique<Power> (s, ids::delayOn, col::delay);
    addAndMakeVisible (*delayOn);
    knob (delayTime, ids::delayTime, "Time", col::delay);
    knob (delayFb, ids::delayFb, "Feedback", col::delay);
    knob (delayTone, ids::delayTone, "Tone", col::delay);
    knob (delayMix, ids::delayMix, "Mix", col::delay);
    delaySync = std::make_unique<Toggle> (s, ids::delaySync, "SYNC", col::delay);
    delayPing = std::make_unique<Toggle> (s, ids::delayPing, "PING-PONG", col::delay);
    delayDiv = std::make_unique<Choice> (s, ids::delayDiv, "Rate");
    addAndMakeVisible (*delaySync);
    addAndMakeVisible (*delayPing);
    addAndMakeVisible (*delayDiv);

    // Reverb
    revOn = std::make_unique<Power> (s, ids::revOn, col::reverb);
    addAndMakeVisible (*revOn);
    knob (revSize, ids::revSize, "Size", col::reverb);
    knob (revDecay, ids::revDecay, "Decay", col::reverb);
    knob (revDamp, ids::revDamp, "Damping", col::reverb);
    knob (revPre, ids::revPre, "Pre-Delay", col::reverb);
    knob (revWidth, ids::revWidth, "Width", col::reverb);
    knob (revMix, ids::revMix, "Mix", col::reverb);

    setSize (kWidth, kHeight);
    refreshPresetBox();
    timerCallback();
    startTimerHz (15);
}

MainView::~MainView() { setLookAndFeel (nullptr); }

void MainView::refreshPresetBox()
{
    shownProgram = proc.getCurrentProgram();
    presetBox.setSelectedId (shownProgram + 1, juce::dontSendNotification);
}

void MainView::timerCallback()
{
    if (proc.getCurrentProgram() != shownProgram) refreshPresetBox();

    auto on = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };

    const bool gSync = on (ids::sync);
    density->setEnabledLook (! gSync);
    grainDiv->setEnabled (gSync);

    const bool dSync = on (ids::delaySync);
    delayTime->setEnabledLook (! dSync);
    delayDiv->setEnabled (dSync);

    auto dim = [] (std::initializer_list<Knob*> ks, bool enabled)
    {
        for (auto* k : ks) k->setAlpha (enabled ? 1.0f : 0.45f);
    };
    dim ({ tapeDrive.get(), tapeWow.get(), tapeFlutter.get(), tapeAge.get(), tapeHiss.get() }, on (ids::tapeOn));
    dim ({ delayTime.get(), delayFb.get(), delayTone.get(), delayMix.get() }, on (ids::delayOn));
    for (juce::Component* c : { (juce::Component*) delaySync.get(), (juce::Component*) delayPing.get(), (juce::Component*) delayDiv.get() })
        c->setAlpha (on (ids::delayOn) ? 1.0f : 0.45f);
    dim ({ revSize.get(), revDecay.get(), revDamp.get(), revPre.get(), revWidth.get(), revMix.get() }, on (ids::revOn));
}

void MainView::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
{
    const auto rf = r.toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (rf, 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (rf.reduced (0.5f), 14.0f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle (rf.getX() + 16.0f, rf.getY() + 15.0f, 14.0f, 4.0f, 2.0f);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
    g.drawText (title, r.getX() + 38, r.getY() + 8, 200, 18, juce::Justification::centredLeft);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // Wordmark
    const auto head = juce::Rectangle<int> (24, 0, 400, 62);
    g.setColour (col::grain);
    for (int i = 0; i < 5; ++i)
    {
        const float r = 2.5f + (float) (i % 3) * 1.5f;
        g.fillEllipse (26.0f + (float) i * 7.0f - r, 31.0f + std::sin ((float) i * 1.7f) * 7.0f - r, r * 2, r * 2);
    }
    g.setColour (col::text);
    g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
    g.drawText ("GRANULA", head.withTrimmedLeft (44), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("granular texture machine", head.withTrimmedLeft (172).withTrimmedTop (6), juce::Justification::centredLeft);

    drawSection (g, grainArea, "GRAIN", col::grain);
    drawSection (g, tapeArea, "CASSETTE", col::tape);
    drawSection (g, delayArea, "DELAY", col::delay);
    drawSection (g, revArea, "REVERB", col::reverb);
}

void MainView::resized()
{
    // ---- Header
    presetBox.setBounds (kWidth / 2 - 130, 16, 260, 30);
    prevPreset.setBounds (presetBox.getX() - 36, 18, 28, 26);
    nextPreset.setBounds (presetBox.getRight() + 8, 18, 28, 26);
    outKnob->setBounds (kWidth - 96, 2, 72, 70);
    outKnob->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outKnob->slider.setPopupDisplayEnabled (true, true, this);

    // ---- Grain
    grainArea = { 16, 70, kWidth - 32, 334 };
    xyPad.setBounds (grainArea.getX() + 14, grainArea.getY() + 36, 284, 284);

    const int gx = xyPad.getRight() + 14;
    const int gw = grainArea.getRight() - 14 - gx;
    cloud.setBounds (gx, grainArea.getY() + 36, gw, 92);

    const int cellW = gw / 8, cellH = 88;
    int y1 = cloud.getBottom() + 8;
    Knob* row1[] = { size.get(), density.get(), position.get(), spray.get(), pitch.get(), pitchRand.get(), detune.get(), spread.get() };
    for (int i = 0; i < 8; ++i) row1[i]->setBounds (gx + i * cellW, y1, cellW, cellH);

    int y2 = y1 + cellH + 4;
    Knob* row2[] = { reverse.get(), shape.get(), feedback.get(), grainMix.get() };
    for (int i = 0; i < 4; ++i) row2[i]->setBounds (gx + i * cellW, y2, cellW, cellH);

    const int cx = gx + 4 * cellW + 10;
    const int ctrlW = (gw - 4 * cellW - 10) / 3 - 10;
    scale->setBounds (cx, y2 + 14, ctrlW, 44);
    grainSync->setBounds (cx + ctrlW + 10, y2 + 2, ctrlW, 26);
    grainDiv->setBounds (cx + ctrlW + 10, y2 + 32, ctrlW, 44);
    freeze->setBounds (cx + 2 * (ctrlW + 10), y2 + 18, ctrlW, 40);

    // ---- FX row
    const int fy = grainArea.getBottom() + 12;
    const int fh = kHeight - fy - 14;
    const int fw = (kWidth - 32 - 2 * 12) / 3;
    tapeArea = { 16, fy, fw, fh };
    delayArea = { tapeArea.getRight() + 12, fy, fw, fh };
    revArea = { delayArea.getRight() + 12, fy, fw, fh };

    auto power = [] (juce::Component& c, juce::Rectangle<int> area) { c.setBounds (area.getRight() - 40, area.getY() + 4, 28, 28); };
    power (*tapeOn, tapeArea);
    power (*delayOn, delayArea);
    power (*revOn, revArea);

    auto layoutRow = [] (juce::Rectangle<int> area, int y, std::initializer_list<juce::Component*> comps, int cell)
    {
        const int total = cell * (int) comps.size();
        int x = area.getX() + (area.getWidth() - total) / 2;
        for (auto* c : comps) { c->setBounds (x, y, cell, 90); x += cell; }
    };

    const int r1 = fy + 34, r2 = r1 + 94;
    layoutRow (tapeArea, r1, { tapeDrive.get(), tapeWow.get(), tapeFlutter.get() }, 92);
    layoutRow (tapeArea, r2, { tapeAge.get(), tapeHiss.get() }, 92);

    layoutRow (delayArea, r1, { delayTime.get(), delayFb.get(), delayTone.get(), delayMix.get() }, 80);
    {
        const int w = 92, gap = 10;
        int x = delayArea.getX() + (delayArea.getWidth() - (3 * w + 2 * gap)) / 2;
        delaySync->setBounds (x, r2 + 30, w, 28);
        delayDiv->setBounds (x + w + gap, r2 + 12, w, 44);
        delayPing->setBounds (x + 2 * (w + gap), r2 + 30, w, 28);
    }

    layoutRow (revArea, r1, { revSize.get(), revDecay.get(), revDamp.get() }, 92);
    layoutRow (revArea, r2, { revPre.get(), revWidth.get(), revMix.get() }, 92);
}

// ============================================================================

GranulaEditor::GranulaEditor (GranulaProcessor& p) : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (MainView::kWidth * 6 / 10, MainView::kHeight * 6 / 10, MainView::kWidth * 2, MainView::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainView::kWidth / (double) MainView::kHeight);
    setSize (MainView::kWidth * 9 / 10, MainView::kHeight * 9 / 10);
}

void GranulaEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::kWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
}
