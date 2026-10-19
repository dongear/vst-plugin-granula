#include "InterferenceEditor.h"

using namespace interference;
namespace col = granula::ui::colours;

namespace
{
constexpr int kUserBase = 1000, kActionSave = 9001, kActionFolder = 9002, kActionDelete = 9003;
}

InterferenceView::InterferenceView (InterferenceProcessor& p) : proc (p), spectrum (p), keyboard (p), tuneMeter (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    // ---- header
    presetBox.onChange = [this] { presetChosen (presetBox.getSelectedId()); };
    presetBox.setColour (juce::ComboBox::backgroundColourId, col::panel);
    presetBox.setTextWhenNothingSelected ("Preset");
    addAndMakeVisible (presetBox);
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.onClick = [this] { showSaveDialog(); };
    savePreset.setTooltip ("Save the current settings as your own preset");
    for (auto* b : { &prevPreset, &nextPreset, &savePreset, &zeroLat, &hq })
    {
        b->setColour (juce::TextButton::buttonOnColourId, col::text);
        addAndMakeVisible (*b);
    }
    zeroLat.setTooltip ("Resonant filter bank: no latency, great for live use and bass");
    hq.setTooltip ("Spectral engine: most precise, enables SNAP (adds ~85 ms latency)");
    auto setQuality = [this] (int q)
    {
        if (auto* prm = proc.apvts.getParameter (ids::quality))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) q));
            prm->endChangeGesture();
        }
    };
    zeroLat.onClick = [setQuality] { setQuality (0); };
    hq.onClick = [setQuality] { setQuality (1); };

    // ---- spectrum + key
    addAndMakeVisible (spectrum);
    addAndMakeVisible (keyboard);
    rootBox.addItemList (noteNames(), 1);
    for (size_t i = 0; i < scales().size(); ++i) scaleBox.addItem (scales()[i].name, (int) i + 1);
    auto applyKey = [this]
    {
        proc.setScaleFromUI (rootBox.getSelectedId() - 1, scaleBox.getSelectedId() - 1);
    };
    rootBox.onChange = applyKey;
    scaleBox.onChange = applyKey;
    addAndMakeVisible (rootBox);
    addAndMakeVisible (scaleBox);
    midiToggle = std::make_unique<granula::ui::Toggle> (s, ids::midi, "MIDI KEY", col::text);
    midiToggle->setTooltip ("Play a chord on the MIDI input and the key follows it");
    addAndMakeVisible (*midiToggle);
    keyLabel.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
    keyLabel.setColour (juce::Label::textColourId, col::text);
    keyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (keyLabel);

    // ---- knobs
    auto knob = [&] (std::unique_ptr<Knob>& k, const char* id, const char* name, bool bip = false)
    {
        k = std::make_unique<Knob> (s, id, name, col::text, bip);
        addAndMakeVisible (*k);
    };
    knob (color, ids::color, "Color");
    knob (snap, ids::snap, "Snap");
    knob (focus, ids::focus, "Focus");
    knob (gate, ids::gate, "Gate");
    knob (trans, ids::trans, "Transients");
    knob (low, ids::low, "Low");
    knob (high, ids::high, "High");
    knob (mix, ids::mix, "Mix");
    knob (out, ids::out, "Output", true);
    color->label.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
    color->slider.setTooltip ("Amount. Above 100% in-key notes ring out into a coloured tail");
    snap->slider.setTooltip ("HQ only: moves out-of-key energy onto the nearest in-key note");
    focus->slider.setTooltip ("How narrowly the key is matched");
    gate->slider.setTooltip ("Spectral gate: removes the quiet in-between material for a tighter sound");
    trans->slider.setTooltip ("Lets attacks through unprocessed");

    // ---- tune
    tuneOn = std::make_unique<granula::ui::PowerButton> (s, ids::tuneOn, col::text);
    addAndMakeVisible (*tuneOn);
    knob (tSpeed, ids::tuneSpeed, "Speed");
    knob (tHumanize, ids::tuneHumanize, "Humanize");
    knob (tAmount, ids::tuneAmount, "Amount");
    knob (tFormant, ids::tuneFormant, "Formant", true);
    tSpeed->slider.setTooltip ("How fast notes snap to the key. 0 = hard robotic tune");
    tHumanize->slider.setTooltip ("Gentler correction on long notes, keeps natural vibrato");
    tFormant->slider.setTooltip ("Voice character: lower = bigger, higher = smaller");
    addAndMakeVisible (tuneMeter);

    modeInfo.setFont (juce::FontOptions (12.0f));
    modeInfo.setColour (juce::Label::textColourId, col::textDim);
    modeInfo.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (modeInfo);

    setSize (kWidth, kHeight);
    refreshPresets();
    timerCallback();
    startTimerHz (12);
}

InterferenceView::~InterferenceView() { setLookAndFeel (nullptr); }

void InterferenceView::timerCallback()
{
    if (proc.getCurrentProgram() != shownProgram || proc.getPresetChangeCount() != shownChange) refreshPresets();

    const bool isHq = proc.apvts.getRawParameterValue (ids::quality)->load() > 0.5f;
    zeroLat.setToggleState (! isHq, juce::dontSendNotification);
    hq.setToggleState (isHq, juce::dontSendNotification);
    snap->setEnabledLook (isHq);
    const bool tOn = proc.apvts.getRawParameterValue (ids::tuneOn)->load() > 0.5f;
    for (auto* k : { tSpeed.get(), tHumanize.get(), tAmount.get(), tFormant.get() }) k->setAlpha (tOn ? 1.0f : 0.45f);

    const int root = (int) proc.apvts.getRawParameterValue (ids::root)->load();
    const int sc = (int) proc.apvts.getRawParameterValue (ids::scale)->load();
    rootBox.setSelectedId (root + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (sc + 1, juce::dontSendNotification);
    keyLabel.setText (proc.describeKey(), juce::dontSendNotification);

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const int latMs = juce::roundToInt (proc.getLatencySamples() * 1000.0 / sr);
    modeInfo.setText ((isHq ? juce::String ("HQ  -  spectral engine\nSNAP active") : juce::String ("ZERO LATENCY  -  filter bank\nSNAP needs HQ"))
                          + "\nlatency " + juce::String (latMs) + " ms" + (latMs > 0 ? " (compensated by your DAW)" : ""),
                      juce::dontSendNotification);
}

// ---------------------------------------------------------------- presets

void InterferenceView::refreshPresets()
{
    shownProgram = proc.getCurrentProgram();
    shownChange = proc.getPresetChangeCount();
    userFiles = proc.getUserPresets();
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    if (! userFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), kUserBase + i);
    }
    presetBox.addSeparator();
    presetBox.addItem ("Save current settings...", kActionSave);
    presetBox.addItem ("Open presets folder", kActionFolder);
    if (proc.getUserPresetName().isNotEmpty()) presetBox.addItem ("Delete \"" + proc.getUserPresetName() + "\"", kActionDelete);

    const auto user = proc.getUserPresetName();
    if (user.isEmpty()) { presetBox.setSelectedId (shownProgram + 1, juce::dontSendNotification); return; }
    for (int i = 0; i < userFiles.size(); ++i)
        if (userFiles[i].getFileNameWithoutExtension() == user) { presetBox.setSelectedId (kUserBase + i, juce::dontSendNotification); return; }
    presetBox.setText (user, juce::dontSendNotification);
}

void InterferenceView::presetChosen (int id)
{
    if (id <= 0) return;
    if (id <= proc.getNumPrograms()) proc.setCurrentProgram (id - 1);
    else if (id >= kUserBase && id < kUserBase + userFiles.size()) proc.loadUserPreset (userFiles[id - kUserBase]);
    else if (id == kActionSave) showSaveDialog();
    else if (id == kActionFolder) { auto f = proc.getUserPresetFolder(); f.createDirectory(); f.startAsProcess(); }
    else if (id == kActionDelete)
        for (auto& f : userFiles)
            if (f.getFileNameWithoutExtension() == proc.getUserPresetName()) { proc.deleteUserPreset (f); break; }
    refreshPresets();
}

void InterferenceView::stepPreset (int dir)
{
    const int nf = proc.getNumPrograms(), total = nf + userFiles.size();
    int cur = proc.getCurrentProgram();
    for (int i = 0; i < userFiles.size(); ++i)
        if (proc.getUserPresetName().isNotEmpty() && userFiles[i].getFileNameWithoutExtension() == proc.getUserPresetName()) cur = nf + i;
    const int next = (cur + dir + total) % total;
    if (next < nf) proc.setCurrentProgram (next); else proc.loadUserPreset (userFiles[next - nf]);
    refreshPresets();
}

void InterferenceView::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->setColour (juce::AlertWindow::backgroundColourId, col::panel);
    w->setColour (juce::AlertWindow::textColourId, col::text);
    w->addTextEditor ("name", proc.getUserPresetName().isNotEmpty() ? proc.getUserPresetName() : juce::String ("My preset"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<InterferenceView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isNotEmpty() && ! safe->proc.saveUserPreset (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Interference",
                                                    "Couldn't save the preset to\n" + safe->proc.getUserPresetFolder().getFullPathName());
        safe->refreshPresets();
    }), true);
}

// ---------------------------------------------------------------- drawing

void InterferenceView::panel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (col::text);
        g.fillRoundedRectangle ((float) r.getX() + 16.0f, (float) r.getY() + 15.0f, 14.0f, 4.0f, 2.0f);
        g.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
        g.drawText (title, r.getX() + 38, r.getY() + 8, 300, 18, juce::Justification::centredLeft);
    }
}

void InterferenceView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // Logo: two waves interfering
    {
        juce::Path w1, w2;
        for (int i = 0; i <= 40; ++i)
        {
            const float x = 24.0f + (float) i;
            const float y1 = 32.0f + 9.0f * std::sin ((float) i * 0.32f);
            const float y2 = 32.0f + 9.0f * std::sin ((float) i * 0.32f + 2.2f);
            if (i == 0) { w1.startNewSubPath (x, y1); w2.startNewSubPath (x, y2); }
            else { w1.lineTo (x, y1); w2.lineTo (x, y2); }
        }
        g.setColour (col::text);
        g.strokePath (w1, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::text.withAlpha (0.45f));
        g.strokePath (w2, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (col::text);
    g.setFont (juce::FontOptions (25.0f).withStyle ("Bold"));
    g.drawText ("INTERFERENCE", 74, 0, 260, 64, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("by artempidor", 74, 38, 200, 18, juce::Justification::centredLeft);

    panel (g, spectrumArea, {});
    panel (g, keyArea, "KEY");
    panel (g, tuneArea, "TUNE");
    panel (g, controlArea, "SOUND");
}

void InterferenceView::resized()
{
    // header
    presetBox.setBounds (kWidth / 2 - 150, 17, 240, 30);
    prevPreset.setBounds (presetBox.getX() - 36, 19, 28, 26);
    nextPreset.setBounds (presetBox.getRight() + 8, 19, 28, 26);
    savePreset.setBounds (nextPreset.getRight() + 8, 19, 60, 26);
    hq.setBounds (kWidth - 150, 19, 50, 26);
    zeroLat.setBounds (hq.getX() - 116, 19, 110, 26);
    out->setBounds (kWidth - 92, 4, 72, 64);
    out->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    out->slider.setPopupDisplayEnabled (true, true, this);
    out->label.setText ("OUT", juce::dontSendNotification);

    spectrumArea = { 16, 72, kWidth - 32, 214 };
    spectrum.setBounds (spectrumArea.reduced (8));

    keyArea = { 16, spectrumArea.getBottom() + 12, kWidth - 32, 104 };
    keyboard.setBounds (keyArea.getX() + 16, keyArea.getY() + 32, 330, 62);
    const int kx = keyboard.getRight() + 24;
    rootBox.setBounds (kx, keyArea.getY() + 40, 80, 28);
    scaleBox.setBounds (rootBox.getRight() + 8, keyArea.getY() + 40, 170, 28);
    midiToggle->setBounds (scaleBox.getRight() + 12, keyArea.getY() + 40, 100, 28);
    keyLabel.setBounds (midiToggle->getRight() + 20, keyArea.getY() + 34, keyArea.getRight() - midiToggle->getRight() - 36, 40);

    tuneArea = { 16, keyArea.getBottom() + 12, kWidth - 32, 124 };
    tuneOn->setBounds (tuneArea.getX() + 84, tuneArea.getY() + 4, 28, 28);
    {
        const int cell = 96, y = tuneArea.getY() + 26;
        Knob* tk[] = { tSpeed.get(), tHumanize.get(), tAmount.get(), tFormant.get() };
        for (int i = 0; i < 4; ++i) tk[i]->setBounds (tuneArea.getX() + 24 + i * cell, y, cell, 92);
        tuneMeter.setBounds (tuneArea.getX() + 24 + 4 * cell + 24, tuneArea.getY() + 20, tuneArea.getRight() - (tuneArea.getX() + 24 + 4 * cell + 24) - 16, 92);
    }

    controlArea = { 16, tuneArea.getBottom() + 12, kWidth - 32, kHeight - tuneArea.getBottom() - 26 };
    const int cy = controlArea.getY() + 30;
    color->setBounds (controlArea.getX() + 24, cy, 170, controlArea.getHeight() - 40);
    color->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);

    const int gx = color->getRight() + 20, cell = 96, rowH = 88;
    Knob* row1[] = { snap.get(), focus.get(), gate.get(), trans.get() };
    Knob* row2[] = { low.get(), high.get(), mix.get() };
    for (int i = 0; i < 4; ++i) row1[i]->setBounds (gx + i * cell, cy - 4, cell, rowH);
    for (int i = 0; i < 3; ++i) row2[i]->setBounds (gx + i * cell, cy - 4 + rowH, cell, rowH);
    modeInfo.setBounds (gx + 4 * cell + 30, cy + 10, controlArea.getRight() - (gx + 4 * cell + 30) - 20, 120);
}

// ============================================================================

InterferenceEditor::InterferenceEditor (InterferenceProcessor& p) : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (InterferenceView::kWidth * 6 / 10, InterferenceView::kHeight * 6 / 10,
                     InterferenceView::kWidth * 2, InterferenceView::kHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) InterferenceView::kWidth / InterferenceView::kHeight);
    setSize (InterferenceView::kWidth, InterferenceView::kHeight);
}

void InterferenceEditor::resized()
{
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) InterferenceView::kWidth));
}
