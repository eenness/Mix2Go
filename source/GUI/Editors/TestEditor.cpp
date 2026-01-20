//
// Created by Landon Viator on 11/14/25.
//

#include "TestEditor.h"

namespace viator::gui::editors
{
    TestEditor::TestEditor(viator::dsp::processors::TestProcessor &p)
            : AudioProcessorEditor(&p), processorRef(p)
    {
        juce::ignoreUnused(processorRef);

        setSize(1000, 600);
    }

    TestEditor::~TestEditor()
    {
    }

//==============================================================================
    void TestEditor::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colours::blue);

        g.setColour(juce::Colours::black);
        g.drawText("Test", 0, 0, getWidth(), getHeight(), juce::Justification::centred);
    }

    void TestEditor::resized()
    {
    }
}