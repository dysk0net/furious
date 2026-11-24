#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include <bitset>
#include <memory>

class StratumProcessor : public juce::AudioProcessor
{
public:
    StratumProcessor();
    ~StratumProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Stratum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    struct PresetInfo
    {
        juce::String name = "Init Saw", category = "Init", author = "", comment = "";
        juce::String layerNames[stratum::kLayers];
        juce::String macroNames[3] = { "Macro 1", "Macro 2", "Macro 3" };
        juce::String userTable[stratum::kLayers][stratum::kOscs];   // wavetable file per oscillator, relative to the wavetable folder
    };

    stratum::APVTS apvts;
    stratum::EngineParams engineParams;      // working copy used by the audio thread (also holds step data)
    PresetInfo info;

    std::atomic<float> modWheelValue { 0.0f }, pitchBendValue { 0.0f };
    std::atomic<int> lastNote { -1 }, lastVelocity { 0 };
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<bool> panicRequested { false };

    float getLayerPeak (int l) const { return engine ? engine->layerPeak (l) : 0.0f; }
    int getSeqPosition (int layer, int seq) const { return engine ? engine->seqPosition (layer, seq) : -1; }
    int getArpPosition (int layer) const { return engine ? engine->arpPosition (layer) : -1; }
    float getOutputPeak() const { return engine ? engine->outPeak() : 0.0f; }
    int getVoiceCount() const { return voiceCount.load(); }

    // presets
    int getNumFactoryPresets() const;
    juce::String getFactoryPresetLabel (int i) const;
    void loadFactoryPreset (int index);
    int currentFactoryIndex = 0;

    static juce::File getUserPresetDirectory();
    bool saveUserPreset (const juce::String& name);
    bool loadPresetFile (const juce::File& file);
    juce::Array<juce::File> listUserPresets() const;

    // user wavetables: Serum-format .wav files in Documents/Stratum/Wavetables (the user creates the folder)
    static juce::File getWavetableDirectory();
    // Returns an error message, or an empty string when the table was loaded.
    juce::String setUserWavetable (int layer, int osc, const juce::String& relativePath);
    void clearUserWavetable (int layer, int osc);

    // Moves the effect in slot `from` to slot `to` of a layer; the ones in between shift by one.
    // Settings, the trancegate pattern and modulation routes that aim at an effect's mix all follow the effect.
    void moveEffect (int layer, int from, int to);
    juce::String getUserWavetableName (int layer, int osc) const { return info.userTable[layer][osc]; }
    std::shared_ptr<stratum::UserTable> getUserWavetable (int layer, int osc) const { return bank.getShared (layer * stratum::kOscs + osc); }

private:
    static stratum::APVTS::ParameterLayout createLayout();
    void applySnapshot (const stratum::EngineParams& snap, const PresetInfo& newInfo);
    void copyCustomState (const stratum::EngineParams& src);
    std::unique_ptr<juce::XmlElement> makeStateXml();
    void restoreFromXml (juce::XmlElement& xml);
    void handleMidi (const juce::MidiMessage& m);
    void renderChunk (float* l, float* r, int start, int count);
    juce::String loadWavetableIntoSlot (int slot, const juce::String& relativePath);
    void reloadUserWavetables();

    std::unique_ptr<stratum::ParamBinder> binder;
    stratum::UserWaveBank bank;
    std::unique_ptr<stratum::Engine> engine;
    double sampleRateHz = 44100.0;
    int maxBlock = 512;
    stratum::Transport transport;
    double internalPpq = 0.0;
    std::atomic<int> voiceCount { 0 };
    bool sustainDown = false;
    std::bitset<128> held, sustained;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StratumProcessor)
};
