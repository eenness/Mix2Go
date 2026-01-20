//
// Created by Landon Viator on 11/8/25.
//

#pragma once
#include <juce_dsp/juce_dsp.h>

namespace viator::dsp
{
    class BaseDspModule
    {
    public:
        BaseDspModule() = default;

        void prepare(juce::dsp::ProcessSpec& spec)
        {
            for (int i = 0; i < m_input_smoothers.size(); ++i)
            {
                m_input_smoothers[i].reset(spec.sampleRate, 0.02);
                m_output_smoothers[i].reset(spec.sampleRate, 0.02);
                m_mix_smoothers[i].reset(spec.sampleRate, 0.02);
            }
        }

        void processBlock(juce::dsp::AudioBlock<float>& block, const int num_samples)
        {
            for (size_t channel = 0; channel < block.getNumChannels(); ++channel) {
                auto *data = block.getChannelPointer(channel);
                for (size_t sample = 0; sample < num_samples; ++sample) {
                    const float xn = data[sample];
                    const float yn = processSample(xn);
                    data[sample] = yn;
                }
            }
        }

        virtual float processSample(float xn) = 0;

        std::array<juce::SmoothedValue<float>, 2>& getInputs() { return m_input_smoothers; }
        std::array<juce::SmoothedValue<float>, 2>& getOutputs() { return m_output_smoothers; }
        std::array<juce::SmoothedValue<float>, 2>& getMixes() { return m_mix_smoothers; }

    private:

        std::array<juce::SmoothedValue<float>, 2> m_input_smoothers, m_output_smoothers, m_mix_smoothers;
    };
}
