#pragma once

// Good Reverb DSP core: predelay -> early diffusion -> early reflections + attack
// build-up -> 16-line modulated feedback delay network with 3-band decay ->
// output EQ -> dry/wet mix. Pure C++17, no JUCE dependency.

#include "DspPrimitives.h"

#include <array>

namespace gr
{

enum class Mode : int
{
    Hall = 0,
    BrightHall,
    Plate,
    SmoothPlate,
    Room,
    Chamber,
    Ambience,
    RandomHall,
    ChorusHall,
    DirtyHall,
    Nonlinear,
    Shimmer,
    count
};

enum class Color : int
{
    Vintage = 0, // dark, band-limited, slightly saturated, linear interpolation
    Classic,     // mid-era: a little band-limiting and more random motion
    Modern,      // full bandwidth, cubic interpolation, clean
    count
};

const char* modeName (Mode m);
const char* modeDescription (Mode m);
const char* colorName (Color c);

struct ReverbParams
{
    float mix = 0.35f;         // 0..1
    float predelayMs = 10.0f;  // 0..500
    float decaySeconds = 2.5f; // 0.2..30
    float size = 0.6f;         // 0..1
    float attack = 0.4f;       // 0..1
    float earlyDiffusion = 0.7f;
    float lateDiffusion = 0.6f;
    float modRateHz = 0.6f;    // 0.05..5
    float modDepth = 0.4f;     // 0..1
    float highCutHz = 9000.0f;
    float lowCutHz = 60.0f;
    float bassMult = 1.2f;     // low-band decay multiplier 0.25..4
    float bassXoverHz = 500.0f;
    float highShelfDb = -6.0f; // -24..0, how much faster the highs decay
    float highFreqHz = 5000.0f;
    float width = 1.0f;        // 0..1
    Mode mode = Mode::Hall;
    Color color = Color::Modern;
};

class ReverbEngine
{
public:
    static constexpr int numLines = 16;

    ReverbEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const ReverbParams& p) { target = p; }

    // In-place stereo processing. Both pointers must be valid (pass the same
    // data twice for mono-to-stereo upstream handling).
    void process (float* left, float* right, int numSamples);

private:
    void configureMode (Mode m);
    void updateControl (int blockLength);
    void processChunk (float* left, float* right, int numSamples);

    float sampleRate = 48000.0f;
    bool prepared = false;

    ReverbParams target;

    // Mode switching is done with a short wet fade so buffers can be cleared.
    Mode activeMode = Mode::Hall;
    float modeFade = 1.0f;
    int fadeDirection = 0; // -1 fading out, +1 fading in
    float fadeStep = 0.0f;

    // Per-sample smoothed values.
    float mixSm = 0.35f, widthSm = 1.0f, predelaySm = 1.0f;
    float mixGainsFor = -1.0f, dryGain = 1.0f, wetGain = 0.0f;
    float attackWindowSm = 100.0f, gateWindowSm = 1000.0f, erSpanSm = 100.0f;
    float sampleSmooth = 0.0f, lengthSmooth = 0.0f;

    // Block-rate smoothed values.
    float decaySm = 2.5f, bassMultSm = 1.2f, highShelfSm = -6.0f;
    float bassXoverSm = 500.0f, highFreqSm = 5000.0f, highCutSm = 9000.0f, lowCutSm = 60.0f;
    float modRateSm = 0.6f, modDepthSm = 0.4f, earlyDiffSm = 0.7f, lateDiffSm = 0.6f;
    bool firstControlUpdate = true;

    // Targets/coefficients derived once per control block.
    float mixTarget = 0.35f, widthTarget = 1.0f, predelayTarget = 1.0f;
    float attackWindowTarget = 100.0f, gateWindowTarget = 1000.0f, erSpanTarget = 100.0f;
    float attackMinSamples = 24.0f;
    float earlyCoef = 0.5f, lateCoef = 0.5f, modDepthSamples = 0.0f;
    float lowSplitCoef = 0.1f, highSplitCoef = 0.5f, bandwidthCoef = 1.0f;
    float drive = 0.0f, shimmerGain = 0.0f, outputTilt = 0.0f, colorLowpassCoef = 1.0f;
    float lfoShape = 0.0f;
    bool cubic = true;

    // Input stages.
    DelayLine predelayL, predelayR;
    std::array<Allpass, 4> earlyApL, earlyApR;
    DelayLine diffusedL, diffusedR; // feeds attack taps, early reflections and nonlinear gate

    static constexpr int numAttackTaps = 8;
    std::array<float, numAttackTaps> attackTapPos {}, attackTapGain {};

    static constexpr int numErTaps = 12;
    std::array<float, numErTaps> erPosL {}, erPosR {}, erGainL {}, erGainR {};
    float erLevel = 0.0f;

    static constexpr int numGateTaps = 32;
    std::array<float, numGateTaps> gatePosL {}, gatePosR {}, gateShapeL {}, gateShapeR {}, gateGainL {}, gateGainR {};
    std::array<Allpass, 3> gateApL, gateApR;

    // Feedback delay network.
    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> baseLength {};   // samples at size = 100%
    std::array<float, numLines> lengthTarget {}, lengthSm {};
    std::array<float, numLines> gainLow {}, gainMid {}, gainHigh {};
    std::array<float, numLines> lowState {}, highState {}, bandwidthState {};
    std::array<Allpass, numLines> lateAp;
    std::array<Lfo, numLines> lfos;
    std::array<float, numLines> lfoRateSpread {};

    // Shimmer.
    PitchShifter shifter;
    OnePoleLowpass shimmerHpState;
    float shimmerFeedback = 0.0f;

    // Output.
    Biquad lowCutL, lowCutR, highCutL, highCutR;
    OnePoleLowpass colorLpL1, colorLpL2, colorLpR1, colorLpR2, tiltL, tiltR;
    float outputGain = 1.0f;
};

} // namespace gr
