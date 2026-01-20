//
// Created by Landon Viator on 11/1/25.
//

#pragma once

namespace viator::parameters
{
    inline const juce::String oversamplingChoiceID = "oversamplingChoiceID";
    inline const juce::String oversamplingChoiceName = "oversamplingChoiceName";

    inline const juce::String macro1ID = "macro1ID";
    inline const juce::String macro1Name = "31 Hz";
    inline const juce::String macro2ID = "macro2ID";
    inline const juce::String macro2Name = "63 Hz";
    inline const juce::String macro3ID = "macro3ID";
    inline const juce::String macro3Name = "125 Hz";
    inline const juce::String macro4ID = "macro4ID";
    inline const juce::String macro4Name = "250 Hz";
    inline const juce::String macro5ID = "macro5ID";
    inline const juce::String macro5Name = "500 Hz";
    inline const juce::String macro6ID = "macro6ID";
    inline const juce::String macro6Name = "1 kHz";
    inline const juce::String macro7ID = "macro7ID";
    inline const juce::String macro7Name = "2 kHz";
    inline const juce::String macro8ID = "macro8ID";
    inline const juce::String macro8Name = "4 kHz";
    inline const juce::String macro9ID = "macro9ID";
    inline const juce::String macro9Name = "8 kHz";
    inline const juce::String macro10ID = "macro10ID";
    inline const juce::String macro10Name = "16 kHz";

    struct parameters
    {
        explicit parameters(const juce::AudioProcessorValueTreeState &state)
        {
            oversamplingParam = dynamic_cast<juce::AudioParameterChoice *>(state.getParameter(
                    viator::parameters::oversamplingChoiceID));

            macro1Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro1ID));
            macro2Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro2ID));
            macro3Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro3ID));
            macro4Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro4ID));
            macro5Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro5ID));
            macro6Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro6ID));
            macro7Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro7ID));
            macro8Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro8ID));
            macro9Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro9ID));
            macro10Param = dynamic_cast<juce::AudioParameterFloat *>(state.getParameter(
                    viator::parameters::macro10ID));

        }

        juce::AudioParameterChoice *oversamplingParam{nullptr};

        juce::AudioParameterFloat *macro1Param{nullptr};
        juce::AudioParameterFloat *macro2Param{nullptr};
        juce::AudioParameterFloat *macro3Param{nullptr};
        juce::AudioParameterFloat *macro4Param{nullptr};
        juce::AudioParameterFloat *macro5Param{nullptr};
        juce::AudioParameterFloat *macro6Param{nullptr};
        juce::AudioParameterFloat *macro7Param{nullptr};
        juce::AudioParameterFloat *macro8Param{nullptr};
        juce::AudioParameterFloat *macro9Param{nullptr};
        juce::AudioParameterFloat *macro10Param{nullptr};
    };
}
