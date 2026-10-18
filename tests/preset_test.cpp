// Round-trip test for user presets: save -> change -> load -> compare -> delete.
#include "PluginProcessor.h"
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    GranulaProcessor proc;
    auto* p = proc.apvts.getParameter (granula::ids::size);
    p->setValueNotifyingHost (p->convertTo0to1 (333.0f));
    if (! proc.saveUserPreset ("Test Preset")) { std::puts ("save FAILED"); return 1; }
    const auto file = proc.getUserPresetFolder().getChildFile ("Test Preset.artempidor");
    std::printf ("saved: %s (%d presets)\n", file.getFullPathName().toRawUTF8(), proc.getUserPresets().size());
    p->setValueNotifyingHost (p->convertTo0to1 (50.0f));
    proc.setCurrentProgram (2);
    if (! proc.loadUserPreset (file)) { std::puts ("load FAILED"); return 1; }
    const float v = p->convertFrom0to1 (p->getValue());
    std::printf ("size after load: %.1f ms, current='%s'\n", v, proc.getUserPresetName().toRawUTF8());
    const bool ok = std::abs (v - 333.0f) < 1.0f && proc.getUserPresetName() == "Test Preset";
    proc.deleteUserPreset (file);
    std::printf ("deleted: %s\n%s\n", file.existsAsFile() ? "no" : "yes", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
