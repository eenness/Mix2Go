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

    // Start listening for the Mix2Go app — streaming starts automatically on discovery.
    const int boundPort = processorRef.getStreamManager().startDiscovery();

    // streamStateChanged(Disconnected) is never called at startup because the state
    // doesn't *change* — it starts as Disconnected.  Set initial UI here explicitly.
    if (boundPort > 0)
    {
        m_status_label.setText("Suche Mix2Go App… (UDP " + juce::String(boundPort) + ")",
                               juce::dontSendNotification);
        m_stream_button.setButtonText("Suche läuft…");
        m_stream_button.setEnabled(false);
    }
    else
    {
        m_status_label.setText("UDP-Bind fehlgeschlagen! Port 40051-40059 belegt?",
                               juce::dontSendNotification);
        m_status_label.setColour(juce::Label::textColourId, juce::Colours::red);
        m_stream_button.setButtonText("Fehler");
        m_stream_button.setEnabled(false);
    }

    setSize(1500, 700);
    startTimerHz(30);                       //setzt das intervall vom pegel aktualisieren auf 30ms
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Stop discovery before unregistering to avoid late callAsync hitting a dead object.
    processorRef.getStreamManager().stopDiscovery();

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

    // Streaming UI — top bar (auto-discovery, no IP/port inputs needed)
    const int streamX      = 200;
    const int streamY      = 8;
    const int rowHeight    = 24;
    const int spacing      = 8;
    const int statusWidth  = 220;
    const int deviceWidth  = 240;
    const int buttonWidth  = 130;

    // [● Status]  [device info]  [Stop button]  [stats]
    m_status_label.setBounds(streamX, streamY, statusWidth, rowHeight);
    m_device_label.setBounds(streamX + statusWidth + spacing, streamY, deviceWidth, rowHeight);
    m_stream_button.setBounds(streamX + statusWidth + deviceWidth + spacing * 2, streamY, buttonWidth, rowHeight);
    m_stats_label.setBounds(streamX, streamY + rowHeight + 2, statusWidth + deviceWidth + buttonWidth + spacing * 2, rowHeight);
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
    // Status label — shows discovery / connection state
    m_status_label.setJustificationType(juce::Justification::centredLeft);
    m_status_label.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(m_status_label);

    // Device label — shows "IP:port" once an app is discovered
    m_device_label.setJustificationType(juce::Justification::centredLeft);
    m_device_label.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(m_device_label);

    // Stop button — text and enabled state updated by streamStateChanged / initState.
    m_stream_button.setButtonText("Suche läuft…");
    m_stream_button.setEnabled(false);
    m_stream_button.onClick = [this]() { onStreamButtonClicked(); };
    addAndMakeVisible(m_stream_button);

    // Stats label — packet / byte counters
    m_stats_label.setJustificationType(juce::Justification::centredLeft);
    m_stats_label.setColour(juce::Label::textColourId, juce::Colours::grey);
    m_stats_label.setFont(juce::Font(12.0f));
    addAndMakeVisible(m_stats_label);
}

void AudioPluginAudioProcessorEditor::onStreamButtonClicked()
{
    auto& sm = processorRef.getStreamManager();

    if (sm.isStreaming())
    {
        // User manually stops: disable auto-reconnect so the 1-second
        // heartbeat doesn't immediately restart streaming.
        sm.setAutoConnect(false);
        sm.stopStreaming();
        // streamStateChanged(Disconnected) fires → UI shows "Enable" button.
    }
    else
    {
        // User re-enables auto-connect.  Discovery is still running; the next
        // heartbeat (≤1 s) will trigger startStreaming() automatically.
        sm.setAutoConnect(true);
        m_status_label.setText("Suche Mix2Go App…", juce::dontSendNotification);
        m_status_label.setColour(juce::Label::textColourId, juce::Colours::orange);
        m_stream_button.setButtonText("Auto-Connect läuft");
        m_stream_button.setEnabled(false);
    }
}

void AudioPluginAudioProcessorEditor::streamStateChanged(mix2go::streaming::StreamState newState)
{
    // Always called on the message thread via callAsync in AudioStreamManager.
    juce::MessageManager::callAsync([this, newState]()
    {
        auto& mgr = processorRef.getStreamManager();

        switch (newState)
        {
            case mix2go::streaming::StreamState::Disconnected:
                m_device_label.setText("", juce::dontSendNotification);
                if (mgr.isAutoConnectEnabled())
                {
                    // Normal searching state — show port so user can verify.
                    const int port = mgr.discoveryBoundPort();
                    const juce::String portStr = port > 0
                        ? " (UDP " + juce::String(port) + ")"
                        : " (Port-Fehler!)";
                    m_status_label.setText("Suche Mix2Go App…" + portStr,
                                           juce::dontSendNotification);
                    m_status_label.setColour(juce::Label::textColourId, juce::Colours::orange);
                    m_stream_button.setButtonText("Suche läuft…");
                    m_stream_button.setEnabled(false);
                }
                else
                {
                    // User manually stopped — let them re-enable.
                    m_status_label.setText("Gestoppt", juce::dontSendNotification);
                    m_status_label.setColour(juce::Label::textColourId, juce::Colours::grey);
                    m_stream_button.setButtonText("Auto-Connect aktivieren");
                    m_stream_button.setEnabled(true);
                }
                break;

            case mix2go::streaming::StreamState::Connecting:
                m_status_label.setText("Verbinde…", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::yellow);
                m_device_label.setText(mgr.discoveredIP() + ":" + juce::String(mgr.discoveredPort()),
                                       juce::dontSendNotification);
                m_stream_button.setButtonText("Auto-Connect läuft");
                m_stream_button.setEnabled(false);
                break;

            case mix2go::streaming::StreamState::Streaming:
                m_status_label.setText("● Streaming", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::limegreen);
                m_device_label.setText(mgr.discoveredIP() + ":" + juce::String(mgr.discoveredPort()),
                                       juce::dontSendNotification);
                m_stream_button.setButtonText("Stop");
                m_stream_button.setEnabled(true);
                break;

            case mix2go::streaming::StreamState::Error:
                m_status_label.setText("Fehler", juce::dontSendNotification);
                m_status_label.setColour(juce::Label::textColourId, juce::Colours::red);
                m_device_label.setText("", juce::dontSendNotification);
                m_stream_button.setButtonText("Auto-Connect aktivieren");
                m_stream_button.setEnabled(true);
                break;
        }
    });
}

void AudioPluginAudioProcessorEditor::updateStreamingUI()
{
    auto& sm = processorRef.getStreamManager();

    if (sm.isStreaming())
    {
        juce::String stats;
        stats << "Pkts: "       << sm.getPacketsSent()
              << "  KB: "       << (sm.getBytesSent() / 1024)
              << "  FIFO: "     << sm.getFIFOLevel()
              << "  Underruns: "<< sm.getFIFOUnderruns();

        m_stats_label.setText(stats, juce::dontSendNotification);
    }
    else
    {
        // Show discovery diagnostics so the user can see if it's working.
        const int pkts = sm.discoveryPacketsReceived();
        const int port = sm.discoveryBoundPort();

        juce::String info;
        if (port <= 0)
        {
            info = "Discovery: Port-Bind fehlgeschlagen!";
        }
        else if (pkts == 0)
        {
            info = "Discovery: Lausche auf UDP " + juce::String(port)
                 + " — noch kein Paket empfangen";
        }
        else
        {
            info = "Discovery: "  + juce::String(pkts)
                 + " Pkt empfangen  |  letztes Gerat: "
                 + sm.discoveredIP();
            if (sm.discoveredPort() > 0)
                info += ":" + juce::String(sm.discoveredPort());
        }

        m_stats_label.setText(info, juce::dontSendNotification);
    }
}