#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioPacket.h"
#include "ThreadSafeFIFO.h"
#include "NetworkSender.h"
#include "OpusEncoder.h"

namespace mix2go {
namespace streaming {

// Status für Streaming
enum class StreamState
{
    Disconnected,
    Connecting,
    Streaming,
    Error
};

// Interface für Listener (GUI updates usw)
class StreamListener
{
public:
    virtual ~StreamListener() = default;
    virtual void streamStateChanged(StreamState newState) = 0;
    virtual void streamStatsUpdated(uint64_t packetsSent, uint64_t bytesSent) {}
};

// Verwaltet das ganze Audio Streaming
// Verbindet FIFO, Network Sender und so weiter
class AudioStreamManager
{
public:
    AudioStreamManager()
    {
        m_sender.setAudioCallback([this](std::vector<uint8_t>& outBytes) {
            return fillPacketFromFIFO(outBytes);
        });
    }
    
    ~AudioStreamManager()
    {
        stopStreaming();
    }
    
    //==========================================================================
    // Config Kram
    //==========================================================================
    
    // Setup machen
    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        m_sampleRate = sampleRate;
        m_samplesPerBlock = samplesPerBlock;
        m_numChannels = numChannels;

        // Prepare Opus encoder: 10ms frames (480 samples @ 48kHz)
        m_opusEncoder.prepare(sampleRate, numChannels);

        // Input samples per Opus frame after resampling from DAW rate → 48kHz
        m_packetSamples = m_opusEncoder.getInputSamplesPerFrame();

        // FIFO: 500ms headroom is more than enough
        int fifoSize = (int)(sampleRate * 0.5);
        m_fifo.prepare(numChannels, fifoSize);

        // Send interval matches exactly one Opus input frame at DAW sample rate
        double exactIntervalMs = (double)m_packetSamples / sampleRate * 1000.0;
        m_sender.setSendInterval(exactIntervalMs);

        DBG("[Mix2Go] Manager Prepared (Opus v2): SR=" << (int)sampleRate
            << " OpusFrameSamples=" << m_opusEncoder.getFrameSize()
            << " InputSamples=" << m_packetSamples
            << " Interval=" << exactIntervalMs << "ms");
    }
    
    // IP setzen
    void setTarget(juce::String ipAddress, int port)
    {
        m_targetIP = ipAddress;
        m_targetPort = port;
        m_sender.setTarget(ipAddress, port);
    }
    
    juce::String getTargetIP() { return m_targetIP; }
    int getTargetPort() { return m_targetPort; }
    
    //==========================================================================
    // Streaming Start/Stop
    //==========================================================================
    
    bool startStreaming()
    {
        if (m_state == StreamState::Streaming)
            return true;

        if (m_targetIP.isEmpty() || m_targetIP == "0.0.0.0")
        {
            DBG("[Mix2Go] startStreaming() aborted: no target IP set");
            setState(StreamState::Error);
            return false;
        }

        setState(StreamState::Connecting);

        m_fifo.reset();
        m_opusEncoder.reset();
        m_sequenceNumber = 0;
        m_networkUnderruns = 0;
        m_streamStartTime = juce::Time::getHighResolutionTicks();

        if (!m_sender.start())
        {
            setState(StreamState::Error);
            return false;
        }

        m_isStreaming = true;
        setState(StreamState::Streaming);

        const int pps = (m_packetSamples > 0)
                          ? (int)juce::roundToInt(m_sampleRate / m_packetSamples)
                          : 0;

        DBG("[Mix2Go] === Streaming Started (Opus v2) ==================");
        DBG("[Mix2Go]   Format:           28-byte v2 header + Opus payload");
        DBG("[Mix2Go]   Target:           " << m_targetIP << ":" << m_targetPort);
        DBG("[Mix2Go]   DAW SampleRate:   " << (int)m_sampleRate << " Hz → resampled to 48000 Hz");
        DBG("[Mix2Go]   Channels:         " << m_numChannels);
        DBG("[Mix2Go]   OpusFrameSize:    " << m_opusEncoder.getFrameSize() << " samples (10ms)");
        DBG("[Mix2Go]   InputSamples:     " << m_packetSamples << " per frame");
        DBG("[Mix2Go]   PacketsPerSec:    ~" << pps);
        DBG("[Mix2Go]   Bitrate (Opus):   128 kbps");
        DBG("[Mix2Go] =====================================================");

        return true;
    }
    
    void stopStreaming()
    {
        m_isStreaming = false;
        m_sender.stop();
        m_fifo.reset();
        setState(StreamState::Disconnected);
        
        DBG("Streaming stopped");
    }
    
    bool isStreaming() const
    {
        return m_isStreaming;
    }
    
    StreamState getState()
    {
        return m_state;
    }
    
    // Status als Text für GUI
    juce::String getStateString()
    {
        if (m_state == StreamState::Disconnected) return "Disconnected";
        if (m_state == StreamState::Connecting) return "Connecting...";
        if (m_state == StreamState::Streaming) return "Streaming";
        if (m_state == StreamState::Error) return "Error";
        return "Unknown";
    }
    
    //==========================================================================
    // Audio Thread
    //==========================================================================
    
    // Hier kommen die Daten vom Audio Thread an
    void pushAudioData(const juce::AudioBuffer<float>& buffer)
    {
        if (!m_isStreaming)
            return;

        // Push immer wenn streaming läuft - kein Silence-Gate.
        // Das FIFO schickt dann auch Stille, damit der Empfänger
        // einen kontinuierlichen Datenstrom bekommt.
        m_fifo.push(buffer);
    }
    
    bool hasAudioSignal()
    {
        return m_silentBlocks < 10;
    }
    
    //==========================================================================
    // Stats
    //==========================================================================
    
    uint64_t getPacketsSent() { return m_sender.getPacketsSent(); }
    uint64_t getBytesSent() { return m_sender.getBytesSent(); }
    uint64_t getFIFOOverruns() { return m_fifo.getOverrunCount(); }
    uint64_t getFIFOUnderruns() { return m_fifo.getUnderrunCount(); }
    int getFIFOLevel() { return m_fifo.getNumReady(); }
    
    //==========================================================================
    // Listener
    //==========================================================================
    
    void addListener(StreamListener* listener)
    {
        juce::ScopedLock lock(m_listenerLock);
        m_listeners.addIfNotAlreadyThere(listener);
    }
    
    void removeListener(StreamListener* listener)
    {
        juce::ScopedLock lock(m_listenerLock);
        m_listeners.removeFirstMatchingValue(listener);
    }
    
private:
    void setState(StreamState newState)
    {
        if (m_state == newState)
            return;
            
        m_state = newState;
        
        // Listener benachrichtigen
        juce::ScopedLock lock(m_listenerLock);
        for (auto* listener : m_listeners)
        {
            if (listener)
                listener->streamStateChanged(newState);
        }
    }
    
    // Called from network thread: pop one Opus frame from FIFO, encode, build v2 packet.
    bool fillPacketFromFIFO(std::vector<uint8_t>& outBytes)
    {
        auto overruns = m_fifo.getOverrunCount();
        if (overruns > m_lastLoggedOverruns)
        {
            DBG("[Mix2Go] FIFO overrun! total=" << (int)overruns
                << " (+" << (int)(overruns - m_lastLoggedOverruns) << " new)"
                << "  fifoLevel=" << m_fifo.getNumReady());
            m_lastLoggedOverruns = overruns;
        }

        // Pop one Opus input frame from FIFO (or use silence on underrun)
        juce::AudioBuffer<float> tempBuffer(m_numChannels, m_packetSamples);
        if (!m_fifo.pop(tempBuffer, m_packetSamples))
        {
            ++m_networkUnderruns;
            if (m_networkUnderruns == 1 || (m_networkUnderruns % 100) == 0)
                DBG("[Mix2Go] FIFO underrun: ready=" << m_fifo.getNumReady()
                    << " needed=" << m_packetSamples
                    << " total=" << (int)m_networkUnderruns);
            tempBuffer.clear(); // encode silence → keeps seq numbers continuous
        }

        bool packetReady = false;

        const float* ch[2] = {
            tempBuffer.getReadPointer(0),
            m_numChannels > 1 ? tempBuffer.getReadPointer(1) : tempBuffer.getReadPointer(0)
        };

        const double ticksPerUs = juce::Time::getHighResolutionTicksPerSecond() / 1000000.0;
        const uint64_t ts = (uint64_t)((juce::Time::getHighResolutionTicks() - m_streamStartTime) / ticksPerUs);
        const uint32_t seq = m_sequenceNumber++;

        m_opusEncoder.pushSamples(ch, m_numChannels, m_packetSamples,
            [&](const uint8_t* opusData, int opusBytes)
            {
                outBytes = buildV2Packet(seq, ts, opusData, opusBytes);
                packetReady = true;
            });

        if ((seq % 100) == 0)
        {
            DBG("[Mix2Go] Stats: seq=" << (int)seq
                << "  sent=" << (int)m_sender.getPacketsSent()
                << "  fifoLevel=" << m_fifo.getNumReady()
                << "  overruns=" << (int)m_fifo.getOverrunCount()
                << "  netUnderruns=" << (int)m_networkUnderruns
                << "  KB=" << (int)(m_sender.getBytesSent() / 1024));
        }

        return packetReady;
    }

    // Serialise one Opus frame as a Mix2Go v2 UDP packet (28-byte header + payload).
    std::vector<uint8_t> buildV2Packet(uint32_t seq, uint64_t timestamp,
                                        const uint8_t* opusData, int opusBytes)
    {
        const uint16_t payloadLen   = static_cast<uint16_t>(opusBytes);
        const uint32_t magic        = 0x4D324731u; // "M2G1"
        const uint8_t  frameType    = 0;            // Opus
        const uint8_t  numCh        = static_cast<uint8_t>(m_numChannels);
        const uint32_t sr           = 48000u;
        const uint32_t frameSamples = static_cast<uint32_t>(m_opusEncoder.getFrameSize());

        std::vector<uint8_t> buf(28 + opusBytes);
        size_t off = 0;

        auto w8  = [&](uint8_t  v) { buf[off++] = v; };
        auto w16 = [&](uint16_t v) { std::memcpy(buf.data()+off, &v, 2); off+=2; };
        auto w32 = [&](uint32_t v) { std::memcpy(buf.data()+off, &v, 4); off+=4; };
        auto w64 = [&](uint64_t v) { std::memcpy(buf.data()+off, &v, 8); off+=8; };

        w32(magic);       // 0
        w8(frameType);    // 4
        w8(numCh);        // 5
        w16(payloadLen);  // 6
        w32(seq);         // 8
        w64(timestamp);   // 12  (8 bytes)
        w32(sr);          // 20
        w32(frameSamples);// 24
        std::memcpy(buf.data()+28, opusData, opusBytes); // 28

        return buf;
    }
    
    double m_sampleRate = 44100.0;
    int m_samplesPerBlock = 512;
    int m_numChannels = 2;
    int m_packetSamples = 441;

    juce::String m_targetIP = "127.0.0.1";
    int m_targetPort = 12345;

    ThreadSafeFIFO m_fifo;
    NetworkSender m_sender;
    MixOpusEncoder m_opusEncoder { 480 }; // 10ms frames @ 48kHz
    
    StreamState m_state = StreamState::Disconnected;
    std::atomic<bool> m_isStreaming { false };
    std::atomic<int> m_silentBlocks { 0 };
    uint32_t m_sequenceNumber = 0;
    juce::int64 m_streamStartTime = 0;
    uint64_t m_lastLoggedOverruns = 0;
    
    uint64_t m_networkUnderruns = 0;

    juce::CriticalSection m_listenerLock;
    juce::Array<StreamListener*> m_listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStreamManager)
};

} // namespace streaming
} // namespace mix2go
