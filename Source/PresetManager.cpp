#include "PresetManager.h"

namespace vfp
{
    namespace
    {
        struct FactoryPreset
        {
            const char *name;
            float amount;
        };

        // Factory presets differ in processing intensity only; everything else uses defaults.
        constexpr FactoryPreset factoryPresets[] = {
            {"Gentle Cleanup", 35.0f},
            {"Studio Voiceover", 45.0f},
            {"Podcast Dialogue", 60.0f},
            {"Noisy Interview", 78.0f},
            {"Outdoor Dialogue", 72.0f},
            {"Strong Suppression", 92.0f},
            {"Natural Speech", 40.0f},
        };

        const juce::Identifier presetTag{"VoiceFramePreset"};
        const juce::Identifier paramTag{"PARAM"};
        const juce::Identifier idProp{"id"};
        const juce::Identifier valueProp{"value"};
        const juce::Identifier nameProp{"name"};
        const juce::String presetExtension{".vfpreset"};
    }

    //==============================================================================
    PresetManager::PresetManager(juce::AudioProcessorValueTreeState &state)
        : apvts(state)
    {
        refreshUserPresets();
    }

    bool PresetManager::isPresetParameter(const juce::String &paramID)
    {
        return paramID != "bypass" && paramID != "monitor" && paramID != "bandSolo" && paramID != "bandDelta";
    }

    juce::File PresetManager::getUserPresetDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("VoiceFrame Audio")
            .getChildFile("VoiceFrame Pro")
            .getChildFile("Presets");
    }

    void PresetManager::refreshUserPresets()
    {
        const auto currentName = presets.empty() ? juce::String() : getCurrentPreset().name;
        const bool currentFactory = presets.empty() || getCurrentPreset().factory;

        presets.clear();
        for (const auto &p : factoryPresets)
            presets.push_back({p.name, true, p.amount, {}});
        numFactory = (int)presets.size();

        auto files = getUserPresetDirectory().findChildFiles(juce::File::findFiles, false, "*" + presetExtension);
        files.sort();
        for (const auto &file : files)
        {
            juce::String name;
            const auto values = readPresetFile(file, &name);
            if (values.empty())
                continue;

            float amount = 50.0f;
            if (auto *param = apvts.getParameter("amount"); param != nullptr && values.count("amount") > 0)
                amount = param->convertFrom0to1(values.at("amount"));

            presets.push_back({name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), false, amount, file});
        }

        if (currentName.isNotEmpty())
            for (int i = 0; i < (int)presets.size(); ++i)
                if (presets[(size_t)i].name == currentName && presets[(size_t)i].factory == currentFactory)
                    currentIndex = i;

        currentIndex = juce::jlimit(0, (int)presets.size() - 1, currentIndex);
    }

    //==============================================================================
    PresetManager::ValueMap PresetManager::captureCurrent() const
    {
        ValueMap values;
        for (auto *p : apvts.processor.getParameters())
            if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p))
                if (isPresetParameter(ranged->getParameterID()))
                    values[ranged->getParameterID()] = ranged->getValue();
        return values;
    }

    void PresetManager::apply(const ValueMap &values)
    {
        for (const auto &[id, value] : values)
        {
            if (!isPresetParameter(id))
                continue;

            if (auto *param = apvts.getParameter(id))
            {
                if (std::abs(param->getValue() - value) < 1.0e-6f)
                    continue;

                param->beginChangeGesture();
                param->setValueNotifyingHost(value);
                param->endChangeGesture();
            }
        }
    }

    PresetManager::ValueMap PresetManager::factoryValues(float amount) const
    {
        ValueMap values;
        for (auto *p : apvts.processor.getParameters())
            if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p))
                if (isPresetParameter(ranged->getParameterID()))
                    values[ranged->getParameterID()] = ranged->getDefaultValue();

        if (auto *param = apvts.getParameter("amount"))
            values["amount"] = param->convertTo0to1(amount);
        return values;
    }

    //==============================================================================
    void PresetManager::loadPreset(int index)
    {
        if (!juce::isPositiveAndBelow(index, (int)presets.size()))
            return;

        const auto &preset = presets[(size_t)index];
        auto values = preset.factory ? factoryValues(preset.amount) : readPresetFile(preset.file);
        if (values.empty())
            return;

        // Parameters missing from an older preset file fall back to their defaults
        for (const auto &[id, value] : factoryValues(50.0f))
            values.emplace(id, value);

        currentIndex = index;
        snapshot = values;
        apply(values);
    }

    void PresetManager::loadNext()
    {
        loadPreset((currentIndex + 1) % (int)presets.size());
    }

    void PresetManager::loadPrevious()
    {
        loadPreset((currentIndex - 1 + (int)presets.size()) % (int)presets.size());
    }

    void PresetManager::resetToPreset()
    {
        loadPreset(currentIndex);
    }

    bool PresetManager::isModified() const
    {
        if (snapshot.empty())
            return false;

        for (const auto &[id, value] : captureCurrent())
        {
            const auto it = snapshot.find(id);
            if (it == snapshot.end() || std::abs(it->second - value) > 1.0e-4f)
                return true;
        }
        return false;
    }

    bool PresetManager::saveUserPreset(const juce::String &name)
    {
        const auto trimmed = name.trim();
        if (trimmed.isEmpty())
            return false;

        auto dir = getUserPresetDirectory();
        if (!dir.createDirectory())
            return false;

        auto tree = mapToTree(presetTag, captureCurrent());
        tree.setProperty(nameProp, trimmed, nullptr);

        const auto file = dir.getChildFile(juce::File::createLegalFileName(trimmed) + presetExtension);
        auto xml = tree.createXml();
        if (xml == nullptr || !xml->writeTo(file))
            return false;

        refreshUserPresets();
        for (int i = numFactory; i < (int)presets.size(); ++i)
            if (presets[(size_t)i].file == file)
                currentIndex = i;

        snapshot = captureCurrent();
        return true;
    }

    PresetManager::ValueMap PresetManager::readPresetFile(const juce::File &file, juce::String *nameOut)
    {
        auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || !xml->hasTagName(presetTag.toString()))
            return {};

        const auto tree = juce::ValueTree::fromXml(*xml);
        if (nameOut != nullptr)
            *nameOut = tree.getProperty(nameProp).toString();
        return treeToMap(tree);
    }

    //==============================================================================
    void PresetManager::selectSlot(int slot)
    {
        slot = juce::jlimit(0, 1, slot);
        if (slot == activeSlot)
            return;

        slots[(size_t)activeSlot] = captureCurrent();
        activeSlot = slot;

        if (slots[(size_t)slot].empty())
            slots[(size_t)slot] = slots[(size_t)(1 - slot)];
        else
            apply(slots[(size_t)slot]);
    }

    void PresetManager::copyActiveToOther()
    {
        slots[(size_t)activeSlot] = captureCurrent();
        slots[(size_t)(1 - activeSlot)] = slots[(size_t)activeSlot];
    }

    //==============================================================================
    juce::ValueTree PresetManager::mapToTree(const juce::Identifier &type, const ValueMap &values)
    {
        juce::ValueTree tree(type);
        for (const auto &[id, value] : values)
        {
            juce::ValueTree param(paramTag);
            param.setProperty(idProp, id, nullptr);
            param.setProperty(valueProp, value, nullptr);
            tree.appendChild(param, nullptr);
        }
        return tree;
    }

    PresetManager::ValueMap PresetManager::treeToMap(const juce::ValueTree &tree)
    {
        ValueMap values;
        for (const auto &child : tree)
            if (child.hasType(paramTag))
                values[child.getProperty(idProp).toString()] = (float)child.getProperty(valueProp);
        return values;
    }

    juce::ValueTree PresetManager::toValueTree() const
    {
        juce::ValueTree tree("PresetSession");
        const auto &preset = getCurrentPreset();
        tree.setProperty("presetName", preset.name, nullptr);
        tree.setProperty("presetFactory", preset.factory, nullptr);
        tree.setProperty("activeSlot", activeSlot, nullptr);

        auto activeValues = captureCurrent();
        for (int s = 0; s < 2; ++s)
        {
            const auto &values = s == activeSlot ? activeValues : slots[(size_t)s];
            if (!values.empty())
                tree.appendChild(mapToTree(s == 0 ? "SlotA" : "SlotB", values), nullptr);
        }
        tree.appendChild(mapToTree("Snapshot", snapshot), nullptr);
        return tree;
    }

    void PresetManager::fromValueTree(const juce::ValueTree &tree)
    {
        if (!tree.isValid())
            return;

        refreshUserPresets();
        const auto name = tree.getProperty("presetName").toString();
        const bool factory = tree.getProperty("presetFactory", true);
        for (int i = 0; i < (int)presets.size(); ++i)
            if (presets[(size_t)i].name == name && presets[(size_t)i].factory == factory)
                currentIndex = i;

        activeSlot = juce::jlimit(0, 1, (int)tree.getProperty("activeSlot", 0));
        slots[0] = treeToMap(tree.getChildWithName("SlotA"));
        slots[1] = treeToMap(tree.getChildWithName("SlotB"));
        snapshot = treeToMap(tree.getChildWithName("Snapshot"));
    }
}
