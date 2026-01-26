#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
        : AudioProcessorEditor(&p), processorRef(p), m_rack(processorRef)
{
    juce::ignoreUnused(processorRef);

    const auto items = viator::globals::Oversampling::items;
    setComboBoxProps(m_oversampling_menu, items);

    addAndMakeVisible(m_rack);
    m_rack.addActionListener(this);
    m_rack.rebuild_editors();
    initMacroKnobs();
    initStreamingUI();  // Initialize streaming controls

    m_view_port.setViewedComponent(&m_rack, false);
    m_view_port.setScrollBarsShown(false, true);
    addAndMakeVisible(m_view_port);

    refreshMacroMappings();

    // Register as stream listener
    processorRef.getStreamManager().addListener(this);

    setSize(1500, 700);
    startTimerHz(30);                       //setzt das intervall vom pegel aktualisieren auf 30ms
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Unregister stream listener
    processorRef.getStreamManager().removeListener(this);

    for (auto &macro: m_macro_knobs)
    {
        macro.removeMouseListener(this);
    }

    m_rack.removeActionListener(this);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black.brighter(0.12f));

    g.setColour(juce::Colours::black);
    g.drawRect(0, 0, getWidth(), getHeight(), 3);

    // Breite der Pegelbalken
    const int meterWidth = 40;

    // Maximale Höhe der Pegel
    const int meterHeight = getHeight() - 40;

    // X-Positionen für Links und Rechts
    int leftX = 40;
    int rightX = 120;

    // Unterkante (Pegel wachsen nach oben)
    int bottom = getHeight() - 20;

    // Umrechnung von 0..1 auf Pixelhöhe
    int hL = juce::jlimit(0, meterHeight, (int)(meterL * meterHeight));
    int hR = juce::jlimit(0, meterHeight, (int)(meterR * meterHeight));

    // Farbe setzen
    g.setColour(juce::Colours::green);

    // Linken Pegel zeichnen
    g.fillRect(leftX, bottom - hL, meterWidth, hL);

    // Rechten Pegel zeichnen
    g.fillRect(rightX, bottom - hR, meterWidth, hR);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // OS MENU
    const auto padding = juce::roundToInt(getWidth() * 0.03);
    auto width = juce::roundToInt(getWidth() * 0.1);
    auto height = juce::roundToInt(getHeight() * 0.05);
    auto x = getWidth() - width - padding;
    auto y = padding;
    m_oversampling_menu.setBounds(x, y, width, height);

    const int rackX = juce::roundToInt(getWidth() * 0.0);
    const int rackY = getHeight() / 10;
    const int rackWidth = juce::roundToInt(getWidth());
    const int rackHeight = juce::roundToInt(getHeight() * 0.8);
    const int numEditors = static_cast<int>(m_rack.getEditors().size());
    const auto rack_extra = juce::roundToInt((rackWidth * 0.25)) * numEditors;

    m_rack.setParentWidth(rackWidth);
    m_rack.setBounds(
            rackX,
            rackY,
            numEditors < 4 ? rackWidth : rackWidth + rack_extra,
            rackHeight);

    m_view_port.setBounds(rackX, rackY, rackWidth, rackHeight);

    // MACRO DIALS
    x = juce::roundToInt(getWidth() * 0.026);
    y = juce::roundToInt(getHeight() * 0.9);
    width = juce::roundToInt(getWidth() * 0.05);
    height = width;
    for (auto &knob: m_macro_knobs)
    {
        knob.setBounds(x, y, width, height);
        x += width * 2;
    }

    // Streaming UI - top left area
    const int streamX = 200;
    const int streamY = 10;
    const int labelWidth = 80;
    const int inputWidth = 120;
    const int buttonWidth = 140;
    const int rowHeight = 25;
    const int spacing = 5;

    m_ip_label.setBounds(streamX, streamY, labelWidth, rowHeight);
    m_ip_input.setBounds(streamX + labelWidth + spacing, streamY, inputWidth, rowHeight);
    m_port_label.setBounds(streamX + labelWidth + inputWidth + spacing * 2, streamY, 50, rowHeight);
    m_port_input.setBounds(streamX + labelWidth + inputWidth + 50 + spacing * 3, streamY, 60, rowHeight);
    m_stream_button.setBounds(streamX + labelWidth + inputWidth + 50 + 60 + spacing * 4, streamY, buttonWidth, rowHeight);
    m_status_label.setBounds(streamX + labelWidth + inputWidth + 50 + 60 + buttonWidth + spacing * 5, streamY, 150, rowHeight);
    m_stats_label.setBounds(streamX, streamY + rowHeight + spacing, 400, rowHeight);
}

void AudioPluginAudioProcessorEditor::setComboBoxProps(juce::ComboBox &box, const juce::StringArray &items)
{
    box.addItemList(items, 1);
    box.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(box);
}

void AudioPluginAudioProcessorEditor::initMacroKnobs()
{
    for (int i = 0; i < m_macro_knobs.size(); ++i)
    {
        m_macro_knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        m_macro_knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m_macro_knobs[i].setComponentID("macro" + juce::String(i + 1) + "ID");
        m_macro_knobs[i].addMouseListener(this, true);
        m_macro_attaches.emplace_back(
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef
                                                                                               .getTreeState(),
                                                                                       "macro" + juce::String(i + 1) +
                                                                                       "ID", m_macro_knobs[i]));
        addAndMakeVisible(m_macro_knobs[i]);
    }
}

void AudioPluginAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster *source)
{
    if (auto slider = dynamic_cast<viator::gui::widgets::BaseSlider *>(source))
    {
        const auto slider_id_to_map = slider->getComponentID();
        const auto is_mapped = slider->getIsMapped();

        if (is_mapped)
        {
            processorRef.getMacroMap().removeMacroAssignment(slider_id_to_map);
        } else
        {
            processorRef.getMacroMap().addMacroAssignment(slider_id_to_map);
            slider->getProperties().set(viator::globals::WidgetProperties::macroKey, processorRef.getMacroMap()
                    .getCurrentMacro());
        }

        slider->setIsMapped(!is_mapped);
        slider->showMapping(!is_mapped);
    }
}

void AudioPluginAudioProcessorEditor::actionListenerCallback(const juce::String &message)
{
    if (message == viator::globals::ActionCommands::editorAdded)
    {
        for (auto &editor: m_rack.getEditors())
        {
            if (auto *base_editor = dynamic_cast<viator::gui::editors::BaseEditor *>(editor.get()))
            {
                for (auto &slider: base_editor->getSliders())
                {
                    slider->removeChangeListener(this);
                    slider->addChangeListener(this);
                }
            }
        }

        resized();
    }

    if (message == viator::globals::ActionCommands::editorDeleted)
    {
        resized();
    }
}

void AudioPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent &event)
{
    if (event.mods.isRightButtonDown())
    {
        if (auto *macro_slider = dynamic_cast<viator::gui::widgets::MacroSlider *>(event.eventComponent))
        {
            for (auto &macro: m_macro_knobs)
            {
                if (&macro != macro_slider)
                    macro.enableMacroState(false);
            }

            const auto selected_macro = macro_slider->getComponentID();
            macro_slider->toggleMacroState();
            const auto macro_state = macro_slider->getMacroState();
            processorRef.getMacroMap().setMacroLearnState(macro_state);
            processorRef.getMacroMap().macroStateChanged(selected_macro);

            for (auto &editor: m_rack.getEditors())
            {
                if (auto *base_editor = dynamic_cast<viator::gui::editors::BaseEditor *>(editor.get()))
                {
                    for (auto &slider: base_editor->getSliders())
                    {
                        const auto state = static_cast<bool>(static_cast<int>(macro_state));
                        const auto is_macro = selected_macro == slider->getProperties().getWithDefault(
                                viator::globals::WidgetProperties::macroKey, "").toString();
                        slider->showMapping(state && is_macro);
                    }
                }
            }
        }
    }
}

void AudioPluginAudioProcessorEditor::refreshMacroMappings()
{
    for (auto& editor : m_rack.getEditors())
    {
        if (auto* base_editor = dynamic_cast<viator::gui::editors::BaseEditor*>(editor.get()))
        {
            for (auto* slider : base_editor->getSliders())
            {
                const auto sliderID = slider->getComponentID();
                const auto macroID  = processorRef.getMacroMap().getMacroForSlider(sliderID);
                const bool mapped   = macroID.isNotEmpty();

                // restore component property (GUI-only)
                if (mapped)
                    slider->getProperties().set(viator::globals::WidgetProperties::macroKey, macroID);
                else
                    slider->getProperties().remove(viator::globals::WidgetProperties::macroKey);

                // restore internal slider state
                slider->setIsMapped(mapped);

                // default hide until a macro knob is active
                slider->showMapping(false);
            }
        }
    }
}
void AudioPluginAudioProcessorEditor::timerCallback()
{
    // Pegel vom Processor holen
    meterL = processorRef.getMeterL();
    meterR = processorRef.getMeterR();

    // Update streaming stats
    updateStreamingUI();

    // GUI neu zeichnen
    repaint();
}

void AudioPluginAudioProcessorEditor::initStreamingUI()
{
    // IP Input
    m_ip_label.setJustificationType(juce::Justification::centredRight);
    m_ip_label.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(m_ip_label);

    m_ip_input.setText("127.0.0.1");
    m_ip_input.setJustification(juce::Justification::centredLeft);
    addAndMakeVisible(m_ip_input);

    // Port Input
    m_port_label.setJustificationType(juce::Justification::centredRight);
    m_port_label.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(m_port_label);

    m_port_input.setText("12345");
    m_port_input.setJustification(juce::Justification::centredLeft);
    m_port_input.setInputRestrictions(5, "0123456789");
    addAndMakeVisible(m_port_input);

    // Stream Button
    m_stream_button.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
    m_stream_button.onClick = [this]() { onStreamButtonClicked(); };
    addAndMakeVisible(m_stream_button);

    // Status Label
    m_status_label.setJustificationType(juce::Justification::centredLeft);
    m_status_label.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(m_status_label);

    // Stats Label
    m_stats_label.setJustificationType(juce::Justification::centredLeft);
    m_stats_label.setColour(juce::Label::textColourId, juce::Colours::grey);
    m_stats_label.setFont(juce::Font(12.0f));
    addAndMakeVisible(m_stats_label);
}

void AudioPluginAudioProcessorEditor::onStreamButtonClicked()
{
    auto& streamManager = processorRef.getStreamManager();

    if (streamManager.isStreaming())
    {
        streamManager.stopStreaming();
    }
    else
    {
        // Get IP and port from inputs
        const auto ip = m_ip_input.getText();
        const auto port = m_port_input.getText().getIntValue();

        if (ip.isEmpty() || port <= 0 || port > 65535)
        {
            m_status_label.setText("Invalid IP/Port", juce::dontSendNotification);
            m_status_label.setColour(juce::Label::textColourId, juce::Colours::red);
            return;
        }

        streamManager.setTarget(ip, port);
        streamManager.startStreaming();
    }
}

void AudioPluginAudioProcessorEditor::streamStateChanged(mix2go::streaming::StreamState newState)
{
    // Update UI on message thread
    juce::MessageManager::callAsync([this, newState]()
    {
        switch (newState)
        {
            case mix2go::streaming::StreamState::Disconnected:
                m_stream_button.setButtonText("Start Streaming");
                m_stream_button.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
                m_status_label.setText("Disconnected", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::orange);
                break;

            case mix2go::streaming::StreamState::Connecting:
                m_stream_button.setButtonText("Connecting...");
                m_stream_button.setColour(juce::TextButton::buttonColourId, juce::Colours::yellow.darker());
                m_status_label.setText("Connecting...", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::yellow);
                break;

            case mix2go::streaming::StreamState::Streaming:
                m_stream_button.setButtonText("Stop Streaming");
                m_stream_button.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
                m_status_label.setText("Streaming", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::limegreen);
                break;

            case mix2go::streaming::StreamState::Error:
                m_stream_button.setButtonText("Start Streaming");
                m_stream_button.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
                m_status_label.setText("Error", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::red);
                break;
        }
    });
}

void AudioPluginAudioProcessorEditor::updateStreamingUI()
{
    auto& streamManager = processorRef.getStreamManager();

    if (streamManager.isStreaming())
    {
        const auto packets = streamManager.getPacketsSent();
        const auto bytes = streamManager.getBytesSent();
        const auto fifoLevel = streamManager.getFIFOLevel();

        juce::String stats;
        stats << "Packets: " << juce::String(packets)
              << " | Bytes: " << juce::String(bytes / 1024) << " KB"
              << " | FIFO: " << juce::String(fifoLevel);

        m_stats_label.setText(stats, juce::dontSendNotification);
    }
    else
    {
        m_stats_label.setText("", juce::dontSendNotification);
    }
}