#pragma once

// Small, allocation-free building blocks used by the reverb engine.
// Everything here is plain C++17 so the DSP can be built and tested without JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gr
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

inline int nextPowerOfTwo (int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

// Coefficient for a one-pole lowpass with the given cutoff.
inline float onePoleCoef (float hz, float sampleRate)
{
    return 1.0f - std::exp (-kTwoPi * hz / sampleRate);
}

// Coefficient for a one-pole smoother reaching ~63% of a step after `ms`.
inline float smoothingCoef (float ms, float rate)
{
    return 1.0f - std::exp (-1.0f / (0.001f * ms * rate));
}

// Gentle saturator: unity slope at zero, never exceeds |1.5|.
inline float softClip (float x)
{
    x = std::clamp (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9e3779b9u) : state (seed ? seed : 1u) {}

    void seed (uint32_t s) { state = s ? s : 1u; }

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float uniform() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }                       // [-1, 1)

private:
    uint32_t state;
};

// Circular delay line. Reads happen *before* the write of the current sample,
// so read (d) returns the sample written d samples ago (d >= 1, cubic needs d >= 2).
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        const int size = nextPowerOfTwo (maxDelaySamples + 4);
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int capacity() const { return mask - 3; }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    float readInt (int d) const { return buffer[(size_t) ((writePos - d) & mask)]; }

    float readLinear (float d) const
    {
        const int di = (int) d;
        const float f = d - (float) di;
        const float a = buffer[(size_t) ((writePos - di) & mask)];
        const float b = buffer[(size_t) ((writePos - di - 1) & mask)];
        return a + f * (b - a);
    }

    // 4-point, 3rd-order Hermite interpolation.
    float readCubic (float d) const
    {
        const int di = (int) d;
        const float f = d - (float) di;
        const float xm1 = buffer[(size_t) ((writePos - di + 1) & mask)];
        const float x0 = buffer[(size_t) ((writePos - di) & mask)];
        const float x1 = buffer[(size_t) ((writePos - di - 1) & mask)];
        const float x2 = buffer[(size_t) ((writePos - di - 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

// Schroeder allpass: H(z) = (-g + z^-D) / (1 - g z^-D). Lossless for |g| < 1.
class Allpass
{
public:
    void prepare (int maxDelaySamples) { line.prepare (maxDelaySamples); }
    void clear() { line.clear(); }
    void setDelay (int samples) { delay = std::clamp (samples, 1, line.capacity()); }
    int getDelay() const { return delay; }

    float process (float x, float g)
    {
        const float delayed = line.readInt (delay);
        const float w = x + g * delayed;
        line.push (w);
        return delayed - g * w;
    }

private:
    DelayLine line;
    int delay = 1;
};

class OnePoleLowpass
{
public:
    void setCoef (float a) { coef = a; }
    void reset (float v = 0.0f) { z = v; }
    float process (float x)
    {
        z += coef * (x - z);
        return z;
    }
    float state() const { return z; }

private:
    float coef = 1.0f;
    float z = 0.0f;
};

// RBJ biquad, transposed direct form II.
class Biquad
{
public:
    void reset() { z1 = z2 = 0.0f; }

    void setLowpass (float sampleRate, float hz, float q) { design (sampleRate, hz, q, false); }
    void setHighpass (float sampleRate, float hz, float q) { design (sampleRate, hz, q, true); }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (float sampleRate, float hz, float q, bool highpass)
    {
        hz = std::clamp (hz, 5.0f, 0.49f * sampleRate);
        const float w0 = kTwoPi * hz / sampleRate;
        const float cw = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        if (highpass)
        {
            b0 = (1.0f + cw) * 0.5f / a0;
            b1 = -(1.0f + cw) / a0;
        }
        else
        {
            b0 = (1.0f - cw) * 0.5f / a0;
            b1 = (1.0f - cw) / a0;
        }
        b2 = b0;
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// sin (2 pi phase) for phase in [0, 1); parabolic approximation, error < 0.1%.
inline float fastSine (float phase)
{
    const float u = 0.5f - phase; // sin (2 pi phase) == sin (2 pi u) for u = 0.5 - phase
    float y = 8.0f * u - 16.0f * u * std::abs (u);
    return 0.225f * (y * std::abs (y) - y) + y;
}

// LFO producing a sine, a smoothed random walk, or a blend of the two.
class Lfo
{
public:
    void setRate (float hz, float sampleRate) { increment = hz / sampleRate; }
    void setPhase (float p) { phase = p - std::floor (p); }

    void seed (uint32_t s)
    {
        rng.seed (s);
        previous = rng.bipolar();
        target = rng.bipolar();
        randomPhase = rng.uniform();
    }

    // shapeBlend: 0 = pure sine, 1 = pure smoothed random.
    float process (float shapeBlend)
    {
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;

        randomPhase += increment;
        if (randomPhase >= 1.0f)
        {
            randomPhase -= 1.0f;
            previous = target;
            target = rng.bipolar();
        }

        const float t = randomPhase;
        const float smooth = t * t * (3.0f - 2.0f * t);
        const float random = previous + (target - previous) * smooth;
        const float sine = fastSine (phase);
        return sine + shapeBlend * (random - sine);
    }

private:
    Rng rng;
    float phase = 0.0f;
    float increment = 0.0f;
    float randomPhase = 0.0f;
    float previous = 0.0f;
    float target = 0.0f;
};

// Two-head granular pitch shifter (used for the octave-up shimmer).
class PitchShifter
{
public:
    void prepare (float sampleRate, float windowMs)
    {
        window = std::max (64.0f, 0.001f * windowMs * sampleRate);
        line.prepare ((int) window + 8);
        phase = 0.0f;
    }

    void clear()
    {
        line.clear();
        phase = 0.0f;
    }

    float process (float x, float ratio)
    {
        phase += (ratio - 1.0f) / window;
        phase -= std::floor (phase);
        float phase2 = phase + 0.5f;
        if (phase2 >= 1.0f)
            phase2 -= 1.0f;

        // Delay shrinks as phase grows, so each head reads faster than real time.
        const float d1 = 2.0f + (1.0f - phase) * window;
        const float d2 = 2.0f + (1.0f - phase2) * window;
        // sin/cos windows: w1^2 + w2^2 == 1, constant power for decorrelated heads.
        const float w1 = std::sin (kPi * phase);
        const float w2 = std::sin (kPi * phase2);
        const float y = w1 * line.readLinear (d1) + w2 * line.readLinear (d2);
        line.push (x);
        return y;
    }

private:
    DelayLine line;
    float window = 1024.0f;
    float phase = 0.0f;
};

// In-place orthonormal 16-point Walsh-Hadamard transform (the FDN feedback matrix).
inline void hadamard16 (float* x)
{
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h * 2)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j];
                const float b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    for (int i = 0; i < 16; ++i)
        x[i] *= 0.25f;
}

} // namespace gr
