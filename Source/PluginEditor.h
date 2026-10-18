#pragma once

#include "GoodLookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Draws the shape of the reverb (predelay, build-up, and the low/mid/high decay
// slopes) from the current parameter values.
class DecayDisplay : public juce::Component
{
public:
    void setParams (const gr::ReverbParams& p);
    void paint (juce::Graphics&) override;

private:
    gr::ReverbParams params;
};

// Everything in the editor lives in this panel, laid out at a fixed design size
// and scaled as a whole when the window is resized.
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 960;
    static constexpr int designHeight = 570;

    explicit MainPanel (GoodReverbProcessor&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        std::vector<Knob*> knobs;
    };

    Knob& addKnob (const char* paramId, const juce::String& label, juce::Colour accent);
    void layoutSection (const Section& section, int knobSize);
    void timerCallback() override;
    void refreshPresetBox();
    void stepPreset (int delta);

    GoodReverbProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Section> sections;

    juce::ComboBox modeBox, colorBox, presetBox;
    juce::Label modeLabel, colorLabel, descriptionLabel, presetLabel;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, colorAttachment;
    DecayDisplay display;

    gr::ReverbParams lastParams;
    int lastProgram = -1;
};

class GoodReverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit GoodReverbEditor (GoodReverbProcessor&);
    ~GoodReverbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GoodLookAndFeel lookAndFeel;
    MainPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoodReverbEditor)
};
