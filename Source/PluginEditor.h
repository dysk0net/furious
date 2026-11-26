#pragma once
// Stratum - editor. Fixed 1280x720 design surface that scales with the window.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

namespace ui
{
namespace col
{
    const juce::Colour bg (0xff171925), panel (0xff222536), panelHi (0xff2d3147), line (0xff3a3f57),
                       text (0xffdfe1ea), dim (0xff8a90a6), orange (0xffffb36b), blue (0xff52a8ff),
                       pink (0xffff5f8a), hdrTop (0xff9a9ead), hdrBot (0xff767a8b), hdrText (0xff1c1e29);
}
inline juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

//==============================================================================
class LnF : public juce::LookAndFeel_V4
{
public:
    LnF()
    {
        setColour (juce::ComboBox::backgroundColourId, col::bg);
        setColour (juce::ComboBox::outlineColourId, col::line);
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::orange.withAlpha (0.8f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::Label::textColourId, col::dim);
        setColour (juce::TextButton::buttonColourId, col::panelHi);
        setColour (juce::TextButton::textColourOffId, col::text);
        setColour (juce::TextEditor::backgroundColourId, col::bg);
        setColour (juce::TextEditor::textColourId, col::text);
        setColour (juce::TextEditor::outlineColourId, col::line);
        setColour (juce::ListBox::backgroundColourId, col::bg);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        float d = std::min (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (d, d);
        const float r = d * 0.5f;
        const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0);

        juce::Path track; track.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, a0, a1, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (track, juce::PathStrokeType (2.5f));

        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
        juce::Path val; val.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, std::min (zero, ang), std::max (zero, ang), true);
        g.setColour (s.isEnabled() ? col::orange : col::dim);
        g.strokePath (val, juce::PathStrokeType (3.0f));

        if (s.getProperties().contains ("mod"))
        {
            float m = (float) s.getProperties()["mod"];
            if (std::fabs (m) > 0.001f)
            {
                float a2 = juce::jlimit (a0, a1, ang + m * (a1 - a0));
                juce::Path mp; mp.addCentredArc (c.x, c.y, r + 0.5f, r + 0.5f, 0.0f, std::min (ang, a2), std::max (ang, a2), true);
                g.setColour (col::blue);
                g.strokePath (mp, juce::PathStrokeType (2.0f));
            }
        }

        auto body = b.reduced (r * 0.22f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f66), body.getX(), body.getY(), juce::Colour (0xff23263a), body.getX(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (body, 1.0f);
        const float pr = body.getWidth() * 0.5f;
        g.setColour (col::text);
        g.drawLine (c.x + std::sin (ang) * pr * 0.25f, c.y - std::cos (ang) * pr * 0.25f,
                    c.x + std::sin (ang) * pr * 0.92f, c.y - std::cos (ang) * pr * 0.92f, 2.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (col::bg); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.hasKeyboardFocus (true) ? col::orange : col::line); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        juce::Path p;
        p.addTriangle ((float) w - 14.0f, (float) h * 0.4f, (float) w - 6.0f, (float) h * 0.4f, (float) w - 10.0f, (float) h * 0.62f);
        g.setColour (col::dim); g.fillPath (p);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont (font (13.0f));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float s = std::min (11.0f, r.getHeight() - 2.0f);
        auto led = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - s * 0.5f, s, s);
        g.setColour (col::bg); g.fillRoundedRectangle (led, 2.0f);
        g.setColour (col::line); g.drawRoundedRectangle (led, 2.0f, 1.0f);
        if (b.getToggleState()) { g.setColour (col::orange); g.fillRoundedRectangle (led.reduced (2.0f), 1.0f); }
        if (b.getButtonText().isNotEmpty())
        {
            g.setColour (over ? col::text : (b.getToggleState() ? col::text : col::dim));
            g.setFont (font (13.0f));
            g.drawText (b.getButtonText(), r.withTrimmedLeft (s + 7.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? col::orange.withAlpha (0.6f) : (over ? col::panelHi.brighter (0.15f) : col::panelHi));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::line); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f); }
};

//==============================================================================
// Parameter-bound controls. The layer index can change, so every control can be re-bound.
class Ctl : public juce::Component
{
public:
    explicit Ctl (stratum::APVTS& a) : apvts (a) {}
    void setSuffix (const juce::String& s, bool g = false) { suffix = s; global = g; }
    juce::String id (int layer) const { return global ? suffix : stratum::layerId (layer, suffix); }
    const juce::String& getSuffix() const { return suffix; }
    virtual void rebind (int layer) = 0;
protected:
    stratum::APVTS& apvts;
    juce::String suffix;
    bool global = false;
};

class Knob : public Ctl
{
public:
    Knob (stratum::APVTS& a, const juce::String& label, const juce::String& suf, bool glob = false) : Ctl (a)
    {
        setSuffix (suf, glob);
        addAndMakeVisible (slider); addAndMakeVisible (name);
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setScrollWheelEnabled (false);
        name.setText (label, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setFont (font (13.0f));
        name.setInterceptsMouseClicks (false, false);
    }
    void setLabel (const juce::String& t) { name.setText (t, juce::dontSendNotification); }
    void setMod (float m)
    {
        if (m == lastMod) return;
        lastMod = m; slider.getProperties().set ("mod", m); slider.repaint();
    }
    void rebind (int layer) override { att.reset(); att = std::make_unique<stratum::APVTS::SliderAttachment> (apvts, id (layer), slider); }
    void resized() override { auto r = getLocalBounds(); name.setBounds (r.removeFromBottom (15)); slider.setBounds (r); }
    juce::Slider slider;
    juce::Label name;
    int modTarget = -1;
private:
    std::unique_ptr<stratum::APVTS::SliderAttachment> att;
    float lastMod = 0.0f;
};

// Draggable "Name  value" readout in the style of the oscillator morph/phase fields.
class DragLabel : public Ctl
{
public:
    DragLabel (stratum::APVTS& a, const juce::String& label, const juce::String& suf, bool glob = false) : Ctl (a), text (label)
    {
        setSuffix (suf, glob);
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setAlpha (0.0f);
        slider.setScrollWheelEnabled (false);
        slider.onValueChange = [this] { repaint(); };
    }
    void setLabel (const juce::String& t) { text = t; repaint(); }
    void rebind (int layer) override { att.reset(); att = std::make_unique<stratum::APVTS::SliderAttachment> (apvts, id (layer), slider); repaint(); }
    void resized() override { slider.setBounds (getLocalBounds()); }
    void paint (juce::Graphics& g) override
    {
        g.setFont (font (13.0f));
        g.setColour (col::blue);
        g.drawText (text, getLocalBounds(), juce::Justification::centredLeft);
        auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 8;
        g.setColour (col::text);
        g.drawText (slider.getTextFromValue (slider.getValue()), getLocalBounds().withTrimmedLeft (w), juce::Justification::centredLeft);
    }
    juce::Slider slider;
private:
    juce::String text;
    std::unique_ptr<stratum::APVTS::SliderAttachment> att;
};

class Combo : public Ctl
{
public:
    Combo (stratum::APVTS& a, const juce::StringArray& items, const juce::String& suf, bool glob = false) : Ctl (a)
    {
        setSuffix (suf, glob);
        addAndMakeVisible (box);
        setItems (items);
    }
    void setItems (const juce::StringArray& items)
    {
        att.reset();
        box.clear (juce::dontSendNotification);
        for (int i = 0; i < items.size(); ++i) box.addItem (items[i], i + 1);
    }
    void rebind (int layer) override { att.reset(); att = std::make_unique<stratum::APVTS::ComboBoxAttachment> (apvts, id (layer), box); }
    void resized() override { box.setBounds (getLocalBounds()); }
    juce::ComboBox box;
private:
    std::unique_ptr<stratum::APVTS::ComboBoxAttachment> att;
};

class Toggle : public Ctl
{
public:
    Toggle (stratum::APVTS& a, const juce::String& label, const juce::String& suf, bool glob = false) : Ctl (a)
    {
        setSuffix (suf, glob);
        addAndMakeVisible (button);
        button.setButtonText (label);
    }
    void rebind (int layer) override { att.reset(); att = std::make_unique<stratum::APVTS::ButtonAttachment> (apvts, id (layer), button); }
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;
private:
    std::unique_ptr<stratum::APVTS::ButtonAttachment> att;
};

//==============================================================================
inline void panelBackground (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {})
{
    g.setColour (col::panel); g.fillRect (r);
    g.setColour (col::line); g.drawRect (r, 1);
    if (title.isNotEmpty()) { g.setColour (col::text); g.setFont (font (16.0f, true)); g.drawText (title, r.getX() + 8, r.getY() + 3, 120, 20, juce::Justification::centredLeft); }
}

} // namespace ui

//==============================================================================
class StratumEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StratumEditor (StratumProcessor&);
    ~StratumEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Content;
    struct LayerView;
    struct MasterView;
    struct Header;
    struct TabBar;

    void timerCallback() override;
    void selectTab (int tab);

    StratumProcessor& proc;
    ui::LnF lnf;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StratumEditor)
};
