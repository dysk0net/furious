#pragma once
// Stratum - user wavetables: a loader for Serum-style .wav wavetables, a band-limited mip builder,
// and the slot bank the audio thread reads from. No JUCE dependency.
//
// Accepted file format (the de-facto standard that Serum and many other synths use):
//   * RIFF/WAVE, 8/16/24/32-bit PCM or 32/64-bit float. Mono; for stereo files the left channel is used.
//   * The audio is a stack of single-cycle frames, 2048 samples each by default.
//   * An optional 'clm ' chunk ("<!>2048 ...") states the frame length when it is not 2048.
//   * Up to 256 frames are kept (longer files are thinned out evenly).
// A file shorter than one frame is treated as a single cycle and resampled.
#include "Common.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace stratum
{
//==============================================================================
// A loaded wavetable: every frame stored at several band-limited resolutions (mips).
// Mip m holds S0>>m samples per cycle and the first H0>>m harmonics, so there is always a copy
// that stays below Nyquist for the note being played. Each cycle has one guard sample for interpolation.
struct UserTable
{
    static constexpr int M = 9;
    static constexpr int S0 = 4096;
    static constexpr int H0 = 1023;
    static constexpr int kMaxFrames = 256;

    std::string name;
    int frames = 0;
    std::vector<float> data;
    size_t mipOffset[M] = {};

    static int mipSize (int m) { return S0 >> m; }
    static int mipHarmonics (int m) { return H0 >> m; }

    static int mipFor (float freq, float sampleRate)
    {
        const float limit = 0.5f * sampleRate / std::max (freq, 1.0f);
        int m = 0;
        while (m < M - 1 && (float) mipHarmonics (m) > limit) ++m;
        return m;
    }

    const float* frame (int mip, int f) const
    {
        return &data[mipOffset[mip] + (size_t) f * (size_t) (mipSize (mip) + 1)];
    }

    // phase 0..1, morph 0..1 (position through the frames)
    float read (int mip, float morph, float phase) const
    {
        const int S = mipSize (mip);
        const float fp = clampf (morph, 0.0f, 1.0f) * (float) (frames - 1);
        const int f0 = (int) fp;
        const int f1 = std::min (f0 + 1, frames - 1);
        const float ff = fp - (float) f0;
        const float ip = phase * (float) S;
        int i0 = (int) ip;
        const float fi = ip - (float) i0;
        i0 &= (S - 1);
        const float* a = frame (mip, f0);
        const float* b = frame (mip, f1);
        const float va = a[i0] + (a[i0 + 1] - a[i0]) * fi;
        const float vb = b[i0] + (b[i0 + 1] - b[i0]) * fi;
        return va + (vb - va) * ff;
    }
};

//==============================================================================
namespace detail
{
struct Cpx { double r = 0.0, i = 0.0; };

class Fft
{
public:
    explicit Fft (int n) : n_ (n), rev_ ((size_t) n), twr_ ((size_t) n / 2), twi_ ((size_t) n / 2)
    {
        int lg = 0;
        while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev_[(size_t) i] = r;
        }
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -6.283185307179586 * (double) k / (double) n;
            twr_[(size_t) k] = std::cos (a);
            twi_[(size_t) k] = std::sin (a);
        }
    }

    // In-place transform. The inverse is scaled by 1/n.
    void run (Cpx* a, bool inverse) const
    {
        for (int i = 0; i < n_; ++i)
        {
            const int j = rev_[(size_t) i];
            if (i < j) std::swap (a[i], a[j]);
        }
        for (int len = 2; len <= n_; len <<= 1)
        {
            const int half = len >> 1, step = n_ / len;
            for (int i = 0; i < n_; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const double wr = twr_[(size_t) (j * step)];
                    const double wi = inverse ? -twi_[(size_t) (j * step)] : twi_[(size_t) (j * step)];
                    Cpx& u = a[i + j];
                    Cpx& v = a[i + j + half];
                    const double tr = v.r * wr - v.i * wi, ti = v.r * wi + v.i * wr;
                    v.r = u.r - tr; v.i = u.i - ti;
                    u.r += tr;      u.i += ti;
                }
        }
        if (inverse)
        {
            const double s = 1.0 / (double) n_;
            for (int i = 0; i < n_; ++i) { a[i].r *= s; a[i].i *= s; }
        }
    }

private:
    int n_;
    std::vector<int> rev_;
    std::vector<double> twr_, twi_;
};

inline uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
} // namespace detail

//==============================================================================
struct WavData
{
    std::vector<float> samples;   // first channel, -1..1
    int sampleRate = 0;
    int frameSizeHint = 0;        // from the 'clm ' chunk, 0 if absent
};

inline bool parseWav (const uint8_t* d, size_t n, WavData& out, std::string& err)
{
    using namespace detail;
    out = WavData();
    if (d == nullptr || n < 12) { err = "the file is empty or too small"; return false; }
    if (std::memcmp (d, "RF64", 4) == 0) { err = "RF64 files are not supported"; return false; }
    if (std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) { err = "not a WAV file"; return false; }

    bool haveFmt = false, haveData = false;
    unsigned tag = 0, channels = 0, bits = 0;
    const uint8_t* dataPtr = nullptr;
    size_t dataLen = 0;

    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const uint8_t* c = d + pos;
        const uint32_t sz = rd32 (c + 4);
        const size_t body = pos + 8;
        const size_t len = std::min ((size_t) sz, n - body);

        if (std::memcmp (c, "fmt ", 4) == 0 && len >= 16)
        {
            tag = rd16 (d + body);
            channels = rd16 (d + body + 2);
            out.sampleRate = (int) rd32 (d + body + 4);
            bits = rd16 (d + body + 14);
            if (tag == 0xFFFE && len >= 26) tag = rd16 (d + body + 24);   // WAVE_FORMAT_EXTENSIBLE
            haveFmt = true;
        }
        else if (std::memcmp (c, "data", 4) == 0)
        {
            dataPtr = d + body; dataLen = len; haveData = true;
        }
        else if (std::memcmp (c, "clm ", 4) == 0)
        {
            const std::string t ((const char*) (d + body), len);
            const size_t k = t.find ("<!>");
            if (k != std::string::npos)
            {
                const long v = std::strtol (t.c_str() + k + 3, nullptr, 10);
                if (v >= 32 && v <= 65536) out.frameSizeHint = (int) v;
            }
        }

        const size_t next = body + (size_t) sz + (size_t) (sz & 1u);
        if (next <= pos) break;
        pos = next;
    }

    if (! haveFmt || ! haveData) { err = "the WAV file has no audio data"; return false; }
    if (channels < 1 || bits < 8 || (bits & 7) != 0) { err = "unsupported WAV layout"; return false; }
    const bool isFloat = tag == 3;
    if (tag != 1 && tag != 3) { err = "only PCM and float WAV files are supported"; return false; }
    if (isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 24 && bits != 32))
    {
        err = "unsupported bit depth (" + std::to_string (bits) + ")";
        return false;
    }

    const size_t bytes = bits / 8, stride = bytes * channels;
    const size_t count = dataLen / stride;
    out.samples.resize (count);
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t* s = dataPtr + i * stride;
        double v = 0.0;
        if (isFloat)
        {
            if (bits == 32) { float f; std::memcpy (&f, s, 4); v = (double) f; }
            else { double dd; std::memcpy (&dd, s, 8); v = dd; }
        }
        else if (bits == 8)  v = ((double) s[0] - 128.0) / 128.0;
        else if (bits == 16) v = (double) (int16_t) rd16 (s) / 32768.0;
        else if (bits == 24) v = (double) ((int32_t) (((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16) | ((uint32_t) s[2] << 24)) >> 8) / 8388608.0;
        else                 v = (double) (int32_t) rd32 (s) / 2147483648.0;
        if (! std::isfinite (v)) v = 0.0;
        out.samples[i] = (float) clampf ((float) v, -4.0f, 4.0f);
    }
    return true;
}

//==============================================================================
inline std::shared_ptr<UserTable> buildUserTable (const WavData& w, const std::string& name, std::string& err)
{
    using namespace detail;
    const int total = (int) std::min<size_t> (w.samples.size(), (size_t) 0x7fffffff);
    if (total < 8) { err = "the file is too short to hold a wavetable"; return nullptr; }

    int L = w.frameSizeHint > 0 ? w.frameSizeHint : 2048;
    int srcFrames = total / L;
    if (srcFrames < 1) { L = total; srcFrames = 1; }          // short file: one single cycle
    const bool pow2 = L >= 32 && L <= 65536 && (L & (L - 1)) == 0;
    const int Lc = pow2 ? L : 2048;                           // length of the cycle that gets analysed
    const int outFrames = std::min (srcFrames, UserTable::kMaxFrames);

    auto t = std::make_shared<UserTable>();
    t->name = name;
    t->frames = outFrames;
    size_t off = 0;
    for (int m = 0; m < UserTable::M; ++m)
    {
        t->mipOffset[m] = off;
        off += (size_t) outFrames * (size_t) (UserTable::mipSize (m) + 1);
    }
    t->data.assign (off, 0.0f);

    const Fft fwd (Lc);
    std::vector<Fft> inv;
    for (int m = 0; m < UserTable::M; ++m) inv.emplace_back (UserTable::mipSize (m));
    std::vector<Cpx> spec ((size_t) Lc), work ((size_t) UserTable::S0);

    double peak = 0.0;
    for (int f = 0; f < outFrames; ++f)
    {
        const int src = outFrames == 1 ? 0 : (int) std::lround ((double) f * (double) (srcFrames - 1) / (double) (outFrames - 1));
        const float* in = w.samples.data() + (size_t) src * (size_t) L;
        for (int i = 0; i < Lc; ++i)
        {
            double v;
            if (pow2) v = (double) in[i];
            else
            {
                const double pos = (double) i * (double) L / (double) Lc;
                const int i0 = (int) pos;
                const double fr = pos - (double) i0;
                v = (double) in[i0] * (1.0 - fr) + (double) in[(i0 + 1) % L] * fr;
            }
            spec[(size_t) i].r = v; spec[(size_t) i].i = 0.0;
        }
        fwd.run (spec.data(), false);

        for (int m = 0; m < UserTable::M; ++m)
        {
            const int S = UserTable::mipSize (m);
            const int hmax = std::min (UserTable::mipHarmonics (m), Lc / 2 - 1);
            for (int i = 0; i < S; ++i) work[(size_t) i] = Cpx();
            const double g = (double) S / (double) Lc;
            for (int h = 1; h <= hmax; ++h)           // bin 0 (DC) is left out on purpose
            {
                work[(size_t) h].r = spec[(size_t) h].r * g;
                work[(size_t) h].i = spec[(size_t) h].i * g;
                work[(size_t) (S - h)].r = work[(size_t) h].r;
                work[(size_t) (S - h)].i = -work[(size_t) h].i;
            }
            inv[(size_t) m].run (work.data(), true);

            float* out = &t->data[t->mipOffset[m] + (size_t) f * (size_t) (S + 1)];
            for (int i = 0; i < S; ++i) out[i] = (float) work[(size_t) i].r;
            out[S] = out[0];
            if (m == 0) for (int i = 0; i < S; ++i) peak = std::max (peak, (double) std::fabs (out[i]));
        }
    }

    // one gain for the whole table keeps the author's balance between frames; peak sits at 0.9
    const float scale = peak > 1.0e-9 ? (float) (0.9 / peak) : 1.0f;
    for (float& v : t->data) v *= scale;
    return t;
}

inline std::shared_ptr<UserTable> loadUserTableFromMemory (const void* bytes, size_t size, const std::string& name, std::string& err)
{
    WavData w;
    if (! parseWav (static_cast<const uint8_t*> (bytes), size, w, err)) return nullptr;
    return buildUserTable (w, name, err);
}

//==============================================================================
// One slot per oscillator (layer * kOscs + osc). The message thread loads tables and assigns them;
// the audio thread only does an atomic pointer read. A table that has just been replaced is kept alive
// for a few seconds so a block that is still rendering with it can finish.
class UserWaveBank
{
public:
    static constexpr int kSlots = kLayers * kOscs;

    UserWaveBank() { for (auto& s : slots_) s.store (nullptr); }

    const UserTable* get (int slot) const noexcept { return slots_[slot].load (std::memory_order_acquire); }

    std::shared_ptr<UserTable> getShared (int slot) const
    {
        std::lock_guard<std::mutex> lock (m_);
        const UserTable* p = get (slot);
        for (const auto& t : owned_) if (t.get() == p) return t;
        return nullptr;
    }

    void assign (int slot, const std::shared_ptr<UserTable>& t)
    {
        std::lock_guard<std::mutex> lock (m_);
        const UserTable* old = slots_[slot].exchange (t.get(), std::memory_order_acq_rel);
        bool known = false;
        for (const auto& o : owned_) known = known || o == t;
        if (! known) owned_.push_back (t);
        retireIfUnused (old);
    }

    void clear (int slot)
    {
        std::lock_guard<std::mutex> lock (m_);
        const UserTable* old = slots_[slot].exchange (nullptr, std::memory_order_acq_rel);
        retireIfUnused (old);
    }

    // Frees tables that were replaced more than a few seconds ago.
    void collect()
    {
        std::lock_guard<std::mutex> lock (m_);
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = retired_.size(); i-- > 0;)
            if (now - retired_[i].at > std::chrono::seconds (3)) retired_.erase (retired_.begin() + (std::ptrdiff_t) i);
    }

    // Cache so several oscillators using the same file share one copy.
    std::shared_ptr<UserTable> lookup (const std::string& key) const
    {
        std::lock_guard<std::mutex> lock (m_);
        auto it = cache_.find (key);
        return it == cache_.end() ? nullptr : it->second.lock();
    }
    void remember (const std::string& key, const std::shared_ptr<UserTable>& t)
    {
        std::lock_guard<std::mutex> lock (m_);
        cache_[key] = t;
    }

private:
    struct Retired { std::shared_ptr<UserTable> t; std::chrono::steady_clock::time_point at; };

    void retireIfUnused (const UserTable* old)
    {
        if (old == nullptr) return;
        for (const auto& s : slots_) if (s.load() == old) return;
        for (size_t i = 0; i < owned_.size(); ++i)
            if (owned_[i].get() == old)
            {
                retired_.push_back ({ owned_[i], std::chrono::steady_clock::now() });
                owned_.erase (owned_.begin() + (std::ptrdiff_t) i);
                return;
            }
    }

    mutable std::mutex m_;
    std::atomic<const UserTable*> slots_[kSlots];
    std::vector<std::shared_ptr<UserTable>> owned_;
    std::vector<Retired> retired_;
    std::map<std::string, std::weak_ptr<UserTable>> cache_;
};
} // namespace stratum
