#pragma once

#include "dsp/ReverbEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
inline constexpr const char* mix = "mix";
inline constexpr const char* predelay = "predelay";
inline constexpr const char* decay = "decay";
inline constexpr const char* size = "size";
inline constexpr const char* attack = "attack";
inline constexpr const char* earlyDiff = "earlyDiff";
inline constexpr const char* lateDiff = "lateDiff";
inline constexpr const char* modRate = "modRate";
inline constexpr const char* modDepth = "modDepth";
inline constexpr const char* highCut = "highCut";
inline constexpr const char* lowCut = "lowCut";
inline constexpr const char* bassMult = "bassMult";
inline constexpr const char* bassXover = "bassXover";
inline constexpr const char* highShelf = "highShelf";
inline constexpr const char* highFreq = "highFreq";
inline constexpr const char* width = "width";
inline constexpr const char* mode = "mode";
inline constexpr const char* color = "color";
} // namespace ParamIDs

class GoodReverbProcessor : public juce::AudioProcessor
{
public:
    GoodReverbProcessor();
    ~GoodReverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    gr::ReverbParams currentParams() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    gr::ReverbEngine engine;
    juce::AudioBuffer<float> monoScratch;
    int currentProgram = 0;
    std::atomic<bool> resetPending { false };

    struct RawParams
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* predelay = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* size = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* earlyDiff = nullptr;
        std::atomic<float>* lateDiff = nullptr;
        std::atomic<float>* modRate = nullptr;
        std::atomic<float>* modDepth = nullptr;
        std::atomic<float>* highCut = nullptr;
        std::atomic<float>* lowCut = nullptr;
        std::atomic<float>* bassMult = nullptr;
        std::atomic<float>* bassXover = nullptr;
        std::atomic<float>* highShelf = nullptr;
        std::atomic<float>* highFreq = nullptr;
        std::atomic<float>* width = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* color = nullptr;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoodReverbProcessor)
};
