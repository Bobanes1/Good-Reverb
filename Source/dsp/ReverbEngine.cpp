#include "ReverbEngine.h"

namespace gr
{

namespace
{
constexpr int kControlBlock = 32;
constexpr float kLn1000 = 6.907755279f; // ln(1000): 60 dB

constexpr float kMaxPredelayMs = 500.0f;
constexpr float kMaxLineMs = 200.0f;
constexpr float kMaxDiffusedMs = 1300.0f;
constexpr float kMaxModDepthMs = 2.5f;
constexpr float kMaxLateApMs = 10.0f;
constexpr float kModeFadeMs = 15.0f;

// Fixed, hand-picked tables so every mode is deterministic.
constexpr float kLineJitter[16] = { 0.31f, -0.62f, 0.87f, -0.15f, 0.54f, -0.93f, 0.12f, 0.71f,
                                    -0.44f, 0.96f, -0.28f, 0.65f, -0.79f, 0.08f, 0.39f, -0.57f };
constexpr int kLinePermutation[16] = { 0, 9, 4, 13, 2, 11, 6, 15, 1, 8, 5, 12, 3, 10, 7, 14 };
constexpr float kLateApFraction[16] = { 0.12f, 0.83f, 0.47f, 0.95f, 0.26f, 0.68f, 0.04f, 0.57f,
                                        0.91f, 0.35f, 0.74f, 0.18f, 0.62f, 0.41f, 0.99f, 0.29f };
constexpr float kLfoSpread[16] = { -0.93f, 0.41f, -0.27f, 0.88f, 0.05f, -0.66f, 0.72f, -0.12f,
                                   0.33f, -0.81f, 0.59f, -0.45f, 0.97f, -0.03f, 0.21f, -0.54f };
constexpr float kEarlyApMs[4] = { 1.37f, 2.23f, 3.71f, 5.17f };
constexpr float kEarlyApSkewR = 1.071f; // right channel diffusers are slightly longer
constexpr float kGateApMs[3] = { 2.9f, 4.3f, 6.1f };
constexpr float kAttackJitter[8] = { 0.0f, 0.06f, -0.04f, 0.08f, -0.07f, 0.03f, -0.05f, 0.0f };

// Sign of Hadamard row r at column i: (-1)^popcount(r & i).
float hadamardSign (int row, int column)
{
    int bits = row & column;
    int parity = 0;
    while (bits)
    {
        parity ^= bits & 1;
        bits >>= 1;
    }
    return parity ? -1.0f : 1.0f;
}

constexpr int kOutRowL = 6;
constexpr int kOutRowR = 11;
constexpr int kShimmerRow = 3;

struct SignTables
{
    float outL[16], outR[16], shimmer[16];
    SignTables()
    {
        for (int i = 0; i < 16; ++i)
        {
            outL[i] = hadamardSign (kOutRowL, i);
            outR[i] = hadamardSign (kOutRowR, i);
            shimmer[i] = hadamardSign (kShimmerRow, i);
        }
    }
};
const SignTables kSigns;

struct ModeSpec
{
    const char* name;
    const char* description;
    float minMs, maxMs;          // FDN delay spread at size 100%
    float erLevel, erSpanMs;     // early reflections
    float earlyApScale;          // early diffuser length scale
    float lateApMs;              // in-loop allpass base length
    float earlyDiffMax, lateDiffMax;
    float modDepthScale, modRateScale, lfoShape; // shape: 0 = sine, 1 = random
    float bandwidthHz;           // in-loop lowpass (0 = off)
    float drive;                 // in-loop saturation (0 = off)
    float tilt;                  // output high-shelf amount
    float attackMaxMs;           // build-up window at attack = 100%
    float outGain;
    bool nonlinear;
    bool shimmer;
};

// clang-format off
const ModeSpec kModes[(int) Mode::count] = {
    { "Hall",         "Large, smooth hall with a gentle build-up",             40, 150, 0.25f, 60, 1.0f, 6.0f, 0.75f, 0.65f, 1.0f, 1.0f, 0.4f,    0, 0.0f,  0.00f, 140, 1.57f, false, false },
    { "Bright Hall",  "Open, airy hall that keeps its top end",                 32, 125, 0.20f, 50, 0.9f, 5.0f, 0.75f, 0.60f, 0.8f, 1.0f, 0.3f,    0, 0.0f,  0.50f, 110, 1.31f, false, false },
    { "Plate",        "Dense, fast, bright plate with no early reflections",    9,  55, 0.00f,  0, 0.7f, 3.5f, 0.78f, 0.70f, 0.5f, 1.3f, 0.2f,    0, 0.0f,  0.25f,  40, 0.87f, false, false },
    { "Smooth Plate", "Plate with a slower onset and silky modulation",        11,  65, 0.00f,  0, 1.2f, 5.0f, 0.80f, 0.72f, 0.7f, 0.6f, 0.6f,    0, 0.0f,  0.00f,  90, 1.06f, false, false },
    { "Room",         "Small to medium room with strong reflections",           7,  42, 0.60f, 30, 0.6f, 2.5f, 0.70f, 0.55f, 0.4f, 1.0f, 0.5f,    0, 0.0f,  0.00f,  50, 0.77f, false, false },
    { "Chamber",      "Warm, slightly boxy echo chamber",                      13,  75, 0.40f, 40, 0.8f, 4.0f, 0.72f, 0.60f, 0.6f, 1.0f, 0.5f,    0, 0.0f, -0.15f,  80, 1.07f, false, false },
    { "Ambience",     "Short, mostly early energy: space without a long tail",  4,  28, 0.60f, 22, 0.5f, 2.0f, 0.65f, 0.45f, 0.3f, 1.2f, 0.6f,    0, 0.0f,  0.10f,  30, 0.55f, false, false },
    { "Random Hall",  "Hall with randomly wandering delay lengths",            38, 145, 0.20f, 55, 1.0f, 6.0f, 0.75f, 0.65f, 1.6f, 0.8f, 1.0f,    0, 0.0f,  0.00f, 130, 1.59f, false, false },
    { "Chorus Hall",  "Hall with deep, regular chorusing in the tail",         36, 140, 0.15f, 55, 1.0f, 6.0f, 0.72f, 0.60f, 2.0f, 1.1f, 0.0f,    0, 0.0f,  0.00f, 120, 1.60f, false, false },
    { "Dirty Hall",   "Band-limited, saturated hall with a grainy character",  42, 160, 0.20f, 60, 1.0f, 6.0f, 0.75f, 0.60f, 1.3f, 0.9f, 0.8f, 6000, 1.6f, -0.20f, 140, 2.00f, false, false },
    { "Nonlinear",    "Gated/reverse cloud: Decay sets gate length, Attack the envelope",
                                                                               10,  40, 0.00f,  0, 0.8f, 3.0f, 0.75f, 0.70f, 0.0f, 1.0f, 0.0f,    0, 0.0f,  0.10f,  40, 0.95f, true,  false },
    { "Shimmer",      "Hall with octave-up pitch shifting in the feedback",    45, 165, 0.10f, 60, 1.1f, 7.0f, 0.78f, 0.70f, 0.8f, 0.7f, 0.5f,    0, 0.0f,  0.10f, 160, 1.74f, false, true  },
};
// clang-format on

const ModeSpec& spec (Mode m) { return kModes[std::clamp ((int) m, 0, (int) Mode::count - 1)]; }

float maxModeLineMs()
{
    float m = 0.0f;
    for (const auto& s : kModes)
        m = std::max (m, s.maxMs);
    return m;
}
} // namespace

const char* modeName (Mode m) { return spec (m).name; }
const char* modeDescription (Mode m) { return spec (m).description; }

const char* colorName (Color c)
{
    switch (c)
    {
        case Color::Vintage: return "Vintage";
        case Color::Classic: return "Classic";
        case Color::Modern:
        case Color::count: break;
    }
    return "Modern";
}

ReverbEngine::ReverbEngine() = default;

void ReverbEngine::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = (float) std::max (8000.0, newSampleRate);
    const float msToSamples = 0.001f * sampleRate;

    predelayL.prepare ((int) (kMaxPredelayMs * msToSamples) + 4);
    predelayR.prepare ((int) (kMaxPredelayMs * msToSamples) + 4);

    const float maxEarlyAp = kEarlyApMs[3] * 1.2f * kEarlyApSkewR * msToSamples;
    for (int i = 0; i < 4; ++i)
    {
        earlyApL[(size_t) i].prepare ((int) maxEarlyAp + 2);
        earlyApR[(size_t) i].prepare ((int) maxEarlyAp + 2);
    }

    diffusedL.prepare ((int) (kMaxDiffusedMs * msToSamples));
    diffusedR.prepare ((int) (kMaxDiffusedMs * msToSamples));

    for (int i = 0; i < 3; ++i)
    {
        gateApL[(size_t) i].prepare ((int) (kGateApMs[i] * 1.1f * msToSamples) + 2);
        gateApR[(size_t) i].prepare ((int) (kGateApMs[i] * 1.1f * msToSamples) + 2);
    }

    const float maxLine = std::max (kMaxLineMs, maxModeLineMs() * 1.1f + kMaxModDepthMs * 3.0f);
    for (int i = 0; i < numLines; ++i)
    {
        lines[(size_t) i].prepare ((int) (maxLine * msToSamples) + 8);
        lateAp[(size_t) i].prepare ((int) (kMaxLateApMs * msToSamples) + 2);
    }

    shifter.prepare (sampleRate, 90.0f);
    shimmerHpState.setCoef (onePoleCoef (250.0f, sampleRate));
    tiltL.setCoef (onePoleCoef (3000.0f, sampleRate));
    tiltR.setCoef (onePoleCoef (3000.0f, sampleRate));

    sampleSmooth = smoothingCoef (20.0f, sampleRate);
    lengthSmooth = smoothingCoef (80.0f, sampleRate);
    fadeStep = 1.0f / (0.001f * kModeFadeMs * sampleRate);

    prepared = true;
    reset();
}

void ReverbEngine::reset()
{
    if (! prepared)
        return;

    predelayL.clear();
    predelayR.clear();
    firstControlUpdate = true;
    fadeDirection = 0;
    modeFade = 1.0f;
    configureMode (target.mode);
    updateControl (kControlBlock);
}

void ReverbEngine::configureMode (Mode m)
{
    activeMode = m;
    const ModeSpec& s = spec (m);
    const float msToSamples = 0.001f * sampleRate;

    for (int k = 0; k < numLines; ++k)
    {
        const float t = (float) k / (float) (numLines - 1);
        const float ms = s.minMs * std::pow (s.maxMs / s.minMs, t) * (1.0f + 0.06f * kLineJitter[k]);
        baseLength[(size_t) kLinePermutation[k]] = ms * msToSamples;
    }

    for (int i = 0; i < numLines; ++i)
    {
        const float apMs = std::min (kMaxLateApMs, s.lateApMs * (0.6f + 0.8f * kLateApFraction[i]));
        lateAp[(size_t) i].setDelay ((int) (apMs * msToSamples));
        lateAp[(size_t) i].clear();
        lines[(size_t) i].clear();
        lowState[(size_t) i] = highState[(size_t) i] = bandwidthState[(size_t) i] = 0.0f;
        lfos[(size_t) i].seed (0x1234567u + 7919u * (uint32_t) i + 104729u * (uint32_t) m);
        lfos[(size_t) i].setPhase ((float) i / (float) numLines);
        lfoRateSpread[(size_t) i] = 1.0f + 0.35f * kLfoSpread[i];
    }

    for (int i = 0; i < 4; ++i)
    {
        const float scale = std::clamp (s.earlyApScale, 0.3f, 1.2f);
        earlyApL[(size_t) i].setDelay ((int) (kEarlyApMs[i] * scale * msToSamples));
        earlyApR[(size_t) i].setDelay ((int) (kEarlyApMs[i] * scale * kEarlyApSkewR * msToSamples));
        earlyApL[(size_t) i].clear();
        earlyApR[(size_t) i].clear();
    }

    for (int i = 0; i < 3; ++i)
    {
        gateApL[(size_t) i].setDelay ((int) (kGateApMs[i] * msToSamples));
        gateApR[(size_t) i].setDelay ((int) (kGateApMs[i] * 1.07f * msToSamples));
        gateApL[(size_t) i].clear();
        gateApR[(size_t) i].clear();
    }

    diffusedL.clear();
    diffusedR.clear();

    Rng rng (0xC0FFEEu + 31337u * (uint32_t) m);

    // Early reflections: clustered towards the start, random polarity, energy normalised.
    auto makeEr = [&rng] (std::array<float, numErTaps>& pos, std::array<float, numErTaps>& gain)
    {
        float energy = 0.0f;
        for (int j = 0; j < numErTaps; ++j)
        {
            const float frac = std::clamp (((float) j + 0.5f + 0.4f * rng.bipolar()) / (float) numErTaps, 0.0f, 1.0f);
            pos[(size_t) j] = std::pow (frac, 1.4f);
            const float sign = rng.uniform() < 0.5f ? -1.0f : 1.0f;
            gain[(size_t) j] = sign * (1.0f - 0.75f * (float) j / (float) numErTaps);
            energy += gain[(size_t) j] * gain[(size_t) j];
        }
        const float norm = 1.0f / std::sqrt (energy);
        for (auto& g : gain)
            g *= norm;
    };
    makeEr (erPosL, erGainL);
    makeEr (erPosR, erGainR);
    erLevel = s.erLevel;

    // Nonlinear gate cloud: random positions in the window, random polarity.
    auto makeGate = [&rng] (std::array<float, numGateTaps>& pos, std::array<float, numGateTaps>& shape)
    {
        for (int j = 0; j < numGateTaps; ++j)
        {
            pos[(size_t) j] = std::clamp (((float) j + 0.5f + 0.45f * rng.bipolar()) / (float) numGateTaps, 0.001f, 1.0f);
            shape[(size_t) j] = rng.uniform() < 0.5f ? -1.0f : 1.0f;
        }
    };
    makeGate (gatePosL, gateShapeL);
    makeGate (gatePosR, gateShapeR);

    shifter.clear();
    shimmerHpState.reset();
    shimmerFeedback = 0.0f;

    lowCutL.reset();
    lowCutR.reset();
    highCutL.reset();
    highCutR.reset();
    colorLpL1.reset();
    colorLpL2.reset();
    colorLpR1.reset();
    colorLpR2.reset();
    tiltL.reset();
    tiltR.reset();

    outputGain = s.outGain;

    // Jump straight to the current size so a mode change doesn't sweep the delays.
    const float sizeScale = 0.1f + 0.9f * std::clamp (target.size, 0.0f, 1.0f);
    for (int i = 0; i < numLines; ++i)
        lengthSm[(size_t) i] = lengthTarget[(size_t) i] = baseLength[(size_t) i] * sizeScale;
}

void ReverbEngine::updateControl (int blockLength)
{
    // Mode-change state machine (fade the wet signal out, rebuild, fade back in).
    if (fadeDirection == 0 && target.mode != activeMode)
        fadeDirection = -1;
    if (fadeDirection < 0 && modeFade <= 0.0f)
    {
        configureMode (target.mode);
        fadeDirection = 1;
    }
    if (fadeDirection > 0 && modeFade >= 1.0f)
        fadeDirection = 0;

    const ModeSpec& s = spec (activeMode);
    const ReverbParams& p = target;
    const float msToSamples = 0.001f * sampleRate;

    // Block-rate smoothing of the "slow" controls.
    const float a = firstControlUpdate ? 1.0f : 1.0f - std::exp (-(float) blockLength / (0.03f * sampleRate));
    auto smooth = [a] (float& v, float t) { v += a * (t - v); };
    smooth (decaySm, std::clamp (p.decaySeconds, 0.1f, 60.0f));
    smooth (bassMultSm, std::clamp (p.bassMult, 0.1f, 8.0f));
    smooth (highShelfSm, std::clamp (p.highShelfDb, -48.0f, 0.0f));
    smooth (bassXoverSm, std::clamp (p.bassXoverHz, 20.0f, 5000.0f));
    smooth (highFreqSm, std::clamp (p.highFreqHz, 200.0f, 20000.0f));
    smooth (highCutSm, std::clamp (p.highCutHz, 200.0f, 22000.0f));
    smooth (lowCutSm, std::clamp (p.lowCutHz, 5.0f, 5000.0f));
    smooth (modRateSm, std::clamp (p.modRateHz, 0.01f, 10.0f));
    smooth (modDepthSm, std::clamp (p.modDepth, 0.0f, 1.0f));
    smooth (earlyDiffSm, std::clamp (p.earlyDiffusion, 0.0f, 1.0f));
    smooth (lateDiffSm, std::clamp (p.lateDiffusion, 0.0f, 1.0f));

    const float size = std::clamp (p.size, 0.0f, 1.0f);
    const float sizeScale = 0.1f + 0.9f * size;
    const float attack = std::clamp (p.attack, 0.0f, 1.0f);

    // Per-sample smoothed targets.
    mixTarget = std::clamp (p.mix, 0.0f, 1.0f);
    widthTarget = std::clamp (p.width, 0.0f, 1.0f);
    predelayTarget = 1.0f + std::clamp (p.predelayMs, 0.0f, kMaxPredelayMs) * msToSamples;
    attackMinSamples = 0.5f * msToSamples;
    attackWindowTarget = (3.0f + attack * s.attackMaxMs) * (0.4f + 0.6f * size) * msToSamples;
    erSpanTarget = s.erSpanMs * (0.3f + 0.7f * size) * msToSamples;
    gateWindowTarget = std::clamp (decaySm * 0.15f, 0.04f, 1.2f) * (0.6f + 0.4f * size) * sampleRate;
    for (int i = 0; i < numLines; ++i)
        lengthTarget[(size_t) i] = baseLength[(size_t) i] * sizeScale;

    // Colour era.
    float colorBandwidth = 0.0f, colorOut = 0.0f, colorDrive = 0.0f, colorDepth = 1.0f, colorShape = 0.0f;
    switch (p.color)
    {
        case Color::Vintage:
            colorBandwidth = 7000.0f;
            colorOut = 8500.0f;
            colorDrive = 1.2f;
            colorDepth = 1.3f;
            colorShape = 0.3f;
            break;
        case Color::Classic:
            colorBandwidth = 11000.0f;
            colorOut = 13000.0f;
            colorDepth = 1.15f;
            colorShape = 0.2f;
            break;
        case Color::Modern:
        case Color::count:
            break;
    }
    cubic = (p.color == Color::Modern);

    float bandwidth = s.bandwidthHz;
    if (colorBandwidth > 0.0f)
        bandwidth = bandwidth > 0.0f ? std::min (bandwidth, colorBandwidth) : colorBandwidth;
    bandwidthCoef = bandwidth > 0.0f ? onePoleCoef (std::min (bandwidth, 0.45f * sampleRate), sampleRate) : 1.0f;
    colorLowpassCoef = colorOut > 0.0f ? onePoleCoef (std::min (colorOut, 0.45f * sampleRate), sampleRate) : 1.0f;
    colorLpL1.setCoef (colorLowpassCoef);
    colorLpL2.setCoef (colorLowpassCoef);
    colorLpR1.setCoef (colorLowpassCoef);
    colorLpR2.setCoef (colorLowpassCoef);
    drive = std::max (s.drive, colorDrive);
    outputTilt = s.tilt;
    lfoShape = std::clamp (s.lfoShape + colorShape, 0.0f, 1.0f);

    // Diffusion.
    earlyCoef = earlyDiffSm * s.earlyDiffMax;
    lateCoef = lateDiffSm * s.lateDiffMax;

    // Modulation.
    modDepthSamples = modDepthSm * kMaxModDepthMs * s.modDepthScale * colorDepth * msToSamples;
    for (int i = 0; i < numLines; ++i)
        lfos[(size_t) i].setRate (modRateSm * s.modRateScale * lfoRateSpread[(size_t) i], sampleRate);

    // Three-band decay: each band's per-pass gain follows from its RT60 and the loop length.
    const float rtMid = decaySm;
    const float rtLow = std::min (200.0f, decaySm * bassMultSm);
    const float rtHigh = decaySm * std::pow (10.0f, highShelfSm / 24.0f);
    float averageLoop = 0.0f;
    for (int i = 0; i < numLines; ++i)
    {
        const float loop = lengthSm[(size_t) i] + (float) lateAp[(size_t) i].getDelay();
        averageLoop += loop;
        const float k = -kLn1000 * loop / sampleRate;
        gainLow[(size_t) i] = std::exp (k / rtLow);
        gainMid[(size_t) i] = std::exp (k / rtMid);
        gainHigh[(size_t) i] = std::exp (k / rtHigh);
    }
    averageLoop /= (float) numLines;

    const float lowSplit = std::min (bassXoverSm, 0.3f * sampleRate);
    const float highSplit = std::min (std::max (highFreqSm, 1.5f * lowSplit), 0.45f * sampleRate);
    lowSplitCoef = onePoleCoef (lowSplit, sampleRate);
    highSplitCoef = onePoleCoef (highSplit, sampleRate);

    // Shimmer feedback is scaled so the estimated loop power gain stays well below unity.
    if (s.shimmer)
    {
        const float tankEnergyGain = rtMid * sampleRate / (2.0f * kLn1000 * (float) numLines * averageLoop);
        shimmerGain = std::min (0.7f, std::sqrt (0.5f / std::max (1.0e-3f, 0.5f * tankEnergyGain)));
    }
    else
    {
        shimmerGain = 0.0f;
    }

    // Attack build-up taps: front-loaded for low attack, back-loaded for high attack.
    {
        const float alpha = (attack - 0.5f) * 5.0f;
        float energy = 0.0f;
        for (int k = 0; k < numAttackTaps; ++k)
        {
            const float t = (float) k / (float) (numAttackTaps - 1);
            attackTapPos[(size_t) k] = std::clamp (t * (1.0f + kAttackJitter[k]), 0.0f, 1.0f);
            attackTapGain[(size_t) k] = std::exp (alpha * (t - 0.5f));
            energy += attackTapGain[(size_t) k] * attackTapGain[(size_t) k];
        }
        const float norm = 1.0f / std::sqrt (energy);
        for (auto& g : attackTapGain)
            g *= norm;
    }

    // Nonlinear envelope: decaying (attack 0), flat gate (0.5), reverse (1).
    if (s.nonlinear)
    {
        auto shapeGate = [attack] (const std::array<float, numGateTaps>& pos, const std::array<float, numGateTaps>& sign,
                                   std::array<float, numGateTaps>& gain)
        {
            float energy = 0.0f;
            for (int j = 0; j < numGateTaps; ++j)
            {
                const float f = pos[(size_t) j];
                const float env = (1.0f - attack) * (1.0f - 0.85f * f) + attack * (0.15f + 0.85f * f);
                gain[(size_t) j] = sign[(size_t) j] * env;
                energy += env * env;
            }
            const float norm = 1.0f / std::sqrt (energy);
            for (auto& g : gain)
                g *= norm;
        };
        shapeGate (gatePosL, gateShapeL, gateGainL);
        shapeGate (gatePosR, gateShapeR, gateGainR);
    }

    // Output EQ.
    lowCutL.setHighpass (sampleRate, lowCutSm, 0.7071f);
    lowCutR.setHighpass (sampleRate, lowCutSm, 0.7071f);
    highCutL.setLowpass (sampleRate, highCutSm, 0.7071f);
    highCutR.setLowpass (sampleRate, highCutSm, 0.7071f);

    if (firstControlUpdate)
    {
        mixSm = mixTarget;
        widthSm = widthTarget;
        predelaySm = predelayTarget;
        attackWindowSm = attackWindowTarget;
        erSpanSm = erSpanTarget;
        gateWindowSm = gateWindowTarget;
        lengthSm = lengthTarget;
        firstControlUpdate = false;
    }
}

void ReverbEngine::process (float* left, float* right, int numSamples)
{
    if (! prepared)
        return;

    int pos = 0;
    while (pos < numSamples)
    {
        const int n = std::min (kControlBlock, numSamples - pos);
        updateControl (n);
        processChunk (left + pos, right + pos, n);
        pos += n;
    }
}

void ReverbEngine::processChunk (float* left, float* right, int numSamples)
{
    const ModeSpec& s = spec (activeMode);
    const bool nonlinear = s.nonlinear;
    const bool shimmer = s.shimmer;
    const float maxDiffused = (float) diffusedL.capacity() - 2.0f;

    float inject[numLines];
    float y[numLines];

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryL = left[n];
        const float dryR = right[n];

        mixSm += sampleSmooth * (mixTarget - mixSm);
        if (std::abs (mixTarget - mixSm) < 1.0e-6f)
            mixSm = mixTarget;
        widthSm += sampleSmooth * (widthTarget - widthSm);
        predelaySm += sampleSmooth * (predelayTarget - predelaySm);
        attackWindowSm += sampleSmooth * (attackWindowTarget - attackWindowSm);
        erSpanSm += sampleSmooth * (erSpanTarget - erSpanSm);
        gateWindowSm += sampleSmooth * (gateWindowTarget - gateWindowSm);

        if (fadeDirection != 0)
            modeFade = std::clamp (modeFade + (float) fadeDirection * fadeStep, 0.0f, 1.0f);

        // Predelay.
        float l = predelayL.readLinear (predelaySm);
        float r = predelayR.readLinear (predelaySm);
        predelayL.push (dryL);
        predelayR.push (dryR);

        // Early diffusion.
        for (int i = 0; i < 4; ++i)
        {
            l = earlyApL[(size_t) i].process (l, earlyCoef);
            r = earlyApR[(size_t) i].process (r, earlyCoef);
        }

        // Early reflections.
        float erL = 0.0f, erR = 0.0f;
        if (erLevel > 0.0f)
        {
            const float base = 0.001f * sampleRate;
            for (int j = 0; j < numErTaps; ++j)
            {
                erL += erGainL[(size_t) j] * diffusedL.readLinear (base + erPosL[(size_t) j] * erSpanSm);
                erR += erGainR[(size_t) j] * diffusedR.readLinear (base + erPosR[(size_t) j] * erSpanSm);
            }
            erL *= erLevel;
            erR *= erLevel;
        }

        float wetL = 0.0f, wetR = 0.0f;

        if (nonlinear)
        {
            const float window = std::min (gateWindowSm, maxDiffused);
            for (int j = 0; j < numGateTaps; ++j)
            {
                wetL += gateGainL[(size_t) j] * diffusedL.readLinear (1.0f + gatePosL[(size_t) j] * window);
                wetR += gateGainR[(size_t) j] * diffusedR.readLinear (1.0f + gatePosR[(size_t) j] * window);
            }
            for (int i = 0; i < 3; ++i)
            {
                wetL = gateApL[(size_t) i].process (wetL, lateCoef);
                wetR = gateApR[(size_t) i].process (wetR, lateCoef);
            }
        }
        else
        {
            // Attack build-up: each tap feeds one left and one right line.
            for (int k = 0; k < numAttackTaps; ++k)
            {
                const float d = attackMinSamples + attackTapPos[(size_t) k] * attackWindowSm;
                const float g = attackTapGain[(size_t) k];
                inject[2 * k] = g * diffusedL.readLinear (d);
                inject[2 * k + 1] = ((k & 1) ? -g : g) * diffusedR.readLinear (d);
            }

            if (shimmer)
                for (int i = 0; i < numLines; ++i)
                    inject[i] += 0.25f * kSigns.shimmer[i] * shimmerFeedback;

            // Read, damp and diffuse each line.
            for (int i = 0; i < numLines; ++i)
            {
                float& len = lengthSm[(size_t) i];
                len += lengthSmooth * (lengthTarget[(size_t) i] - len);
                const float depth = std::min (modDepthSamples, 0.45f * len);
                const float d = std::max (2.0f, len + depth * lfos[(size_t) i].process (lfoShape));
                float v = cubic ? lines[(size_t) i].readCubic (d) : lines[(size_t) i].readLinear (d);

                float& lo = lowState[(size_t) i];
                float& hi = highState[(size_t) i];
                lo += lowSplitCoef * (v - lo);
                hi += highSplitCoef * (v - hi);
                v = gainLow[(size_t) i] * lo + gainMid[(size_t) i] * (hi - lo) + gainHigh[(size_t) i] * (v - hi);

                if (bandwidthCoef < 1.0f)
                {
                    float& bw = bandwidthState[(size_t) i];
                    bw += bandwidthCoef * (v - bw);
                    v = bw;
                }
                if (drive > 0.0f)
                    v = softClip (v * drive) / drive;

                y[i] = lateAp[(size_t) i].process (v, lateCoef);
            }

            for (int i = 0; i < numLines; ++i)
            {
                wetL += kSigns.outL[i] * y[i];
                wetR += kSigns.outR[i] * y[i];
            }
            wetL *= 0.25f;
            wetR *= 0.25f;

            hadamard16 (y);
            for (int i = 0; i < numLines; ++i)
                lines[(size_t) i].push (y[i] + inject[i]);

            if (shimmer)
            {
                const float shifted = shifter.process (0.5f * (wetL + wetR), 2.0f);
                const float hp = shifted - shimmerHpState.process (shifted);
                shimmerFeedback = softClip (hp * shimmerGain);
            }
        }

        diffusedL.push (l);
        diffusedR.push (r);

        wetL = wetL * outputGain + erL;
        wetR = wetR * outputGain + erR;

        // Stereo width (mid/side).
        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * widthSm;
        wetL = mid + side;
        wetR = mid - side;

        // Output EQ and colour.
        wetL = highCutL.process (lowCutL.process (wetL));
        wetR = highCutR.process (lowCutR.process (wetR));
        if (colorLowpassCoef < 1.0f)
        {
            wetL = colorLpL2.process (colorLpL1.process (wetL));
            wetR = colorLpR2.process (colorLpR1.process (wetR));
        }
        if (outputTilt != 0.0f)
        {
            wetL += outputTilt * (wetL - tiltL.process (wetL));
            wetR += outputTilt * (wetR - tiltR.process (wetR));
        }

        // Equal-power dry/wet (trig only while the mix control is moving).
        if (mixSm != mixGainsFor)
        {
            mixGainsFor = mixSm;
            dryGain = std::cos (0.5f * kPi * mixSm);
            wetGain = std::sin (0.5f * kPi * mixSm);
        }
        const float wet = wetGain * modeFade;
        left[n] = dryGain * dryL + wet * wetL;
        right[n] = dryGain * dryR + wet * wetR;
    }
}

} // namespace gr
