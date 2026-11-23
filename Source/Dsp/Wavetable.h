#pragma once
// Stratum - procedural band-limited wavetables generated at startup by additive synthesis.
// Every table has kFrames morph positions and kMips band-limited copies for alias control.
#include "Common.h"
#include <vector>

namespace stratum
{
class WaveTables
{
public:
    static constexpr int N = 1024;  // samples per cycle
    static constexpr int F = 16;    // morph frames
    static constexpr int M = 8;     // mip levels
    static constexpr int T = 8;     // number of tables
    static constexpr int MaxHarm = N / 2;

    static const WaveTables& get() { static WaveTables w; return w; }

    static const char* name (int t)
    {
        static const char* n[T] = { "Saw Morph", "Square Morph", "Comb Saw", "Pulse Width",
                                    "Vowel", "Bell", "FM Spectrum", "Grit" };
        return n[t < 0 ? 0 : (t >= T ? T - 1 : t)];
    }

    // Pick mip level for a given oscillator frequency.
    static int mipFor (float freq, float sampleRate)
    {
        float limit = (0.5f * sampleRate) / std::max (freq, 1.0f);
        if (limit >= (float) MaxHarm) return 0;
        int mip = (int) std::ceil (std::log2 ((float) MaxHarm / std::max (limit, 1.0f)));
        return std::min (std::max (mip, 0), M - 1);
    }

    // phase 0..1, morph 0..1
    float read (int t, int mip, float morph, float phase) const
    {
        float fp = clampf (morph, 0.0f, 1.0f) * (float) (F - 1);
        int f0 = (int) fp;
        int f1 = std::min (f0 + 1, F - 1);
        float ff = fp - (float) f0;
        float ip = phase * (float) N;
        int i0 = (int) ip;
        float fi = ip - (float) i0;
        i0 &= (N - 1);
        const float* a = frame (t, mip, f0);
        const float* b = frame (t, mip, f1);
        float va = a[i0] + (a[i0 + 1] - a[i0]) * fi;
        float vb = b[i0] + (b[i0 + 1] - b[i0]) * fi;
        return va + (vb - va) * ff;
    }

private:
    std::vector<float> data;

    const float* frame (int t, int mip, int f) const
    {
        return &data[((((size_t) t * M + (size_t) mip) * F + (size_t) f) * (size_t) (N + 1))];
    }

    static float hash (int a, int b)
    {
        uint32_t h = (uint32_t) a * 374761393u + (uint32_t) b * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
    }

    // Returns sine (s) and cosine (c) coefficients for harmonic k.
    static void harmonic (int t, float m, int k, int f, float& s, float& c)
    {
        float fk = (float) k;
        s = 0; c = 0;
        switch (t)
        {
            case 0: s = lerpf (k == 1 ? 1.0f : 0.0f, 1.0f / fk, m); break;
            case 1: s = lerpf (k == 1 ? 1.0f : 0.0f, (k & 1) ? 1.0f / fk : 0.0f, m); break;
            case 2: s = (1.0f / fk) * (0.5f + 0.5f * std::cos (kPi * fk * m * 2.0f)); break;
            case 3:
            {
                float w = 0.5f - 0.47f * m;
                float amp = (2.0f / (kPi * fk)) * std::sin (kPi * fk * w);
                s = amp * std::sin (kPi * fk * w);
                c = amp * std::cos (kPi * fk * w);
                break;
            }
            case 4:
            {
                float c1 = 3.0f + 5.0f * m, c2 = 9.0f + 22.0f * (1.0f - m * 0.6f);
                auto g = [] (float x, float mu, float wd) { float d = (x - mu) / wd; return std::exp (-d * d); };
                s = std::pow (fk, -0.7f) * (g (fk, c1, 1.6f) + 0.7f * g (fk, c2, 3.0f)) + (k == 1 ? 0.2f : 0.0f);
                break;
            }
            case 5:
            {
                int spacing = 2 + (int) std::round (6.0f * m);
                s = std::exp (-fk * 0.06f) * ((k % spacing) == 0 ? 1.0f : 0.12f) / std::sqrt (fk);
                break;
            }
            case 6:
                s = std::fabs (std::sin (fk * m * kPi * 0.5f + 0.3f)) / std::pow (fk, 1.1f) + (k == 1 ? 0.4f * (1.0f - m) : 0.0f);
                break;
            default:
            {
                float amp = std::pow (fk, -0.7f) * (0.3f + 0.7f * hash (k, f));
                float ph = hash (k * 7 + 3, f + 11) * kTwoPi;
                s = amp * std::sin (ph);
                c = amp * std::cos (ph);
                break;
            }
        }
    }

    WaveTables()
    {
        data.assign ((size_t) T * M * F * (N + 1), 0.0f);
        std::vector<float> sinT ((size_t) N), cosT ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            sinT[(size_t) i] = std::sin (kTwoPi * (float) i / (float) N);
            cosT[(size_t) i] = std::cos (kTwoPi * (float) i / (float) N);
        }

        std::vector<float> sc ((size_t) MaxHarm + 1), cc ((size_t) MaxHarm + 1);
        for (int t = 0; t < T; ++t)
            for (int f = 0; f < F; ++f)
            {
                float m = (float) f / (float) (F - 1);
                for (int k = 1; k <= MaxHarm; ++k)
                    harmonic (t, m, k, f, sc[(size_t) k], cc[(size_t) k]);

                float scale = 1.0f;
                for (int mip = 0; mip < M; ++mip)
                {
                    int maxH = MaxHarm >> mip;
                    float* out = const_cast<float*> (frame (t, mip, f));
                    float peak = 0.0f;
                    for (int i = 0; i < N; ++i)
                    {
                        float acc = 0.0f;
                        for (int k = 1; k <= maxH; ++k)
                        {
                            int idx = (k * i) & (N - 1);
                            acc += sc[(size_t) k] * sinT[(size_t) idx] + cc[(size_t) k] * cosT[(size_t) idx];
                        }
                        out[i] = acc;
                        peak = std::max (peak, std::fabs (acc));
                    }
                    if (mip == 0) scale = peak > 1.0e-6f ? 0.9f / peak : 1.0f;
                    for (int i = 0; i < N; ++i) out[i] *= scale;
                    out[N] = out[0];
                }
            }
    }
};
} // namespace stratum
