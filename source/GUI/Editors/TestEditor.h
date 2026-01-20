//
// Created by Landon Viator on 11/14/25.
//

#pragma once

#include "../../DSP/Processors/TestProcessor.h"

namespace viator::gui::editors
{
    class TestEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit TestEditor(viator::dsp::processors::TestProcessor &);

        ~TestEditor() override;

        //==============================================================================
        void paint(juce::Graphics &) override;

        void resized() override;

    private:
        viator::dsp::processors::TestProcessor &processorRef;
    };
}