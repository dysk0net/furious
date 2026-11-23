#include "PluginEditor.h"
#include "FactoryPresets.h"

using namespace stratum;
using namespace ui;

static constexpr int W = 1280, H = 720;
static const char* kLetters = "ABCD";

static float rawParam (APVTS& a, const juce::String& id)
{
    if (auto* v = a.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

static void setParamPlain (APVTS& a, const juce::String& id, float plain)
{
    if (auto* p = a.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

static juce::StringArray names (int n, const char* (*fn) (int))
{
    juce::StringArray a;
    for (int i = 0; i < n; ++i) a.add (fn (i));
    return a;
}
static const char* lfoWave (int i) { return lfoWaveName (i); }
static const char* mixOrderLabel (int i) { return mixOrderName (i); }

// Owns child controls, adds them to a parent and records the parameter-bound ones.
struct Registry
{
    juce::Component* parent = nullptr;
    std::vector<Ctl*> ctls;
    juce::OwnedArray<juce::Component> store;

    template <class T, class... A>
    T& make (A&&... a)
    {
        auto* c = new T (std::forward<A> (a)...);
        store.add (c);
        parent->addAndMakeVisible (c);
        if (auto* ctl = dynamic_cast<Ctl*> (c)) ctls.push_back (ctl);
        return *c;
    }
    void rebindAll (int layer) { for (auto* c : ctls) c->rebind (layer); }
};

//==============================================================================
class WaveDisplay : public juce::Component
{
public:
    WaveDisplay (StratumProcessor& p, int o) : proc (p), osc (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    int layer = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg); g.fillRoundedRectangle (r, 3.0f);
        const juce::String base = layerId (layer, juce::String ("o") + juce::String::charToString (kLetters[osc]) + "_");
        int table = (int) rawParam (proc.apvts, base + "tab");
        float morph = rawParam (proc.apvts, base + "morph"), phase = rawParam (proc.apvts, base + "pha");
        bool on = rawParam (proc.apvts, base + "on") > 0.5f;

        g.setColour (col::line.withAlpha (0.5f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        const auto& wt = WaveTables::get();
        juce::Path p, fill;
        const int n = std::max (2, (int) r.getWidth());
        const float midY = r.getCentreY(), amp = r.getHeight() * 0.36f;
        for (int x = 0; x < n; ++x)
        {
            float v = wt.read (table, 0, morph, std::fmod ((float) x / (float) n + phase, 1.0f));
            float y = midY - v * amp;
            if (x == 0) { p.startNewSubPath (r.getX(), y); fill.startNewSubPath (r.getX(), midY); fill.lineTo (r.getX(), y); }
            else { p.lineTo (r.getX() + (float) x, y); fill.lineTo (r.getX() + (float) x, y); }
        }
        fill.lineTo (r.getRight(), midY); fill.closeSubPath();
        auto c = on ? col::blue : col::dim;
        g.setColour (c.withAlpha (0.22f)); g.fillPath (fill);
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.4f));

        g.setColour (c);
        g.setFont (font (14.0f));
        g.drawText ("TABLE  |  " + juce::String (table) + " : " + WaveTables::name (table), r.reduced (8.0f, 4.0f), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        const juce::String id = layerId (layer, juce::String ("o") + juce::String::charToString (kLetters[osc]) + "_tab");
        int cur = (int) rawParam (proc.apvts, id);
        for (int i = 0; i < WaveTables::T; ++i) m.addItem (i + 1, WaveTables::name (i), true, i == cur);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, id] (int r) { if (r > 0) setParamPlain (proc.apvts, id, (float) (r - 1)); });
    }

private:
    StratumProcessor& proc;
    int osc;
};

//==============================================================================
class OscPanel : public juce::Component
{
public:
    OscPanel (StratumProcessor& p, int o, std::vector<Ctl*>& all) : osc (o), wave (p, o)
    {
        R.parent = this;
        auto& A = p.apvts;
        const juce::String pre = juce::String ("o") + juce::String::charToString (kLetters[o]) + "_";
        on = &R.make<Toggle> (A, "", pre + "on");
        semi = &R.make<DragLabel> (A, "Semi", pre + "semi");
        cent = &R.make<DragLabel> (A, "Cent", pre + "cent");
        key = &R.make<Toggle> (A, "Key", pre + "key");
        skip = &R.make<Toggle> (A, "Skip", pre + "skip");
        uni = &R.make<DragLabel> (A, "Unison", pre + "uni");
        det = &R.make<Knob> (A, "Detune", pre + "det");
        spr = &R.make<Knob> (A, "Spread", pre + "spr");
        bass = &R.make<Knob> (A, "Bass", pre + "bass");
        treb = &R.make<Knob> (A, "Treble", pre + "treb");
        morph = &R.make<DragLabel> (A, "Morph", pre + "morph");
        phase = &R.make<DragLabel> (A, "Phase", pre + "pha");
        rnd = &R.make<DragLabel> (A, "Random", pre + "rnd");
        fxType = &R.make<Combo> (A, names (OfxNum, oscFxName), pre + "fx");
        fxAmt = &R.make<Knob> (A, "FX Amt", pre + "fxa");
        fxTune = &R.make<Knob> (A, "FX Tune", pre + "fxt");
        bal = &R.make<Knob> (A, "Balance", pre + "bal");
        vol = &R.make<Knob> (A, "Volume", pre + "vol");
        vol->modTarget = TgOscVol1 + osc;
        addAndMakeVisible (wave);
        title.setText (juce::String ("OSC ") + juce::String::charToString (kLetters[o]), juce::dontSendNotification);
        title.setFont (font (16.0f, true));
        title.setColour (juce::Label::textColourId, col::text);
        addAndMakeVisible (title);
        for (auto* c : R.ctls) all.push_back (c);
    }

    Knob* volKnob() { return vol; }
    void setLayer (int l) { wave.layer = l; }
    void paint (juce::Graphics& g) override { panelBackground (g, getLocalBounds()); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        auto top = r.removeFromTop (22);
        on->setBounds (top.removeFromLeft (18));
        title.setBounds (top.removeFromLeft (66));
        semi->setBounds (top.removeFromLeft (84)); cent->setBounds (top.removeFromLeft (92));
        key->setBounds (top.removeFromLeft (56)); skip->setBounds (top.removeFromLeft (56));
        fxType->setBounds (top.removeFromRight (130).reduced (0, 1));
        r.removeFromTop (4);

        auto left = r.removeFromLeft (122);
        uni->setBounds (left.removeFromTop (20));
        auto row1 = left.removeFromTop (62), row2 = left;
        det->setBounds (row1.removeFromLeft (61)); spr->setBounds (row1);
        bass->setBounds (row2.removeFromLeft (61)); treb->setBounds (row2);

        auto right = r.removeFromRight (122);
        auto rr1 = right.removeFromTop (62), rr2 = right;
        bal->setBounds (rr1.removeFromLeft (61)); vol->setBounds (rr1);
        fxAmt->setBounds (rr2.removeFromLeft (61)); fxTune->setBounds (rr2);

        r.reduce (6, 0);
        auto bottom = r.removeFromBottom (20);
        wave.setBounds (r);
        morph->setBounds (bottom.removeFromLeft (110)); phase->setBounds (bottom.removeFromLeft (110)); rnd->setBounds (bottom);
    }

private:
    int osc;
    Registry R;
    WaveDisplay wave;
    juce::Label title;
    Toggle *on, *key, *skip;
    DragLabel *semi, *cent, *uni, *morph, *phase, *rnd;
    Knob *det, *spr, *bass, *treb, *fxAmt, *fxTune, *bal, *vol;
    Combo* fxType;
};

//==============================================================================
// Filter / insert / mix row.
class SoundShapingPanel : public juce::Component
{
public:
    std::vector<Knob*> modKnobs;

    SoundShapingPanel (StratumProcessor& p, std::vector<Ctl*>& all)
    {
        R.parent = this;
        auto& A = p.apvts;
        fType = &R.make<Combo> (A, names (FltNumTypes, filterTypeName), "f_type");
        cutoff = &R.make<Knob> (A, "Cutoff", "f_cut"); cutoff->modTarget = TgCutoff;
        ftreble = &R.make<Knob> (A, "Treble", "f_treb");
        reso = &R.make<Knob> (A, "Resonance", "f_res"); reso->modTarget = TgResonance;
        fenv = &R.make<Knob> (A, "Envelope", "f_env");
        fkey = &R.make<Knob> (A, "Keyfollow", "f_key");
        fwid = &R.make<Knob> (A, "Width", "f_wid");
        iType = &R.make<Combo> (A, names (InsNumTypes, insertTypeName), "i_type");
        drive = &R.make<Knob> (A, "Drive", "i_drv"); drive->modTarget = TgInsertDrive;
        ip2 = &R.make<Knob> (A, "Ctrl 2", "i_p2");
        ip3 = &R.make<Knob> (A, "Ctrl 3", "i_p3");
        imix = &R.make<Knob> (A, "Mix", "i_mix");
        mOrder = &R.make<Combo> (A, names (2, mixOrderLabel), "m_order");
        mPan = &R.make<Knob> (A, "Pan", "m_pan");
        mGain = &R.make<Knob> (A, "Gain", "m_gain");
        for (auto* c : R.ctls) all.push_back (c);
        modKnobs = { cutoff, reso, drive };
    }

    void paint (juce::Graphics& g) override
    {
        const int fw = (int) (getWidth() * 0.5f), iw = (int) (getWidth() * 0.27f);
        panelBackground (g, { 0, 0, fw, getHeight() }, "FILTER");
        panelBackground (g, { fw, 0, iw, getHeight() }, "INSERT");
        panelBackground (g, { fw + iw, 0, getWidth() - fw - iw, getHeight() }, "MIX");
    }

    void resized() override
    {
        const int fw = (int) (getWidth() * 0.5f), iw = (int) (getWidth() * 0.27f);
        auto f = juce::Rectangle<int> (0, 0, fw, getHeight()).reduced (6);
        fType->setBounds (f.removeFromTop (22).withTrimmedLeft (90).reduced (0, 1));
        {
            Knob* ks[6] = { cutoff, ftreble, reso, fenv, fkey, fwid };
            int kw = f.getWidth() / 6;
            for (int i = 0; i < 6; ++i) ks[i]->setBounds (f.getX() + i * kw, f.getY() + 2, kw, f.getHeight() - 2);
        }
        auto ins = juce::Rectangle<int> (fw, 0, iw, getHeight()).reduced (6);
        iType->setBounds (ins.removeFromTop (22).withTrimmedLeft (66).reduced (0, 1));
        {
            Knob* ks[4] = { drive, ip2, ip3, imix };
            int kw = ins.getWidth() / 4;
            for (int i = 0; i < 4; ++i) ks[i]->setBounds (ins.getX() + i * kw, ins.getY() + 2, kw, ins.getHeight() - 2);
        }
        auto mx = juce::Rectangle<int> (fw + iw, 0, getWidth() - fw - iw, getHeight()).reduced (6);
        mOrder->setBounds (mx.removeFromTop (22).withTrimmedLeft (44).reduced (0, 1));
        mPan->setBounds (mx.removeFromLeft (mx.getWidth() / 2).withTrimmedTop (2)); mGain->setBounds (mx.withTrimmedTop (2));
    }

private:
    Registry R;
    Combo *fType, *iType, *mOrder;
    Knob *cutoff, *ftreble, *reso, *fenv, *fkey, *fwid, *drive, *ip2, *ip3, *imix, *mPan, *mGain;
};

//==============================================================================
// Modulator display: envelope curve, LFO shape, editable step sequence, or arpeggiator grid.
class ModDisplay : public juce::Component
{
public:
    enum Kind { Env = 0, Lfo, Seq, Arp };
    ModDisplay (StratumProcessor& p) : proc (p) {}
    int layer = 0, index = 0;
    Kind kind = Env;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg); g.fillRoundedRectangle (r, 3.0f);
        auto& A = proc.apvts;
        const auto& P = proc.engineParams.layer[layer];
        const juce::String l = juce::String::charToString (kLetters[index]);
        auto inner = r.reduced (8.0f, 22.0f);
        g.setColour (col::line.withAlpha (0.5f));
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
        g.setColour (col::orange); g.setFont (font (14.0f));

        if (kind == Env)
        {
            auto get = [&] (const char* s) { return rawParam (A, layerId (layer, "e" + l + "_" + s)); };
            float dly = get ("dly"), atk = get ("atk"), lvl = get ("lvl"), dec = get ("dec"), sus = get ("sus"), rel = get ("rel");
            float sa = get ("sa"), sd = get ("sd"), sr = get ("sr");
            auto seg = [] (float t) { return 0.25f + std::sqrt (std::max (t, 0.0f)); };
            float segs[5] = { seg (dly) * 0.5f, seg (atk), seg (dec), 0.8f, seg (rel) };
            float tot = 0; for (float s : segs) tot += s;
            auto yOf = [&] (float v) { return inner.getBottom() - v * inner.getHeight(); };
            juce::Path p; float x = inner.getX(); p.startNewSubPath (x, yOf (0));
            auto adv = [&] (int i) { return segs[i] / tot * inner.getWidth(); };
            x += adv (0); p.lineTo (x, yOf (0));
            const int steps = 24;
            float w1 = adv (1); for (int i = 1; i <= steps; ++i) p.lineTo (x + w1 * (float) i / steps, yOf (lvl * Envelope::shape ((float) i / steps, sa)));
            x += w1;
            float w2 = adv (2); for (int i = 1; i <= steps; ++i) p.lineTo (x + w2 * (float) i / steps, yOf (lerpf (lvl, sus, Envelope::shape ((float) i / steps, sd))));
            x += w2;
            float w3 = adv (3); p.lineTo (x + w3, yOf (sus)); x += w3;
            float w4 = adv (4); for (int i = 1; i <= steps; ++i) p.lineTo (x + w4 * (float) i / steps, yOf (sus * (1.0f - Envelope::shape ((float) i / steps, sr))));
            juce::Path fill (p); fill.lineTo (inner.getRight(), inner.getBottom()); fill.lineTo (inner.getX(), inner.getBottom()); fill.closeSubPath();
            g.setColour (col::orange.withAlpha (0.14f)); g.fillPath (fill);
            g.setColour (col::orange); g.strokePath (p, juce::PathStrokeType (1.6f));
            g.drawText ("Envelope " + l + "   " + (index == 0 ? "(amplitude)" : index == 1 ? "(filter cutoff)" : ""), getLocalBounds().reduced (8, 3), juce::Justification::topLeft);
        }
        else if (kind == Lfo)
        {
            auto get = [&] (const char* s) { return rawParam (A, layerId (layer, "l" + l + "_" + s)); };
            int wa = (int) get ("wa"), wb = (int) get ("wb");
            float xf = get ("xf"), ph = get ("pha"), bnd = get ("bnd"), lvl = get ("lvl");
            bool bip = get ("bip") > 0.5f;
            juce::Path p; const int n = std::max (2, (int) inner.getWidth());
            Rng rng; float r0 = rng.next(), r1 = rng.next();
            for (int i = 0; i < n; ++i)
            {
                float t = std::fmod ((float) i / (float) n * 2.0f, 1.0f);
                float q = std::pow (std::fmod (t + ph, 1.0f), std::pow (2.0f, bnd * 2.0f));
                float v = stratum::Lfo::wave (wa, q, r0, r1);
                if (xf > 0.001f) v = lerpf (v, stratum::Lfo::wave (wb, q, r0, r1), xf);
                if (! bip) v = v * 0.5f + 0.5f;
                v *= lvl;
                float y = bip ? inner.getCentreY() - v * inner.getHeight() * 0.5f : inner.getBottom() - v * inner.getHeight();
                if (i == 0) p.startNewSubPath (inner.getX(), y); else p.lineTo (inner.getX() + (float) i, y);
            }
            g.setColour (col::orange); g.strokePath (p, juce::PathStrokeType (1.6f));
            g.drawText ("LFO " + l + "   Rate " + juce::String (get ("rate"), 2) + " Hz", getLocalBounds().reduced (8, 3), juce::Justification::topLeft);
        }
        else if (kind == Seq)
        {
            const auto& S = P.seq[index];
            int len = (int) rawParam (A, layerId (layer, "s" + l + "_len"));
            int loop = (int) rawParam (A, layerId (layer, "s" + l + "_loop"));
            len = juce::jlimit (2, kMaxSteps, len);
            float sw = inner.getWidth() / (float) len;
            for (int i = 0; i < len; ++i)
            {
                float v = S.steps[i];
                auto bar = juce::Rectangle<float> (inner.getX() + (float) i * sw + 1.0f, v >= 0 ? inner.getCentreY() - v * inner.getHeight() * 0.5f : inner.getCentreY(),
                                                   sw - 2.0f, std::fabs (v) * inner.getHeight() * 0.5f);
                g.setColour (i >= loop ? col::orange : col::orange.withAlpha (0.4f));
                g.fillRect (bar.withHeight (std::max (2.0f, bar.getHeight())));
            }
            g.setColour (col::orange); g.drawText ("Sequence " + l + "   (drag to draw steps)", getLocalBounds().reduced (8, 3), juce::Justification::topLeft);
        }
        else
        {
            int len = juce::jlimit (1, kArpSteps, (int) rawParam (A, layerId (layer, "a_len")));
            float sw = inner.getWidth() / (float) len;
            for (int i = 0; i < len; ++i)
            {
                bool on = P.arp.stepOn[i];
                auto cell = juce::Rectangle<float> (inner.getX() + (float) i * sw + 1.0f, inner.getY(), sw - 2.0f, inner.getHeight() * 0.28f);
                g.setColour (on ? col::orange : col::panelHi); g.fillRoundedRectangle (cell, 2.0f);
                float t = P.arp.tune[i] / 12.0f;
                auto zone = juce::Rectangle<float> (cell.getX(), inner.getY() + inner.getHeight() * 0.36f, cell.getWidth(), inner.getHeight() * 0.64f);
                g.setColour (col::panelHi.withAlpha (0.6f)); g.fillRect (zone);
                g.setColour (on ? col::blue : col::dim);
                float my = zone.getCentreY() - t * zone.getHeight() * 0.5f;
                g.fillRect (zone.getX(), std::min (my, zone.getCentreY()), zone.getWidth(), std::max (2.0f, std::fabs (my - zone.getCentreY())));
            }
            g.setColour (col::orange); g.drawText ("Arpeggiator   click: step on/off   drag lower area: transpose", getLocalBounds().reduced (8, 3), juce::Justification::topLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { dragging = true; edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }

private:
    StratumProcessor& proc;
    bool dragging = false;
    bool paintOn = true;

    void edit (const juce::MouseEvent& e, bool first)
    {
        auto inner = getLocalBounds().toFloat().reduced (8.0f, 22.0f);
        auto& P = proc.engineParams.layer[layer];
        const juce::String l = juce::String::charToString (kLetters[index]);
        if (kind == Seq)
        {
            int len = juce::jlimit (2, kMaxSteps, (int) rawParam (proc.apvts, layerId (layer, "s" + l + "_len")));
            int i = (int) ((e.position.x - inner.getX()) / inner.getWidth() * (float) len);
            if (i < 0 || i >= len) return;
            float v = 1.0f - 2.0f * (e.position.y - inner.getY()) / inner.getHeight();
            P.seq[index].steps[i] = juce::jlimit (-1.0f, 1.0f, std::round (v * 16.0f) / 16.0f);
            repaint();
        }
        else if (kind == Arp)
        {
            int len = juce::jlimit (1, kArpSteps, (int) rawParam (proc.apvts, layerId (layer, "a_len")));
            int i = (int) ((e.position.x - inner.getX()) / inner.getWidth() * (float) len);
            if (i < 0 || i >= len) return;
            if (e.position.y < inner.getY() + inner.getHeight() * 0.32f)
            {
                if (first) { paintOn = ! P.arp.stepOn[i]; }
                P.arp.stepOn[i] = paintOn;
            }
            else
            {
                float zy = inner.getY() + inner.getHeight() * 0.36f, zh = inner.getHeight() * 0.64f;
                float v = 1.0f - 2.0f * (e.position.y - zy) / zh;
                P.arp.tune[i] = std::round (juce::jlimit (-1.0f, 1.0f, v) * 12.0f);
            }
            repaint();
        }
    }
};

class ModPanel : public juce::Component
{
public:
    ModPanel (StratumProcessor& p, std::vector<Ctl*>& all) : display (p)
    {
        R.parent = this;
        auto& A = p.apvts;
        addAndMakeVisible (display);
        const char* groups[3] = { "Envelope", "LFO", "Sequence" };
        for (int g = 0; g < 3; ++g)
            for (int k = 0; k < 4; ++k)
            {
                auto* b = new juce::TextButton (juce::String::charToString (kLetters[k]));
                buttons.add (b); addAndMakeVisible (b);
                b->setClickingTogglesState (true); b->setRadioGroupId (77);
                b->setColour (juce::TextButton::buttonOnColourId, col::orange);
                b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                b->onClick = [this, g, k] { select (g, k); };
                (void) groups;
            }
        auto* ab = new juce::TextButton ("ARP");
        buttons.add (ab); addAndMakeVisible (ab);
        ab->setClickingTogglesState (true); ab->setRadioGroupId (77);
        ab->setColour (juce::TextButton::buttonOnColourId, col::orange);
        ab->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        ab->onClick = [this] { select (3, 0); };

        for (int i = 0; i < 10; ++i) knobs[i] = &R.make<Knob> (A, "", "e" "A_vel");
        for (int i = 0; i < 3; ++i) combos[i] = &R.make<Combo> (A, juce::StringArray ("-"), "e" "A_vel");
        for (int i = 0; i < 3; ++i) toggles[i] = &R.make<Toggle> (A, "", "e" "A_vel");
        for (auto* c : R.ctls) all.push_back (c);
        buttons[0]->setToggleState (true, juce::dontSendNotification);
        configure();
    }

    void setLayer (int l) { layer = l; display.layer = l; }
    void rebindAfterConfigure() { for (auto* c : R.ctls) c->rebind (layer); }

    void paint (juce::Graphics& g) override
    {
        panelBackground (g, getLocalBounds());
        g.setColour (col::text); g.setFont (font (13.0f, true));
        g.drawText ("Envelope", 8, 4, 70, 18, juce::Justification::centredLeft);
        g.drawText ("LFO", 8 + 4 * 30 + 80, 4, 40, 18, juce::Justification::centredLeft);
        g.drawText ("Sequence", 8 + 8 * 30 + 150, 4, 70, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (22);
        int x = top.getX() + 70;
        for (int k = 0; k < 4; ++k) buttons[k]->setBounds (x + k * 30, top.getY(), 28, 22);
        x += 4 * 30 + 12 + 30;
        for (int k = 0; k < 4; ++k) buttons[4 + k]->setBounds (x + k * 30, top.getY(), 28, 22);
        x += 4 * 30 + 12 + 70;
        for (int k = 0; k < 4; ++k) buttons[8 + k]->setBounds (x + k * 30, top.getY(), 28, 22);
        x += 4 * 30 + 24;
        buttons[12]->setBounds (x, top.getY(), 44, 22);
        r.removeFromTop (3);
        auto lower = r.removeFromBottom (74);
        display.setBounds (r);
        int nk = visibleKnobs;
        auto kr = lower.removeFromLeft (nk * 56 + 4);
        for (int i = 0; i < 10; ++i) knobs[i]->setBounds (kr.getX() + i * 56, kr.getY(), 56, kr.getHeight());
        lower.removeFromLeft (8);
        int cy = lower.getY() + 2;
        for (int i = 0; i < 3; ++i) combos[i]->setBounds (lower.getX() + (i % 2) * 120, cy + (i / 2) * 26, 116, 22);
        for (int i = 0; i < 3; ++i) toggles[i]->setBounds (lower.getX() + 250, cy + i * 21, 100, 20);
    }

    ModDisplay display;

private:
    struct Def { const char* label; const char* suffix; };

    void select (int group, int idx)
    {
        modeGroup = group; modeIdx = idx;
        display.kind = (ModDisplay::Kind) group; display.index = idx;
        configure();
        rebindAfterConfigure();
        display.repaint();
    }

    void configure()
    {
        const juce::String l = juce::String::charToString (kLetters[modeIdx]);
        std::vector<Def> k, c, t;
        juce::String pre;
        std::vector<juce::StringArray> comboItems;
        if (modeGroup == 0)
        {
            pre = "e" + l + "_";
            k = { { "Velocity", "vel" }, { "Delay", "dly" }, { "Attack", "atk" }, { "Level", "lvl" }, { "Decay", "dec" },
                  { "Sustain", "sus" }, { "Release", "rel" }, { "Slope A", "sa" }, { "Slope D", "sd" }, { "Slope R", "sr" } };
        }
        else if (modeGroup == 1)
        {
            pre = "l" + l + "_";
            k = { { "Delay", "dly" }, { "Fade", "fade" }, { "Level", "lvl" }, { "Rate", "rate" }, { "X-Fade", "xf" }, { "Phase", "pha" }, { "Bend", "bnd" } };
            c = { { "A", "wa" }, { "B", "wb" }, { "Sync Div", "div" } };
            t = { { "Retrigger", "rtg" }, { "BPM Sync", "sync" }, { "Bipolar", "bip" } };
            comboItems = { names (7, lfoWave), names (7, lfoWave), names (kNumDivs, divName) };
        }
        else if (modeGroup == 2)
        {
            pre = "s" + l + "_";
            k = { { "Level", "lvl" }, { "Rate", "rate" }, { "Smooth", "smo" }, { "Shuffle", "shf" }, { "Length", "len" }, { "Loop", "loop" } };
            c = { { "Sync Div", "div" } };
            t = { { "Retrigger", "rtg" }, { "BPM Sync", "sync" }, { "Oneshot", "one" } };
            comboItems = { names (kNumDivs, divName) };
        }
        else
        {
            pre = "a_";
            k = { { "Octaves", "oct" }, { "Length", "len" }, { "Gate", "gate" }, { "Shuffle", "shf" } };
            c = { { "Order", "ord" }, { "Rate", "div" } };
            t = { { "Arp On", "on" } };
            comboItems = { names (ArpNumOrders, arpOrderName), names (kNumDivs, divName) };
        }
        visibleKnobs = (int) k.size();
        for (int i = 0; i < 10; ++i)
        {
            knobs[i]->setVisible (i < (int) k.size());
            if (i < (int) k.size()) { knobs[i]->setLabel (k[(size_t) i].label); knobs[i]->setSuffix (pre + k[(size_t) i].suffix); }
        }
        for (int i = 0; i < 3; ++i)
        {
            combos[i]->setVisible (i < (int) c.size());
            if (i < (int) c.size()) { combos[i]->setItems (comboItems[(size_t) i]); combos[i]->setSuffix (pre + c[(size_t) i].suffix); }
            toggles[i]->setVisible (i < (int) t.size());
            if (i < (int) t.size()) { toggles[i]->button.setButtonText (t[(size_t) i].label); toggles[i]->setSuffix (pre + t[(size_t) i].suffix); }
        }
        resized();
    }

    Registry R;
    juce::OwnedArray<juce::TextButton> buttons;
    Knob* knobs[10];
    Combo* combos[3];
    Toggle* toggles[3];
    int layer = 0, modeGroup = 0, modeIdx = 0, visibleKnobs = 10;
};

//==============================================================================
class GateGrid : public juce::Component
{
public:
    GateGrid (StratumProcessor& p) : proc (p) {}
    int layer = 0, slot = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg); g.fillRoundedRectangle (r, 3.0f);
        const auto& F = proc.engineParams.layer[layer].fx[slot];
        int len = juce::jlimit (1, kMaxSteps, (int) rawParam (proc.apvts, layerId (layer, "x" + juce::String (slot + 1) + "_len")));
        auto in = r.reduced (6.0f);
        float sw = in.getWidth() / (float) len, lh = in.getHeight() / 2.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
            {
                auto cell = juce::Rectangle<float> (in.getX() + (float) i * sw + 1.0f, in.getY() + (float) c * lh + 1.0f, sw - 2.0f, lh - 2.0f);
                g.setColour (F.gate[c][i] > 0.5f ? col::orange : col::panelHi); g.fillRect (cell);
                if (i % 4 == 0) { g.setColour (col::line); g.drawVerticalLine ((int) cell.getX() - 1, in.getY(), in.getBottom()); }
            }
    }
    void mouseDown (const juce::MouseEvent& e) override { first = true; edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }

private:
    StratumProcessor& proc;
    bool first = true, paintVal = true;
    void edit (const juce::MouseEvent& e)
    {
        auto& F = proc.engineParams.layer[layer].fx[slot];
        int len = juce::jlimit (1, kMaxSteps, (int) rawParam (proc.apvts, layerId (layer, "x" + juce::String (slot + 1) + "_len")));
        auto in = getLocalBounds().toFloat().reduced (6.0f);
        int i = (int) ((e.position.x - in.getX()) / in.getWidth() * (float) len);
        int c = e.position.y < in.getCentreY() ? 0 : 1;
        if (i < 0 || i >= len) return;
        if (first) { paintVal = F.gate[c][i] < 0.5f; first = false; }
        F.gate[c][i] = paintVal ? 1.0f : 0.0f;
        repaint();
    }
};

class FxPanel : public juce::Component
{
public:
    FxPanel (StratumProcessor& p, std::vector<Ctl*>& all) : proc (p), grid (p)
    {
        R.parent = this;
        auto& A = p.apvts;
        addAndMakeVisible (grid);
        for (int i = 0; i < kFxSlots; ++i)
        {
            auto* b = new juce::TextButton();
            slots.add (b); addAndMakeVisible (b);
            b->setClickingTogglesState (true); b->setRadioGroupId (91);
            b->setColour (juce::TextButton::buttonOnColourId, col::panelHi.brighter (0.3f));
            b->onClick = [this, i] { selectSlot (i); };
        }
        slots[0]->setToggleState (true, juce::dontSendNotification);
        type = &R.make<Combo> (A, names (FxNumTypes, fxTypeName), "x1_type");
        div = &R.make<Combo> (A, names (kNumDivs, divName), "x1_div");
        len = &R.make<DragLabel> (A, "Steps", "x1_len");
        for (int i = 0; i < 5; ++i) knobs[i] = &R.make<Knob> (A, "", "x1_mix");
        for (auto* c : R.ctls) all.push_back (c);
        setLayer (0);
    }

    void setLayer (int l) { layer = l; grid.layer = l; applySlot(); }
    void refreshLabels()
    {
        int t = (int) rawParam (proc.apvts, layerId (layer, "x" + juce::String (slot + 1) + "_type"));
        if (t == shownType) return;
        shownType = t;
        knobs[0]->setLabel ("Mix");
        for (int i = 0; i < 4; ++i) { knobs[i + 1]->setLabel (fxParamLabel (t, i)); knobs[i + 1]->setVisible (t != FxNone && fxParamLabel (t, i)[0] != 0); }
        div->setVisible (t == FxDelay || t == FxTrancegate || t == FxSidechain);
        len->setVisible (t == FxTrancegate);
        grid.setVisible (t == FxTrancegate);
        repaint();
    }
    void refreshSlotButtons()
    {
        for (int i = 0; i < kFxSlots; ++i)
        {
            int t = (int) rawParam (proc.apvts, layerId (layer, "x" + juce::String (i + 1) + "_type"));
            slots[i]->setButtonText (juce::String (i + 1) + "  " + fxTypeName (t));
        }
    }

    void paint (juce::Graphics& g) override
    {
        panelBackground (g, getLocalBounds(), "EFFECTS");
        if (shownType == FxNone || shownType != FxTrancegate)
        {
            g.setColour (col::dim); g.setFont (font (13.0f));
            g.drawFittedText (shownType == FxNone ? "Choose an effect type for this slot." : "Tweak the controls on the left. Reverb, delay, chorus, phaser, distortion, bitcrush, trancegate, compressor, sidechain and balancer are available in every slot.",
                              getWidth() - 470, 60, 440, 80, juce::Justification::topLeft, 3);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (22);
        auto list = r.removeFromLeft (150);
        for (int i = 0; i < kFxSlots; ++i) slots[i]->setBounds (list.removeFromTop (list.getHeight() / (kFxSlots - i)).reduced (0, 1));
        r.removeFromLeft (8);
        auto top = r.removeFromTop (22);
        type->setBounds (top.removeFromLeft (150));
        top.removeFromLeft (10);
        div->setBounds (top.removeFromLeft (90));
        top.removeFromLeft (10);
        len->setBounds (top.removeFromLeft (90));
        auto kr = r.removeFromLeft (5 * 58);
        for (int i = 0; i < 5; ++i) knobs[i]->setBounds (kr.getX() + i * 58, kr.getY(), 58, kr.getHeight());
        r.removeFromLeft (8);
        grid.setBounds (r);
    }

private:
    void selectSlot (int s) { slot = s; grid.slot = s; applySlot(); }
    void applySlot()
    {
        const juce::String x = "x" + juce::String (slot + 1) + "_";
        type->setSuffix (x + "type"); div->setSuffix (x + "div"); len->setSuffix (x + "len");
        const char* sfx[5] = { "mix", "a", "b", "c", "d" };
        for (int i = 0; i < 5; ++i) knobs[i]->setSuffix (x + sfx[i]);
        for (auto* c : R.ctls) c->rebind (layer);
        shownType = -1;
        refreshLabels();
        refreshSlotButtons();
    }

    StratumProcessor& proc;
    Registry R;
    GateGrid grid;
    juce::OwnedArray<juce::TextButton> slots;
    Combo *type, *div;
    DragLabel* len;
    Knob* knobs[5];
    int layer = 0, slot = 0, shownType = -1;
};

//==============================================================================
class MatrixPanel : public juce::Component
{
public:
    MatrixPanel (StratumProcessor& p, std::vector<Ctl*>& all)
    {
        R.parent = this;
        auto& A = p.apvts;
        for (int k = 0; k < kModSlots; ++k)
        {
            const juce::String pre = "r" + juce::String (k + 1) + "_";
            on[k] = &R.make<Toggle> (A, "R" + juce::String (k + 1), pre + "on");
            amt[k] = &R.make<DragLabel> (A, "Amount", pre + "amt");
            src[k] = &R.make<Combo> (A, names (SrcNum, modSourceName), pre + "src");
            tgt[k] = &R.make<Combo> (A, names (TgNum, modTargetName), pre + "tgt");
        }
        for (auto* c : R.ctls) all.push_back (c);
    }
    void paint (juce::Graphics& g) override
    {
        panelBackground (g, getLocalBounds());
        g.setColour (col::dim); g.setFont (font (12.0f));
        const int cw = getWidth() / kModSlots;
        for (int k = 0; k < kModSlots; ++k)
        {
            g.drawText ("Source", k * cw + 8, 52, 50, 20, juce::Justification::centredLeft);
            g.drawText ("Target", k * cw + 8, 82, 50, 20, juce::Justification::centredLeft);
            if (k > 0) { g.setColour (col::line); g.drawVerticalLine (k * cw, 4.0f, (float) getHeight() - 4.0f); g.setColour (col::dim); }
        }
    }
    void resized() override
    {
        const int cw = getWidth() / kModSlots;
        for (int k = 0; k < kModSlots; ++k)
        {
            auto c = juce::Rectangle<int> (k * cw, 0, cw, getHeight()).reduced (8, 6);
            auto top = c.removeFromTop (24);
            on[k]->setBounds (top.removeFromLeft (46));
            amt[k]->setBounds (top);
            c.removeFromTop (16);
            src[k]->setBounds (c.removeFromTop (22).withTrimmedLeft (46));
            c.removeFromTop (8);
            tgt[k]->setBounds (c.removeFromTop (22).withTrimmedLeft (46));
        }
    }
private:
    Registry R;
    Toggle* on[kModSlots]; DragLabel* amt[kModSlots]; Combo *src[kModSlots], *tgt[kModSlots];
};

//==============================================================================
struct StratumEditor::LayerView : public juce::Component
{
    LayerView (StratumProcessor& p) : proc (p)
    {
        for (int o = 0; o < 3; ++o)
        {
            osc.add (new OscPanel (p, o, ctls));
            addAndMakeVisible (osc[o]);
        }
        shaping = std::make_unique<SoundShapingPanel> (p, ctls); addAndMakeVisible (*shaping);
        mod = std::make_unique<ModPanel> (p, ctls); addAndMakeVisible (*mod);
        fx = std::make_unique<FxPanel> (p, ctls); addAndMakeVisible (*fx);
        matrix = std::make_unique<MatrixPanel> (p, ctls); addAndMakeVisible (*matrix);
        modKnobs = shaping->modKnobs;
        for (auto* o : osc) modKnobs.push_back (o->volKnob());
    }

    void setLayer (int l)
    {
        layer = l;
        for (auto* o : osc) o->setLayer (l);
        mod->setLayer (l);
        for (auto* c : ctls) c->rebind (l);
        fx->setLayer (l);
        for (auto* c : ctls) c->rebind (l);
    }

    void tick()
    {
        const auto& P = proc.engineParams.layer[layer];
        for (auto* k : modKnobs)
        {
            float m = 0.0f;
            for (const auto& s : P.mod) if (s.on && s.source != SrcNone && s.target == k->modTarget) m += s.amount;
            k->setMod (m);
        }
        fx->refreshLabels();
        fx->refreshSlotButtons();
        for (auto* o : osc) o->repaint();
        mod->display.repaint();
    }

    void resized() override
    {
        const int leftW = 610;
        const int rowH = 164;
        for (int o = 0; o < 3; ++o) osc[o]->setBounds (0, o * rowH, leftW, rowH);
        shaping->setBounds (leftW, 0, getWidth() - leftW, 100);
        mod->setBounds (leftW, 100, getWidth() - leftW, 232);
        fx->setBounds (leftW, 332, getWidth() - leftW, 160);
        matrix->setBounds (0, 492, getWidth(), getHeight() - 492);
    }

    StratumProcessor& proc;
    int layer = 0;
    std::vector<Ctl*> ctls;
    juce::OwnedArray<OscPanel> osc;
    std::unique_ptr<SoundShapingPanel> shaping;
    std::unique_ptr<ModPanel> mod;
    std::unique_ptr<FxPanel> fx;
    std::unique_ptr<MatrixPanel> matrix;
    std::vector<Knob*> modKnobs;
};

//==============================================================================
struct StratumEditor::MasterView : public juce::Component, private juce::ListBoxModel
{
    explicit MasterView (StratumProcessor& p) : proc (p)
    {
        R.parent = this;
        auto& A = p.apvts;
        list.setModel (this); list.setRowHeight (22);
        addAndMakeVisible (list);
        saveName.setText ("My Preset", juce::dontSendNotification);
        saveName.setFont (font (14.0f));
        addAndMakeVisible (saveName);
        saveBtn.setButtonText ("Save");
        saveBtn.onClick = [this] { proc.saveUserPreset (saveName.getText()); refresh(); };
        addAndMakeVisible (saveBtn);
        refresh();

        for (int l = 0; l < kLayers; ++l)
        {
            Strip& s = strips[l];
            auto id = [l] (const char* x) { return layerId (l, x); };
            s.on = &R.make<Toggle> (A, "Layer " + juce::String (l + 1), id ("on"), true);
            s.solo = &R.make<Toggle> (A, "Solo", id ("solo"), true);
            s.pan = &R.make<Knob> (A, "Pan", id ("pan"), true);
            s.lo = &R.make<DragLabel> (A, "Lo", id ("v_lo"), true);
            s.hi = &R.make<DragLabel> (A, "Hi", id ("v_hi"), true);
            s.ch = &R.make<DragLabel> (A, "Ch", id ("v_ch"), true);
            s.lim = &R.make<DragLabel> (A, "Voices", id ("v_lim"), true);
            s.mono = &R.make<Toggle> (A, "Mono", id ("v_mono"), true);
            s.glide = &R.make<Knob> (A, "Glide", id ("v_glide"), true);
            s.fader.setSliderStyle (juce::Slider::LinearVertical);
            s.fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.fader.setPopupDisplayEnabled (true, true, nullptr);
            addAndMakeVisible (s.fader);
            s.faderAtt = std::make_unique<APVTS::SliderAttachment> (A, id ("gain"), s.fader);
        }
        masterGain = &R.make<Knob> (A, "Master", "master_gain", true);
        limiter = &R.make<Toggle> (A, "Limiter", "master_lim", true);
        thresh = &R.make<Knob> (A, "Threshold", "master_thr", true);
        pbRange = &R.make<Knob> (A, "Bend +/-", "pb_range", true);
        keyOff = &R.make<Knob> (A, "Key Offset", "key_offset", true);
        R.rebindAll (0);

        for (auto* s : { &modWheel, &pitchWheel })
        {
            s->setSliderStyle (juce::Slider::LinearVertical);
            s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            addAndMakeVisible (s);
        }
        modWheel.setRange (0.0, 1.0); pitchWheel.setRange (-1.0, 1.0);
        modWheel.onValueChange = [this] { proc.modWheelValue = (float) modWheel.getValue(); };
        pitchWheel.onValueChange = [this] { proc.pitchBendValue = (float) pitchWheel.getValue(); };
        pitchWheel.onDragEnd = [this] { pitchWheel.setValue (0.0, juce::sendNotificationSync); };
    }

    void refresh()
    {
        rows.clear();
        for (int i = 0; i < proc.getNumFactoryPresets(); ++i) rows.add ({ proc.getFactoryPresetLabel (i), i, juce::File() });
        for (auto& f : proc.listUserPresets()) rows.add ({ "User  |  " + f.getFileNameWithoutExtension(), -1, f });
        list.updateContent(); list.repaint();
    }

    void tick() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        panelBackground (g, { 0, 0, 300, getHeight() }, "PRESETS");
        panelBackground (g, { 300, 0, 780, getHeight() }, "MIXER");
        panelBackground (g, { 1080, 0, getWidth() - 1080, getHeight() }, "MASTER");
        for (int l = 0; l < kLayers; ++l)
        {
            auto s = stripBounds (l);
            g.setColour (col::line); g.drawRect (s);
            // meter
            auto m = juce::Rectangle<float> ((float) s.getRight() - 14.0f, (float) s.getY() + 170.0f, 8.0f, 150.0f);
            g.setColour (col::bg); g.fillRect (m);
            float pk = juce::jlimit (0.0f, 1.0f, proc.getLayerPeak (l) * 1.4f);
            g.setColour (pk > 0.9f ? col::pink : col::blue);
            g.fillRect (m.withTop (m.getBottom() - m.getHeight() * pk));
        }
        auto mm = juce::Rectangle<float> (1100.0f, 300.0f, 14.0f, 300.0f);
        g.setColour (col::bg); g.fillRect (mm);
        float pk = juce::jlimit (0.0f, 1.0f, proc.getOutputPeak());
        g.setColour (pk > 0.95f ? col::pink : col::orange);
        g.fillRect (mm.withTop (mm.getBottom() - mm.getHeight() * pk));
        g.setColour (col::dim); g.setFont (font (12.0f));
        g.drawText ("Mod", 1170, 560, 40, 16, juce::Justification::centred);
        g.drawText ("Pitch", 1220, 560, 40, 16, juce::Justification::centred);
    }

    void resized() override
    {
        auto pr = juce::Rectangle<int> (0, 0, 300, getHeight()).reduced (6);
        pr.removeFromTop (22);
        auto bottom = pr.removeFromBottom (28);
        saveBtn.setBounds (bottom.removeFromRight (60)); bottom.removeFromRight (6); saveName.setBounds (bottom);
        pr.removeFromBottom (6);
        list.setBounds (pr);
        for (int l = 0; l < kLayers; ++l)
        {
            Strip& s = strips[l];
            auto b = stripBounds (l).reduced (4);
            s.on->setBounds (b.removeFromTop (20));
            s.solo->setBounds (b.removeFromTop (20));
            s.pan->setBounds (b.removeFromTop (60));
            auto mid = b.removeFromTop (150);
            s.fader.setBounds (mid.removeFromLeft (50).reduced (6, 0));
            b.removeFromTop (6);
            s.lo->setBounds (b.removeFromTop (18)); s.hi->setBounds (b.removeFromTop (18));
            s.ch->setBounds (b.removeFromTop (18)); s.lim->setBounds (b.removeFromTop (18));
            s.mono->setBounds (b.removeFromTop (20));
            s.glide->setBounds (b.removeFromTop (58));
        }
        auto m = juce::Rectangle<int> (1120, 30, getWidth() - 1130, getHeight() - 40);
        masterGain->setBounds (m.removeFromTop (70).withSizeKeepingCentre (70, 70));
        limiter->setBounds (m.removeFromTop (22));
        thresh->setBounds (m.removeFromTop (70).withSizeKeepingCentre (70, 70));
        pbRange->setBounds (m.removeFromTop (70).withSizeKeepingCentre (70, 70));
        keyOff->setBounds (m.removeFromTop (70).withSizeKeepingCentre (70, 70));
        modWheel.setBounds (1175, 300, 30, 250); pitchWheel.setBounds (1225, 300, 30, 250);
    }

    struct Strip
    {
        Toggle *on = nullptr, *solo = nullptr, *mono = nullptr;
        Knob *pan = nullptr, *glide = nullptr;
        DragLabel *lo = nullptr, *hi = nullptr, *ch = nullptr, *lim = nullptr;
        juce::Slider fader;
        std::unique_ptr<APVTS::SliderAttachment> faderAtt;
    };

    juce::Rectangle<int> stripBounds (int l) const { return { 306 + l * 96, 28, 92, getHeight() - 36 }; }

    struct Row { juce::String label; int factoryIndex; juce::File file; };
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row >= rows.size()) return;
        if (selected) { g.setColour (col::orange.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
        g.setColour (col::text); g.setFont (font (14.0f));
        g.drawText (rows[row].label, 8, 0, w - 8, h, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= rows.size()) return;
        if (rows[row].factoryIndex >= 0) proc.loadFactoryPreset (rows[row].factoryIndex);
        else proc.loadPresetFile (rows[row].file);
    }

    StratumProcessor& proc;
    Registry R;
    juce::ListBox list;
    juce::Array<Row> rows;
    juce::TextEditor saveName;
    juce::TextButton saveBtn;
    Strip strips[kLayers];
    Knob *masterGain, *thresh, *pbRange, *keyOff;
    Toggle* limiter;
    juce::Slider modWheel, pitchWheel;
};

//==============================================================================
struct StratumEditor::Header : public juce::Component
{
    explicit Header (StratumProcessor& p) : proc (p)
    {
        R.parent = this;
        auto& A = p.apvts;
        for (int i = 0; i < 3; ++i) macro[i] = &R.make<Knob> (A, "Macro " + juce::String (i + 1), "macro" + juce::String (i + 1), true);
        limiter = &R.make<Toggle> (A, "", "master_lim", true);
        thresh = &R.make<Knob> (A, "Threshold", "master_thr", true);
        master = &R.make<Knob> (A, "Master", "master_gain", true);
        R.rebindAll (0);
        prev.setButtonText ("<"); next.setButtonText (">"); panic.setButtonText ("Panic");
        for (auto* b : { &prev, &next, &panic }) addAndMakeVisible (b);
        prev.onClick = [this] { proc.loadFactoryPreset ((proc.currentFactoryIndex + proc.getNumFactoryPresets() - 1) % proc.getNumFactoryPresets()); };
        next.onClick = [this] { proc.loadFactoryPreset ((proc.currentFactoryIndex + 1) % proc.getNumFactoryPresets()); };
        panic.onClick = [this] { proc.panicRequested = true; };
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setGradientFill (juce::ColourGradient (col::hdrTop, 0, 0, col::hdrBot, 0, (float) r.getHeight(), false));
        g.fillRect (r);
        g.setColour (col::hdrText); g.setFont (font (40.0f, true));
        g.drawText ("STRATUM", 16, 2, 230, 48, juce::Justification::centredLeft);
        g.setFont (font (12.0f));
        g.drawText ("0.1.0  |  VST3  |  64-bit", 18, 50, 220, 16, juce::Justification::centredLeft);

        auto disp = displayBounds();
        g.setColour (juce::Colour (0xff6d7184)); g.fillRoundedRectangle (disp.toFloat(), 4.0f);
        g.setColour (col::hdrText);
        g.setFont (font (18.0f));
        g.drawText (proc.info.category.isEmpty() ? "Preset" : proc.info.category, disp.withTrimmedLeft (14).withWidth (130), juce::Justification::centredLeft);
        g.drawText (proc.info.name, disp.withTrimmedLeft (150).withWidth (320), juce::Justification::centredLeft);
        g.drawText (proc.info.author, disp.withTrimmedRight (90).withTrimmedLeft (disp.getWidth() - 260), juce::Justification::centredRight);

        // readouts
        g.setFont (font (13.0f));
        const int y = 52;
        g.drawText ("CPU", 260, y, 40, 16, juce::Justification::centredLeft);
        bar (g, { 296, y + 4, 90, 8 }, juce::jlimit (0.0f, 1.0f, proc.cpuLoad.load()));
        g.drawText (juce::String ((int) (proc.cpuLoad.load() * 100.0f)) + "%", 392, y, 40, 16, juce::Justification::centredLeft);
        g.drawText ("Voices", 440, y, 50, 16, juce::Justification::centredLeft);
        bar (g, { 490, y + 4, 90, 8 }, juce::jlimit (0.0f, 1.0f, (float) proc.getVoiceCount() / 64.0f));
        g.drawText (juce::String (proc.getVoiceCount()), 586, y, 30, 16, juce::Justification::centredLeft);
        int n = proc.lastNote.load();
        g.drawText (n < 0 ? "--" : juce::MidiMessage::getMidiNoteName (n, true, true, 3), 630, y, 50, 16, juce::Justification::centredLeft);
        bar (g, { 680, y + 4, 90, 8 }, (float) proc.lastVelocity.load() / 127.0f);
        g.drawText (juce::String (proc.lastVelocity.load()), 776, y, 30, 16, juce::Justification::centredLeft);

        // meter
        float pk = juce::jlimit (0.0f, 1.0f, proc.getOutputPeak());
        g.setColour (col::bg); g.fillRect (1252, 6, 8, 58);
        g.setColour (pk > 0.95f ? col::pink : col::blue);
        g.fillRect (1252.0f, 64.0f - 58.0f * pk, 8.0f, 58.0f * pk);
    }

    juce::Rectangle<int> displayBounds() const { return { 250, 8, 570, 36 }; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! displayBounds().contains (e.getPosition())) return;
        juce::PopupMenu m;
        for (int i = 0; i < proc.getNumFactoryPresets(); ++i) m.addItem (1000 + i, proc.getFactoryPresetLabel (i), true, i == proc.currentFactoryIndex);
        auto users = proc.listUserPresets();
        if (users.size() > 0) { m.addSeparator(); for (int i = 0; i < users.size(); ++i) m.addItem (2000 + i, "User  |  " + users[i].getFileNameWithoutExtension()); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (displayBounds())), [this, users] (int r)
        {
            if (r >= 2000) proc.loadPresetFile (users[r - 2000]);
            else if (r >= 1000) proc.loadFactoryPreset (r - 1000);
        });
    }

    void resized() override
    {
        prev.setBounds (828, 12, 26, 28); next.setBounds (858, 12, 26, 28);
        panic.setBounds (810, 50, 76, 20);
        for (int i = 0; i < 3; ++i) macro[i]->setBounds (900 + i * 66, 2, 64, 72);
        limiter->setBounds (1104, 24, 16, 16);
        thresh->setBounds (1122, 2, 64, 72);
        master->setBounds (1184, 2, 64, 72);
    }

    static void bar (juce::Graphics& g, juce::Rectangle<int> r, float v)
    {
        g.setColour (col::hdrText.withAlpha (0.35f)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (col::orange); g.fillRoundedRectangle (r.toFloat().withWidth (std::max (3.0f, r.getWidth() * v)), 3.0f);
        g.setColour (col::hdrText);
    }

    StratumProcessor& proc;
    Registry R;
    Knob* macro[3];
    Knob *thresh, *master;
    Toggle* limiter;
    juce::TextButton prev, next, panic;
};

//==============================================================================
struct StratumEditor::TabBar : public juce::Component
{
    TabBar (StratumProcessor& p, std::function<void (int)> onSelect) : proc (p), select (std::move (onSelect)) {}
    int current = 0;

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::bg); g.fillAll();
        const int tw = getWidth() / 9;
        for (int t = 0; t < 9; ++t)
        {
            auto r = juce::Rectangle<int> (t * tw, 2, tw, getHeight() - 2);
            bool sel = t == current;
            g.setColour (sel ? col::panelHi : col::panel); g.fillRect (r);
            g.setColour (sel ? col::orange : col::line); g.drawLine ((float) r.getX(), (float) r.getY(), (float) r.getRight(), (float) r.getY(), sel ? 2.0f : 1.0f);
            g.setFont (font (15.0f, sel));
            if (t == 0) { g.setColour (col::text); g.drawText ("M", r, juce::Justification::centred); continue; }
            bool en = rawParam (proc.apvts, layerId (t - 1, "on")) > 0.5f;
            auto led = juce::Rectangle<float> ((float) r.getX() + 10.0f, (float) r.getCentreY() - 6.0f, 12.0f, 12.0f);
            g.setColour (col::bg); g.fillRoundedRectangle (led, 2.0f);
            g.setColour (col::line); g.drawRoundedRectangle (led, 2.0f, 1.0f);
            if (en) { g.setColour (col::orange); g.fillRoundedRectangle (led.reduced (2.5f), 1.0f); }
            g.setColour (en ? col::text : col::dim);
            g.drawText (juce::String (t) + ". " + proc.info.layerNames[t - 1], r.withTrimmedLeft (30), juce::Justification::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int tw = getWidth() / 9;
        int t = juce::jlimit (0, 8, e.x / tw);
        if (t > 0 && e.x - t * tw < 28)
        {
            auto id = layerId (t - 1, "on");
            setParamPlain (proc.apvts, id, rawParam (proc.apvts, id) > 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }
        select (t);
    }
    StratumProcessor& proc;
    std::function<void (int)> select;
};

//==============================================================================
struct StratumEditor::Content : public juce::Component
{
    explicit Content (StratumProcessor& p, StratumEditor& e)
        : header (p), tabs (p, [&e] (int t) { e.selectTab (t); }), layerView (p), masterView (p)
    {
        addAndMakeVisible (header); addAndMakeVisible (tabs);
        addAndMakeVisible (layerView); addAndMakeVisible (masterView);
        setSize (W, H);
        layerView.setLayer (0);
        show (0);
    }
    void show (int tab)
    {
        tabs.current = tab;
        masterView.setVisible (tab == 0);
        layerView.setVisible (tab > 0);
        if (tab > 0) layerView.setLayer (tab - 1);
        tabs.repaint();
    }
    void tick()
    {
        header.repaint(); tabs.repaint();
        if (layerView.isVisible()) layerView.tick();
        if (masterView.isVisible()) masterView.tick();
    }
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void resized() override
    {
        header.setBounds (0, 0, W, 76);
        tabs.setBounds (0, 76, W, 26);
        layerView.setBounds (0, 102, W, H - 102);
        masterView.setBounds (0, 102, W, H - 102);
    }
    Header header;
    TabBar tabs;
    LayerView layerView;
    MasterView masterView;
};

//==============================================================================
StratumEditor::StratumEditor (StratumProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p, *this);
    addAndMakeVisible (*content);
    setResizable (true, true);
    setResizeLimits (640, 360, 2560, 1440);
    getConstrainer()->setFixedAspectRatio ((double) W / (double) H);
    setSize (W, H);
    startTimerHz (30);
}

StratumEditor::~StratumEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StratumEditor::selectTab (int tab) { content->show (tab); }
void StratumEditor::timerCallback() { content->tick(); }
void StratumEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void StratumEditor::resized()
{
    const float s = (float) getWidth() / (float) W;
    content->setTransform (juce::AffineTransform::scale (s));
    content->setBounds (0, 0, W, H);
}
