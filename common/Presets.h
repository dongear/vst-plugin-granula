#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/ui/LookAndFeel.h"
#include <atomic>
#include <string>
#include <vector>

// Shared preset handling for the newer artempidor plugins:
// factory presets (plain values over defaults) + user presets saved as XML files.
namespace common
{
struct FactoryPreset
{
    std::string name;
    std::vector<std::pair<std::string, float>> values;
};

class PresetManager
{
public:
    PresetManager (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, juce::String folderName,
                   juce::String extension, std::vector<FactoryPreset> factoryList)
        : proc (p), apvts (s), folder (std::move (folderName)), ext (std::move (extension)), factory (std::move (factoryList)) {}

    int numFactory() const { return (int) factory.size(); }
    juce::String factoryName (int i) const
    {
        return juce::isPositiveAndBelow (i, numFactory()) ? juce::String (factory[(size_t) i].name) : juce::String();
    }
    int currentFactory() const { return current; }
    juce::String currentUser() const { return userName; }
    int changeCount() const { return changes.load(); }

    void loadFactory (int i)
    {
        if (! juce::isPositiveAndBelow (i, numFactory())) return;
        resetToDefaults();
        for (const auto& [id, v] : factory[(size_t) i].values) setPlain (id, v);
        current = i;
        userName.clear();
        ++changes;
    }

    juce::File userFolder() const
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("artempidor").getChildFile (folder);
    }

    juce::Array<juce::File> userPresets() const
    {
        auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*" + ext);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        return files;
    }

    bool save (const juce::String& raw)
    {
        const auto name = juce::File::createLegalFileName (raw.trim());
        if (name.isEmpty()) return false;
        juce::XmlElement xml ("ArtempidorPreset");
        xml.setAttribute ("plugin", proc.getName());
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
            }
        auto dir = userFolder();
        if (! dir.createDirectory() || ! xml.writeTo (dir.getChildFile (name + ext))) return false;
        userName = name;
        ++changes;
        return true;
    }

    bool load (const juce::File& f)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("ArtempidorPreset")) return false;
        resetToDefaults();
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            setPlain (e->getStringAttribute ("id").toStdString(), (float) e->getDoubleAttribute ("value"));
        userName = f.getFileNameWithoutExtension();
        ++changes;
        return true;
    }

    bool remove (const juce::File& f)
    {
        if (f.getParentDirectory() != userFolder()) return false;
        const bool ok = f.moveToTrash() || f.deleteFile();
        if (ok && userName == f.getFileNameWithoutExtension()) userName.clear();
        ++changes;
        return ok;
    }

    void writeState (juce::ValueTree& st) const
    {
        st.setProperty ("program", current, nullptr);
        st.setProperty ("userPreset", userName, nullptr);
    }
    void readState (const juce::ValueTree& st)
    {
        current = st.getProperty ("program", 0);
        userName = st.getProperty ("userPreset", {}).toString();
        ++changes;
    }

private:
    void resetToDefaults()
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    }
    void setPlain (const std::string& id, float v)
    {
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    }

    juce::AudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String folder, ext;
    std::vector<FactoryPreset> factory;
    int current = 0;
    juce::String userName;
    std::atomic<int> changes { 0 };
};

// Header strip: < [preset box] > SAVE  — factory + user presets, save/delete/open folder.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    PresetBar (PresetManager& m, juce::LookAndFeel& lnf) : pm (m), laf (lnf)
    {
        box.onChange = [this] { chosen (box.getSelectedId()); };
        box.setColour (juce::ComboBox::backgroundColourId, granula::ui::colours::panel);
        box.setTextWhenNothingSelected ("Preset");
        addAndMakeVisible (box);
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        save.onClick = [this] { showSave(); };
        save.setTooltip ("Save the current sound as your own preset");
        for (auto* b : { &prev, &next, &save }) addAndMakeVisible (*b);
        refresh();
        startTimerHz (8);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        prev.setBounds (r.removeFromLeft (28).withSizeKeepingCentre (28, 26));
        save.setBounds (r.removeFromRight (60).withSizeKeepingCentre (60, 26));
        r.removeFromRight (8);
        next.setBounds (r.removeFromRight (28).withSizeKeepingCentre (28, 26));
        r.reduce (8, 0);
        box.setBounds (r.withSizeKeepingCentre (r.getWidth(), 30));
    }

private:
    static constexpr int kUser = 1000, kSave = 9001, kFolder = 9002, kDelete = 9003;

    void timerCallback() override { if (pm.changeCount() != shown) refresh(); }

    void refresh()
    {
        shown = pm.changeCount();
        files = pm.userPresets();
        box.clear (juce::dontSendNotification);
        box.addSectionHeading ("Factory");
        for (int i = 0; i < pm.numFactory(); ++i) box.addItem (pm.factoryName (i), i + 1);
        if (! files.isEmpty())
        {
            box.addSeparator();
            box.addSectionHeading ("My presets");
            for (int i = 0; i < files.size(); ++i) box.addItem (files[i].getFileNameWithoutExtension(), kUser + i);
        }
        box.addSeparator();
        box.addItem ("Save current settings...", kSave);
        box.addItem ("Open presets folder", kFolder);
        if (pm.currentUser().isNotEmpty()) box.addItem ("Delete \"" + pm.currentUser() + "\"", kDelete);

        if (pm.currentUser().isEmpty()) { box.setSelectedId (pm.currentFactory() + 1, juce::dontSendNotification); return; }
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == pm.currentUser()) { box.setSelectedId (kUser + i, juce::dontSendNotification); return; }
        box.setText (pm.currentUser(), juce::dontSendNotification);
    }

    void chosen (int id)
    {
        if (id <= 0) return;
        if (id <= pm.numFactory()) pm.loadFactory (id - 1);
        else if (id >= kUser && id < kUser + files.size()) pm.load (files[id - kUser]);
        else if (id == kSave) showSave();
        else if (id == kFolder) { auto f = pm.userFolder(); f.createDirectory(); f.startAsProcess(); }
        else if (id == kDelete)
            for (auto& f : files)
                if (f.getFileNameWithoutExtension() == pm.currentUser()) { pm.remove (f); break; }
        refresh();
    }

    void step (int dir)
    {
        const int nf = pm.numFactory(), total = nf + files.size();
        if (total <= 0) return;
        int cur = pm.currentFactory();
        for (int i = 0; i < files.size(); ++i)
            if (pm.currentUser().isNotEmpty() && files[i].getFileNameWithoutExtension() == pm.currentUser()) cur = nf + i;
        const int nx = (cur + dir + total) % total;
        if (nx < nf) pm.loadFactory (nx); else pm.load (files[nx - nf]);
        refresh();
    }

    void showSave()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&laf);
        w->setColour (juce::AlertWindow::backgroundColourId, granula::ui::colours::panel);
        w->setColour (juce::AlertWindow::textColourId, granula::ui::colours::text);
        w->addTextEditor ("name", pm.currentUser().isNotEmpty() ? pm.currentUser() : juce::String ("My preset"));
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBar> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (safe == nullptr || r != 1) return;
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && ! safe->pm.save (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset",
                                                        "Couldn't save to\n" + safe->pm.userFolder().getFullPathName());
            safe->refresh();
        }), true);
    }

    PresetManager& pm;
    juce::LookAndFeel& laf;
    juce::ComboBox box;
    juce::TextButton prev { "<" }, next { ">" }, save { "SAVE" };
    juce::Array<juce::File> files;
    int shown = -1;
};

// Rounded panel with a small title bar, shared look across the suite.
inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    namespace col = granula::ui::colours;
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
} // namespace common
