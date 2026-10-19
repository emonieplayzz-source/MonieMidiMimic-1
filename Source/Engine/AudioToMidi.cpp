#include "AudioToMidi.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace mm::AudioToMidi
{
    namespace
    {
        constexpr double kTargetRate = 22050.0;
        constexpr double kMaxSeconds = 600.0;
        constexpr int    kMinPitch   = 33;   // A1
        constexpr int    kMaxPitch   = 96;   // C7
        constexpr int    kNumPitches = kMaxPitch - kMinPitch + 1;
        constexpr int    kHarmonics  = 8;

        struct Cancelled {};

        std::vector<float> toMonoResampled (const juce::AudioBuffer<float>& audio, double sampleRate, double& outRate)
        {
            const int n  = audio.getNumSamples();
            const int ch = juce::jmax (1, audio.getNumChannels());

            std::vector<float> mono ((size_t) n, 0.0f);
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                auto* src = audio.getReadPointer (c);
                for (int i = 0; i < n; ++i)
                    mono[(size_t) i] += src[i] / (float) ch;
            }

            if (sampleRate <= kTargetRate * 1.05)
            {
                outRate = sampleRate;
                return mono;
            }

            // Two passes of a 2nd-order low-pass as an anti-alias filter, then resample.
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::IIRFilter f;
                f.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, kTargetRate * 0.43));
                f.processSamples (mono.data(), n);
            }

            const double ratio = sampleRate / kTargetRate;
            const int outN = (int) std::floor (n / ratio) - 4;
            std::vector<float> out ((size_t) juce::jmax (0, outN));
            if (outN > 0)
            {
                juce::LagrangeInterpolator interp;
                interp.process (ratio, mono.data(), out.data(), outN);
            }
            outRate = kTargetRate;
            return out;
        }

        /** Runs a centred STFT and hands each frame's magnitudes to perFrame(frameIndex, mags). */
        template <typename PerFrame>
        int runStft (const std::vector<float>& x, int order, int hop, PerFrame&& perFrame,
                     Progress& progress, float progStart, float progEnd)
        {
            const int N = 1 << order;
            const int frames = juce::jmax (1, (int) ((x.size() + (size_t) hop - 1) / (size_t) hop));
            juce::dsp::FFT fft (order);
            juce::dsp::WindowingFunction<float> window ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
            std::vector<float> buf ((size_t) N * 2);

            for (int t = 0; t < frames; ++t)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                const long long first = (long long) t * hop - N / 2;
                for (int i = 0; i < N; ++i)
                {
                    const long long idx = first + i;
                    if (idx >= 0 && idx < (long long) x.size())
                        buf[(size_t) i] = x[(size_t) idx];
                }

                window.multiplyWithWindowingTable (buf.data(), (size_t) N);
                fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                perFrame (t, buf.data());

                if (progress && (t & 255) == 0)
                    if (! progress (progStart + (progEnd - progStart) * (float) t / (float) frames))
                        throw Cancelled();
            }
            return frames;
        }

        std::vector<bool> pickPeaks (const std::vector<float>& env, int halfWindow, int meanWindow,
                                     float meanFactor, float absFraction, int minGap)
        {
            const int n = (int) env.size();
            std::vector<bool> peaks ((size_t) n, false);
            if (n == 0) return peaks;

            const float maxV = *std::max_element (env.begin(), env.end());
            if (maxV <= 0.0f) return peaks;

            int last = -minGap - 1;
            for (int t = 0; t < n; ++t)
            {
                const float v = env[(size_t) t];
                bool isMax = true;
                for (int k = juce::jmax (0, t - halfWindow); k <= juce::jmin (n - 1, t + halfWindow) && isMax; ++k)
                    if (env[(size_t) k] > v) isMax = false;
                if (! isMax) continue;

                double mean = 0; int cnt = 0;
                for (int k = juce::jmax (0, t - meanWindow); k <= juce::jmin (n - 1, t + meanWindow); ++k)
                {
                    mean += env[(size_t) k];
                    ++cnt;
                }
                mean /= juce::jmax (1, cnt);

                if (v > meanFactor * mean + absFraction * maxV && t - last >= minGap)
                {
                    peaks[(size_t) t] = true;
                    last = t;
                }
            }
            return peaks;
        }

        struct RawNote { int pitch; double startSec, endSec; float velocity; };

        void toPhrase (std::vector<RawNote>& raw, const std::vector<float>& onsetEnv, double fps,
                       const TranscribeOptions& opt, Phrase& out)
        {
            const double bpm = opt.bpm > 0.0 ? opt.bpm : estimateTempo (onsetEnv, fps);
            const double period = 60.0 / bpm;

            // Beat phase: where a beat grid at this tempo lines up best with the onsets.
            double bestPhase = 0.0, bestScore = -1.0;
            for (double phase = 0.0; phase < period; phase += 1.0 / fps)
            {
                double score = 0;
                for (double t = phase; t < onsetEnv.size() / fps; t += period)
                {
                    const size_t idx = (size_t) std::llround (t * fps);
                    if (idx < onsetEnv.size()) score += onsetEnv[idx];
                }
                if (score > bestScore) { bestScore = score; bestPhase = phase; }
            }

            double firstNote = 1.0e9;
            for (auto& r : raw) firstNote = juce::jmin (firstNote, r.startSec);
            const double beat0 = bestPhase + std::floor ((firstNote - bestPhase) / period + 0.15) * period;

            Phrase p;
            p.bpm = bpm;
            p.isDrums = opt.drums;

            for (auto& r : raw)
            {
                double start = (r.startSec - beat0) / period;
                double len   = (r.endSec - r.startSec) / period;
                if (start < -0.1) continue;
                start = juce::jmax (0.0, start);

                if (opt.quantize)
                {
                    start = std::round (start * 4.0) / 4.0;
                    len   = juce::jmax (0.25, std::round (len * 4.0) / 4.0);
                }

                Note n;
                n.pitch = r.pitch;
                n.start = start;
                n.length = opt.drums ? 0.125 : juce::jmax (1.0 / 32.0, len);
                n.velocity = r.velocity;
                n.channel = opt.drums ? 10 : 1;
                p.notes.push_back (n);
            }

            p.sort();

            // Remove duplicates and overlaps of the same pitch.
            std::vector<Note> cleaned;
            for (auto& n : p.notes)
            {
                bool merged = false;
                for (auto it = cleaned.rbegin(); it != cleaned.rend(); ++it)
                {
                    if (it->pitch != n.pitch) continue;
                    if (std::abs (it->start - n.start) < 1.0e-6)
                    {
                        it->length = juce::jmax (it->length, n.length);
                        it->velocity = juce::jmax (it->velocity, n.velocity);
                        merged = true;
                    }
                    else if (it->start + it->length > n.start)
                    {
                        it->length = juce::jmax (1.0 / 32.0, n.start - it->start);
                    }
                    break;
                }
                if (! merged) cleaned.push_back (n);
            }
            p.notes = std::move (cleaned);
            p.computeLengthFromNotes();
            out = std::move (p);
        }

        bool transcribePitched (const std::vector<float>& x, double fs, const TranscribeOptions& opt,
                                Phrase& out, juce::String& error, Progress& progress)
        {
            const int order = fs > 16000.0 ? 12 : 11;
            const int N = 1 << order;
            const int hop = fs > 16000.0 ? 256 : 128;
            const int bins = N / 2 + 1;
            const double binHz = fs / N;
            const double fps = fs / hop;

            std::vector<std::array<float, kNumPitches>> sal;
            std::vector<float> flux;
            std::vector<float> prevLog ((size_t) bins, 0.0f);

            double f0s[kNumPitches];
            for (int m = 0; m < kNumPitches; ++m)
                f0s[m] = 440.0 * std::pow (2.0, (m + kMinPitch - 69) / 12.0);

            runStft (x, order, hop, [&] (int, const float* mag)
            {
                auto M = [&] (double f) -> float
                {
                    const int b = (int) std::round (f / binHz);
                    if (b < 1 || b >= bins - 1) return 0.0f;
                    return std::sqrt (juce::jmax (mag[b - 1], juce::jmax (mag[b], mag[b + 1])));
                };

                std::array<float, kNumPitches> s {};
                for (int m = 0; m < kNumPitches; ++m)
                {
                    float H[kHarmonics] = {};
                    float maxH = 0.0f, sum = 0.0f, w = 1.0f;
                    for (int h = 0; h < kHarmonics; ++h)
                    {
                        const double f = f0s[m] * (h + 1);
                        if (f < fs * 0.47) H[h] = M (f);
                        maxH = juce::jmax (maxH, H[h]);
                        sum += w * H[h];
                        w *= 0.8f;
                    }
                    // A real note needs a fundamental: this stops "ghost" notes an octave below.
                    const float fund = juce::jmin (1.0f, H[0] / (0.3f * maxH + 1.0e-9f));
                    s[(size_t) m] = sum * (0.4f + 0.6f * fund);
                }
                sal.push_back (s);

                float fl = 0.0f;
                for (int b = 1; b < bins; ++b)
                {
                    const float l = std::log1p (10.0f * mag[b]);
                    fl += juce::jmax (0.0f, l - prevLog[(size_t) b]);
                    prevLog[(size_t) b] = l;
                }
                flux.push_back (fl);
            }, progress, 0.05f, 0.8f);

            const int frames = (int) sal.size();
            float globalMax = 0.0f;
            for (auto& s : sal) for (float v : s) globalMax = juce::jmax (globalMax, v);

            if (globalMax <= 1.0e-6f)
            {
                error = "That audio is silent.";
                return false;
            }

            const float sens = juce::jlimit (0.0f, 1.0f, opt.sensitivity);
            const float relThr = 0.55f - 0.3f * sens;
            const float absThr = 0.25f - 0.2f * sens;
            const int maxPoly = juce::jlimit (1, 8, opt.maxPolyphony);

            // Pick the strongest pitches in each frame.
            std::vector<std::array<bool, kNumPitches>> act ((size_t) frames);
            for (int t = 0; t < frames; ++t)
            {
                auto& s = sal[(size_t) t];
                act[(size_t) t].fill (false);
                const float frameMax = *std::max_element (s.begin(), s.end());
                if (frameMax < absThr * globalMax) continue;

                std::vector<int> cands;
                for (int m = 0; m < kNumPitches; ++m)
                {
                    const float v = s[(size_t) m];
                    const bool peak = (m == 0 || v >= s[(size_t) m - 1]) && (m == kNumPitches - 1 || v >= s[(size_t) m + 1]);
                    if (peak && v >= relThr * frameMax && v >= absThr * globalMax)
                        cands.push_back (m);
                }
                std::sort (cands.begin(), cands.end(), [&] (int a, int b) { return s[(size_t) a] > s[(size_t) b]; });

                std::vector<int> chosen;
                for (int m : cands)
                {
                    if ((int) chosen.size() >= maxPoly) break;
                    bool overtone = false;
                    for (int k : chosen)
                    {
                        const int diff = m - k;
                        if ((diff == 12 || diff == 19 || diff == 24 || diff == 28 || diff == 31 || diff == 36)
                            && s[(size_t) m] < 0.9f * s[(size_t) k])
                            overtone = true;
                    }
                    if (! overtone) chosen.push_back (m);
                }
                for (int m : chosen) act[(size_t) t][(size_t) m] = true;
            }

            const auto onsets = pickPeaks (flux, 3, 8, 1.3f, 0.05f, (int) std::round (0.05 * fps));
            const int gapTol = 2;
            const int minFrames = juce::jmax (2, (int) std::round (0.06 * fps));

            std::vector<RawNote> raw;
            for (int m = 0; m < kNumPitches; ++m)
            {
                int t = 0;
                while (t < frames)
                {
                    if (! act[(size_t) t][(size_t) m]) { ++t; continue; }

                    int start = t, last = t, gap = 0;
                    double salSum = 0; int salCnt = 0;
                    int u = t;
                    for (; u < frames; ++u)
                    {
                        if (act[(size_t) u][(size_t) m])
                        {
                            // A fresh attack on the same pitch splits it into two notes.
                            if (u - start >= minFrames && onsets[(size_t) u])
                            {
                                const float before = juce::jmin (sal[(size_t) juce::jmax (0, u - 1)][(size_t) m],
                                                                 sal[(size_t) juce::jmax (0, u - 2)][(size_t) m]);
                                if (sal[(size_t) u][(size_t) m] > 1.25f * before)
                                    break;
                            }
                            last = u; gap = 0;
                            salSum += sal[(size_t) u][(size_t) m]; ++salCnt;
                        }
                        else if (++gap > gapTol)
                        {
                            break;
                        }
                    }

                    if (last - start + 1 >= minFrames)
                    {
                        const float strength = (float) (salSum / juce::jmax (1, salCnt)) / globalMax;
                        raw.push_back ({ m + kMinPitch, start / fps, (last + 1) / fps,
                                         juce::jlimit (0.2f, 1.0f, 0.35f + 0.65f * std::sqrt (strength)) });
                    }
                    t = juce::jmax (u, last + 1);
                }
            }

            if (raw.empty())
            {
                error = "Couldn't find any clear notes. Try raising Sensitivity, or use a cleaner loop.";
                return false;
            }

            if (progress) progress (0.9f);
            toPhrase (raw, flux, fps, opt, out);
            return true;
        }

        bool transcribeDrums (const std::vector<float>& x, double fs, const TranscribeOptions& opt,
                              Phrase& out, juce::String& error, Progress& progress)
        {
            const int order = 10;
            const int N = 1 << order;
            const int hop = 128;
            const int bins = N / 2 + 1;
            const double binHz = fs / N;
            const double fps = fs / hop;

            struct Band { double lo, hi; int pitch; };
            const Band bands[3] = { { 35.0, 120.0, 36 }, { 250.0, 4000.0, 38 }, { 6000.0, 10500.0, 42 } };

            std::vector<float> flux[3], level[3], total;
            std::vector<float> prevMag ((size_t) bins, 0.0f);

            runStft (x, order, hop, [&] (int, const float* mag)
            {
                // Linear-magnitude flux: only real energy counts, so faint leakage
                // (a hat's tail in the snare band, a snare's body in the kick band) doesn't trigger.
                float f[3] = {}, e[3] = {};
                for (int b = 1; b < bins; ++b)
                {
                    const float d = juce::jmax (0.0f, mag[b] - prevMag[(size_t) b]);
                    prevMag[(size_t) b] = mag[b];
                    const double hz = b * binHz;
                    for (int k = 0; k < 3; ++k)
                        if (hz >= bands[k].lo && hz < bands[k].hi)
                        {
                            f[k] += d;
                            e[k] += mag[b] * mag[b];
                        }
                }
                for (int k = 0; k < 3; ++k)
                {
                    flux[k].push_back (f[k]);
                    level[k].push_back (std::sqrt (e[k]));
                }
                total.push_back (f[0] + f[1] + f[2]);
            }, progress, 0.05f, 0.8f);

            const float sens = juce::jlimit (0.0f, 1.0f, opt.sensitivity);
            const int minGap = (int) std::round (0.05 * fps);

            std::vector<bool> peaks[3];
            float maxes[3], levelMax[3];
            for (int k = 0; k < 3; ++k)
            {
                peaks[k] = pickPeaks (flux[k], 4, 12, 1.5f, 0.1f - 0.08f * sens, minGap);
                maxes[k] = juce::jmax (1.0e-9f, *std::max_element (flux[k].begin(), flux[k].end()));
                levelMax[k] = juce::jmax (1.0e-9f, *std::max_element (level[k].begin(), level[k].end()));
            }

            const int nFrames = (int) flux[0].size();
            // "Same moment" window. A kick's low end peaks a few ms after its click, so ~35 ms.
            const int kNear = juce::jmax (2, (int) std::round (0.035 * fps));
            auto near = [&] (int k, int t)
            {
                for (int u = juce::jmax (0, t - kNear); u <= juce::jmin (nFrames - 1, t + kNear); ++u)
                    if (peaks[k][(size_t) u]) return true;
                return false;
            };

            // Each band's loudness just after the hit, relative to that band's loudest moment.
            auto bandLevel = [&] (int k, int t)
            {
                float v = 0.0f;
                for (int u = t; u <= juce::jmin (nFrames - 1, t + 3); ++u)
                    v = juce::jmax (v, level[k][(size_t) u]);
                return v / levelMax[k];
            };

            // When two voices fire together, the band that is clearly louder wins; if both are
            // strong, both were really played. A hit is only thrown away when it is overpowered
            // by another hit that is not itself overpowered - so a kick's thump leaking into the
            // snare band can't in turn knock out the real hi-hat on that kick.
            // A band's "typical hit": the 75th-percentile onset strength over its detected peaks
            // (above the median, so bleed from other drums can't drag it down).
            float typical[3];
            for (int k = 0; k < 3; ++k)
            {
                std::vector<float> v;
                for (int t = 0; t < nFrames; ++t)
                    if (peaks[k][(size_t) t]) v.push_back (flux[k][(size_t) t]);
                if (v.empty()) { typical[k] = maxes[k]; continue; }
                const size_t q = juce::jmin (v.size() - 1, (v.size() * 3) / 4);
                std::nth_element (v.begin(), v.begin() + (long) q, v.end());
                typical[k] = juce::jmax (1.0e-9f, v[q]);
            }

            auto overpowers = [&] (int o, int k, int t)
            {
                // Bleed only happens between neighbouring bands (kick<->snare, snare<->hat):
                // a kick can't fake a hi-hat, so those two never cancel each other.
                if (std::abs (o - k) != 1)
                    return false;

                const float vsTypical = flux[k][(size_t) t] / typical[k];
                return bandLevel (o, t) > 2.0f * bandLevel (k, t) || vsTypical < 0.35f;
            };

            std::vector<bool> dominated[3];
            for (int k = 0; k < 3; ++k)
            {
                dominated[k].assign ((size_t) nFrames, false);
                for (int t = 0; t < nFrames; ++t)
                    if (peaks[k][(size_t) t])
                        for (int o = 0; o < 3; ++o)
                            if (o != k && near (o, t) && overpowers (o, k, t))
                                dominated[k][(size_t) t] = true;
            }

            auto strongHitNear = [&] (int o, int t)
            {
                for (int u = juce::jmax (0, t - kNear); u <= juce::jmin (nFrames - 1, t + kNear); ++u)
                    if (peaks[o][(size_t) u] && ! dominated[o][(size_t) u]) return true;
                return false;
            };

            std::vector<RawNote> raw;
            for (int k = 0; k < 3; ++k)
            {
                for (int t = 0; t < nFrames; ++t)
                {
                    if (! peaks[k][(size_t) t]) continue;

                    bool leak = false;
                    for (int o = 0; o < 3 && ! leak; ++o)
                        if (o != k && strongHitNear (o, t) && overpowers (o, k, t))
                            leak = true;
                    if (leak) continue;

                    const float rel = flux[k][(size_t) t] / maxes[k];
                    raw.push_back ({ bands[k].pitch, t / fps, t / fps + 0.05,
                                     juce::jlimit (0.2f, 1.0f, 0.3f + 0.7f * std::sqrt (rel)) });
                }
            }

            if (raw.empty())
            {
                error = "Couldn't find any drum hits. Try raising Sensitivity.";
                return false;
            }

            if (progress) progress (0.9f);
            toPhrase (raw, total, fps, opt, out);
            return true;
        }
    }

    //==============================================================================
    double estimateTempo (const std::vector<float>& env, double fps)
    {
        const int n = (int) env.size();
        if (n < (int) (fps * 2.0))
            return 120.0;

        // Remove the slow trend so only the pulses remain.
        std::vector<double> e ((size_t) n);
        const int w = (int) std::round (fps * 0.25);
        double run = 0;
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + env[(size_t) i];
        for (int i = 0; i < n; ++i)
        {
            const int a = juce::jmax (0, i - w), b = juce::jmin (n, i + w + 1);
            run = (prefix[(size_t) b] - prefix[(size_t) a]) / (b - a);
            e[(size_t) i] = juce::jmax (0.0, env[(size_t) i] - run);
        }

        const int minLag = (int) std::floor (60.0 * fps / 200.0);
        const int maxLag = juce::jmin (n / 2, (int) std::ceil (60.0 * fps / 60.0));

        std::vector<double> ac ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag; lag <= maxLag + 1 && lag < n; ++lag)
        {
            double sum = 0;
            for (int i = 0; i + lag < n; ++i) sum += e[(size_t) i] * e[(size_t) i + (size_t) lag];
            ac[(size_t) lag] = sum / (n - lag);
        }

        double best = -1; int bestLag = (int) std::round (60.0 * fps / 120.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double bpm = 60.0 * fps / lag;
            const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.8, 2.0));
            const double score = ac[(size_t) lag] * prior;
            if (score > best) { best = score; bestLag = lag; }
        }

        // Octave check: a slow pick whose double tempo is also strongly supported is usually
        // the double tempo (hip-hop / trap sits at 130-160, not 65-80).
        if (60.0 * fps / bestLag < 95.0)
        {
            const int half = bestLag / 2;
            int bestHalf = half; double bestHalfAc = -1;
            for (int l = juce::jmax (minLag, half - 2); l <= half + 2; ++l)
                if (ac[(size_t) l] > bestHalfAc) { bestHalfAc = ac[(size_t) l]; bestHalf = l; }
            if (bestHalfAc >= 0.5 * ac[(size_t) bestLag])
                bestLag = bestHalf;
        }

        double lag = bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
            const double denom = a - 2 * b + c;
            if (std::abs (denom) > 1.0e-12)
                lag += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom);
        }

        double bpm = 60.0 * fps / lag;
        bpm = std::round (bpm * 10.0) / 10.0;
        // Most music sits on (or near) whole-number tempos.
        if (std::abs (bpm - std::round (bpm)) < 0.35) bpm = std::round (bpm);
        return juce::jlimit (60.0, 200.0, bpm);
    }

    bool transcribeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate,
                           const TranscribeOptions& options, Phrase& out,
                           juce::String& error, Progress progress)
    {
        if (audio.getNumSamples() < (int) (sampleRate * 0.25))
        {
            error = "That audio is too short.";
            return false;
        }

        try
        {
            double fs = sampleRate;
            const auto mono = toMonoResampled (audio, sampleRate, fs);
            if (progress && ! progress (0.05f)) return false;

            return options.drums ? transcribeDrums (mono, fs, options, out, error, progress)
                                 : transcribePitched (mono, fs, options, out, error, progress);
        }
        catch (const Cancelled&)
        {
            error = "Cancelled.";
            return false;
        }
    }

    bool transcribeFile (const juce::File& file, const TranscribeOptions& options,
                         Phrase& out, juce::String& error, Progress progress)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Couldn't read " + file.getFileName() + ". Supported audio: WAV, AIFF, FLAC, OGG, MP3.";
            return false;
        }

        const auto total = (juce::int64) juce::jmin ((double) reader->lengthInSamples, reader->sampleRate * kMaxSeconds);
        juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader->numChannels), (int) total);
        reader->read (&buffer, 0, (int) total, 0, true, true);

        if (! transcribeBuffer (buffer, reader->sampleRate, options, out, error, std::move (progress)))
            return false;

        out.name = file.getFileNameWithoutExtension();
        return true;
    }

    juce::String supportedWildcard()
    {
        return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
    }
}
