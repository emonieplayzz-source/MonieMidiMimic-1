#include "Generator.h"

#include <cmath>
#include <set>

namespace mm
{
    namespace
    {
        // Krumhansl-Kessler key profiles
        constexpr double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        constexpr double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        const char* const kNoteNames[12]   = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

        int pitchClass (int p) noexcept { return ((p % 12) + 12) % 12; }

        double gauss (juce::Random& r)
        {
            const double u1 = juce::jmax (1.0e-9, r.nextDouble());
            const double u2 = r.nextDouble();
            return std::sqrt (-2.0 * std::log (u1)) * std::cos (juce::MathConstants<double>::twoPi * u2);
        }

        int foldInterval (int iv) noexcept
        {
            while (iv > 12)  iv -= 12;
            while (iv < -12) iv += 12;
            return iv;
        }

        template <typename T>
        T pickWeighted (juce::Random& r, const std::vector<std::pair<T, double>>& items, T fallback)
        {
            double total = 0.0;
            for (auto& it : items) total += it.second;
            if (total <= 0.0) return fallback;

            double x = r.nextDouble() * total;
            for (auto& it : items)
            {
                x -= it.second;
                if (x <= 0.0) return it.first;
            }
            return items.back().first;
        }

        double correlation (const double* a, const double* b, int n)
        {
            double ma = 0, mb = 0;
            for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
            ma /= n; mb /= n;

            double num = 0, da = 0, db = 0;
            for (int i = 0; i < n; ++i)
            {
                num += (a[i] - ma) * (b[i] - mb);
                da  += (a[i] - ma) * (a[i] - ma);
                db  += (b[i] - mb) * (b[i] - mb);
            }
            return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
        }

        int anchorOf (const std::vector<int>& pitches, bool chordal)
        {
            if (pitches.empty()) return 60;
            return chordal ? *std::min_element (pitches.begin(), pitches.end())
                           : *std::max_element (pitches.begin(), pitches.end());
        }
    }

    //==============================================================================
    std::array<bool, 12> KeyInfo::scale() const
    {
        static constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
        std::array<bool, 12> s {};
        for (int i = 0; i < 7; ++i)
            s[(size_t) pitchClass (root + (minor ? minorSteps[i] : majorSteps[i]))] = true;
        return s;
    }

    juce::String KeyInfo::name() const
    {
        return juce::String (kNoteNames[pitchClass (root)]) + (minor ? " minor" : " major");
    }

    KeyInfo detectKey (const Phrase& phrase)
    {
        double hist[12] = {};
        double total = 0.0;
        for (auto& n : phrase.notes)
        {
            const double w = juce::jmin (2.0, n.length) * (0.5 + n.velocity);
            hist[pitchClass (n.pitch)] += w;
            total += w;
        }

        KeyInfo best;
        if (total <= 0.0)
            return best;

        double bestScore = -2.0;
        for (int root = 0; root < 12; ++root)
        {
            for (int m = 0; m < 2; ++m)
            {
                double profile[12];
                for (int i = 0; i < 12; ++i)
                    profile[i] = (m == 0 ? kMajorProfile : kMinorProfile)[pitchClass (i - root)];

                const double score = correlation (hist, profile, 12);
                if (score > bestScore)
                {
                    bestScore = score;
                    best.root = root;
                    best.minor = (m == 1);
                    best.confidence = score;
                }
            }
        }
        return best;
    }

    double similarityScore (const Phrase& a, const Phrase& b)
    {
        if (a.empty() || b.empty())
            return 0.0;

        const double grid = 0.25;
        const int cycle = juce::jmax (1, (int) std::round (a.lengthBeats / grid));

        std::set<int> onsetsA, onsetsB;
        std::set<std::pair<int, int>> notesA;
        double pcA[12] = {}, pcB[12] = {};

        for (auto& n : a.notes)
        {
            const int step = ((int) std::round (n.start / grid)) % cycle;
            onsetsA.insert (step);
            notesA.insert ({ step, n.pitch });
            pcA[pitchClass (n.pitch)] += 1.0;
        }

        int exact = 0;
        for (auto& n : b.notes)
        {
            const int step = ((int) std::round (n.start / grid)) % cycle;
            onsetsB.insert (step);
            pcB[pitchClass (n.pitch)] += 1.0;
            if (notesA.count ({ step, n.pitch }) > 0)
                ++exact;
        }

        int inter = 0;
        for (int s : onsetsB)
            if (onsetsA.count (s) > 0)
                ++inter;

        const double uni = (double) (onsetsA.size() + onsetsB.size() - (size_t) inter);
        const double rhythm = uni > 0 ? inter / uni : 0.0;

        double dot = 0, na = 0, nb = 0;
        for (int i = 0; i < 12; ++i) { dot += pcA[i] * pcB[i]; na += pcA[i] * pcA[i]; nb += pcB[i] * pcB[i]; }
        const double pc = (na > 0 && nb > 0) ? dot / std::sqrt (na * nb) : 0.0;

        const double exactFrac = (double) exact / (double) b.notes.size();
        return juce::jlimit (0.0, 1.0, 0.4 * rhythm + 0.3 * pc + 0.3 * exactFrac);
    }

    //==============================================================================
    void StyleModel::analyse (const Phrase& src, StyleMode mode)
    {
        ready = false;
        source = src;
        source.sort();

        srcEvents.clear(); eventsByBar.clear(); barSignature.clear();
        onsetProb.clear(); velByStep.clear(); pcProb.fill (0.0);
        durationSteps.clear(); shapeCounts.clear();
        interval2.clear(); interval1.clear(); interval0.clear();
        drumProb.clear(); drumVel.clear(); drumBars.clear();

        if (source.empty())
            return;

        drums = (mode == StyleMode::Drums) || (mode == StyleMode::Auto && source.isDrums);
        beatsPerBar = source.beatsPerBar();
        srcBars = juce::jmax (1, (int) std::round (source.lengthBeats / beatsPerBar));

        // Quantize grid: the coarsest grid that explains (almost) every onset.
        {
            const double candidates[] = { 0.25, 1.0 / 3.0, 0.125, 1.0 / 6.0 };
            double bestScore = -1.0;
            bool found = false;
            for (double g : candidates)
            {
                int hits = 0;
                for (auto& n : source.notes)
                {
                    const double q = std::round (n.start / g) * g;
                    if (std::abs (q - n.start) < 0.03)
                        ++hits;
                }
                const double score = hits / (double) source.notes.size();
                if (score >= 0.9) { grid = g; found = true; break; }
                if (score > bestScore) { bestScore = score; grid = g; }
            }
            if (! found && bestScore < 0.5)
                grid = 0.25; // loose/unquantized playing: a 16th grid is the most useful default
        }
        stepsPerBar = juce::jmax (1, (int) std::round (beatsPerBar / grid));

        // Most common channel, plus velocity stats
        {
            std::map<int, int> chCount;
            double vSum = 0, vSq = 0;
            for (auto& n : source.notes)
            {
                ++chCount[n.channel];
                vSum += n.velocity;
                vSq  += n.velocity * n.velocity;
            }
            channel = std::max_element (chCount.begin(), chCount.end(),
                                        [] (auto& x, auto& y) { return x.second < y.second; })->first;
            const double cnt = (double) source.notes.size();
            avgVelocity = (float) (vSum / cnt);
            velocitySpread = (float) std::sqrt (juce::jmax (0.0, vSq / cnt - (vSum / cnt) * (vSum / cnt)));
        }

        lowPitch  = source.lowestPitch();
        highPitch = source.highestPitch();

        if (drums) analyseDrums();
        else       analyseMelodic();

        ready = true;
    }

    void StyleModel::analyseMelodic()
    {
        key = detectKey (source);
        scale = key.scale();

        const double tol = juce::jmin (grid * 0.35, 0.06);

        for (auto& n : source.notes)
        {
            if (! srcEvents.empty() && n.start - srcEvents.back().start <= tol)
            {
                auto& ev = srcEvents.back();
                bool dup = false;
                for (auto& sn : ev.notes)
                    if (sn.pitch == n.pitch) { sn.length = juce::jmax (sn.length, n.length); dup = true; }
                if (! dup)
                    ev.notes.push_back ({ n.pitch, n.length, n.velocity });
                continue;
            }

            SrcEvent ev;
            ev.start = n.start;
            ev.notes.push_back ({ n.pitch, n.length, n.velocity });
            srcEvents.push_back (ev);
        }

        int totalNotes = 0;
        for (auto& ev : srcEvents) totalNotes += (int) ev.notes.size();
        avgPolyphony = totalNotes / (double) juce::jmax<size_t> (1, srcEvents.size());
        const bool chordal = avgPolyphony >= 1.6;

        eventsByBar.assign ((size_t) srcBars, {});
        onsetProb.assign ((size_t) stepsPerBar, 0.0);
        velByStep.assign ((size_t) stepsPerBar, 0.0);
        std::vector<int> velCount ((size_t) stepsPerBar, 0);
        std::vector<std::set<int>> barSteps ((size_t) srcBars);

        double legatoSum = 0; int legatoCount = 0;

        for (size_t i = 0; i < srcEvents.size(); ++i)
        {
            auto& ev = srcEvents[i];
            std::sort (ev.notes.begin(), ev.notes.end(), [] (auto& x, auto& y) { return x.pitch < y.pitch; });

            ev.bar = juce::jlimit (0, srcBars - 1, (int) std::floor ((ev.start + 1.0e-6) / beatsPerBar));
            ev.step = (int) std::round ((ev.start - ev.bar * beatsPerBar) / grid);
            if (ev.step >= stepsPerBar)
            {
                if (ev.bar + 1 < srcBars) { ++ev.bar; ev.step = 0; }
                else ev.step = stepsPerBar - 1;
            }
            ev.step = juce::jlimit (0, stepsPerBar - 1, ev.step);

            std::vector<int> pitches;
            float vel = 0;
            for (auto& sn : ev.notes) { pitches.push_back (sn.pitch); vel += sn.velocity; }
            ev.velocity = vel / (float) ev.notes.size();
            ev.anchor = anchorOf (pitches, chordal);
            ev.shape.clear();
            for (int p : pitches) ev.shape.push_back (p - ev.anchor);

            ++shapeCounts[ev.shape];
            pcProb[(size_t) pitchClass (ev.anchor)] += 1.0;

            double anchorLen = 0.25;
            for (auto& sn : ev.notes) if (sn.pitch == ev.anchor) anchorLen = sn.length;
            ++durationSteps[juce::jmax (1, (int) std::round (anchorLen / grid))];

            if (i + 1 < srcEvents.size())
            {
                const double gap = srcEvents[i + 1].start - ev.start;
                if (gap > 1.0e-3)
                {
                    legatoSum += juce::jmin (4.0, anchorLen / gap);
                    ++legatoCount;
                }
            }

            eventsByBar[(size_t) ev.bar].push_back ((int) i);
            barSteps[(size_t) ev.bar].insert (ev.step);
            velByStep[(size_t) ev.step] += ev.velocity;
            ++velCount[(size_t) ev.step];
        }

        legato = legatoCount > 0 ? legatoSum / legatoCount : 1.0;

        for (int st = 0; st < stepsPerBar; ++st)
        {
            int bars = 0;
            for (auto& bs : barSteps) if (bs.count (st) > 0) ++bars;
            onsetProb[(size_t) st] = bars / (double) srcBars;
            velByStep[(size_t) st] = velCount[(size_t) st] > 0 ? velByStep[(size_t) st] / velCount[(size_t) st] : avgVelocity;
        }

        double pcTotal = 0;
        for (double v : pcProb) pcTotal += v;
        if (pcTotal > 0) for (auto& v : pcProb) v /= pcTotal;

        // Melodic motion: interval Markov chains (order 0-2), treated as a loop.
        const int n = (int) srcEvents.size();
        if (n >= 2)
        {
            std::vector<int> iv ((size_t) n);
            for (int k = 0; k < n; ++k)
                iv[(size_t) k] = foldInterval (srcEvents[(size_t) k].anchor - srcEvents[(size_t) ((k - 1 + n) % n)].anchor);

            for (int k = 0; k < n; ++k)
            {
                const int cur = iv[(size_t) k];
                const int p1  = iv[(size_t) ((k - 1 + n) % n)];
                const int p2  = iv[(size_t) ((k - 2 + n) % n)];
                ++interval0[cur];
                ++interval1[p1][cur];
                if (n >= 3)
                    ++interval2[{ p2, p1 }][cur];
            }
        }
        else
        {
            ++interval0[0];
        }

        // Bar fingerprints, so repeated bars in the source stay repeated in the output.
        barSignature.assign ((size_t) srcBars, {});
        for (int b = 0; b < srcBars; ++b)
        {
            juce::String sig;
            for (int idx : eventsByBar[(size_t) b])
            {
                auto& ev = srcEvents[(size_t) idx];
                sig << ev.step << ':';
                for (auto& sn : ev.notes) sig << sn.pitch << ',';
                sig << ';';
            }
            barSignature[(size_t) b] = sig.isEmpty() ? juce::String ("rest") : sig;
        }
    }

    void StyleModel::analyseDrums()
    {
        key = {};
        scale.fill (true);
        avgPolyphony = 1.0;

        drumBars.assign ((size_t) srcBars, {});
        std::map<int, int> hitCount;

        for (auto& n : source.notes)
        {
            int bar = juce::jlimit (0, srcBars - 1, (int) std::floor ((n.start + 1.0e-6) / beatsPerBar));
            int step = (int) std::round ((n.start - bar * beatsPerBar) / grid);
            if (step >= stepsPerBar)
            {
                if (bar + 1 < srcBars) { ++bar; step = 0; }
                else step = stepsPerBar - 1;
            }
            step = juce::jlimit (0, stepsPerBar - 1, step);

            auto& v = drumBars[(size_t) bar][n.pitch];
            if (v.empty()) v.assign ((size_t) stepsPerBar, 0.0f);
            v[(size_t) step] = juce::jmax (v[(size_t) step], n.velocity);
            ++hitCount[n.pitch];
        }

        for (auto& [pitch, cnt] : hitCount)
        {
            juce::ignoreUnused (cnt);
            drumProb[pitch].assign ((size_t) stepsPerBar, 0.0);
            drumVel[pitch].assign ((size_t) stepsPerBar, 0.0);
        }

        for (auto& bar : drumBars)
            for (auto& [pitch, v] : bar)
                for (int st = 0; st < stepsPerBar; ++st)
                    if (v[(size_t) st] > 0.0f)
                    {
                        drumProb[pitch][(size_t) st] += 1.0;
                        drumVel[pitch][(size_t) st] += v[(size_t) st];
                    }

        for (auto& [pitch, probs] : drumProb)
            for (int st = 0; st < stepsPerBar; ++st)
            {
                auto& vel = drumVel[pitch][(size_t) st];
                vel = probs[(size_t) st] > 0 ? vel / probs[(size_t) st] : avgVelocity;
                probs[(size_t) st] /= srcBars;
            }

        // The busiest non-kick, non-snare voice is almost always the hi-hat.
        int best = -1;
        hatPitch = 42;
        for (auto& [pitch, cnt] : hitCount)
            if (pitch != 35 && pitch != 36 && pitch != 38 && pitch != 40 && cnt > best)
            {
                best = cnt;
                hatPitch = pitch;
            }

        barSignature.assign ((size_t) srcBars, {});
        for (int b = 0; b < srcBars; ++b)
        {
            juce::String sig;
            for (auto& [pitch, v] : drumBars[(size_t) b])
            {
                sig << pitch << ':';
                for (int st = 0; st < stepsPerBar; ++st)
                    if (v[(size_t) st] > 0.0f) sig << st << ',';
                sig << ';';
            }
            barSignature[(size_t) b] = sig.isEmpty() ? juce::String ("rest") : sig;
        }
    }

    //==============================================================================
    int StyleModel::snap (int pitch, int preferDir) const
    {
        if (scale[(size_t) pitchClass (pitch)])
            return pitch;

        const bool down = scale[(size_t) pitchClass (pitch - 1)];
        const bool up   = scale[(size_t) pitchClass (pitch + 1)];
        if (down && up) return preferDir > 0 ? pitch + 1 : pitch - 1;
        if (down) return pitch - 1;
        if (up)   return pitch + 1;
        return pitch;
    }

    int StyleModel::sampleDurationSteps (juce::Random& r) const
    {
        std::vector<std::pair<int, double>> items;
        for (auto& [steps, cnt] : durationSteps)
            items.push_back ({ steps, (double) cnt });
        return pickWeighted (r, items, 1);
    }

    std::vector<int> StyleModel::pickShape (juce::Random& r, float creativity) const
    {
        if (shapeCounts.empty())
            return { 0 };

        const double T = 0.35 + 1.3 * creativity;
        std::vector<std::pair<std::vector<int>, double>> items;
        for (auto& [shape, cnt] : shapeCounts)
            items.push_back ({ shape, std::pow ((double) cnt, 1.0 / T) });
        return pickWeighted (r, items, std::vector<int> { 0 });
    }

    int StyleModel::pickAnchor (juce::Random& r, int prev, int iv1, int iv2, float creativity,
                                bool lock, int target, float pull) const
    {
        const std::map<int, int>* dist = &interval0;

        auto it2 = interval2.find ({ iv2, iv1 });
        int total2 = 0;
        if (it2 != interval2.end())
            for (auto& kv : it2->second) total2 += kv.second;

        if (total2 >= 2)
            dist = &it2->second;
        else if (auto it1 = interval1.find (iv1); it1 != interval1.end())
            dist = &it1->second;

        double total = 0;
        for (auto& kv : *dist) total += kv.second;
        if (total <= 0) total = 1;

        double maxPc = 0;
        for (double v : pcProb) maxPc = juce::jmax (maxPc, v);

        const double T   = 0.35 + 1.3 * creativity;
        const double eps = 0.004 + 0.05 * creativity;
        const double width = 1.0 + 10.0 * (1.0 - pull);

        std::vector<std::pair<int, double>> cands;
        for (int iv = -12; iv <= 12; ++iv)
        {
            const int p = prev + iv;
            if (p < 0 || p > 127) continue;

            const int pc = pitchClass (p);
            if (lock && ! scale[(size_t) pc]) continue;

            auto f = dist->find (iv);
            double w = (f != dist->end() ? f->second / total : 0.0) + eps;
            w *= 0.3 + (maxPc > 0 ? pcProb[(size_t) pc] / maxPc : 1.0);

            if (p < lowPitch - 3 || p > highPitch + 3)
                w *= 0.03;

            if (target >= 0)
                w *= std::exp (-std::abs (p - target) / width);

            cands.push_back ({ p, std::pow (w, 1.0 / T) });
        }

        if (cands.empty())
            return snap (prev, 0);

        return pickWeighted (r, cands, prev);
    }

    //==============================================================================
    Phrase StyleModel::generate (const GenParams& params) const
    {
        if (! ready)
            return {};

        return drums ? generateDrums (params) : generateMelodic (params);
    }

    Phrase StyleModel::generateMelodic (const GenParams& gp) const
    {
        juce::Random rng ((juce::int64) gp.seed * 7919 + 17);

        const float s = juce::jlimit (0.0f, 1.0f, gp.similarity);
        const float c = juce::jlimit (0.0f, 1.0f, gp.creativity);
        const float d = juce::jlimit (-1.0f, 1.0f, gp.density);
        const int outBars = gp.bars > 0 ? gp.bars : srcBars;
        const bool chordal = avgPolyphony >= 1.6;
        const bool mono = avgPolyphony < 1.15;
        const bool lock = gp.lockToKey;

        // Seed the melodic history from the end of the source so the first note follows naturally.
        const int n = (int) srcEvents.size();
        int prev = srcEvents.back().anchor;
        int iv1 = n >= 2 ? foldInterval (srcEvents[(size_t) (n - 1)].anchor - srcEvents[(size_t) (n - 2)].anchor) : 0;
        int iv2 = n >= 3 ? foldInterval (srcEvents[(size_t) (n - 2)].anchor - srcEvents[(size_t) (n - 3)].anchor) : 0;

        auto pushHistory = [&] (int anchor)
        {
            iv2 = iv1;
            iv1 = foldInterval (anchor - prev);
            prev = anchor;
        };

        auto buildPitches = [&] (int anchor, const std::vector<int>& shape)
        {
            std::vector<int> out;
            for (int iv : shape)
            {
                int p = anchor + iv;
                if (lock && iv != 0) p = snap (p, -1);
                p = juce::jlimit (0, 127, p);
                if (std::find (out.begin(), out.end(), p) == out.end())
                    out.push_back (p);
            }
            std::sort (out.begin(), out.end());
            return out;
        };

        std::vector<std::vector<OutEvent>> bars ((size_t) outBars);
        std::map<juce::String, int> firstBarForSignature;

        for (int ob = 0; ob < outBars; ++ob)
        {
            int sb = ob % srcBars;
            const bool variationSlot = (ob % 4 == 3);
            if (srcBars > 1 && rng.nextFloat() < (1.0f - s) * 0.3f)
                sb = rng.nextInt (srcBars);

            const auto& sig = barSignature[(size_t) sb];

            // Repeat an earlier output bar where the source repeats itself.
            if (auto found = firstBarForSignature.find (sig); found != firstBarForSignature.end())
            {
                float reuse = 0.35f + 0.45f * s;
                if (variationSlot) reuse *= 0.5f;

                if (rng.nextFloat() < reuse)
                {
                    auto copy = bars[(size_t) found->second];
                    for (auto& e : copy)
                    {
                        if (! e.pitches.empty() && rng.nextFloat() < (1.0f - s) * 0.2f)
                        {
                            const int old = anchorOf (e.pitches, chordal);
                            const int a = pickAnchor (rng, prev, iv1, iv2, c, lock, old, 0.8f);
                            std::vector<int> shape;
                            for (int p : e.pitches) shape.push_back (p - old);
                            e.pitches = buildPitches (a, shape);
                        }
                        pushHistory (anchorOf (e.pitches, chordal));
                    }
                    bars[(size_t) ob] = std::move (copy);
                    continue;
                }
            }

            std::vector<OutEvent> evs;
            const bool sourceBarEmpty = eventsByBar[(size_t) sb].empty();
            const bool markovBar = ! gp.keepRhythm
                && (sourceBarEmpty ? rng.nextFloat() < (1.0f - s) * 0.3f + juce::jmax (0.0f, d) * 0.3f
                                   : rng.nextFloat() < std::pow (1.0f - s, 1.5f) * 0.9f);

            if (markovBar)
            {
                // A new bar drawn from the source's groove (where in the bar it tends to play).
                const double densMult = std::pow (2.0, (double) d);
                double meanP = 0;
                for (double p : onsetProb) meanP += p;
                meanP /= (double) stepsPerBar;

                for (int st = 0; st < stepsPerBar; ++st)
                {
                    double p = onsetProb[(size_t) st];
                    p = p + (meanP - p) * c * 0.4;
                    p = juce::jlimit (0.0, 0.98, p * densMult);
                    if (rng.nextDouble() < p)
                    {
                        OutEvent e;
                        e.start = st * grid;
                        e.length = sampleDurationSteps (rng) * grid;
                        e.velocity = (float) (velByStep[(size_t) st] + gauss (rng) * velocitySpread * 0.5);
                        evs.push_back (e);
                    }
                }

                if (evs.empty())
                {
                    const int st = (int) std::distance (onsetProb.begin(), std::max_element (onsetProb.begin(), onsetProb.end()));
                    OutEvent e;
                    e.start = st * grid;
                    e.length = sampleDurationSteps (rng) * grid;
                    e.velocity = (float) velByStep[(size_t) st];
                    evs.push_back (e);
                }
            }
            else
            {
                for (int idx : eventsByBar[(size_t) sb])
                {
                    const auto& se = srcEvents[(size_t) idx];
                    OutEvent e;
                    e.start = se.start - se.bar * beatsPerBar;
                    e.length = 0.25;
                    for (auto& sn : se.notes) if (sn.pitch == se.anchor) e.length = sn.length;
                    e.velocity = se.velocity;
                    e.templateIndex = idx;
                    e.keepPitch = rng.nextFloat() < std::pow (s, 0.7f);
                    evs.push_back (e);
                }

                if (! gp.keepRhythm)
                {
                    const float pm = (1.0f - s) * 0.5f + std::abs (d) * 0.35f;
                    std::vector<OutEvent> mutated;

                    for (auto& e : evs)
                    {
                        if (rng.nextFloat() >= pm) { mutated.push_back (e); continue; }

                        const double wDrop = 1.0 - 0.8 * d, wShift = 1.0, wSplit = 0.6 * (1.0 + d), wAdd = 0.6 * (1.0 + d);
                        const int op = pickWeighted<int> (rng, { { 0, wDrop }, { 1, wShift }, { 2, wSplit }, { 3, wAdd } }, 1);

                        if (op == 0)
                        {
                            if (e.start < 1.0e-3 && rng.nextFloat() < 0.7f)
                                mutated.push_back (e); // protect the downbeat most of the time
                        }
                        else if (op == 1)
                        {
                            const double ns = (std::round (e.start / grid) + (rng.nextBool() ? 1 : -1)) * grid;
                            if (ns >= 0.0 && ns <= beatsPerBar - grid) e.start = ns;
                            mutated.push_back (e);
                        }
                        else if (op == 2 && e.length >= 2.0 * grid - 1.0e-6)
                        {
                            const double half = juce::jmax (grid, std::round (e.length / grid / 2.0) * grid);
                            OutEvent a = e, b = e;
                            a.length = half;
                            b.start = e.start + half;
                            b.length = juce::jmax (grid * 0.5, e.length - half);
                            b.keepPitch = false;
                            mutated.push_back (a);
                            mutated.push_back (b);
                        }
                        else if (op == 3)
                        {
                            mutated.push_back (e);
                            OutEvent extra = e;
                            extra.start = std::round ((e.start + juce::jmax (grid, e.length)) / grid) * grid;
                            extra.length = sampleDurationSteps (rng) * grid;
                            extra.keepPitch = false;
                            if (extra.start <= beatsPerBar - grid)
                                mutated.push_back (extra);
                        }
                        else
                        {
                            mutated.push_back (e);
                        }
                    }

                    std::stable_sort (mutated.begin(), mutated.end(), [] (auto& x, auto& y) { return x.start < y.start; });
                    evs.clear();
                    for (auto& e : mutated)
                        if (evs.empty() || std::abs (e.start - evs.back().start) > 1.0e-4)
                            evs.push_back (e);
                }
            }

            // Pitches, in time order so the melodic chain flows across bars.
            for (auto& e : evs)
            {
                if (e.templateIndex >= 0 && e.keepPitch)
                {
                    const auto& se = srcEvents[(size_t) e.templateIndex];
                    for (auto& sn : se.notes) e.pitches.push_back (sn.pitch);
                    pushHistory (se.anchor);
                    continue;
                }

                const int target = e.templateIndex >= 0 ? srcEvents[(size_t) e.templateIndex].anchor : -1;
                const int a = pickAnchor (rng, prev, iv1, iv2, c, lock, target, s);

                std::vector<int> shape;
                if (e.templateIndex >= 0 && rng.nextFloat() < s)
                    shape = srcEvents[(size_t) e.templateIndex].shape;
                else if (mono)
                    shape = { 0 };
                else
                    shape = pickShape (rng, c);

                e.pitches = buildPitches (a, shape);
                pushHistory (a);
            }

            bars[(size_t) ob] = evs;
            if (firstBarForSignature.find (sig) == firstBarForSignature.end())
                firstBarForSignature[sig] = ob;
        }

        // Flatten bars into a phrase.
        Phrase out;
        out.bpm = source.bpm;
        out.timeSigNum = source.timeSigNum;
        out.timeSigDen = source.timeSigDen;
        out.isDrums = false;
        out.lengthBeats = outBars * beatsPerBar;

        struct Flat { double start, length; std::vector<int> pitches; float vel; bool generated; };
        std::vector<Flat> flat;
        for (int ob = 0; ob < outBars; ++ob)
            for (auto& e : bars[(size_t) ob])
                flat.push_back ({ ob * beatsPerBar + e.start, e.length, e.pitches, e.velocity, ! e.keepPitch });

        std::stable_sort (flat.begin(), flat.end(), [] (auto& x, auto& y) { return x.start < y.start; });

        const int shift = gp.transpose + 12 * gp.octave;
        const double h = juce::jlimit (0.0f, 1.0f, gp.humanize);

        for (size_t i = 0; i < flat.size(); ++i)
        {
            auto& f = flat[i];
            const double nextStart = (i + 1 < flat.size()) ? flat[i + 1].start : out.lengthBeats;
            const double gap = juce::jmax (grid * 0.25, nextStart - f.start);

            double len = f.length;
            if (mono)
                len = juce::jmin (len, gap);
            else if (f.generated)
                len = juce::jmin (len, gap * juce::jmax (1.0, legato));

            double start = f.start + gauss (rng) * h * 0.02;
            start = juce::jlimit (0.0, out.lengthBeats - grid * 0.25, start);
            len = juce::jlimit (grid * 0.25, juce::jmax (grid * 0.25, out.lengthBeats - start), len);

            const float vel = juce::jlimit (0.05f, 1.0f, f.vel + (float) (gauss (rng) * h * 0.08));

            for (int p : f.pitches)
            {
                Note note;
                note.pitch = juce::jlimit (0, 127, p + shift);
                note.start = start;
                note.length = len;
                note.velocity = vel;
                note.channel = channel;
                out.notes.push_back (note);
            }
        }

        out.sort();
        return out;
    }

    Phrase StyleModel::generateDrums (const GenParams& gp) const
    {
        juce::Random rng ((juce::int64) gp.seed * 104729 + 3);

        const float s = juce::jlimit (0.0f, 1.0f, gp.similarity);
        const float c = juce::jlimit (0.0f, 1.0f, gp.creativity);
        const float d = juce::jlimit (-1.0f, 1.0f, gp.density);
        const int outBars = gp.bars > 0 ? gp.bars : srcBars;
        const double densMult = std::pow (2.0, (double) d);

        // The voice used for fills: the most-used pitch that isn't the kick or the hat.
        int fillPitch = -1;
        {
            double best = -1;
            for (auto& [pitch, probs] : drumProb)
            {
                if (pitch == 35 || pitch == 36 || pitch == hatPitch) continue;
                double sum = 0;
                for (double p : probs) sum += p;
                if (sum > best) { best = sum; fillPitch = pitch; }
            }
        }

        std::vector<std::vector<Note>> bars ((size_t) outBars);
        std::map<juce::String, int> firstBarForSignature;

        for (int ob = 0; ob < outBars; ++ob)
        {
            int sb = ob % srcBars;
            const bool variationSlot = (ob % 4 == 3);
            if (srcBars > 1 && rng.nextFloat() < (1.0f - s) * 0.25f)
                sb = rng.nextInt (srcBars);

            const auto& sig = barSignature[(size_t) sb];
            if (auto found = firstBarForSignature.find (sig); found != firstBarForSignature.end())
            {
                float reuse = 0.3f + 0.5f * s;
                if (variationSlot) reuse *= 0.5f;
                if (rng.nextFloat() < reuse)
                {
                    bars[(size_t) ob] = bars[(size_t) found->second];
                    continue;
                }
            }

            std::vector<Note> notes;
            const auto& srcBar = drumBars[(size_t) sb];

            for (auto& [pitch, probs] : drumProb)
            {
                auto hits = srcBar.find (pitch);
                const bool isKick = (pitch == 35 || pitch == 36);

                for (int st = 0; st < stepsPerBar; ++st)
                {
                    const float orig = hits != srcBar.end() ? hits->second[(size_t) st] : 0.0f;
                    bool hit;

                    if (gp.keepRhythm && isKick)
                    {
                        hit = orig > 0.0f;
                    }
                    else
                    {
                        double p = s * (orig > 0.0f ? 1.0 : 0.0) + (1.0 - s) * probs[(size_t) st];
                        p = juce::jlimit (0.0, 1.0, p * densMult);
                        hit = (rng.nextFloat() < 0.15f + 0.7f * c) ? (rng.nextDouble() < p) : (p >= 0.5);
                    }

                    if (hit)
                    {
                        Note note;
                        note.pitch = pitch;
                        note.start = st * grid;
                        note.length = grid * 0.5;
                        note.velocity = orig > 0.0f ? orig : (float) drumVel.at (pitch)[(size_t) st];
                        note.channel = channel;
                        notes.push_back (note);
                    }
                }
            }

            // Hi-hat rolls (trap style), more likely with high creativity.
            if (grid <= 0.25 + 1.0e-9 && rng.nextFloat() < c * 0.35f)
            {
                std::vector<size_t> hats;
                for (size_t i = 0; i < notes.size(); ++i)
                    if (notes[i].pitch == hatPitch) hats.push_back (i);

                if (! hats.empty())
                {
                    const Note base = notes[hats[(size_t) rng.nextInt ((int) hats.size())]];
                    const int count = 2 + rng.nextInt (3);
                    const double sub = grid / count;
                    notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& x)
                                 { return x.pitch == base.pitch && std::abs (x.start - base.start) < 1.0e-6; }), notes.end());
                    for (int k = 0; k < count; ++k)
                    {
                        Note r = base;
                        r.start = base.start + k * sub;
                        r.length = sub * 0.8;
                        r.velocity = juce::jlimit (0.1f, 1.0f, base.velocity * (0.65f + 0.35f * (float) k / (float) count));
                        notes.push_back (r);
                    }
                }
            }

            // Fill at the end of a 4-bar phrase.
            if (variationSlot && fillPitch >= 0 && rng.nextFloat() < c * 0.6f)
            {
                const int hitsToAdd = 1 + rng.nextInt (3);
                const int firstStep = (stepsPerBar * 3) / 4;
                for (int k = 0; k < hitsToAdd; ++k)
                {
                    Note note;
                    note.pitch = fillPitch;
                    note.start = (firstStep + rng.nextInt (juce::jmax (1, stepsPerBar - firstStep))) * grid;
                    note.length = grid * 0.5;
                    note.velocity = juce::jlimit (0.2f, 1.0f, avgVelocity * (0.7f + 0.4f * rng.nextFloat()));
                    note.channel = channel;
                    notes.push_back (note);
                }
            }

            bars[(size_t) ob] = notes;
            if (firstBarForSignature.find (sig) == firstBarForSignature.end())
                firstBarForSignature[sig] = ob;
        }

        Phrase out;
        out.bpm = source.bpm;
        out.timeSigNum = source.timeSigNum;
        out.timeSigDen = source.timeSigDen;
        out.isDrums = true;
        out.lengthBeats = outBars * beatsPerBar;

        const double h = juce::jlimit (0.0f, 1.0f, gp.humanize);
        std::set<std::pair<long long, int>> seen; // drop exact duplicates

        for (int ob = 0; ob < outBars; ++ob)
            for (auto note : bars[(size_t) ob])
            {
                note.start += ob * beatsPerBar;
                const auto keyStep = std::make_pair ((long long) std::llround (note.start * 960.0), note.pitch);
                if (! seen.insert (keyStep).second) continue;

                note.start = juce::jlimit (0.0, out.lengthBeats - 0.01, note.start + gauss (rng) * h * 0.008);
                note.velocity = juce::jlimit (0.05f, 1.0f, note.velocity + (float) (gauss (rng) * h * 0.1));
                // Transpose/octave are ignored for drums: shifting a drum note changes the instrument.
                out.notes.push_back (note);
            }

        out.sort();
        return out;
    }
}
