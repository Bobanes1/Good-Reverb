#include "GoodLookAndFeel.h"

GoodLookAndFeel::GoodLookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, Palette::background);
    setColour (Label::textColourId, Palette::text);
    setColour (Slider::textBoxTextColourId, Palette::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Palette::amber.withAlpha (0.4f));
    setColour (Slider::rotarySliderFillColourId, Palette::amber);
    setColour (Slider::rotarySliderOutlineColourId, Palette::track);
    setColour (ComboBox::backgroundColourId, Palette::knobBody);
    setColour (ComboBox::outlineColourId, Palette::panelEdge);
    setColour (ComboBox::textColourId, Palette::text);
    setColour (ComboBox::arrowColourId, Palette::amber);
    setColour (PopupMenu::backgroundColourId, Palette::panel);
    setColour (PopupMenu::textColourId, Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::amber.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId, Palette::background);
    setColour (TextButton::buttonColourId, Palette::knobBody);
    setColour (TextButton::textColourOffId, Palette::text);
    setColour (TextEditor::backgroundColourId, Palette::background);
    setColour (TextEditor::textColourId, Palette::text);
    setColour (TextEditor::highlightColourId, Palette::amber.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, Palette::amber);
}

void GoodLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;

    const auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcWidth = jmax (2.5f, radius * 0.11f);
    const float arcRadius = radius - arcWidth * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool hot = slider.isMouseOverOrDragging() && slider.isEnabled();

    // Track and value arcs.
    Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::track);
    g.strokePath (trackArc, PathStrokeType (arcWidth, PathStrokeType::curved, PathStrokeType::rounded));

    // Bipolar-looking parameters (like High Shelf) still read naturally from the left.
    Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (hot ? accent.brighter (0.2f) : accent);
    g.strokePath (valueArc, PathStrokeType (arcWidth, PathStrokeType::curved, PathStrokeType::rounded));

    // Knob body.
    const float bodyRadius = arcRadius - arcWidth * 1.4f;
    const auto body = Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (ColourGradient (Palette::knobBody.brighter (0.25f), body.getX(), body.getY(), Palette::knobBody.darker (0.35f),
                                       body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (Palette::panelEdge.brighter (0.15f));
    g.drawEllipse (body, 1.0f);

    // Pointer.
    Path pointer;
    const float pointerLength = bodyRadius * 0.62f;
    const float pointerWidth = jmax (2.0f, bodyRadius * 0.12f);
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -bodyRadius + bodyRadius * 0.14f, pointerWidth, pointerLength, pointerWidth * 0.5f);
    g.setColour (hot ? Colours::white : Palette::text);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre));
}

void GoodLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    const auto bounds = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId).brighter (box.isMouseOver() ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Palette::amber.withAlpha (0.7f) : box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    const float arrowSize = jmin (10.0f, (float) height * 0.3f);
    const float ax = (float) width - arrowSize - 12.0f;
    const float ay = (float) height * 0.5f;
    Path arrow;
    arrow.addTriangle (ax, ay - arrowSize * 0.3f, ax + arrowSize, ay - arrowSize * 0.3f, ax + arrowSize * 0.5f, ay + arrowSize * 0.4f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font GoodLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions (juce::jmin (17.0f, (float) box.getHeight() * 0.5f), juce::Font::bold));
}

void GoodLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Label* GoodLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::Font (juce::FontOptions (13.0f)));
    label->setColour (juce::Label::textColourId, Palette::dimText.brighter (0.3f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, Palette::amber);
    return label;
}

juce::Font GoodLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f));
}

void GoodLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                            bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = colour;
    if (isDown)
        fill = fill.brighter (0.2f);
    else if (isHighlighted)
        fill = fill.brighter (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}
