#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;

Range skewed (float min, float max, float centre, float interval = 0.0f)
{
    Range r (min, max, interval);
    r.setSkewForCentre (centre);
    return r;
}

juce::String formatHz (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

float parseHz (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const float v = t.getFloatValue();
    return t.containsChar ('k') ? v * 1000.0f : v;
}

juce::AudioParameterFloatAttributes withText (std::function<juce::String (float, int)> toText,
                                              std::function<float (const juce::String&)> fromText = nullptr)
{
    if (! fromText)
        fromText = [] (const juce::String& t) { return t.getFloatValue(); };
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText)).withValueFromStringFunction (std::move (fromText));
}

auto percentText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // anything not listed uses the default
};

const std::vector<Preset>& factoryPresets()
{
    using namespace ParamIDs;
    static const std::vector<Preset> presets = {
        { "Init Hall", {} },
        { "Vocal Plate", { { mode, (float) gr::Mode::Plate }, { decay, 1.8f }, { predelay, 25.0f }, { size, 50.0f }, { mix, 25.0f },
                           { highCut, 8000.0f }, { lowCut, 150.0f }, { attack, 20.0f }, { earlyDiff, 80.0f }, { lateDiff, 70.0f },
                           { modRate, 0.8f }, { modDepth, 30.0f } } },
        { "Drum Room", { { mode, (float) gr::Mode::Room }, { decay, 0.9f }, { predelay, 0.0f }, { size, 40.0f }, { mix, 20.0f },
                         { lowCut, 80.0f }, { highCut, 10000.0f }, { color, (float) gr::Color::Classic }, { attack, 15.0f } } },
        { "Huge Space", { { mode, (float) gr::Mode::Hall }, { decay, 12.0f }, { size, 100.0f }, { predelay, 40.0f }, { attack, 70.0f },
                          { modRate, 0.3f }, { modDepth, 60.0f }, { mix, 40.0f }, { highCut, 7000.0f }, { bassMult, 1.5f } } },
        { "Vintage Chamber", { { mode, (float) gr::Mode::Chamber }, { color, (float) gr::Color::Vintage }, { decay, 2.2f }, { size, 55.0f },
                               { predelay, 15.0f }, { mix, 30.0f } } },
        { "Bright Strings Hall", { { mode, (float) gr::Mode::BrightHall }, { decay, 3.5f }, { size, 75.0f }, { predelay, 30.0f },
                                   { highCut, 14000.0f }, { highShelf, -3.0f }, { mix, 30.0f } } },
        { "Tight Ambience", { { mode, (float) gr::Mode::Ambience }, { decay, 0.6f }, { size, 30.0f }, { mix, 25.0f }, { predelay, 0.0f } } },
        { "Chorus Wash", { { mode, (float) gr::Mode::ChorusHall }, { decay, 6.0f }, { modDepth, 80.0f }, { modRate, 0.9f }, { size, 80.0f },
                           { mix, 40.0f } } },
        { "Wandering Hall", { { mode, (float) gr::Mode::RandomHall }, { decay, 4.5f }, { modDepth, 70.0f }, { modRate, 0.5f }, { size, 75.0f } } },
        { "Dirty Tape Hall", { { mode, (float) gr::Mode::DirtyHall }, { color, (float) gr::Color::Vintage }, { decay, 4.0f }, { mix, 35.0f },
                               { lowCut, 120.0f } } },
        { "Gated Snare", { { mode, (float) gr::Mode::Nonlinear }, { decay, 2.5f }, { attack, 50.0f }, { size, 50.0f }, { mix, 30.0f },
                           { lowCut, 120.0f } } },
        { "Reverse Swell", { { mode, (float) gr::Mode::Nonlinear }, { decay, 4.0f }, { attack, 100.0f }, { size, 80.0f }, { mix, 35.0f } } },
        { "Shimmer Pad", { { mode, (float) gr::Mode::Shimmer }, { decay, 9.0f }, { size, 90.0f }, { mix, 45.0f }, { attack, 70.0f },
                           { modRate, 0.4f }, { modDepth, 50.0f }, { highCut, 12000.0f }, { lowCut, 200.0f } } },
        { "Silky Plate", { { mode, (float) gr::Mode::SmoothPlate }, { decay, 3.0f }, { size, 70.0f }, { predelay, 20.0f }, { mix, 30.0f } } },
    };
    return presets;
}
} // namespace

GoodReverbProcessor::GoodReverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GoodReverb", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw.mix = get (ParamIDs::mix);
    raw.predelay = get (ParamIDs::predelay);
    raw.decay = get (ParamIDs::decay);
    raw.size = get (ParamIDs::size);
    raw.attack = get (ParamIDs::attack);
    raw.earlyDiff = get (ParamIDs::earlyDiff);
    raw.lateDiff = get (ParamIDs::lateDiff);
    raw.modRate = get (ParamIDs::modRate);
    raw.modDepth = get (ParamIDs::modDepth);
    raw.highCut = get (ParamIDs::highCut);
    raw.lowCut = get (ParamIDs::lowCut);
    raw.bassMult = get (ParamIDs::bassMult);
    raw.bassXover = get (ParamIDs::bassXover);
    raw.highShelf = get (ParamIDs::highShelf);
    raw.highFreq = get (ParamIDs::highFreq);
    raw.width = get (ParamIDs::width);
    raw.mode = get (ParamIDs::mode);
    raw.color = get (ParamIDs::color);
}

juce::AudioProcessorValueTreeState::ParameterLayout GoodReverbProcessor::createLayout()
{
    using namespace ParamIDs;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* id, const char* name, Range range, float def, juce::AudioParameterFloatAttributes attr)
    { layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, attr)); };

    auto msText = [] (float v, int) { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    auto secText = [] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; };
    auto hzText = [] (float v, int) { return formatHz (v); };
    auto rateText = [] (float v, int) { return juce::String (v, 2) + " Hz"; };
    auto multText = [] (float v, int) { return juce::String (v, 2) + "x"; };
    auto dbText = [] (float v, int) { return juce::String (v, 1) + " dB"; };

    addFloat (mix, "Mix", Range (0.0f, 100.0f, 0.1f), 35.0f, withText (percentText));
    addFloat (predelay, "Predelay", skewed (0.0f, 500.0f, 60.0f, 0.1f), 10.0f, withText (msText));
    addFloat (decay, "Decay", skewed (0.2f, 30.0f, 3.0f, 0.01f), 2.5f, withText (secText));
    addFloat (size, "Size", Range (0.0f, 100.0f, 0.1f), 60.0f, withText (percentText));
    addFloat (attack, "Attack", Range (0.0f, 100.0f, 0.1f), 40.0f, withText (percentText));
    addFloat (earlyDiff, "Early Diffusion", Range (0.0f, 100.0f, 0.1f), 70.0f, withText (percentText));
    addFloat (lateDiff, "Late Diffusion", Range (0.0f, 100.0f, 0.1f), 60.0f, withText (percentText));
    addFloat (modRate, "Mod Rate", skewed (0.05f, 5.0f, 0.8f, 0.01f), 0.6f, withText (rateText));
    addFloat (modDepth, "Mod Depth", Range (0.0f, 100.0f, 0.1f), 40.0f, withText (percentText));
    addFloat (highCut, "High Cut", skewed (500.0f, 20000.0f, 4000.0f, 1.0f), 9000.0f, withText (hzText, parseHz));
    addFloat (lowCut, "Low Cut", skewed (10.0f, 2000.0f, 150.0f, 1.0f), 60.0f, withText (hzText, parseHz));
    addFloat (bassMult, "Bass Mult", skewed (0.25f, 4.0f, 1.0f, 0.01f), 1.2f, withText (multText));
    addFloat (bassXover, "Bass Xover", skewed (100.0f, 2000.0f, 500.0f, 1.0f), 500.0f, withText (hzText, parseHz));
    addFloat (highShelf, "High Shelf", Range (-24.0f, 0.0f, 0.1f), -6.0f, withText (dbText));
    addFloat (highFreq, "High Freq", skewed (1000.0f, 16000.0f, 4000.0f, 1.0f), 5000.0f, withText (hzText, parseHz));
    addFloat (width, "Width", Range (0.0f, 100.0f, 0.1f), 100.0f, withText (percentText));

    juce::StringArray modes, colors;
    for (int i = 0; i < (int) gr::Mode::count; ++i)
        modes.add (gr::modeName ((gr::Mode) i));
    for (int i = 0; i < (int) gr::Color::count; ++i)
        colors.add (gr::colorName ((gr::Color) i));

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { mode, 1 }, "Mode", modes, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { color, 1 }, "Color", colors, (int) gr::Color::Modern));
    return layout;
}

gr::ReverbParams GoodReverbProcessor::currentParams() const
{
    gr::ReverbParams p;
    p.mix = raw.mix->load() * 0.01f;
    p.predelayMs = raw.predelay->load();
    p.decaySeconds = raw.decay->load();
    p.size = raw.size->load() * 0.01f;
    p.attack = raw.attack->load() * 0.01f;
    p.earlyDiffusion = raw.earlyDiff->load() * 0.01f;
    p.lateDiffusion = raw.lateDiff->load() * 0.01f;
    p.modRateHz = raw.modRate->load();
    p.modDepth = raw.modDepth->load() * 0.01f;
    p.highCutHz = raw.highCut->load();
    p.lowCutHz = raw.lowCut->load();
    p.bassMult = raw.bassMult->load();
    p.bassXoverHz = raw.bassXover->load();
    p.highShelfDb = raw.highShelf->load();
    p.highFreqHz = raw.highFreq->load();
    p.width = raw.width->load() * 0.01f;
    p.mode = (gr::Mode) juce::jlimit (0, (int) gr::Mode::count - 1, juce::roundToInt (raw.mode->load()));
    p.color = (gr::Color) juce::jlimit (0, (int) gr::Color::count - 1, juce::roundToInt (raw.color->load()));
    return p;
}

void GoodReverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (currentParams());
    engine.prepare (sampleRate, samplesPerBlock);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock), false, false, true);
}

void GoodReverbProcessor::reset()
{
    engine.reset();
}

bool GoodReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();
    return false;
}

void GoodReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numOut == 0 || numSamples == 0)
        return;

    engine.setParams (currentParams());

    if (numOut >= 2)
    {
        if (numIn == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        return;
    }

    // Mono in/out: run the stereo engine and fold back to mono.
    float* mono = buffer.getWritePointer (0);
    float* scratch = monoScratch.getWritePointer (0);
    const int chunkSize = monoScratch.getNumSamples();
    for (int pos = 0; pos < numSamples; pos += chunkSize)
    {
        const int n = juce::jmin (chunkSize, numSamples - pos);
        juce::FloatVectorOperations::copy (scratch, mono + pos, n);
        engine.process (mono + pos, scratch, n);
        juce::FloatVectorOperations::add (mono + pos, scratch, n);
        juce::FloatVectorOperations::multiply (mono + pos, 0.5f, n);
    }
}

double GoodReverbProcessor::getTailLengthSeconds() const
{
    return (double) raw.decay->load() * 1.5 + (double) raw.predelay->load() * 0.001;
}

int GoodReverbProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String GoodReverbProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name) : juce::String();
}

void GoodReverbProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram = index;
    const auto& preset = presets[(size_t) index];

    for (auto* p : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr)
            continue;

        float normalised = ranged->getDefaultValue();
        for (const auto& [id, value] : preset.values)
            if (ranged->getParameterID() == id)
                normalised = ranged->convertTo0to1 (value);

        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }
}

void GoodReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GoodReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* GoodReverbProcessor::createEditor()
{
    return new GoodReverbEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GoodReverbProcessor();
}
