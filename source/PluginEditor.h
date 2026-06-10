#pragma once

#include "PluginProcessor.h"
#include "Streaming/AudioStreamManager.h"
#include "GUI/Style/Mix2GoLookAndFeel.h"

//==============================================================================
// Mix2Go streaming UI. One job: show the streaming status at a glance —
// painted logo + wordmark, big status word, stereo VU meters, and a stats bar.
// Everything except the action button is hand-painted in paint().
//==============================================================================
class AudioPluginAudioProcessorEditor final
        : public juce::AudioProcessorEditor,
          public mix2go::streaming::StreamListener,
          private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ── UI state derived from the stream manager ─────────────────────────
    struct UiState
    {
        juce::String word;      // big hero word
        juce::String subtitle;  // line under it
        juce::String pill;      // top-bar pill label
        juce::Colour colour;    // state colour
        bool streaming;         // meters + streaming stats visible
        bool busy;              // searching/connecting (button disabled)
    };
    UiState currentUiState() const;

    // ── Paint helpers ────────────────────────────────────────────────────
    void paintLogo(juce::Graphics&, juce::Rectangle<float>);
    void paintWordmark(juce::Graphics&, float x, float cy);
    void paintPill(juce::Graphics&, juce::Rectangle<float>, juce::Colour, const juce::String&);
    void paintMeters(juce::Graphics&, juce::Rectangle<float>);
    void paintStatsBar(juce::Graphics&, juce::Rectangle<float>, const UiState&);
    void paintMeterRow(juce::Graphics&, juce::Rectangle<float>, const juce::String&, float level);

    // ── Streaming hooks ──────────────────────────────────────────────────
    void onStreamButtonClicked();
    void streamStateChanged(mix2go::streaming::StreamState newState) override;
    void syncButton();
    void timerCallback() override;

    AudioPluginAudioProcessor& processorRef;
    mix2go::gui::Mix2GoLookAndFeel m_laf;
    juce::TextButton m_stream_button { "Stop" };

    // Smoothed meter values (message-thread only).
    float m_dispL = 0.0f, m_dispR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
