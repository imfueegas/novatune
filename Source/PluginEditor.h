#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Rotary knob with a simple arc.
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

// Row of 12 note buttons. Shows the notes allowed by the current scale; clicking one
// switches to the "Custom" scale and toggles that note.
class NoteKeys : public juce::Component
{
public:
    explicit NoteKeys (NovaTuneProcessor& p) : proc (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    NovaTuneProcessor& proc;
};

class NovaTuneEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit NovaTuneEditor (NovaTuneProcessor&);
    ~NovaTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void drawMeter (juce::Graphics&, juce::Rectangle<float> area);

    NovaTuneProcessor& proc;
    KnobLookAndFeel laf;

    juce::ComboBox keyBox, scaleBox;
    juce::Slider retune, flex, amount, transpose, refA;
    NoteKeys keys;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboAttachment> keyAtt, scaleAtt;
    std::unique_ptr<SliderAttachment> retuneAtt, flexAtt, amountAtt, transposeAtt, refAAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaTuneEditor)
};
