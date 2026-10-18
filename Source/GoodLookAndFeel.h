#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
inline const juce::Colour background { 0xff15171c };
inline const juce::Colour panel { 0xff1e2128 };
inline const juce::Colour panelEdge { 0xff2b2f38 };
inline const juce::Colour knobBody { 0xff262a33 };
inline const juce::Colour track { 0xff343944 };
inline const juce::Colour text { 0xffe6e8ec };
inline const juce::Colour dimText { 0xff8b919d };
inline const juce::Colour amber { 0xffe8a948 };
inline const juce::Colour teal { 0xff4cc3b0 };
inline const juce::Colour violet { 0xff9a8cf0 };
} // namespace Palette

class GoodLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GoodLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                       int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isHighlighted, bool isDown) override;
};
