#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/Phrase.h"
#include "MimicLookAndFeel.h"
#include <set>

/** Read-only piano roll that draws a phrase, its bar grid and a playhead. */
class PianoRoll : public juce::Component
{
public:
    PianoRoll (juce::Colour noteColour, juce::String emptyMessage)
        : colour (noteColour), emptyText (std::move (emptyMessage)) {}

    void setPhrase (mm::Phrase p)
    {
        phrase = std::move (p);
        rows.clear();

        if (phrase.isDrums)
        {
            std::set<int> pitches;
            for (auto& n : phrase.notes) pitches.insert (n.pitch);
            rows.assign (pitches.rbegin(), pitches.rend());
        }
        else if (! phrase.empty())
        {
            int lo = phrase.lowestPitch() - 2, hi = phrase.highestPitch() + 2;
            while (hi - lo < 14) { --lo; ++hi; }
            for (int pitch = hi; pitch >= lo; --pitch) rows.push_back (pitch);
        }
        repaint();
    }

    void setEmptyText (const juce::String& t)  { emptyText = t; repaint(); }
    void setHighlight (bool h)                 { if (h != highlight) { highlight = h; repaint(); } }

    void setPlayhead (double beat)
    {
        if (std::abs (beat - playhead) < 1.0e-4)
            return;
        const int oldX = playheadX();
        playhead = beat;
        const int newX = playheadX();
        if (oldX >= 0) repaint (oldX - 2, 0, 5, getHeight());
        if (newX >= 0) repaint (newX - 2, 0, 5, getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (highlight ? Palette::teal : Palette::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, highlight ? 2.0f : 1.0f);

        if (phrase.empty() || rows.empty())
        {
            g.setColour (Palette::dim);
            g.setFont (FontOptions (15.0f));
            g.drawFittedText (emptyText, getLocalBounds().reduced (20), Justification::centred, 3);
            return;
        }

        const auto area = noteArea();
        const float rowH = area.getHeight() / (float) rows.size();
        const double L = juce::jmax (1.0, phrase.lengthBeats);
        auto xFor = [&] (double beat) { return area.getX() + (float) (beat / L) * area.getWidth(); };

        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (getLocalBounds().reduced (1));

        // Lanes
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const float y = area.getY() + (float) i * rowH;
            const int pc = rows[i] % 12;
            const bool black = ! phrase.isDrums && (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
            if (black || (phrase.isDrums && i % 2 == 1))
            {
                g.setColour (Palette::bg.withAlpha (0.55f));
                g.fillRect (area.getX(), y, area.getWidth(), rowH);
            }
            if (! phrase.isDrums && pc == 0)
            {
                g.setColour (Palette::line.withAlpha (0.8f));
                g.fillRect (area.getX(), y + rowH - 1.0f, area.getWidth(), 1.0f);
            }
        }

        // Row labels
        g.setFont (FontOptions (11.0f));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const float y = area.getY() + (float) i * rowH;
            String label;
            if (phrase.isDrums)            label = drumName (rows[i]);
            else if (rows[i] % 12 == 0)    label = "C" + String (rows[i] / 12 - 1);
            if (label.isNotEmpty() && rowH >= 7.0f)
            {
                g.setColour (Palette::dim);
                g.drawText (label, Rectangle<float> (4.0f, y - 2.0f, gutter() - 6.0f, juce::jmax (rowH, 12.0f)), Justification::centredLeft);
            }
        }

        // Beat / bar grid
        const double bpb = phrase.beatsPerBar();
        for (double b = 0.0; b <= L + 1.0e-6; b += 1.0)
        {
            const bool bar = std::fmod (b + 1.0e-6, bpb) < 1.0e-3;
            g.setColour (bar ? Palette::line.brighter (0.25f) : Palette::line.withAlpha (0.55f));
            g.fillRect (xFor (b), area.getY(), bar ? 1.2f : 0.8f, area.getHeight());
            if (bar && b < L - 1.0e-6)
            {
                g.setColour (Palette::dim);
                g.drawText (String ((int) std::round (b / bpb) + 1), Rectangle<float> (xFor (b) + 3.0f, 2.0f, 30.0f, 12.0f),
                            Justification::centredLeft);
            }
        }

        // Notes
        for (auto& n : phrase.notes)
        {
            auto it = std::find (rows.begin(), rows.end(), n.pitch);
            if (it == rows.end()) continue;
            const float y = area.getY() + (float) std::distance (rows.begin(), it) * rowH;
            const float x = xFor (n.start);
            const float w = phrase.isDrums ? juce::jmax (3.0f, juce::jmin (rowH, (float) (0.2 / L) * area.getWidth()))
                                           : juce::jmax (2.5f, (float) (n.length / L) * area.getWidth() - 1.0f);
            auto rect = Rectangle<float> (x, y + 1.0f, w, juce::jmax (2.0f, rowH - 2.0f));
            g.setColour (colour.withAlpha (0.35f + 0.65f * n.velocity));
            g.fillRoundedRectangle (rect, juce::jmin (3.0f, rowH * 0.3f));
        }

        // Playhead
        if (const int px = playheadX(); px >= 0)
        {
            g.setColour (Palette::text.withAlpha (0.9f));
            g.fillRect ((float) px, area.getY(), 1.6f, area.getHeight());
        }
    }

private:
    float gutter() const { return phrase.isDrums ? 64.0f : 30.0f; }

    juce::Rectangle<float> noteArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (gutter()).withTrimmedTop (16.0f).reduced (2.0f, 4.0f);
    }

    int playheadX() const
    {
        if (playhead < 0.0 || phrase.empty())
            return -1;
        const auto area = noteArea();
        return (int) (area.getX() + (float) (playhead / juce::jmax (1.0, phrase.lengthBeats)) * area.getWidth());
    }

    static juce::String drumName (int p)
    {
        switch (p)
        {
            case 35: case 36: return "Kick";
            case 37: return "Rim";
            case 38: case 40: return "Snare";
            case 39: return "Clap";
            case 42: return "Hat";
            case 44: return "Pedal hat";
            case 46: return "Open hat";
            case 49: case 57: return "Crash";
            case 51: case 59: return "Ride";
            case 41: case 43: case 45: case 47: case 48: case 50: return "Tom";
            default: return juce::MidiMessage::getMidiNoteName (p, true, true, 4);
        }
    }

    mm::Phrase phrase;
    std::vector<int> rows;
    juce::Colour colour;
    juce::String emptyText;
    double playhead = -1.0;
    bool highlight = false;
};
