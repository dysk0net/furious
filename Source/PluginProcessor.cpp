#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "PluginEditor.h"

using namespace stratum;

//==============================================================================
stratum::APVTS::ParameterLayout StratumProcessor::createLayout()
{
    APVTS::ParameterLayout layout;
    static EngineParams defaults = makeDefaultEngineParams();   // supplies parameter defaults
    ParamBinder b (layout, defaults);
    describeAll (b, defaults);
    return layout;
}

StratumProcessor::StratumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STRATUM", createLayout()),
      engineParams (makeDefaultEngineParams()),
      engine (new Engine())
{
    binder = std::make_unique<ParamBinder> (apvts, engineParams);
    describeAll (*binder, engineParams);
    for (int l = 0; l < kLayers; ++l) info.layerNames[l] = juce::String (l + 1);
}

StratumProcessor::~StratumProcessor() = default;

//==============================================================================
void StratumProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    maxBlock = std::max (samplesPerBlock, 512);
    engine->prepare (sampleRate, maxBlock);
    held.reset(); sustained.reset(); sustainDown = false;
}

bool StratumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StratumProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        int n = m.getNoteNumber();
        held.set ((size_t) n); sustained.reset ((size_t) n);
        engine->noteOn (m.getChannel(), n, m.getFloatVelocity(), engineParams);
        lastNote = n; lastVelocity = m.getVelocity();
    }
    else if (m.isNoteOff())
    {
        int n = m.getNoteNumber();
        held.reset ((size_t) n);
        if (sustainDown) sustained.set ((size_t) n);
        else engine->noteOff (m.getChannel(), n, engineParams);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine->allNotesOff();
        held.reset(); sustained.reset();
    }
    else if (m.isController())
    {
        int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 1) modWheelValue = (float) v / 127.0f;
        else if (cc == 64)
        {
            bool down = v >= 64;
            if (sustainDown && ! down)
            {
                for (int n = 0; n < 128; ++n)
                    if (sustained.test ((size_t) n) && ! held.test ((size_t) n)) engine->noteOff (1, n, engineParams);
                sustained.reset();
            }
            sustainDown = down;
        }
    }
    else if (m.isPitchWheel())
    {
        pitchBendValue = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    }
}

void StratumProcessor::renderChunk (float* l, float* r, int start, int count)
{
    while (count > 0)
    {
        int c = std::min (count, maxBlock);
        Transport tp = transport;
        tp.ppq += (double) start * transport.bpm / 60.0 / sampleRateHz;
        engine->render (l + start, r + start, c, engineParams, tp);
        start += c; count -= c;
    }
}

void StratumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    binder->pull (engineParams);
    engineParams.modWheel = modWheelValue.load();
    engineParams.pitchBend = pitchBendValue.load();
    if (panicRequested.exchange (false)) { engine->panic(); held.reset(); sustained.reset(); }

    // transport: host if available, otherwise a free-running 120 bpm clock
    transport.bpm = 120.0; transport.playing = false;
    bool hostPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) transport.bpm = std::max (20.0, *bpm);
            transport.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) { transport.ppq = *ppq; hostPpq = true; }
        }
    if (! hostPpq) { transport.ppq = internalPpq; internalPpq += (double) n * transport.bpm / 60.0 / sampleRateHz; }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto metadata : midi)
    {
        int at = std::min (std::max (metadata.samplePosition, 0), n);
        if (at > pos) { renderChunk (l, r, pos, at - pos); pos = at; }
        handleMidi (metadata.getMessage());
    }
    if (pos < n) renderChunk (l, r, pos, n - pos);
    midi.clear();

    voiceCount = engine->totalVoices();
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double budget = (double) n / sampleRateHz;
    cpuLoad = cpuLoad.load() * 0.9f + 0.1f * (float) (elapsed / budget);
}

//==============================================================================
// Custom (non-parameter) state: sequencer steps, arp pattern, trancegate lanes, names.
static juce::String floatsToString (const float* a, int n)
{
    juce::StringArray s;
    for (int i = 0; i < n; ++i) s.add (juce::String (a[i], 3));
    return s.joinIntoString (" ");
}
static void stringToFloats (const juce::String& str, float* a, int n)
{
    juce::StringArray t;
    t.addTokens (str, " ", "");
    for (int i = 0; i < n && i < t.size(); ++i) a[i] = t[i].getFloatValue();
}

std::unique_ptr<juce::XmlElement> StratumProcessor::makeStateXml()
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    if (xml == nullptr) return nullptr;
    auto* c = xml->createNewChildElement ("StratumCustom");
    c->setAttribute ("name", info.name);
    c->setAttribute ("category", info.category);
    c->setAttribute ("author", info.author);
    c->setAttribute ("comment", info.comment);
    for (int k = 0; k < 3; ++k) c->setAttribute ("macro" + juce::String (k + 1), info.macroNames[k]);
    for (int l = 0; l < kLayers; ++l)
    {
        const LayerParams& P = engineParams.layer[l];
        c->setAttribute ("lname" + juce::String (l), info.layerNames[l]);
        for (int k = 0; k < kSeqs; ++k) c->setAttribute ("seq" + juce::String (l) + "_" + juce::String (k), floatsToString (P.seq[k].steps, kMaxSteps));
        float on[kArpSteps];
        for (int s = 0; s < kArpSteps; ++s) on[s] = P.arp.stepOn[s] ? 1.0f : 0.0f;
        c->setAttribute ("arpOn" + juce::String (l), floatsToString (on, kArpSteps));
        c->setAttribute ("arpTune" + juce::String (l), floatsToString (P.arp.tune, kArpSteps));
        for (int k = 0; k < kFxSlots; ++k)
        {
            c->setAttribute ("gateA" + juce::String (l) + "_" + juce::String (k), floatsToString (P.fx[k].gate[0], kMaxSteps));
            c->setAttribute ("gateB" + juce::String (l) + "_" + juce::String (k), floatsToString (P.fx[k].gate[1], kMaxSteps));
        }
    }
    return xml;
}

void StratumProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = makeStateXml()) copyXmlToBinary (*xml, destData);
}

void StratumProcessor::restoreFromXml (juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType())) return;
    std::unique_ptr<juce::XmlElement> custom;
    if (auto* c = xml.getChildByName ("StratumCustom"))
    {
        custom.reset (new juce::XmlElement (*c));
        xml.removeChildElement (c, true);
    }
    apvts.replaceState (juce::ValueTree::fromXml (xml));
    if (custom == nullptr) return;

    info.name = custom->getStringAttribute ("name", "Init");
    info.category = custom->getStringAttribute ("category");
    info.author = custom->getStringAttribute ("author");
    info.comment = custom->getStringAttribute ("comment");
    for (int k = 0; k < 3; ++k) info.macroNames[k] = custom->getStringAttribute ("macro" + juce::String (k + 1), "Macro " + juce::String (k + 1));
    for (int l = 0; l < kLayers; ++l)
    {
        LayerParams& P = engineParams.layer[l];
        info.layerNames[l] = custom->getStringAttribute ("lname" + juce::String (l), juce::String (l + 1));
        for (int k = 0; k < kSeqs; ++k)
            if (custom->hasAttribute ("seq" + juce::String (l) + "_" + juce::String (k)))
                stringToFloats (custom->getStringAttribute ("seq" + juce::String (l) + "_" + juce::String (k)), P.seq[k].steps, kMaxSteps);
        if (custom->hasAttribute ("arpOn" + juce::String (l)))
        {
            float on[kArpSteps];
            for (int s = 0; s < kArpSteps; ++s) on[s] = 1.0f;
            stringToFloats (custom->getStringAttribute ("arpOn" + juce::String (l)), on, kArpSteps);
            for (int s = 0; s < kArpSteps; ++s) P.arp.stepOn[s] = on[s] > 0.5f;
            stringToFloats (custom->getStringAttribute ("arpTune" + juce::String (l)), P.arp.tune, kArpSteps);
        }
        for (int k = 0; k < kFxSlots; ++k)
        {
            if (custom->hasAttribute ("gateA" + juce::String (l) + "_" + juce::String (k)))
                stringToFloats (custom->getStringAttribute ("gateA" + juce::String (l) + "_" + juce::String (k)), P.fx[k].gate[0], kMaxSteps);
            if (custom->hasAttribute ("gateB" + juce::String (l) + "_" + juce::String (k)))
                stringToFloats (custom->getStringAttribute ("gateB" + juce::String (l) + "_" + juce::String (k)), P.fx[k].gate[1], kMaxSteps);
        }
    }
}

void StratumProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes)) restoreFromXml (*xml);
}

//==============================================================================
void StratumProcessor::copyCustomState (const EngineParams& src)
{
    for (int l = 0; l < kLayers; ++l)
    {
        LayerParams& d = engineParams.layer[l];
        const LayerParams& s = src.layer[l];
        for (int k = 0; k < kSeqs; ++k) std::copy (s.seq[k].steps, s.seq[k].steps + kMaxSteps, d.seq[k].steps);
        std::copy (s.arp.stepOn, s.arp.stepOn + kArpSteps, d.arp.stepOn);
        std::copy (s.arp.tune, s.arp.tune + kArpSteps, d.arp.tune);
        for (int k = 0; k < kFxSlots; ++k)
            for (int c = 0; c < 2; ++c) std::copy (s.fx[k].gate[c], s.fx[k].gate[c] + kMaxSteps, d.fx[k].gate[c]);
    }
}

void StratumProcessor::applySnapshot (const EngineParams& snap, const PresetInfo& newInfo)
{
    panicRequested = true;
    copyCustomState (snap);
    binder->push (snap);
    info = newInfo;
}

int StratumProcessor::getNumFactoryPresets() const { return (int) factoryPresets().size(); }

juce::String StratumProcessor::getFactoryPresetLabel (int i) const
{
    const auto& p = factoryPresets()[(size_t) juce::jlimit (0, getNumFactoryPresets() - 1, i)];
    return juce::String (p.category) + "  |  " + p.name;
}

void StratumProcessor::loadFactoryPreset (int index)
{
    index = juce::jlimit (0, getNumFactoryPresets() - 1, index);
    currentFactoryIndex = index;
    EngineParams snap = makeDefaultEngineParams();
    const auto& p = factoryPresets()[(size_t) index];
    p.build (snap);
    PresetInfo pi;
    pi.name = p.name; pi.category = p.category; pi.author = "Stratum";
    for (int l = 0; l < kLayers; ++l) pi.layerNames[l] = juce::String (l + 1);
    applySnapshot (snap, pi);
}

juce::File StratumProcessor::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Stratum").getChildFile ("Presets");
}

bool StratumProcessor::saveUserPreset (const juce::String& name)
{
    auto dir = getUserPresetDirectory();
    if (! dir.createDirectory()) return false;
    info.name = name.isEmpty() ? "Untitled" : name;
    auto xml = makeStateXml();
    if (xml == nullptr) return false;
    return xml->writeTo (dir.getChildFile (juce::File::createLegalFileName (info.name) + ".stpreset"));
}

bool StratumProcessor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        panicRequested = true;
        restoreFromXml (*xml);
        return true;
    }
    return false;
}

juce::Array<juce::File> StratumProcessor::listUserPresets() const
{
    return getUserPresetDirectory().findChildFiles (juce::File::findFiles, true, "*.stpreset");
}

juce::AudioProcessorEditor* StratumProcessor::createEditor() { return new StratumEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StratumProcessor(); }
