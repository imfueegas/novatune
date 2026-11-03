#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/AutoTuneEngine.h"

class NovaTuneProcessor : public juce::AudioProcessor
{
public:
    NovaTuneProcessor();
    ~NovaTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Notes currently allowed by the key / scale / custom selection (index 0 = C).
    std::array<bool, 12> getCurrentScaleMask() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    novatune::AutoTuneEngine engine;

private:
    novatune::EngineParams readParams() const;

    std::atomic<float>* pKey       = nullptr;
    std::atomic<float>* pScale     = nullptr;
    std::atomic<float>* pRetune    = nullptr;
    std::atomic<float>* pFlex      = nullptr;
    std::atomic<float>* pAmount    = nullptr;
    std::atomic<float>* pTranspose = nullptr;
    std::atomic<float>* pRefA      = nullptr;
    std::array<std::atomic<float>*, 12> pNotes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaTuneProcessor)
};
