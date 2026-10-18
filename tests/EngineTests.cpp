// Headless tests for the Good Reverb DSP engine (no JUCE required).
//
//   GoodReverbTests                 run all checks
//   GoodReverbTests --render <dir>  also write a stereo impulse response WAV per mode

#include "dsp/ReverbEngine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    #include <xmmintrin.h>
    #define GR_HAS_SSE 1
#endif

using namespace gr;

namespace
{
int failures = 0;
int checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

struct Stereo
{
    std::vector<float> l, r;
};

ReverbParams flatParams (Mode mode, Color color, float decay)
{
    ReverbParams p;
    p.mode = mode;
    p.color = color;
    p.mix = 1.0f;
    p.predelayMs = 0.0f;
    p.decaySeconds = decay;
    p.bassMult = 1.0f;
    p.highShelfDb = 0.0f;
    p.lowCutHz = 10.0f;
    p.highCutHz = 20000.0f;
    p.modDepth = 0.3f;
    return p;
}

// Renders numSamples of output for the given input generator.
Stereo render (ReverbEngine& engine, int numSamples, const std::function<float (int, int)>& input, int blockSize = 256)
{
    Stereo out;
    out.l.resize ((size_t) numSamples);
    out.r.resize ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        out.l[(size_t) i] = input (i, 0);
        out.r[(size_t) i] = input (i, 1);
    }
    for (int pos = 0; pos < numSamples; pos += blockSize)
        engine.process (out.l.data() + pos, out.r.data() + pos, std::min (blockSize, numSamples - pos));
    return out;
}

bool allFinite (const Stereo& s, float limit, float* peakOut = nullptr)
{
    float peak = 0.0f;
    bool ok = true;
    for (size_t i = 0; i < s.l.size(); ++i)
    {
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            ok = false;
        peak = std::max (peak, std::max (std::abs (s.l[i]), std::abs (s.r[i])));
    }
    if (peakOut)
        *peakOut = peak;
    return ok && peak < limit;
}

// RT60 from the Schroeder energy decay curve, fitted between -5 and -35 dB.
float measureRt60 (const Stereo& s, float sampleRate)
{
    const size_t n = s.l.size();
    std::vector<double> edc (n);
    double acc = 0.0;
    for (size_t i = n; i-- > 0;)
    {
        acc += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
        edc[i] = acc;
    }
    if (acc <= 0.0)
        return 0.0f;

    double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
    int count = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / edc[0] + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double x = (double) i / sampleRate;
            sumX += x;
            sumY += db;
            sumXX += x * x;
            sumXY += x * db;
            ++count;
        }
    }
    if (count < 10)
        return 0.0f;
    const double slope = (count * sumXY - sumX * sumY) / (count * sumXX - sumX * sumX);
    return slope < 0.0 ? (float) (-60.0 / slope) : 0.0f;
}

double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to; ++i)
        acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) std::max<size_t> (1, to - from));
}

void writeWav (const std::string& path, const Stereo& s, int sampleRate)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&f] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&f] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const uint32_t frames = (uint32_t) s.l.size();
    const uint32_t dataBytes = frames * 2 * 4;
    f.write ("RIFF", 4);
    u32 (36 + dataBytes);
    f.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (3); // IEEE float
    u16 (2);
    u32 ((uint32_t) sampleRate);
    u32 ((uint32_t) sampleRate * 8);
    u16 (8);
    u16 (32);
    f.write ("data", 4);
    u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        f.write (reinterpret_cast<const char*> (&s.l[i]), 4);
        f.write (reinterpret_cast<const char*> (&s.r[i]), 4);
    }
}

auto impulse = [] (int i, int) { return i == 0 ? 1.0f : 0.0f; };

void testDecayAccuracy()
{
    std::printf ("[decay accuracy] RT60 vs Decay setting (Modern colour, flat decay EQ)\n");
    const float fs = 48000.0f;
    for (int m = 0; m < (int) Mode::count; ++m)
    {
        const Mode mode = (Mode) m;
        if (mode == Mode::Nonlinear || mode == Mode::DirtyHall || mode == Mode::Shimmer)
            continue; // gated, band-limited or regenerative by design
        for (float decay : { 1.0f, 3.0f, 8.0f })
        {
            ReverbEngine e;
            e.setParams (flatParams (mode, Color::Modern, decay));
            e.prepare (fs, 256);
            const auto out = render (e, (int) (fs * std::max (4.0f, decay * 1.2f)), impulse);
            const float rt = measureRt60 (out, fs);
            std::printf ("  %-13s decay %4.1fs -> RT60 %5.2fs\n", modeName (mode), decay, rt);
            check (rt > decay * 0.75f && rt < decay * 1.3f,
                   std::string (modeName (mode)) + " RT60 " + std::to_string (rt) + " for decay " + std::to_string (decay));
        }
    }
}

void testAllModesAndColours()
{
    std::printf ("[impulse] every mode x colour stays finite and bounded\n");
    const float fs = 48000.0f;
    for (int m = 0; m < (int) Mode::count; ++m)
        for (int c = 0; c < (int) Color::count; ++c)
        {
            ReverbEngine e;
            auto p = flatParams ((Mode) m, (Color) c, 2.5f);
            p.bassMult = 1.5f;
            p.highShelfDb = -6.0f;
            e.setParams (p);
            e.prepare (fs, 512);
            const auto out = render (e, (int) fs * 5, impulse);
            float peak = 0.0f;
            const bool ok = allFinite (out, 2.0f, &peak);
            check (ok, std::string (modeName ((Mode) m)) + "/" + colorName ((Color) c) + " peak " + std::to_string (peak));
            const double tail = rms (out.l, out.l.size() - 4800, out.l.size());
            check (tail < 1e-3, std::string (modeName ((Mode) m)) + " tail did not decay");
        }
}

void testStress()
{
    std::printf ("[stress] max decay, extreme EQ, max modulation, noise burst + 25 s tail\n");
    for (float fs : { 44100.0f, 96000.0f })
        for (int m = 0; m < (int) Mode::count; ++m)
            for (float size : { 0.0f, 1.0f })
            {
                ReverbEngine e;
                auto p = flatParams ((Mode) m, Color::Modern, 30.0f);
                p.size = size;
                p.bassMult = 4.0f;
                p.bassXoverHz = 2000.0f;
                p.highFreqHz = 1000.0f;
                p.modDepth = 1.0f;
                p.modRateHz = 5.0f;
                p.earlyDiffusion = 1.0f;
                p.lateDiffusion = 1.0f;
                e.setParams (p);
                e.prepare (fs, 512);
                Rng rng (42);
                const int burst = (int) (fs * 3);
                const auto out = render (e, (int) (fs * 28), [&] (int i, int) { return i < burst ? 0.5f * rng.bipolar() : 0.0f; }, 512);
                float peak = 0.0f;
                const bool ok = allFinite (out, 8.0f, &peak);
                check (ok, std::string (modeName ((Mode) m)) + " stress at " + std::to_string ((int) fs) + " Hz size " +
                               std::to_string (size) + " peak " + std::to_string (peak));
                // The tail must keep decaying (no runaway): last second quieter than the second after the burst.
                const size_t n = out.l.size();
                const double early = rms (out.l, (size_t) burst, (size_t) burst + (size_t) fs);
                const double late = rms (out.l, n - (size_t) fs, n);
                check (late <= early * 1.01 + 1e-9, std::string (modeName ((Mode) m)) + " tail grows under stress");
            }
}

void testSampleRates()
{
    std::printf ("[sample rates] RT60 holds across sample rates (Hall, 2 s)\n");
    for (float fs : { 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f })
    {
        ReverbEngine e;
        e.setParams (flatParams (Mode::Hall, Color::Modern, 2.0f));
        e.prepare (fs, 512);
        const auto out = render (e, (int) (fs * 4), impulse, 512);
        const float rt = measureRt60 (out, fs);
        std::printf ("  %6.0f Hz -> RT60 %.2fs\n", fs, rt);
        check (allFinite (out, 2.0f), "finite at " + std::to_string (fs));
        check (rt > 1.5f && rt < 2.6f, "RT60 at " + std::to_string (fs) + " = " + std::to_string (rt));
    }
}

void testDryPassThrough()
{
    std::printf ("[mix] mix = 0 is a bit-exact bypass\n");
    ReverbEngine e;
    auto p = flatParams (Mode::Hall, Color::Modern, 2.0f);
    p.mix = 0.0f;
    e.setParams (p);
    e.prepare (48000.0, 256);
    Rng rng (7);
    std::vector<float> ref (48000);
    for (auto& v : ref)
        v = rng.bipolar();
    const auto out = render (e, 48000, [&] (int i, int) { return ref[(size_t) i]; });
    double maxErr = 0.0;
    for (size_t i = 0; i < ref.size(); ++i)
        maxErr = std::max (maxErr, (double) std::abs (out.l[i] - ref[i]));
    check (maxErr < 1e-7, "dry path error " + std::to_string (maxErr));
}

void testAutomationAndModeSwitching()
{
    std::printf ("[automation] random parameter changes and mode switches every block\n");
    ReverbEngine e;
    e.prepare (48000.0, 128);
    Rng rng (99);
    std::vector<float> l (128), r (128);
    float peak = 0.0f, maxStep = 0.0f, prev = 0.0f;
    bool finite = true;
    for (int block = 0; block < 4000; ++block)
    {
        if (block % 8 == 0)
        {
            ReverbParams p;
            p.mix = rng.uniform();
            p.predelayMs = 500.0f * rng.uniform();
            p.decaySeconds = 0.2f + 29.8f * rng.uniform();
            p.size = rng.uniform();
            p.attack = rng.uniform();
            p.earlyDiffusion = rng.uniform();
            p.lateDiffusion = rng.uniform();
            p.modRateHz = 0.05f + 4.95f * rng.uniform();
            p.modDepth = rng.uniform();
            p.highCutHz = 500.0f + 19500.0f * rng.uniform();
            p.lowCutHz = 10.0f + 1990.0f * rng.uniform();
            p.bassMult = 0.25f + 3.75f * rng.uniform();
            p.bassXoverHz = 100.0f + 1900.0f * rng.uniform();
            p.highShelfDb = -24.0f * rng.uniform();
            p.highFreqHz = 1000.0f + 15000.0f * rng.uniform();
            p.width = rng.uniform();
            p.mode = (Mode) (rng.next() % (uint32_t) Mode::count);
            p.color = (Color) (rng.next() % (uint32_t) Color::count);
            e.setParams (p);
        }
        // Quiet sine so sudden jumps would stand out.
        for (int i = 0; i < 128; ++i)
        {
            const float x = 0.25f * std::sin (2.0f * kPi * 220.0f * (float) (block * 128 + i) / 48000.0f);
            l[(size_t) i] = x;
            r[(size_t) i] = x;
        }
        e.process (l.data(), r.data(), 128);
        for (int i = 0; i < 128; ++i)
        {
            finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
            peak = std::max (peak, std::abs (l[(size_t) i]));
            maxStep = std::max (maxStep, std::abs (l[(size_t) i] - prev));
            prev = l[(size_t) i];
        }
    }
    std::printf ("  peak %.3f, largest sample-to-sample step %.3f\n", peak, maxStep);
    check (finite, "non-finite output during automation");
    check (peak < 4.0f, "peak during automation " + std::to_string (peak));
}

void testNonlinearGateCloses()
{
    std::printf ("[nonlinear] the gate actually closes\n");
    ReverbEngine e;
    auto p = flatParams (Mode::Nonlinear, Color::Modern, 2.0f); // window = 0.3 s * (0.6 + 0.4 * size)
    p.attack = 0.5f;
    e.setParams (p);
    e.prepare (48000.0, 256);
    const auto out = render (e, 48000 * 2, impulse);
    const double body = rms (out.l, 0, 48000 / 4);
    const double after = rms (out.l, 48000 / 2, 48000);
    std::printf ("  body %.2e, after gate %.2e\n", body, after);
    check (body > 1e-3, "nonlinear produced no output");
    check (after < body * 1e-3, "nonlinear gate leaks");
}

void testLoudness()
{
    std::printf ("[loudness] wet RMS relative to dry for pink-ish noise, decay 2.5 s (default params)\n");
    for (int m = 0; m < (int) Mode::count; ++m)
    {
        ReverbEngine e;
        ReverbParams p;
        p.mode = (Mode) m;
        p.mix = 1.0f;
        e.setParams (p);
        e.prepare (48000.0, 256);
        Rng rng (3);
        float lp = 0.0f;
        std::vector<float> in (48000 * 6);
        for (auto& v : in)
        {
            lp += 0.2f * (rng.bipolar() - lp);
            v = lp;
        }
        const auto out = render (e, (int) in.size(), [&] (int i, int) { return in[(size_t) i]; });
        const double dryRms = rms (in, 48000 * 2, in.size());
        const double wetRms = 0.5 * (rms (out.l, 48000 * 2, in.size()) + rms (out.r, 48000 * 2, in.size()));
        const double db = 20.0 * std::log10 (wetRms / dryRms);
        std::printf ("  %-13s %+5.1f dB\n", modeName ((Mode) m), db);
        check (db > -15.0 && db < 9.0, std::string (modeName ((Mode) m)) + " loudness " + std::to_string (db));
    }
}

void testStereo()
{
    std::printf ("[stereo] width 1 decorrelates, width 0 is mono\n");
    for (float width : { 1.0f, 0.0f })
    {
        ReverbEngine e;
        auto p = flatParams (Mode::Hall, Color::Modern, 2.0f);
        p.width = width;
        e.setParams (p);
        e.prepare (48000.0, 256);
        const auto out = render (e, 48000 * 2, impulse);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            lr += (double) out.l[i] * out.r[i];
            ll += (double) out.l[i] * out.l[i];
            rr += (double) out.r[i] * out.r[i];
        }
        const double corr = lr / std::sqrt (ll * rr + 1e-30);
        std::printf ("  width %.0f -> L/R correlation %.3f\n", width, corr);
        if (width > 0.5f)
            check (std::abs (corr) < 0.3, "wide output too correlated: " + std::to_string (corr));
        else
            check (corr > 0.999, "mono output not mono: " + std::to_string (corr));
    }
}

void testPerformance()
{
    std::printf ("[performance] realtime factor at 48 kHz, 256-sample blocks\n");
    for (Mode mode : { Mode::Hall, Mode::Nonlinear, Mode::Shimmer })
    {
        ReverbEngine e;
        ReverbParams p;
        p.mode = mode;
        e.setParams (p);
        e.prepare (48000.0, 256);
        const int seconds = 10;
        std::vector<float> l (256, 0.1f), r (256, 0.1f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < seconds * 48000 / 256; ++b)
            e.process (l.data(), r.data(), 256);
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %-13s %.0fx realtime\n", modeName (mode), seconds / elapsed);
        check (seconds / elapsed > 20.0, std::string ("too slow: ") + modeName (mode));
    }
}

void renderImpulses (const std::string& dir)
{
    std::printf ("[render] writing impulse responses to %s\n", dir.c_str());
    for (int m = 0; m < (int) Mode::count; ++m)
    {
        ReverbEngine e;
        ReverbParams p;
        p.mode = (Mode) m;
        p.mix = 1.0f;
        e.setParams (p);
        e.prepare (48000.0, 256);
        const auto out = render (e, 48000 * 5, impulse);
        std::string name = modeName ((Mode) m);
        for (auto& ch : name)
            if (ch == ' ')
                ch = '_';
        writeWav (dir + "/" + name + ".wav", out, 48000);
    }
}
} // namespace

int main (int argc, char** argv)
{
#if GR_HAS_SSE
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals to zero, like the plugin does
#endif

    testDryPassThrough();
    testStereo();
    testDecayAccuracy();
    testSampleRates();
    testAllModesAndColours();
    testNonlinearGateCloses();
    testLoudness();
    testAutomationAndModeSwitching();
    testStress();
    testPerformance();

    if (argc > 2 && std::strcmp (argv[1], "--render") == 0)
        renderImpulses (argv[2]);

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
