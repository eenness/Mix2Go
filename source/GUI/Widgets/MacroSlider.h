//
// Created by Landon Viator on 11/21/25.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Globals/Globals.h"

namespace viator::gui::widgets
{
    class MacroSlider : public juce::Slider, public juce::ActionBroadcaster
    {
    public:
        MacroSlider();
        ~MacroSlider() override;

        using MacroState = viator::globals::MacroLearnState;

        void paint(juce::Graphics &g) override;

        void toggleMacroState();
        void enableMacroState(const bool shouldBeOn);
        MacroState getMacroState();

        enum class MacroLearnState
        {
            kOff,
            kOn
        };

    private:
        MacroState m_macro_learn_state{MacroState::kOff};

        void mouseDown(const juce::MouseEvent &event);
    };
}

