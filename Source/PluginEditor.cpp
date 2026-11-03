#include "PluginEditor.h"

namespace
{
    const juce::Colour colBackground { 0xff12141a };
    const juce::Colour colPanel      { 0xff1d212c };
    const juce::Colour colAccent     { 0xff4fd1c5 };
    const juce::Colour colText       { 0xffe6e9ef };
    const juce::Colour colDim        { 0xff7d8596 };

    bool isBlackKey (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    juce::String describeMidi (float midi)
    {
        const int n = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - (float) n) * 100.f);
        return juce::String (novatune::noteName (n)) + juce::String (n / 12 - 1)
               + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + " ct";
    }
}

//==============================================================================
KnobLookAndFeel::KnobLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, colPanel);
    setColour (juce::ComboBox::textColourId, colText);
    setColour (juce::ComboBox::outlineColourId, colDim.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId, colAccent);
    setColour (juce::PopupMenu::backgroundColourId, colPanel);
    setColour (juce::PopupMenu::textColourId, colText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colAccent.withAlpha (0.35f));
    setColour (juce::Slider::textBoxTextColourId, colText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    g.setColour (colPanel);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.f, radius * 2.f);

    const float arcR = radius - 4.f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, endAngle, true);
    g.setColour (colDim.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, angle, true);
    g.setColour (colAccent);
    g.strokePath (value, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -arcR + 8.f, 3.f, arcR * 0.4f, 1.5f);
    g.setColour (colText);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

//==============================================================================
void NoteKeys::paint (juce::Graphics& g)
{
    const auto mask = proc.getCurrentScaleMask();
    const auto st = proc.engine.getUiState();
    const int active = st.voiced ? (((juce::roundToInt (st.outputMidi) % 12) + 12) % 12) : -1;
    const float w = (float) getWidth() / 12.f;

    for (int i = 0; i < 12; ++i)
    {
        auto area = juce::Rectangle<float> ((float) i * w + 2.f, 0.f, w - 4.f, (float) getHeight());
        auto c = mask[(size_t) i] ? colAccent.withAlpha (isBlackKey (i) ? 0.55f : 0.85f)
                                  : colPanel;
        if (i == active)
            c = c.brighter (0.5f);

        g.setColour (c);
        g.fillRoundedRectangle (area, 6.f);

        g.setColour (mask[(size_t) i] ? juce::Colour (0xff0b1015) : colDim);
        g.setFont (14.f);
        g.drawText (novatune::noteName (i), area, juce::Justification::centred);
    }
}

void NoteKeys::mouseDown (const juce::MouseEvent& e)
{
    const int idx = juce::jlimit (0, 11, (int) (e.position.x / ((float) getWidth() / 12.f)));

    auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scale"));
    if (scaleParam == nullptr)
        return;

    const auto mask = proc.getCurrentScaleMask();

    if (scaleParam->getIndex() != (int) novatune::Custom)
    {
        // Start the custom scale from whatever is currently active.
        for (int j = 0; j < 12; ++j)
            if (auto* b = dynamic_cast<juce::AudioParameterBool*> (proc.apvts.getParameter ("note" + juce::String (j))))
                *b = mask[(size_t) j];

        *scaleParam = (int) novatune::Custom;
    }

    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (proc.apvts.getParameter ("note" + juce::String (idx))))
        *b = ! b->get();
}

//==============================================================================
NovaTuneEditor::NovaTuneEditor (NovaTuneProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), keys (p)
{
    setLookAndFeel (&laf);

    for (int i = 0; i < 12; ++i)
        keyBox.addItem (novatune::noteName (i), i + 1);
    for (int i = 0; i < novatune::NumScales; ++i)
        scaleBox.addItem (novatune::scaleName (i), i + 1);

    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (keys);

    for (auto* s : { &retune, &flex, &amount, &transpose, &refA })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
        addAndMakeVisible (*s);
    }

    keyAtt       = std::make_unique<ComboAttachment> (proc.apvts, "key", keyBox);
    scaleAtt     = std::make_unique<ComboAttachment> (proc.apvts, "scale", scaleBox);
    retuneAtt    = std::make_unique<SliderAttachment> (proc.apvts, "retune", retune);
    flexAtt      = std::make_unique<SliderAttachment> (proc.apvts, "flex", flex);
    amountAtt    = std::make_unique<SliderAttachment> (proc.apvts, "amount", amount);
    transposeAtt = std::make_unique<SliderAttachment> (proc.apvts, "transpose", transpose);
    refAAtt      = std::make_unique<SliderAttachment> (proc.apvts, "refA", refA);

    setSize (680, 440);
    startTimerHz (30);
}

NovaTuneEditor::~NovaTuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NovaTuneEditor::resized()
{
    const int w = getWidth();

    keyBox.setBounds (w - 330, 34, 90, 26);
    scaleBox.setBounds (w - 230, 34, 210, 26);

    keys.setBounds (20, 196, w - 40, 44);

    const int knobW = (w - 40) / 5;
    int x = 20;
    for (auto* s : { &retune, &flex, &amount, &transpose, &refA })
    {
        s->setBounds (x, 282, knobW, 150);
        x += knobW;
    }
}

void NovaTuneEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colPanel);
    g.fillRoundedRectangle (area, 10.f);

    const auto st = proc.engine.getUiState();

    if (! st.voiced)
    {
        g.setColour (colDim);
        g.setFont (22.f);
        g.drawText ("Waiting for a voice...", area, juce::Justification::centred);
        return;
    }

    const int refNote = juce::roundToInt (st.outputMidi);
    const float detectedCents = (st.detectedMidi - (float) refNote) * 100.f;
    const float outputCents   = (st.outputMidi   - (float) refNote) * 100.f;

    auto top = area.reduced (16.f, 10.f);
    auto textRow = top.removeFromTop (46.f);

    g.setColour (colText);
    g.setFont (38.f);
    g.drawText (novatune::noteName (refNote) + juce::String (refNote / 12 - 1),
                textRow.removeFromLeft (140.f), juce::Justification::centredLeft);

    g.setColour (colDim);
    g.setFont (14.f);
    g.drawText ("in  " + describeMidi (st.detectedMidi) + "     out  " + describeMidi (st.outputMidi),
                textRow, juce::Justification::centredRight);

    // Cents bar: +/-50 cents around the output note.
    auto bar = top.removeFromBottom (36.f).reduced (0.f, 8.f);
    g.setColour (colBackground);
    g.fillRoundedRectangle (bar, 4.f);

    auto xFor = [&bar] (float cents)
    {
        const float c = juce::jlimit (-50.f, 50.f, cents);
        return bar.getCentreX() + (c / 50.f) * (bar.getWidth() * 0.5f - 6.f);
    };

    g.setColour (colDim.withAlpha (0.5f));
    for (int c = -50; c <= 50; c += 10)
        g.drawVerticalLine ((int) xFor ((float) c), bar.getY() + (c == 0 ? 0.f : 5.f), bar.getBottom() - (c == 0 ? 0.f : 5.f));

    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (xFor (detectedCents) - 2.f, bar.getY() - 2.f, 4.f, bar.getHeight() + 4.f, 2.f);

    g.setColour (colAccent);
    g.fillEllipse (xFor (outputCents) - 7.f, bar.getCentreY() - 7.f, 14.f, 14.f);
}

void NovaTuneEditor::paint (juce::Graphics& g)
{
    g.fillAll (colBackground);

    g.setColour (colText);
    g.setFont (28.f);
    g.drawText ("NOVATUNE", 20, 14, 260, 40, juce::Justification::centredLeft);

    g.setColour (colDim);
    g.setFont (12.f);
    g.drawText ("KEY",   keyBox.getX(),   keyBox.getY() - 16,   keyBox.getWidth(),   14, juce::Justification::centredLeft);
    g.drawText ("SCALE", scaleBox.getX(), scaleBox.getY() - 16, scaleBox.getWidth(), 14, juce::Justification::centredLeft);

    drawMeter (g, juce::Rectangle<float> (20.f, 74.f, (float) getWidth() - 40.f, 110.f));

    const char* names[] = { "RETUNE SPEED", "FLEX-TUNE", "AMOUNT", "TRANSPOSE", "REFERENCE A" };
    const int knobW = (getWidth() - 40) / 5;
    g.setColour (colDim);
    g.setFont (12.f);
    for (int i = 0; i < 5; ++i)
        g.drawText (names[i], 20 + i * knobW, 262, knobW, 16, juce::Justification::centred);
}
