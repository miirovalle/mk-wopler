// Small, dependency-free DSP primitives shared by the filters and the engine.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace wob
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double clampd (double v, double lo, double hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline double sanitise (double v) noexcept
{
    if (! std::isfinite (v))
        return 0.0;

    return clampd (v, -64.0, 64.0);
}

// Cheap tanh substitute: identical curve shape to std::tanh within ~1e-3.
inline double fastTanh (double x) noexcept
{
    if (x < -3.0) return -1.0;
    if (x >  3.0) return  1.0;

    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

inline double softClip (double x) noexcept
{
    return fastTanh (x);
}

//==============================================================================
// Topology-preserving state variable filter (Simper / Zavalishin form). Because
// the integrators are implicit, coefficients may be swept every sample without
// producing the blow-ups that a naive biquad update would produce - essential
// for an LFO-driven filter.
class Svf
{
public:
    void reset() noexcept
    {
        for (auto& s : st)
            s = { 0.0, 0.0 };
    }

    void setLowPass  (double c, double q, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setLowPassCh  (ch, c, q, sr); }
    void setHighPass (double c, double q, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setHighPassCh (ch, c, q, sr); }
    void setBandPass (double c, double q, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setBandPassCh (ch, c, q, sr); }
    void setNotch    (double c, double q, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setNotchCh    (ch, c, q, sr); }
    void setAllPass  (double c, double q, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setAllPassCh  (ch, c, q, sr); }

    void setBell (double c, double q, double g, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setBellCh (ch, c, q, g, sr); }

    void setLowShelf  (double c, double q, double g, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setLowShelfCh  (ch, c, q, g, sr); }
    void setHighShelf (double c, double q, double g, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setHighShelfCh (ch, c, q, g, sr); }

    void setLowPassCh  (int ch, double c, double q, double sr) noexcept { setSvf (ch, c, q, 0.0, 0.0, 1.0, sr); }
    void setHighPassCh (int ch, double c, double q, double sr) noexcept { const double kk = 1.0 / q; setSvf (ch, c, q, 1.0, -kk, -1.0, sr); }
    void setBandPassCh (int ch, double c, double q, double sr) noexcept { setSvf (ch, c, q, 0.0, 1.0, 0.0, sr); }
    void setNotchCh    (int ch, double c, double q, double sr) noexcept { const double kk = 1.0 / q; setSvf (ch, c, q, 1.0, -kk, 0.0, sr); }
    void setAllPassCh  (int ch, double c, double q, double sr) noexcept { const double kk = 1.0 / q; setSvf (ch, c, q, 1.0, -2.0 * kk, 0.0, sr); }

    void setBellCh (int ch, double c, double q, double g, double sr) noexcept
    {
        const double A = std::pow (10.0, g / 40.0);
        const double kk = 1.0 / (clampd (q, 0.05, 40.0) * A);
        setSvf (ch, c, 1.0 / kk, 1.0, kk * (A * A - 1.0), 0.0, sr);
    }

    void setLowShelfCh (int ch, double c, double q, double g, double sr) noexcept
    {
        const double A = std::pow (10.0, g / 40.0);
        const double kk = 1.0 / q;
        setSvf (ch, c, q, 1.0, kk * (A - 1.0), A * A - 1.0, sr);
    }

    void setHighShelfCh (int ch, double c, double q, double g, double sr) noexcept
    {
        const double A = std::pow (10.0, g / 40.0);
        const double kk = 1.0 / q;
        setSvf (ch, c, q, A * A, kk * (1.0 - A) * A, 1.0 - A * A, sr);
    }

    double process (int ch, double v0) noexcept
    {
        auto& s = st[(size_t) ch];
        const auto& c = cf[(size_t) ch];

        const double v3 = v0 - s[1];
        const double v1 = c.a1 * s[0] + c.a2 * v3;
        const double v2 = s[1] + c.a2 * s[0] + c.a3 * v3;

        s[0] = 2.0 * v1 - s[0];
        s[1] = 2.0 * v2 - s[1];

        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

private:
    struct Coeffs
    {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double m0 = 0.0, m1 = 0.0, m2 = 1.0;
    };

    void setSvf (int ch, double cutoff, double q, double m0_, double m1_, double m2_, double sr) noexcept
    {
        cutoff = clampd (cutoff, 5.0, sr * 0.49);
        q = clampd (q, 0.05, 40.0);

        const double g = std::tan (kPi * cutoff / sr);
        const double k = 1.0 / q;

        auto& c = cf[(size_t) ch];

        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        c.m0 = m0_;
        c.m1 = m1_;
        c.m2 = m2_;
    }

    std::array<Coeffs, 2> cf {};
    std::array<std::array<double, 2>, 2> st {};
};

//==============================================================================
// One-pole low/high pass (used for the 6 dB/oct slopes and as a smoothing aid).
class OnePole
{
public:
    void setCutoff (double cutoff, double sr) noexcept { for (int ch = 0; ch < 2; ++ch) setCutoffCh (ch, cutoff, sr); }

    void setCutoffCh (int ch, double cutoff, double sr) noexcept
    {
        const double g = std::tan (kPi * clampd (cutoff, 5.0, sr * 0.49) / sr);
        c[(size_t) ch] = 1.0 / (1.0 + g);
    }

    void reset() noexcept { ic = { 0.0, 0.0 }; }

    double lowPass (int ch, double x) noexcept
    {
        const double v = (x - ic[(size_t) ch]) * c[(size_t) ch];
        const double y = v + ic[(size_t) ch];
        ic[(size_t) ch] = y + v;
        return y;
    }

    double highPass (int ch, double x) noexcept
    {
        return x - lowPass (ch, x);
    }

private:
    std::array<double, 2> ic {};
    std::array<double, 2> c { 1.0, 1.0 };
};

//==============================================================================
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        size = std::max (maxSamples, 4);
        buf.assign ((size_t) (size * 2), 0.0);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0);
        writePos = 0;
    }

    void write (int ch, double v) noexcept { buf[(size_t) ((writePos * 2) + ch)] = sanitise (v); }

    double read (int ch, double delaySamples) const noexcept
    {
        delaySamples = clampd (delaySamples, 1.0, (double) size - 2.0);

        const double pos = (double) writePos - delaySamples;
        auto i0 = (int) std::floor (pos);
        const double frac = pos - (double) i0;

        while (i0 < 0)
            i0 += size;

        const int i1 = (i0 + 1) % size;

        const double a = buf[(size_t) ((i0 % size) * 2 + ch)];
        const double b = buf[(size_t) (i1 * 2 + ch)];

        return a + (b - a) * frac;
    }

    void advance() noexcept { writePos = (writePos + 1) % size; }

    int capacity() const noexcept { return size; }

private:
    std::vector<double> buf;
    int writePos = 0;
    int size = 0;
};

//==============================================================================
// All-pass section used by the diffuser kernels.
class AllPass1
{
public:
    void reset() noexcept {}

    double process (int ch, double x, double gCoef, double delaySamples, DelayLine& dl) noexcept
    {
        const double delayed = dl.read (ch, delaySamples);
        const double v = x + gCoef * delayed;
        dl.write (ch, sanitise (v));
        return delayed - gCoef * v;
    }
};

//==============================================================================
// Smoothed parameter ramp (one-pole), used for anything that would otherwise
// zipper when automated.
class Smoother
{
public:
    void setTimeMs (float ms, double sr) noexcept
    {
        coeff = ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr));
    }

    void setTarget (float t) noexcept { target = t; }

    void snap (float v) noexcept { current = target = v; }

    float next() noexcept
    {
        current = target + coeff * (current - target);
        return current;
    }

    float value() const noexcept { return current; }
    float targetValue() const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, coeff = 0.0f;
};

} // namespace wob
