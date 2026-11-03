#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace novatune;

juce::AudioProcessorValueTreeState::ParameterLayout NovaTuneProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray keys, scales;
    for (int i = 0; i < 12; ++i) keys.add (noteName (i));
    for (int i = 0; i < NumScales; ++i) scales.add (scaleName (i));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keys, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scales, (int) Chromatic));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "retune", 1 }, "Retune Speed",
        NormalisableRange<float> (0.f, 400.f, 0.1f, 0.4f), 25.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (v, v < 10.f ? 1 : 0) + " ms";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "flex", 1 }, "Flex-Tune",
        NormalisableRange<float> (0.f, 50.f, 0.1f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (v, 1) + " ct";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "amount", 1 }, "Amount",
        NormalisableRange<float> (0.f, 100.f, 1.f), 100.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (roundToInt (v)) + " %";
        })));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "transpose", 1 }, "Transpose", -12, 12, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return String (v > 0 ? "+" : "") + String (v) + " st";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "refA", 1 }, "Reference A",
        NormalisableRange<float> (415.f, 466.f, 0.1f), 440.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (v, 1) + " Hz";
        })));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "note" + String (i), 1 }, String ("Note ") + noteName (i), true));

    return layout;
}

NovaTuneProcessor::NovaTuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    pKey       = apvts.getRawParameterValue ("key");
    pScale     = apvts.getRawParameterValue ("scale");
    pRetune    = apvts.getRawParameterValue ("retune");
    pFlex      = apvts.getRawParameterValue ("flex");
    pAmount    = apvts.getRawParameterValue ("amount");
    pTranspose = apvts.getRawParameterValue ("transpose");
    pRefA      = apvts.getRawParameterValue ("refA");

    for (int i = 0; i < 12; ++i)
        pNotes[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i));
}

EngineParams NovaTuneProcessor::readParams() const
{
    EngineParams p;
    p.key            = (int) pKey->load();
    p.scale          = (int) pScale->load();
    p.retuneMs       = pRetune->load();
    p.flexCents      = pFlex->load();
    p.amount         = pAmount->load() * 0.01f;
    p.transposeSemis = pTranspose->load();
    p.referenceA     = pRefA->load();

    for (size_t i = 0; i < 12; ++i)
        p.customNotes[i] = pNotes[i]->load() > 0.5f;

    return p;
}

std::array<bool, 12> NovaTuneProcessor::getCurrentScaleMask() const
{
    const auto p = readParams();
    return buildMask (p.key, p.scale, p.customNotes);
}

void NovaTuneProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    setLatencySamples (engine.getLatencySamples());
}

bool NovaTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void NovaTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* NovaTuneProcessor::createEditor()
{
    return new NovaTuneEditor (*this);
}

void NovaTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NovaTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NovaTuneProcessor();
}
