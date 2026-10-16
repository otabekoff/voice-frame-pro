#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <map>
#include <vector>

namespace vfp
{
    //==============================================================================
    struct PresetInfo
    {
        juce::String name;
        bool factory = true;
        float amount = 50.0f; // shown next to the name in the preset list
        juce::File file;      // user presets only
    };

    /**
     * Factory / user presets plus the A/B comparison slots.
     *
     * Presets store parameter values only. Monitoring aids (bypass, monitor mode, band solo
     * and band listen) are never stored or recalled. All calls are message-thread only.
     */
    class PresetManager
    {
    public:
        explicit PresetManager(juce::AudioProcessorValueTreeState &state);

        //==============================================================================
        const std::vector<PresetInfo> &getPresets() const noexcept { return presets; }
        int getNumFactoryPresets() const noexcept { return numFactory; }
        void refreshUserPresets();

        int getCurrentIndex() const noexcept { return currentIndex; }
        const PresetInfo &getCurrentPreset() const { return presets[(size_t)juce::jlimit(0, (int)presets.size() - 1, currentIndex)]; }

        void loadPreset(int index);
        void loadNext();
        void loadPrevious();
        void resetToPreset();
        bool saveUserPreset(const juce::String &name);
        bool isModified() const;

        static juce::File getUserPresetDirectory();
        static bool isPresetParameter(const juce::String &paramID);

        //==============================================================================
        int getActiveSlot() const noexcept { return activeSlot; }
        void selectSlot(int slot);
        void copyActiveToOther();

        //==============================================================================
        juce::ValueTree toValueTree() const;
        void fromValueTree(const juce::ValueTree &tree);

    private:
        using ValueMap = std::map<juce::String, float>; // param ID -> normalised value

        ValueMap captureCurrent() const;
        void apply(const ValueMap &values);
        ValueMap factoryValues(float amount) const;
        static ValueMap readPresetFile(const juce::File &file, juce::String *nameOut = nullptr);
        static juce::ValueTree mapToTree(const juce::Identifier &type, const ValueMap &values);
        static ValueMap treeToMap(const juce::ValueTree &tree);

        juce::AudioProcessorValueTreeState &apvts;
        std::vector<PresetInfo> presets;
        int numFactory = 0;
        int currentIndex = 2;
        ValueMap snapshot;

        int activeSlot = 0;
        std::array<ValueMap, 2> slots;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
    };
}
