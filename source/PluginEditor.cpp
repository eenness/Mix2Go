#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using LAF = mix2go::gui::Mix2GoLookAndFeel;
namespace S = mix2go::streaming;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
        : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&m_laf);

    m_stream_button.setLookAndFeel(&m_laf);
    m_stream_button.onClick = [this]() { onStreamButtonClicked(); };
    addAndMakeVisible(m_stream_button);

    processorRef.getStreamManager().addListener(this);

    syncButton();
    setSize(880, 480);
    setResizable(true, true);
    setResizeLimits(680, 380, 2200, 1200);
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    processorRef.getStreamManager().removeListener(this);
    m_stream_button.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

//==============================================================================
AudioPluginAudioProcessorEditor::UiState
AudioPluginAudioProcessorEditor::currentUiState() const
{
    auto& sm = processorRef.getStreamManager();
    switch (sm.getState())
    {
        case S::StreamState::Streaming:
            return { "STREAMING", "Streaming live audio to your device", "Streaming",
                     LAF::stStreaming(), true, false };
        case S::StreamState::Connecting:
            return { "CONNECTING", "Establishing connection...", "Connecting",
                     LAF::stConnecting(), false, true };
        case S::StreamState::Error:
            return { "ERROR", "Stream failed - check the network", "Error",
                     LAF::stError(), false, false };
        case S::StreamState::Disconnected:
        default:
            if (sm.isAutoConnectEnabled())
                return { "SEARCHING", "Searching for the Mix2Go app...", "Searching",
                         LAF::stSearching(), false, true };
            return { "STOPPED", "Auto-connect disabled", "Stopped",
                     LAF::stStopped(), false, false };
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto& sm = processorRef.getStreamManager();
    const auto u = currentUiState();

    auto b = getLocalBounds().toFloat();
    const float W = b.getWidth(), H = b.getHeight();
    const float topH   = juce::jlimit(48.0f, 72.0f, H * 0.12f);
    const float statsH = juce::jlimit(40.0f, 60.0f, H * 0.10f);

    g.fillAll(LAF::bg());

    // ── Top bar ──────────────────────────────────────────────────────────
    auto top = b.removeFromTop(topH);
    g.setColour(LAF::surface1());
    g.fillRect(top);
    g.setColour(LAF::borderDim());
    g.drawHorizontalLine((int) top.getBottom(), 0.0f, W);

    const float iconSz = topH * 0.46f;
    auto iconR = juce::Rectangle<float>(20.0f, top.getCentreY() - iconSz / 2.0f, iconSz, iconSz);
    paintLogo(g, iconR);
    paintWordmark(g, iconR.getRight() + 12.0f, top.getCentreY());

    // pill + device, to the left of the button
    auto pillR = juce::Rectangle<float>(iconR.getRight() + 130.0f, top.getCentreY() - 11.0f, 118.0f, 22.0f);
    paintPill(g, pillR, u.colour, u.pill);

    juce::String dev = sm.hasDiscoveredDevice()
        ? sm.discoveredIP() + ":" + juce::String(sm.discoveredPort())
        : "UDP " + juce::String(sm.discoveryBoundPort());
    g.setColour(LAF::textMuted());
    g.setFont(juce::Font(12.0f));
    g.drawText(dev, juce::Rectangle<float>(pillR.getRight() + 14.0f, top.getY(), 220.0f, topH),
               juce::Justification::centredLeft, false);

    // ── Stats bar ────────────────────────────────────────────────────────
    auto stats = b.removeFromBottom(statsH);
    paintStatsBar(g, stats, u);

    // ── Hero ─────────────────────────────────────────────────────────────
    auto hero = b.reduced(30.0f, 18.0f);
    auto meterArea = hero.removeFromRight(juce::jlimit(220.0f, 320.0f, W * 0.30f));
    meterArea = meterArea.withTrimmedLeft(20.0f);
    paintMeters(g, meterArea.removeFromTop(juce::jmin(96.0f, meterArea.getHeight())));

    // big status word
    g.setColour(u.colour);
    g.setFont(LAF::display(juce::jlimit(40.0f, 72.0f, H * 0.14f)));
    auto wordR = hero.removeFromTop(hero.getHeight() * 0.52f);
    g.drawText(u.word, wordR, juce::Justification::bottomLeft, false);

    // subtitle
    g.setColour(LAF::textMuted());
    g.setFont(juce::Font(13.5f));
    g.drawText(u.subtitle, hero.removeFromTop(24.0f), juce::Justification::topLeft, false);

    // device badge (only when found)
    if (sm.hasDiscoveredDevice())
    {
        auto badge = juce::Rectangle<float>(hero.getX(), hero.getY() + 8.0f, 220.0f, 26.0f);
        g.setColour(LAF::surface2());
        g.fillRoundedRectangle(badge, 7.0f);
        g.setColour(LAF::borderNorm());
        g.drawRoundedRectangle(badge, 7.0f, 1.0f);
        g.setColour(LAF::textPrim());
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(sm.discoveredIP() + " : " + juce::String(sm.discoveredPort()),
                   badge.reduced(12.0f, 0.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paintLogo(juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = r.getWidth() / 1024.0f;
    const float x0 = r.getX(), y0 = r.getY();

    // No background square — container-less logo (matches the transparent SVG).
    static const float bx[] = { 168, 296, 424, 552, 680, 808 };
    static const float by[] = { 546, 426, 290, 370, 460, 550 };
    static const float bh[] = { 170, 360, 520, 420, 250, 110 };
    static const float op[] = { 0.45f, 0.65f, 1.0f, 0.82f, 0.60f, 0.38f };

    for (int i = 0; i < 6; ++i)
    {
        juce::Rectangle<float> bar(x0 + bx[i] * s, y0 + by[i] * s, 80.0f * s, bh[i] * s);
        g.setGradientFill(LAF::accent(bar, op[i]));
        g.fillRoundedRectangle(bar, 40.0f * s);
    }

    auto dot = juce::Point<float>(x0 + 856.0f * s, y0 + 230.0f * s);
    g.setColour(LAF::gradStart().withAlpha(0.12f));
    g.fillEllipse(juce::Rectangle<float>(220.0f * s, 220.0f * s).withCentre(dot));
    auto dotR = juce::Rectangle<float>(144.0f * s, 144.0f * s).withCentre(dot);
    g.setGradientFill(LAF::accent(dotR));
    g.fillEllipse(dotR);
}

void AudioPluginAudioProcessorEditor::paintWordmark(juce::Graphics& g, float x, float cy)
{
    auto f = LAF::display(20.0f);
    g.setFont(f);
    auto seg = [&](const juce::String& t, juce::Colour c)
    {
        g.setColour(c);
        const float w = (float) g.getCurrentFont().getStringWidth(t);
        g.drawText(t, juce::Rectangle<float>(x, cy - 14.0f, w + 4.0f, 28.0f),
                   juce::Justification::centredLeft, false);
        x += w + 2.0f;
    };
    seg("MIX", LAF::textPrim());
    seg("2",   LAF::gradStart());
    seg("GO",  LAF::textPrim());
}

void AudioPluginAudioProcessorEditor::paintPill(juce::Graphics& g, juce::Rectangle<float> r,
                                                juce::Colour col, const juce::String& label)
{
    g.setColour(LAF::surface2());
    g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
    g.setColour(LAF::borderNorm());
    g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);

    auto dot = juce::Rectangle<float>(8.0f, 8.0f).withCentre(
        { r.getX() + 13.0f, r.getCentreY() });
    g.setColour(col);
    g.fillEllipse(dot);

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(label, r.withTrimmedLeft(24.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paintMeters(juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool streaming = processorRef.getStreamManager().isStreaming();

    g.setColour(LAF::textMuted());
    g.setFont(juce::Font(9.0f));
    g.drawText("OUTPUT LEVEL", area.removeFromTop(14.0f), juce::Justification::topLeft, false);
    area.removeFromTop(8.0f);

    const float rowH = 24.0f;
    paintMeterRow(g, area.removeFromTop(rowH), "L", streaming ? m_dispL : 0.0f);
    area.removeFromTop(6.0f);
    paintMeterRow(g, area.removeFromTop(rowH), "R", streaming ? m_dispR : 0.0f);
}

void AudioPluginAudioProcessorEditor::paintMeterRow(juce::Graphics& g, juce::Rectangle<float> row,
                                                    const juce::String& ch, float level)
{
    level = juce::jlimit(0.0f, 1.0f, level);

    auto labelR = row.removeFromLeft(16.0f);
    g.setColour(LAF::gradStart());
    g.setFont(LAF::display(12.0f));
    g.drawText(ch, labelR, juce::Justification::centredLeft, false);

    // Peak amplitude → dB. Map [-54 dB, 0 dB] to the bar so it reads like a
    // DAW meter (a linear-amplitude bar looks far quieter for the same signal —
    // the audio itself is unity-gain, this is purely the display scale).
    const float db = 20.0f * std::log10(juce::jmax(level, 0.00002f));
    const float norm = juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);

    auto dbR = row.removeFromRight(38.0f);
    g.setColour(LAF::textMuted());
    g.setFont(juce::Font(10.0f));
    g.drawText(db <= -54.0f ? "-inf" : juce::String(juce::roundToInt(db)),
               dbR, juce::Justification::centredRight, false);

    row.removeFromLeft(8.0f);
    row.removeFromRight(8.0f);
    auto track = juce::Rectangle<float>(row.getX(), row.getCentreY() - 5.0f, row.getWidth(), 10.0f);
    g.setColour(LAF::borderDim());
    g.fillRoundedRectangle(track, 5.0f);

    if (norm > 0.005f)
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(track, 5.0f);
        g.reduceClipRegion(clip);
        auto fill = track.withWidth(track.getWidth() * norm);
        juce::ColourGradient grad(juce::Colour(0xff22c55e), track.getTopLeft(),
                                  juce::Colour(0xffe8445a), track.getTopRight(), false);
        grad.addColour(0.6, juce::Colour(0xff84cc16));
        grad.addColour(0.85, juce::Colour(0xfff97316));
        g.setGradientFill(grad);
        g.fillRect(fill);
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paintStatsBar(juce::Graphics& g, juce::Rectangle<float> bar,
                                                    const UiState& u)
{
    g.setColour(LAF::surface1());
    g.fillRect(bar);
    g.setColour(LAF::borderDim());
    g.drawHorizontalLine((int) bar.getY(), 0.0f, bar.getWidth());

    auto& sm = processorRef.getStreamManager();

    if (! u.streaming)
    {
        const int pk = sm.discoveryPacketsReceived();
        juce::String info = pk == 0
            ? "Discovery: listening on UDP " + juce::String(sm.discoveryBoundPort()) + " - no packets yet"
            : "Discovery: " + juce::String(pk) + " pkts received"
              + (sm.hasDiscoveredDevice() ? " - last " + sm.discoveredIP() : juce::String());
        g.setColour(LAF::textMuted());
        g.setFont(juce::Font(12.0f));
        g.drawText(info, bar.reduced(20.0f, 0.0f), juce::Justification::centredLeft, false);
        return;
    }

    const auto kb = sm.getBytesSent() / 1024;
    juce::String sent = kb < 1024 ? juce::String(kb) + " KB"
                                  : juce::String(kb / 1024.0, 1) + " MB";
    const int fifoMs = sm.getFIFOLevelMs();
    const auto under = sm.getFIFOUnderruns();

    struct Item { juce::String label, value; juce::Colour col; };
    const Item items[] = {
        { "PACKETS",   juce::String(sm.getPacketsSent()),         LAF::textPrim() },
        { "SENT",      sent,                                      LAF::textPrim() },
        { "FIFO",      juce::String(fifoMs) + " ms",
          fifoMs < 20 ? LAF::stStreaming() : (fifoMs < 50 ? LAF::stConnecting() : LAF::stError()) },
        { "UNDERRUNS", juce::String(under),
          under == 0 ? LAF::stStreaming() : LAF::stSearching() },
        { "DISCOVERY", juce::String(sm.discoveryPacketsReceived()) + " pkts", LAF::textPrim() },
    };

    const int n = (int) (sizeof(items) / sizeof(items[0]));
    const float colW = bar.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = bar.withX(bar.getX() + colW * i).withWidth(colW).reduced(16.0f, 6.0f);
        if (i > 0)
        {
            g.setColour(LAF::borderDim());
            g.drawVerticalLine((int) (bar.getX() + colW * i), bar.getY() + 8.0f, bar.getBottom() - 8.0f);
        }
        g.setColour(LAF::textMuted());
        g.setFont(juce::Font(9.0f));
        g.drawText(items[i].label, cell.removeFromTop(12.0f), juce::Justification::topLeft, false);
        g.setColour(items[i].col);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(items[i].value, cell, juce::Justification::topLeft, false);
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int topH = juce::jlimit(48, 72, juce::roundToInt(getHeight() * 0.12f));
    const int btnW = juce::jlimit(120, 170, juce::roundToInt(W * 0.14f));
    const int btnH = juce::jlimit(28, 40, juce::roundToInt(topH * 0.6f));
    m_stream_button.setBounds(W - btnW - 20, (topH - btnH) / 2, btnW, btnH);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::onStreamButtonClicked()
{
    auto& sm = processorRef.getStreamManager();
    if (sm.isStreaming())
    {
        sm.setAutoConnect(false);
        sm.stopStreaming();
    }
    else
    {
        sm.setAutoConnect(true);
    }
    syncButton();
    repaint();
}

void AudioPluginAudioProcessorEditor::streamStateChanged(mix2go::streaming::StreamState)
{
    juce::MessageManager::callAsync([this]()
    {
        syncButton();
        repaint();
    });
}

void AudioPluginAudioProcessorEditor::syncButton()
{
    auto& sm = processorRef.getStreamManager();
    const auto u = currentUiState();

    if (sm.isStreaming())
    {
        m_stream_button.setButtonText("Stop");
        m_stream_button.getProperties().set("m2g_stop", true);
        m_stream_button.setEnabled(true);
    }
    else if (u.busy)
    {
        m_stream_button.setButtonText(
            sm.getState() == S::StreamState::Connecting ? "Auto-Connect active" : "Searching...");
        m_stream_button.getProperties().set("m2g_stop", false);
        m_stream_button.setEnabled(false);
    }
    else
    {
        m_stream_button.setButtonText("Enable Auto-Connect");
        m_stream_button.getProperties().set("m2g_stop", false);
        m_stream_button.setEnabled(true);
    }
    m_stream_button.repaint();
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    const float l = processorRef.getMeterL();
    const float r = processorRef.getMeterR();
    // Fast attack, smooth release (message-thread display smoothing).
    m_dispL = l > m_dispL ? l : m_dispL * 0.80f;
    m_dispR = r > m_dispR ? r : m_dispR * 0.80f;
    repaint();
}
