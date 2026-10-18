#include "PluginEditor.h"

namespace
{
bool sameParams (const gr::ReverbParams& a, const gr::ReverbParams& b)
{
    auto same = [] (float x, float y) { return std::abs (x - y) < 1.0e-6f; };
    return same (a.mix, b.mix) && same (a.predelayMs, b.predelayMs) && same (a.decaySeconds, b.decaySeconds) && same (a.size, b.size)
           && same (a.attack, b.attack) && same (a.bassMult, b.bassMult) && same (a.highShelfDb, b.highShelfDb) && a.mode == b.mode
           && a.color == b.color && same (a.earlyDiffusion, b.earlyDiffusion) && same (a.lateDiffusion, b.lateDiffusion);
}

juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

//==============================================================================
void DecayDisplay::setParams (const gr::ReverbParams& p)
{
    params = p;
    repaint();
}

void DecayDisplay::paint (juce::Graphics& g)
{
    using namespace juce;

    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::background.darker (0.25f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    const auto plot = bounds.reduced (14.0f, 12.0f).withTrimmedTop (16.0f).withTrimmedBottom (12.0f);
    const bool gated = params.mode == gr::Mode::Nonlinear;
    const float predelay = params.predelayMs * 0.001f;
    const float build = (0.003f + params.attack * 0.12f) * (0.4f + 0.6f * params.size);
    const float gate = jlimit (0.04f, 1.2f, params.decaySeconds * 0.15f) * (0.6f + 0.4f * params.size);
    const float span = gated ? jmax (0.25f, predelay + gate * 1.4f) : jmax (0.5f, predelay + build + params.decaySeconds * 1.05f);

    // Time grid.
    const float step = span > 20.0f ? 5.0f : span > 8.0f ? 2.0f : span > 3.0f ? 1.0f : span > 1.0f ? 0.25f : 0.1f;
    g.setFont (uiFont (10.0f));
    for (float t = step; t < span; t += step)
    {
        const float x = plot.getX() + plot.getWidth() * t / span;
        g.setColour (Palette::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Palette::dimText.withAlpha (0.7f));
        g.drawText (String (t, step < 1.0f ? 2 : 0) + "s", Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 12.0f),
                    Justification::centred);
    }

    // Level in dB over time, mapped 0 dB (top) .. -60 dB (bottom).
    auto levelAt = [&] (float t, float rt) -> float
    {
        t -= predelay;
        if (t < 0.0f)
            return -60.0f;
        if (gated)
        {
            if (t > gate)
                return -60.0f;
            const float f = t / gate;
            const float env = (1.0f - params.attack) * (1.0f - 0.85f * f) + params.attack * (0.15f + 0.85f * f);
            return jmax (-60.0f, 20.0f * std::log10 (jmax (1.0e-3f, env / 0.85f)));
        }
        const float rise = build > 0.0f ? jmin (1.0f, t / build) : 1.0f;
        const float onset = -30.0f * (1.0f - std::pow (rise, 0.5f + params.attack));
        return jmax (-60.0f, onset - 60.0f * t / rt);
    };

    auto curve = [&] (float rt, bool closed)
    {
        Path p;
        const int steps = jmax (32, (int) plot.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float t = span * (float) i / (float) steps;
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const float y = jmap (levelAt (t, rt), -60.0f, 0.0f, plot.getBottom(), plot.getY());
            if (i == 0)
                p.startNewSubPath (x, closed ? plot.getBottom() : y);
            if (i > 0 || closed)
                p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (plot.getRight(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };

    const float rt = jmax (0.05f, params.decaySeconds);
    if (! gated)
    {
        g.setColour (Palette::teal.withAlpha (0.8f));
        g.strokePath (curve (rt * params.bassMult, false), PathStrokeType (1.2f));
        g.setColour (Palette::violet.withAlpha (0.8f));
        g.strokePath (curve (rt * std::pow (10.0f, params.highShelfDb / 24.0f), false), PathStrokeType (1.2f));
    }

    auto fill = curve (rt, true);
    g.setGradientFill (ColourGradient (Palette::amber.withAlpha (0.45f), 0.0f, plot.getY(), Palette::amber.withAlpha (0.03f), 0.0f,
                                       plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (Palette::amber);
    g.strokePath (curve (rt, false), PathStrokeType (2.0f));

    // Legend.
    g.setFont (uiFont (11.0f, true));
    const auto header = bounds.reduced (14.0f, 8.0f).removeFromTop (14.0f);
    g.setColour (Palette::dimText);
    g.drawText (gated ? "GATE " + String (gate * 1000.0f, 0) + " ms" : "RT60 " + String (rt, rt < 10.0f ? 2 : 1) + " s", header,
                Justification::centredLeft);
    if (! gated)
    {
        auto legend = header;
        auto item = [&] (const String& text, Colour c)
        {
            auto r = legend.removeFromRight (46.0f);
            g.setColour (c);
            g.drawText (text, r, Justification::centredRight);
        };
        item ("HIGH", Palette::violet);
        item ("MID", Palette::amber);
        item ("LOW", Palette::teal);
    }
}

//==============================================================================
MainPanel::MainPanel (GoodReverbProcessor& p) : processor (p)
{
    auto& apvts = processor.apvts;

    // Mode / colour selectors.
    auto setupCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (uiFont (11.0f, true));
        label.setColour (juce::Label::textColourId, Palette::dimText);
        addAndMakeVisible (label);
    };
    setupCaption (modeLabel, "MODE");
    setupCaption (colorLabel, "COLOR");
    setupCaption (presetLabel, "PRESET");

    for (auto* box : { &modeBox, &colorBox })
        addAndMakeVisible (*box);
    modeBox.addItemList (apvts.getParameter (ParamIDs::mode)->getAllValueStrings(), 1);
    colorBox.addItemList (apvts.getParameter (ParamIDs::color)->getAllValueStrings(), 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, ParamIDs::mode, modeBox);
    colorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, ParamIDs::color, colorBox);

    descriptionLabel.setFont (uiFont (13.0f));
    descriptionLabel.setColour (juce::Label::textColourId, Palette::dimText.brighter (0.2f));
    descriptionLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (descriptionLabel);

    // Presets.
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
        lastProgram = processor.getCurrentProgram();
    };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    refreshPresetBox();

    addAndMakeVisible (display);

    // Knobs, grouped into sections.
    Section space { "SPACE", { 24, 214, 912, 162 }, {} };
    space.knobs = { &addKnob (ParamIDs::mix, "MIX", Palette::amber), &addKnob (ParamIDs::predelay, "PREDELAY", Palette::amber),
                    &addKnob (ParamIDs::decay, "DECAY", Palette::amber), &addKnob (ParamIDs::size, "SIZE", Palette::amber),
                    &addKnob (ParamIDs::width, "WIDTH", Palette::amber) };

    Section character { "CHARACTER", { 24, 388, 446, 160 }, {} };
    character.knobs = { &addKnob (ParamIDs::attack, "ATTACK", Palette::teal), &addKnob (ParamIDs::earlyDiff, "EARLY DIFF", Palette::teal),
                        &addKnob (ParamIDs::lateDiff, "LATE DIFF", Palette::teal), &addKnob (ParamIDs::modRate, "MOD RATE", Palette::teal),
                        &addKnob (ParamIDs::modDepth, "MOD DEPTH", Palette::teal) };

    Section eq { "DECAY EQ", { 482, 388, 454, 160 }, {} };
    eq.knobs = { &addKnob (ParamIDs::highCut, "HIGH CUT", Palette::violet), &addKnob (ParamIDs::lowCut, "LOW CUT", Palette::violet),
                 &addKnob (ParamIDs::bassMult, "BASS MULT", Palette::violet), &addKnob (ParamIDs::bassXover, "BASS XOVER", Palette::violet),
                 &addKnob (ParamIDs::highShelf, "HIGH SHELF", Palette::violet), &addKnob (ParamIDs::highFreq, "HIGH FREQ", Palette::violet) };

    sections = { space, character, eq };

    lastParams = processor.currentParams();
    display.setParams (lastParams);
    descriptionLabel.setText (gr::modeDescription (lastParams.mode), juce::dontSendNotification);

    setSize (designWidth, designHeight);
    startTimerHz (20);
}

MainPanel::~MainPanel()
{
    stopTimer();
}

MainPanel::Knob& MainPanel::addKnob (const char* paramId, const juce::String& labelText, juce::Colour accent)
{
    auto knob = std::make_unique<Knob>();
    auto& s = knob->slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setVelocityBasedMode (false);
    s.setPopupMenuEnabled (true);
    addAndMakeVisible (s);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, s);

    if (auto* param = processor.apvts.getParameter (paramId))
    {
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        s.setTooltip (param->getName (64) + " (double-click to reset)");
    }

    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setFont (uiFont (11.5f, true));
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, Palette::dimText.brighter (0.15f));
    knob->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob->label);

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

void MainPanel::layoutSection (const Section& section, int knobSize)
{
    auto inner = section.bounds.reduced (10, 8).withTrimmedTop (22);
    const int cellWidth = inner.getWidth() / (int) section.knobs.size();
    for (auto* knob : section.knobs)
    {
        auto cell = inner.removeFromLeft (cellWidth);
        knob->label.setBounds (cell.removeFromTop (16));
        const int size = juce::jmin (knobSize, cell.getWidth() - 8, cell.getHeight() - 22);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (80, cell.getWidth() - 4), 18);
        knob->slider.setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 4, size + 22));
    }
}

void MainPanel::resized()
{
    presetLabel.setBounds (548, 20, 56, 26);
    prevPreset.setBounds (604, 17, 30, 32);
    presetBox.setBounds (638, 17, 264, 32);
    nextPreset.setBounds (906, 17, 30, 32);

    modeLabel.setBounds (40, 96, 120, 16);
    modeBox.setBounds (40, 114, 252, 38);
    colorLabel.setBounds (306, 96, 120, 16);
    colorBox.setBounds (306, 114, 172, 38);
    descriptionLabel.setBounds (38, 160, 444, 36);

    display.setBounds (506, 72, 430, 130);

    layoutSection (sections[0], 104);
    layoutSection (sections[1], 82);
    layoutSection (sections[2], 72);
}

void MainPanel::paint (juce::Graphics& g)
{
    using namespace juce;

    g.setGradientFill (ColourGradient (Palette::background.brighter (0.06f), 0.0f, 0.0f, Palette::background.darker (0.2f), 0.0f,
                                       (float) getHeight(), false));
    g.fillAll();

    // Header.
    g.setFont (uiFont (28.0f, true));
    g.setColour (Palette::amber);
    g.drawText ("GOOD", 24, 12, 90, 40, Justification::centredLeft);
    g.setColour (Palette::text);
    g.drawText ("REVERB", 106, 12, 140, 40, Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (Palette::dimText);
    g.drawText ("algorithmic stereo reverb  v" JucePlugin_VersionString, 250, 22, 260, 24, Justification::centredLeft);
    g.setColour (Palette::panelEdge);
    g.drawHorizontalLine (62, 24.0f, 936.0f);

    // Panels.
    auto drawPanel = [&g] (Rectangle<int> r, const String& title)
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (Palette::panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setFont (uiFont (11.0f, true));
            g.setColour (Palette::dimText);
            g.drawText (title, r.getX() + 16, r.getY() + 8, 200, 16, Justification::centredLeft);
        }
    };

    drawPanel ({ 24, 72, 470, 130 }, {});
    for (const auto& s : sections)
        drawPanel (s.bounds, s.title);
}

void MainPanel::timerCallback()
{
    const auto p = processor.currentParams();
    if (! sameParams (p, lastParams))
    {
        if (p.mode != lastParams.mode)
            descriptionLabel.setText (gr::modeDescription (p.mode), juce::dontSendNotification);
        lastParams = p;
        display.setParams (p);
    }
    if (processor.getCurrentProgram() != lastProgram)
        refreshPresetBox();
}

void MainPanel::refreshPresetBox()
{
    lastProgram = processor.getCurrentProgram();
    presetBox.setSelectedId (lastProgram + 1, juce::dontSendNotification);
}

void MainPanel::stepPreset (int delta)
{
    const int count = processor.getNumPrograms();
    if (count <= 0)
        return;
    const int next = (processor.getCurrentProgram() + delta + count) % count;
    processor.setCurrentProgram (next);
    refreshPresetBox();
}

//==============================================================================
GoodReverbEditor::GoodReverbEditor (GoodReverbProcessor& p) : AudioProcessorEditor (&p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (MainPanel::designWidth * 3 / 4, MainPanel::designHeight * 3 / 4, MainPanel::designWidth * 2,
                     MainPanel::designHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio ((double) MainPanel::designWidth / (double) MainPanel::designHeight);
    setSize (MainPanel::designWidth, MainPanel::designHeight);
}

GoodReverbEditor::~GoodReverbEditor()
{
    setLookAndFeel (nullptr);
}

void GoodReverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void GoodReverbEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainPanel::designWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setBounds (0, 0, MainPanel::designWidth, MainPanel::designHeight);
}
