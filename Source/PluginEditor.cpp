#include "PluginEditor.h"

using namespace granula;
namespace col = granula::ui::colours;

MainView::MainView (GranulaProcessor& p)
    : proc (p),
      xyPad (p.apvts, ids::size, ids::density, ids::sync)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    auto knob = [&] (std::unique_ptr<Knob>& k, const char* id, const char* name, juce::Colour c, bool bip = false)
    {
        k = std::make_unique<Knob> (s, id, name, c, bip);
        addAndMakeVisible (*k);
    };

    // Header: presets
    presetBox.onChange = [this] { presetChosen (presetBox.getSelectedId()); };
    presetBox.setColour (juce::ComboBox::backgroundColourId, col::panel);
    presetBox.setTextWhenNothingSelected ("Preset");
    addAndMakeVisible (presetBox);

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.onClick = [this] { showSaveDialog(); };
    savePreset.setTooltip ("Save the current settings as your own preset");
    for (auto* b : { &prevPreset, &nextPreset, &savePreset })
    {
        b->setColour (juce::TextButton::buttonOnColourId, col::grain);
        addAndMakeVisible (*b);
    }
    knob (outKnob, ids::outGain, "Output", col::text, true);

    // Grain: source switch
    for (auto* b : { &modeSample, &modeInput })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, col::grain);
        addAndMakeVisible (*b);
    }
    modeSample.setTooltip ("Play a loaded sample with MIDI notes");
    modeInput.setTooltip ("Granulate live audio from another track (send it to this plugin's Sidechain input)");
    auto setMode = [this] (int m)
    {
        if (auto* mp = proc.apvts.getParameter (ids::mode))
        {
            mp->beginChangeGesture();
            mp->setValueNotifyingHost (mp->convertTo0to1 ((float) m));
            mp->endChangeGesture();
        }
        applyMode();
    };
    modeSample.onClick = [setMode] { setMode (0); };
    modeInput.onClick = [setMode] { setMode (1); };

    addAndMakeVisible (xyPad);

    waveform = std::make_unique<granula::ui::WaveformView> (proc);
    waveform->onBrowse = [this] { browseForSample(); };
    addChildComponent (*waveform);
    loadButton = std::make_unique<juce::TextButton> ("LOAD SAMPLE");
    loadButton->setColour (juce::TextButton::buttonOnColourId, col::grain);
    loadButton->onClick = [this] { browseForSample(); };
    addChildComponent (*loadButton);
    preview = std::make_unique<granula::ui::PreviewButton> (proc);
    addChildComponent (*preview);

    cloud = std::make_unique<granula::ui::GrainCloud> (proc.getGranular());
    addChildComponent (*cloud);
    inputHint.setText ("Audio in: plugin Sidechain", juce::dontSendNotification);
    inputHint.setJustificationType (juce::Justification::centredRight);
    inputHint.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    inputHint.setColour (juce::Label::textColourId, col::textDim);
    addChildComponent (inputHint);

    knob (size, ids::size, "Size", col::grain);
    knob (density, ids::density, "Density", col::grain);
    knob (positionS, ids::samplePos, "Position", col::grain);
    knob (sprayS, ids::sampleSpray, "Spray", col::grain);
    knob (positionI, ids::position, "Position", col::grain);
    knob (sprayI, ids::spray, "Spray", col::grain);
    knob (pitch, ids::pitch, "Pitch", col::grain, true);
    knob (pitchRand, ids::pitchRand, "Pitch Rnd", col::grain);
    knob (detune, ids::detune, "Detune", col::grain);
    knob (spread, ids::spread, "Spread", col::grain);
    knob (reverse, ids::reverse, "Reverse", col::grain);
    knob (shape, ids::shape, "Shape", col::grain);
    knob (scan, ids::scan, "Scan", col::grain, true);
    knob (attack, ids::attack, "Attack", col::grain);
    knob (release, ids::release, "Release", col::grain);
    knob (feedback, ids::feedback, "Feedback", col::grain);
    knob (grainMix, ids::grainMix, "Mix", col::grain);

    scale = std::make_unique<Choice> (s, ids::scale, "Scale");
    grainDiv = std::make_unique<Choice> (s, ids::division, "Rate");
    grainSync = std::make_unique<Toggle> (s, ids::sync, "SYNC", col::grain);
    freeze = std::make_unique<Toggle> (s, ids::freeze, "FREEZE", col::grain);
    freeze->setTooltip ("Stop recording new audio and keep granulating what is in the buffer");
    addAndMakeVisible (*scale);
    addAndMakeVisible (*grainDiv);
    addAndMakeVisible (*grainSync);
    addChildComponent (*freeze);

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
    applyMode();
    timerCallback();
    startTimerHz (15);
}

MainView::~MainView() { setLookAndFeel (nullptr); }

// ---------------------------------------------------------------- presets UI

namespace
{
constexpr int kUserBase = 1000, kActionSave = 9001, kActionFolder = 9002, kActionDelete = 9003;
}

void MainView::rebuildPresetMenu()
{
    userFiles = proc.getUserPresets();
    presetBox.clear (juce::dontSendNotification);

    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);

    if (! userFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < userFiles.size(); ++i)
            presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), kUserBase + i);
    }

    presetBox.addSeparator();
    presetBox.addItem ("Save current settings...", kActionSave);
    presetBox.addItem ("Open presets folder", kActionFolder);
    if (proc.getUserPresetName().isNotEmpty())
        presetBox.addItem ("Delete \"" + proc.getUserPresetName() + "\"", kActionDelete);
}

void MainView::refreshPresetBox()
{
    shownProgram = proc.getCurrentProgram();
    shownChange = proc.getPresetChangeCount();
    rebuildPresetMenu();

    const auto user = proc.getUserPresetName();
    if (user.isEmpty())
    {
        presetBox.setSelectedId (shownProgram + 1, juce::dontSendNotification);
        return;
    }
    for (int i = 0; i < userFiles.size(); ++i)
        if (userFiles[i].getFileNameWithoutExtension() == user)
        {
            presetBox.setSelectedId (kUserBase + i, juce::dontSendNotification);
            return;
        }
    presetBox.setText (user, juce::dontSendNotification);
}

void MainView::presetChosen (int id)
{
    if (id <= 0) return;
    if (id <= proc.getNumPrograms()) proc.setCurrentProgram (id - 1);
    else if (id >= kUserBase && id < kUserBase + userFiles.size()) proc.loadUserPreset (userFiles[id - kUserBase]);
    else if (id == kActionSave) showSaveDialog();
    else if (id == kActionFolder)
    {
        auto folder = proc.getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    }
    else if (id == kActionDelete)
    {
        for (auto& f : userFiles)
            if (f.getFileNameWithoutExtension() == proc.getUserPresetName()) { proc.deleteUserPreset (f); break; }
    }
    refreshPresetBox();
}

void MainView::stepPreset (int dir)
{
    // Factory presets first, then the user's own, as one continuous list.
    const int nf = proc.getNumPrograms(), total = nf + userFiles.size();
    int cur = proc.getCurrentProgram();
    const auto user = proc.getUserPresetName();
    if (user.isNotEmpty())
        for (int i = 0; i < userFiles.size(); ++i)
            if (userFiles[i].getFileNameWithoutExtension() == user) cur = nf + i;

    const int next = (cur + dir + total) % total;
    if (next < nf) proc.setCurrentProgram (next);
    else proc.loadUserPreset (userFiles[next - nf]);
    refreshPresetBox();
}

void MainView::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->setColour (juce::AlertWindow::backgroundColourId, col::panel);
    w->setColour (juce::AlertWindow::textColourId, col::text);
    const auto suggested = proc.getUserPresetName().isNotEmpty() ? proc.getUserPresetName() : juce::String ("My preset");
    w->addTextEditor ("name", suggested);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    for (int i = 0; i < w->getNumButtons(); ++i)
        if (auto* b = w->getButton (i)) b->setColour (juce::TextButton::buttonOnColourId, col::grain);

    juce::Component::SafePointer<MainView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) return;
        if (! safe->proc.saveUserPreset (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "artempidor",
                                                    "Couldn't save the preset to\n" + safe->proc.getUserPresetFolder().getFullPathName());
        safe->refreshPresetBox();
    }), true);
}

void MainView::browseForSample()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), GranulaProcessor::supportedFileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadSampleAsync (f);
                              if (inputMode) modeSample.triggerClick();
                          });
}

static bool isPresetFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (GranulaProcessor::presetExtension);
}

bool MainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isPresetFile (f) || proc.isSupportedFile (juce::File (f))) return true;
    return false;
}

void MainView::fileDragEnter (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty() && ! isPresetFile (files[0]))
    {
        if (inputMode) { waveform->setVisible (true); cloud->setVisible (false); }
        waveform->setDragHighlight (true);
    }
}
void MainView::fileDragExit (const juce::StringArray&)
{
    waveform->setDragHighlight (false);
    applyMode();
}

void MainView::filesDropped (const juce::StringArray& files, int, int)
{
    if (waveform) waveform->setDragHighlight (false);
    for (auto& path : files)
    {
        const juce::File f (path);
        if (isPresetFile (path))
        {
            // Shared presets are copied into the user's folder so they stay in the list.
            auto dest = proc.getUserPresetFolder().getChildFile (f.getFileName());
            if (f.getParentDirectory() != proc.getUserPresetFolder() && proc.getUserPresetFolder().createDirectory())
                f.copyFileTo (dest);
            proc.loadUserPreset (dest.existsAsFile() ? dest : f);
            refreshPresetBox();
            return;
        }
        if (proc.isSupportedFile (f))
        {
            proc.loadSampleAsync (f);
            if (inputMode) modeSample.triggerClick(); // dropping a sample implies SAMPLE mode
            return;
        }
    }
}

void MainView::timerCallback()
{
    if (proc.getCurrentProgram() != shownProgram || proc.getPresetChangeCount() != shownChange) refreshPresetBox();
    if ((proc.apvts.getRawParameterValue (ids::mode)->load() > 0.5f) != inputMode) applyMode();

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

void MainView::applyMode()
{
    inputMode = proc.apvts.getRawParameterValue (ids::mode)->load() > 0.5f;
    modeSample.setToggleState (! inputMode, juce::dontSendNotification);
    modeInput.setToggleState (inputMode, juce::dontSendNotification);

    for (juce::Component* c : { (juce::Component*) waveform.get(), (juce::Component*) loadButton.get(), (juce::Component*) preview.get(),
                                (juce::Component*) positionS.get(), (juce::Component*) sprayS.get(), (juce::Component*) scan.get(),
                                (juce::Component*) attack.get(), (juce::Component*) release.get() })
        c->setVisible (! inputMode);
    for (juce::Component* c : { (juce::Component*) cloud.get(), (juce::Component*) &inputHint, (juce::Component*) positionI.get(),
                                (juce::Component*) sprayI.get(), (juce::Component*) feedback.get(), (juce::Component*) grainMix.get(),
                                (juce::Component*) freeze.get() })
        c->setVisible (inputMode);
    layoutGrain();
    repaint();
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
    g.drawText ("ARTEMPIDOR", head.withTrimmedLeft (44), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("granular", head.withTrimmedLeft (214).withTrimmedTop (6), juce::Justification::centredLeft);

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
    savePreset.setBounds (nextPreset.getRight() + 10, 18, 64, 26);
    outKnob->setBounds (kWidth - 96, 2, 72, 70);
    outKnob->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outKnob->slider.setPopupDisplayEnabled (true, true, this);

    // ---- Grain
    grainArea = { 16, 70, kWidth - 32, 334 };
    xyPad.setBounds (grainArea.getX() + 14, grainArea.getY() + 36, 284, 284);

    layoutGrain();

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

void MainView::layoutGrain()
{
    if (grainArea.isEmpty()) return;
    const int gx = xyPad.getRight() + 14;
    const int gw = grainArea.getRight() - 14 - gx;
    const juce::Rectangle<int> display (gx, grainArea.getY() + 36, gw, 92);
    waveform->setBounds (display);
    cloud->setBounds (display);
    loadButton->setBounds (grainArea.getRight() - 14 - 120, grainArea.getY() + 6, 120, 24);
    preview->setBounds (loadButton->getX() - 10 - 120, grainArea.getY() + 6, 120, 24);
    inputHint.setBounds (grainArea.getRight() - 14 - 260, grainArea.getY() + 6, 260, 24);
    modeSample.setBounds (grainArea.getX() + 100, grainArea.getY() + 6, 84, 24);
    modeInput.setBounds (modeSample.getRight() + 6, grainArea.getY() + 6, 84, 24);

    const int cellW = gw / 8, cellH = 88;
    const int y1 = display.getBottom() + 8;
    const int y2 = y1 + cellH + 4;

    std::vector<Knob*> row1, row2;
    if (! inputMode)
    {
        row1 = { size.get(), density.get(), positionS.get(), sprayS.get(), scan.get(), pitch.get(), pitchRand.get(), detune.get() };
        row2 = { spread.get(), reverse.get(), shape.get(), attack.get(), release.get() };
    }
    else
    {
        row1 = { size.get(), density.get(), positionI.get(), sprayI.get(), pitch.get(), pitchRand.get(), detune.get(), spread.get() };
        row2 = { reverse.get(), shape.get(), feedback.get(), grainMix.get() };
    }
    for (size_t i = 0; i < row1.size(); ++i) row1[i]->setBounds (gx + (int) i * cellW, y1, cellW, cellH);
    for (size_t i = 0; i < row2.size(); ++i) row2[i]->setBounds (gx + (int) i * cellW, y2, cellW, cellH);

    const int cx = gx + (int) row2.size() * cellW + 10;
    const int nCtrl = inputMode ? 3 : 2;
    const int ctrlW = (gw - (int) row2.size() * cellW - 10) / nCtrl - 10;
    scale->setBounds (cx, y2 + 14, ctrlW, 44);
    grainSync->setBounds (cx + ctrlW + 10, y2 + 2, ctrlW, 26);
    grainDiv->setBounds (cx + ctrlW + 10, y2 + 32, ctrlW, 44);
    freeze->setBounds (cx + 2 * (ctrlW + 10), y2 + 18, ctrlW, 40);
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
