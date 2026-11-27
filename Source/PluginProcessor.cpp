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

//==============================================================================
// Documents/Furious, or the Documents/Stratum folder from the plug-in's earlier name when that is the one that exists.
static juce::File userDataFolder()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto fresh = docs.getChildFile ("Furious"), old = docs.getChildFile ("Stratum");
    return (! fresh.isDirectory() && old.isDirectory()) ? old : fresh;
}


StratumProcessor::StratumProcessor()
    // The first input bus is the host's "main" input and is off by default; the second is a sidechain, which is where Ableton
    // routes another track's audio ("Audio From") for the oscillators set to Audio In.
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Input", juce::AudioChannelSet::stereo(), false)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
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
    inBufL.assign ((size_t) std::max (samplesPerBlock, 512), 0.0f); inBufR = inBufL;
    held.reset(); sustained.reset(); sustainDown = false;
}

bool StratumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 0; i < layouts.inputBuses.size(); ++i)
    {
        const auto& s = layouts.inputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo() && s != juce::AudioChannelSet::mono()) return false;
    }
    return true;
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
        engine->render (l + start, r + start, c, engineParams, tp, haveInput ? inBufL.data() + start : nullptr, haveInput ? inBufR.data() + start : nullptr);
        start += c; count -= c;
    }
}

// Mod slots aimed at "Learned control": move the chosen host parameter (in the engine's working copy only) by
// source x amount, using block-rate layer-wide source values. All sources are read before anything is moved.
void StratumProcessor::applyLearnedModulation()
{
    bool any = false;
    for (int l = 0; l < kLayers && ! any; ++l)
        for (const auto& m : engineParams.layer[l].mod) any = any || (m.on && m.target == TgLearned && m.param >= 0 && m.source != SrcNone && m.amount != 0.0f);
    if (! any) return;

    float srcVals[kLayers][SrcNum];
    for (int l = 0; l < kLayers; ++l)
    {
        engine->blockSources (l, srcVals[l]);
        for (int k = 0; k < 3; ++k) srcVals[l][SrcMacro1 + k] = engineParams.macros[k];
        srcVals[l][SrcModWheel] = engineParams.modWheel;
    }
    // copy the slots first: moving a parameter must not change another slot's source or amount mid-way
    ModSlot slots[kLayers][kModSlots];
    for (int l = 0; l < kLayers; ++l) for (int j = 0; j < kModSlots; ++j) slots[l][j] = engineParams.layer[l].mod[j];
    for (int l = 0; l < kLayers; ++l)
        for (int j = 0; j < kModSlots; ++j)
        {
            const auto& m = slots[l][j];
            if (m.on && m.target == TgLearned && m.param >= 0 && m.source != SrcNone && m.amount != 0.0f)
                binder->modulate (engineParams, m.param, srcVals[l][m.source] * m.amount);
        }
}

void StratumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();

    // Audio in: take the first enabled input bus (the sidechain, or the main input) before the buffer is cleared
    haveInput = false;
    if ((int) inBufL.size() < n) { inBufL.assign ((size_t) n, 0.0f); inBufR = inBufL; }
    for (int b = 0; b < getBusCount (true) && ! haveInput; ++b)
    {
        auto* bus = getBus (true, b);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 1) continue;
        auto in = getBusBuffer (buffer, true, b);
        const float* a = in.getReadPointer (0);
        const float* c = in.getReadPointer (in.getNumChannels() > 1 ? 1 : 0);
        std::copy (a, a + n, inBufL.begin()); std::copy (c, c + n, inBufR.begin());
        haveInput = true;
    }
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    binder->pull (engineParams);
    for (int l = 0; l < kLayers; ++l)
        for (int o = 0; o < kOscs; ++o)
            engineParams.layer[l].osc[o].user = bank.get (l * kOscs + o);
    engineParams.modWheel = modWheelValue.load();
    engineParams.pitchBend = pitchBendValue.load();
    applyLearnedModulation();
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
        for (int o = 0; o < kOscs; ++o)
            if (info.userTable[l][o].isNotEmpty()) c->setAttribute ("utab" + juce::String (l) + "_" + juce::String (o), info.userTable[l][o]);
        for (int k = 0; k < kSeqs; ++k) c->setAttribute ("seq" + juce::String (l) + "_" + juce::String (k), floatsToString (P.seq[k].steps, kMaxSteps));
        for (int k = 0; k < kSeqs; ++k)
        {
            float so[kMaxSteps];
            for (int s = 0; s < kMaxSteps; ++s) so[s] = P.seq[k].stepOn[s] ? 1.0f : 0.0f;
            c->setAttribute ("seqOn" + juce::String (l) + "_" + juce::String (k), floatsToString (so, kMaxSteps));
        }
        float on[kArpSteps];
        for (int s = 0; s < kArpSteps; ++s) on[s] = P.arp.stepOn[s] ? 1.0f : 0.0f;
        c->setAttribute ("arpOn" + juce::String (l), floatsToString (on, kArpSteps));
        c->setAttribute ("arpTune" + juce::String (l), floatsToString (P.arp.tune, kArpSteps));
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
        for (int o = 0; o < kOscs; ++o)
            info.userTable[l][o] = custom->getStringAttribute ("utab" + juce::String (l) + "_" + juce::String (o));
    reloadUserWavetables();
    for (int l = 0; l < kLayers; ++l)
    {
        LayerParams& P = engineParams.layer[l];
        info.layerNames[l] = custom->getStringAttribute ("lname" + juce::String (l), juce::String (l + 1));
        for (int k = 0; k < kSeqs; ++k)
            if (custom->hasAttribute ("seq" + juce::String (l) + "_" + juce::String (k)))
                stringToFloats (custom->getStringAttribute ("seq" + juce::String (l) + "_" + juce::String (k)), P.seq[k].steps, kMaxSteps);
        for (int k = 0; k < kSeqs; ++k)
        {
            for (auto& b : P.seq[k].stepOn) b = true;             // older saves have no step switches
            if (custom->hasAttribute ("seqOn" + juce::String (l) + "_" + juce::String (k)))
            {
                float so[kMaxSteps] = {};
                stringToFloats (custom->getStringAttribute ("seqOn" + juce::String (l) + "_" + juce::String (k)), so, kMaxSteps);
                for (int s = 0; s < kMaxSteps; ++s) P.seq[k].stepOn[s] = so[s] > 0.5f;
            }
        }
        if (custom->hasAttribute ("arpOn" + juce::String (l)))
        {
            float on[kArpSteps];
            for (int s = 0; s < kArpSteps; ++s) on[s] = 1.0f;
            stringToFloats (custom->getStringAttribute ("arpOn" + juce::String (l)), on, kArpSteps);
            for (int s = 0; s < kArpSteps; ++s) P.arp.stepOn[s] = on[s] > 0.5f;
            stringToFloats (custom->getStringAttribute ("arpTune" + juce::String (l)), P.arp.tune, kArpSteps);
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
        for (int k = 0; k < kSeqs; ++k) std::copy (s.seq[k].stepOn, s.seq[k].stepOn + kMaxSteps, d.seq[k].stepOn);
        std::copy (s.arp.stepOn, s.arp.stepOn + kArpSteps, d.arp.stepOn);
        std::copy (s.arp.tune, s.arp.tune + kArpSteps, d.arp.tune);
    }
}

void StratumProcessor::applySnapshot (const EngineParams& snap, const PresetInfo& newInfo)
{
    panicRequested = true;
    copyCustomState (snap);
    binder->push (snap);
    info = newInfo;
    reloadUserWavetables();
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
    pi.name = p.name; pi.category = p.category; pi.author = "Furious";
    for (int l = 0; l < kLayers; ++l) pi.layerNames[l] = juce::String (l + 1);
    applySnapshot (snap, pi);
}

juce::File StratumProcessor::getUserPresetDirectory()
{
    return userDataFolder().getChildFile ("Presets");
}

bool StratumProcessor::saveUserPreset (const juce::String& name, const juce::String& category)
{
    auto dir = getUserPresetDirectory();
    if (! dir.createDirectory()) return false;
    info.name = name.trim().isEmpty() ? "Untitled" : name.trim();
    if (category.trim().isNotEmpty()) info.category = category.trim();
    if (info.author.isEmpty() || info.author == "Furious") info.author = "User";
    auto xml = makeStateXml();
    if (xml == nullptr) return false;
    auto file = dir.getChildFile (juce::File::createLegalFileName (info.name) + ".stpreset");
    if (file.existsAsFile()) file.copyFileTo (file.withFileExtension ("stpreset.bak"));
    const bool ok = xml->writeTo (file);
    if (ok) ++presetListVersion;
    return ok;
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

// User wavetables
juce::File StratumProcessor::getWavetableDirectory()
{
    return userDataFolder().getChildFile ("Wavetables");
}

juce::String StratumProcessor::loadWavetableIntoSlot (int slot, const juce::String& relativePath)
{
    const auto root = getWavetableDirectory();
    const auto file = root.getChildFile (relativePath.replaceCharacter ('/', juce::File::getSeparatorChar()));
    if (! file.existsAsFile() || ! file.isAChildOf (root)) return "The file was not found in the wavetable folder.";
    if (file.getSize() > 128 * 1024 * 1024) return "The file is too large to be a wavetable.";

    const std::string key = (file.getFullPathName() + "|" + juce::String (file.getSize()) + "|"
                             + juce::String (file.getLastModificationTime().toMilliseconds())).toStdString();
    if (auto cached = bank.lookup (key)) { bank.assign (slot, cached); return {}; }

    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes)) return "The file could not be read.";
    std::string err;
    auto table = loadUserTableFromMemory (bytes.getData(), bytes.getSize(), file.getFileNameWithoutExtension().toStdString(), err);
    if (table == nullptr) return juce::String (err).substring (0, 1).toUpperCase() + juce::String (err).substring (1) + ".";

    bank.remember (key, table);
    bank.assign (slot, table);
    return {};
}

juce::String StratumProcessor::setUserWavetable (int layer, int osc, const juce::String& relativePath)
{
    const auto err = loadWavetableIntoSlot (layer * kOscs + osc, relativePath);
    if (err.isEmpty()) info.userTable[layer][osc] = relativePath;
    bank.collect();
    return err;
}

void StratumProcessor::clearUserWavetable (int layer, int osc)
{
    info.userTable[layer][osc] = juce::String();
    bank.clear (layer * kOscs + osc);
    bank.collect();
}

void StratumProcessor::moveEffect (int layer, int from, int to)
{
    if (layer < 0 || layer >= kLayers || from == to || from < 0 || to < 0 || from >= kFxSlots || to >= kFxSlots) return;
    int src[kFxSlots];
    fxMoveOrder (from, to, src);

    auto setPlain = [this] (const juce::String& id, float plain)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const float n = p->convertTo0to1 (plain);
            if (std::abs (p->getValue() - n) < 1.0e-6f) return;
            p->beginChangeGesture(); p->setValueNotifyingHost (n); p->endChangeGesture();
        }
    };
    auto plainOf = [this] (const juce::String& id) -> float
    {
        if (auto* p = apvts.getParameter (id)) return p->convertFrom0to1 (p->getValue());
        return 0.0f;
    };

    static const char* sfx[] = { "type", "mix", "a", "b", "c", "d", "e", "f", "g" };
    float old[kFxSlots][9];
    for (int k = 0; k < kFxSlots; ++k)
        for (int j = 0; j < 9; ++j)
            old[k][j] = plainOf (layerId (layer, "x" + juce::String (k + 1) + "_" + sfx[j]));
    for (int i = 0; i < kFxSlots; ++i)
        for (int j = 0; j < 9; ++j)
            setPlain (layerId (layer, "x" + juce::String (i + 1) + "_" + sfx[j]), old[src[i]][j]);

    // modulation routes to an effect's mix follow it
    for (int j = 0; j < kModSlots; ++j)
    {
        const juce::String id = layerId (layer, "r" + juce::String (j + 1) + "_tgt");
        const int t = (int) std::lround (plainOf (id));
        if (t >= TgFx1Mix && t <= TgFx7Mix)
            setPlain (id, (float) (TgFx1Mix + fxNewPosition (t - TgFx1Mix, from, to)));
    }
}

// Re-applies info.userTable to the audio engine (after a preset or project is loaded).
// A file that cannot be found keeps its name, so the editor can show it as missing.
void StratumProcessor::reloadUserWavetables()
{
    for (int l = 0; l < kLayers; ++l)
        for (int o = 0; o < kOscs; ++o)
        {
            const int slot = l * kOscs + o;
            if (info.userTable[l][o].isEmpty() || loadWavetableIntoSlot (slot, info.userTable[l][o]).isNotEmpty())
                bank.clear (slot);
        }
    bank.collect();
}

juce::AudioProcessorEditor* StratumProcessor::createEditor() { return new StratumEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StratumProcessor(); }
