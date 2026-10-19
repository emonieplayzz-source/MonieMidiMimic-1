#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour bg       { 0xff0f0f15 };
    inline const juce::Colour panel    { 0xff17171f };
    inline const juce::Colour panelHi  { 0xff20202b };
    inline const juce::Colour line     { 0xff2c2c3b };
    inline const juce::Colour text     { 0xffeceaf4 };
    inline const juce::Colour dim      { 0xff8b89a3 };
    inline const juce::Colour pink     { 0xffff4fa3 };
    inline const juce::Colour teal     { 0xff36e2c4 };
    inline const juce::Colour amber    { 0xffffb547 };
    inline const juce::Colour red      { 0xffff5f6d };
}

class MimicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MimicLookAndFeel()
    {
        using namespace juce;
        setColourScheme ({ Palette::bg, Palette::panelHi, Palette::panelHi, Palette::line, Palette::text,
                           Palette::pink, Colours::white, Palette::pink, Palette::text });

        setColour (Label::textColourId, Palette::text);
        setColour (TextButton::buttonColourId, Palette::panelHi);
        setColour (TextButton::buttonOnColourId, Palette::pink);
        setColour (TextButton::textColourOffId, Palette::text);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (ComboBox::backgroundColourId, Palette::panelHi);
        setColour (ComboBox::outlineColourId, Palette::line);
        setColour (ComboBox::textColourId, Palette::text);
        setColour (ComboBox::arrowColourId, Palette::dim);
        setColour (PopupMenu::backgroundColourId, Palette::panelHi);
        setColour (PopupMenu::highlightedBackgroundColourId, Palette::pink.withAlpha (0.8f));
        setColour (Slider::textBoxTextColourId, Palette::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::trackColourId, Palette::pink);
        setColour (Slider::backgroundColourId, Palette::line);
        setColour (Slider::thumbColourId, Palette::text);
        setColour (ToggleButton::textColourId, Palette::text);
        setColour (AlertWindow::backgroundColourId, Palette::panel);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = jmax (3.0f, radius * 0.16f);
        const float arcR = radius - lineW * 0.5f;

        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::line);
        g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        const bool bipolar = slider.getProperties()["bipolar"];
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const float to = startAngle + pos * (endAngle - startAngle);
        const auto colour = slider.findColour (Slider::rotarySliderFillColourId);

        if (std::abs (to - from) > 0.001f)
        {
            Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (from, to), jmax (from, to), true);
            g.setColour (slider.isEnabled() ? colour : Palette::dim);
            g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.setColour (Palette::panelHi);
        g.fillEllipse (Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));

        const auto tip = centre.getPointOnCircumference (radius * 0.5f, to);
        g.setColour (Palette::text);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.18f, to), tip }, 2.2f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat();
        const float h = jmin (16.0f, r.getHeight() - 4.0f);
        auto pill = Rectangle<float> (0.0f, 0.0f, h * 1.8f, h).withCentre ({ r.getX() + h * 0.9f + 2.0f, r.getCentreY() });

        const bool on = b.getToggleState();
        g.setColour (on ? Palette::teal.withAlpha (0.9f) : Palette::line.brighter (highlighted ? 0.2f : 0.0f));
        g.fillRoundedRectangle (pill, h * 0.5f);

        const float knob = h - 4.0f;
        const float kx = on ? pill.getRight() - knob - 2.0f : pill.getX() + 2.0f;
        g.setColour (on ? Palette::bg : Palette::text.withAlpha (0.8f));
        g.fillEllipse (kx, pill.getY() + 2.0f, knob, knob);

        g.setColour (b.isEnabled() ? Palette::text : Palette::dim);
        g.setFont (FontOptions (13.0f));
        g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (pill.getWidth() + 8.0f).toNearestInt(),
                          Justification::centredLeft, 1);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                               bool highlighted, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool primary = b.getProperties()["primary"];

        if (primary)
        {
            ColourGradient grad (Palette::pink, r.getX(), r.getY(), Palette::pink.withRotatedHue (-0.08f), r.getRight(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.setOpacity (! b.isEnabled() ? 0.35f : down ? 0.8f : highlighted ? 1.0f : 0.92f);
            g.fillRoundedRectangle (r, 8.0f);
            g.setOpacity (1.0f);
            return;
        }

        auto c = b.getToggleState() ? Palette::pink.withAlpha (0.85f) : bgColour;
        if (down) c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.08f);
        if (! b.isEnabled()) c = c.withAlpha (0.4f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Palette::line.brighter (0.15f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        const bool primary = b.getProperties()["primary"];
        return juce::Font (juce::FontOptions (primary ? juce::jmin (18.0f, buttonHeight * 0.42f)
                                                      : juce::jmin (14.0f, buttonHeight * 0.5f),
                                              primary ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override    { return juce::Font (juce::FontOptions (13.5f)); }
    juce::Font getPopupMenuFont() override                  { return juce::Font (juce::FontOptions (14.0f)); }
};
