#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioPacket.h"
#include "ThreadSafeFIFO.h"
#include "NetworkSender.h"

namespace mix2go {
namespace streaming {

/**
 * Connection state for the audio stream
 */
enum class StreamState
{
    Disconnected,
    Connecting,
    Streaming,
    Error
};

/**
 * Listener interface for stream state changes
 */
class StreamListener
{
public:
    virtual ~StreamListener() = default;
    virtual void streamStateChanged(StreamState newState) = 0;
    virtual void streamStatsUpdated(uint64_t packetsSent, uint64_t bytesSent) {}
};

/**
 * Central manager for audio streaming.
 * 
 * Coordinates:
 * - Audio FIFO buffer
 * - Network sender thread
 * - State management
 * - Statistics
 */
class AudioStreamManager
{
public:
    AudioStreamManager()
    {
        // Configure network sender callback
        m_sender.setAudioCallback([this](AudioPacket& packet) {
            return fillPacketFromFIFO(packet);
        });
    }
    
    ~AudioStreamManager()
    {
        stopStreaming();
    }
    
    //==========================================================================
    // Configuration
    //==========================================================================
    
    /** Prepare the stream manager with audio settings */
    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        m_sampleRate = sampleRate;
        m_samplesPerBlock = samplesPerBlock;
        m_numChannels = numChannels;
        
        // Size FIFO for ~1 second of audio
        const int fifoSize = static_cast<int>(sampleRate) * 2;
        m_fifo.prepare(numChannels, fifoSize);
        
        // Config packet size: send ~10ms of audio per packet
        m_packetSamples = static_cast<int>(sampleRate * 0.01);
        
        DBG("AudioStreamManager: Prepared - SR=" << sampleRate 
            << " block=" << samplesPerBlock 
            << " ch=" << numChannels
            << " packetSamples=" << m_packetSamples);
    }
    
    /** Set target IP and port */
    void setTarget(const juce::String& ipAddress, int port)
    {
        m_targetIP = ipAddress;
        m_targetPort = port;
        m_sender.setTarget(ipAddress, port);
    }
    
    /** Get current target IP */
    [[nodiscard]] juce::String getTargetIP() const { return m_targetIP; }
    
    /** Get current target port */
    [[nodiscard]] int getTargetPort() const { return m_targetPort; }
    
    //==========================================================================
    // Streaming Control
    //==========================================================================
    
    /** Start streaming audio */
    bool startStreaming()
    {
        if (m_state == StreamState::Streaming)
            return true;
            
        setState(StreamState::Connecting);
        
        m_fifo.reset();
        m_sequenceNumber = 0;
        m_streamStartTime = juce::Time::getHighResolutionTicks();
        
        if (!m_sender.start())
        {
            setState(StreamState::Error);
            return false;
        }
        
        m_isStreaming.store(true);
        setState(StreamState::Streaming);
        
        DBG("AudioStreamManager: Started streaming to " << m_targetIP << ":" << m_targetPort);
        return true;
    }
    
    /** Stop streaming */
    void stopStreaming()
    {
        m_isStreaming.store(false);
        m_sender.stop();
        m_fifo.reset();
        setState(StreamState::Disconnected);
        
        DBG("AudioStreamManager: Stopped streaming");
    }
    
    /** Check if currently streaming */
    [[nodiscard]] bool isStreaming() const noexcept
    {
        return m_isStreaming.load(std::memory_order_relaxed);
    }
    
    /** Get current state */
    [[nodiscard]] StreamState getState() const noexcept
    {
        return m_state;
    }
    
    /** Get state as string */
    [[nodiscard]] juce::String getStateString() const
    {
        switch (m_state)
        {
            case StreamState::Disconnected: return "Disconnected";
            case StreamState::Connecting:   return "Connecting...";
            case StreamState::Streaming:    return "Streaming";
            case StreamState::Error:        return "Error";
        }
        return "Unknown";
    }
    
    //==========================================================================
    // Audio Thread Interface
    //==========================================================================
    
    /** Push audio data from processBlock (called from audio thread) 
     *  Only pushes if audio level exceeds silence threshold */
    void pushAudioData(const juce::AudioBuffer<float>& buffer)
    {
        if (!m_isStreaming.load(std::memory_order_relaxed))
            return;
        
        // Silence detection: check if buffer has meaningful audio
        float maxLevel = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float chLevel = buffer.getMagnitude(ch, 0, buffer.getNumSamples());
            maxLevel = std::max(maxLevel, chLevel);
        }
        
        // Only stream if level exceeds threshold (-60dB ≈ 0.001)
        constexpr float silenceThreshold = 0.001f;
        if (maxLevel < silenceThreshold)
        {
            m_silentBlocks.fetch_add(1, std::memory_order_relaxed);
            return; // Skip silent audio
        }
        
        m_silentBlocks.store(0, std::memory_order_relaxed);
        m_fifo.push(buffer);
    }
    
    /** Check if currently receiving audio (not silent) */
    [[nodiscard]] bool hasAudioSignal() const noexcept
    {
        return m_silentBlocks.load(std::memory_order_relaxed) < 10;
    }
    
    //==========================================================================
    // Statistics
    //==========================================================================
    
    [[nodiscard]] uint64_t getPacketsSent() const { return m_sender.getPacketsSent(); }
    [[nodiscard]] uint64_t getBytesSent() const { return m_sender.getBytesSent(); }
    [[nodiscard]] uint64_t getFIFOOverruns() const { return m_fifo.getOverrunCount(); }
    [[nodiscard]] uint64_t getFIFOUnderruns() const { return m_fifo.getUnderrunCount(); }
    [[nodiscard]] int getFIFOLevel() const { return m_fifo.getNumReady(); }
    
    //==========================================================================
    // Listener Management
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
        
        juce::ScopedLock lock(m_listenerLock);
        for (auto* listener : m_listeners)
        {
            if (listener)
                listener->streamStateChanged(newState);
        }
    }
    
    /** Fill an AudioPacket from the FIFO (called from network thread) */
    bool fillPacketFromFIFO(AudioPacket& packet)
    {
        if (m_fifo.getNumReady() < m_packetSamples)
            return false;
            
        // Create temp buffer and read from FIFO
        juce::AudioBuffer<float> tempBuffer(m_numChannels, m_packetSamples);
        
        if (!m_fifo.pop(tempBuffer, m_packetSamples))
            return false;
        
        // Fill packet
        packet.setFromBuffer(tempBuffer.getArrayOfReadPointers(), 
                            m_numChannels, m_packetSamples,
                            static_cast<uint32_t>(m_sampleRate));
        
        // Set timestamp (microseconds since stream start)
        const double ticksPerMicrosecond = 
            juce::Time::getHighResolutionTicksPerSecond() / 1000000.0;
        const auto ticksSinceStart = 
            juce::Time::getHighResolutionTicks() - m_streamStartTime;
        packet.timestamp = static_cast<uint64_t>(ticksSinceStart / ticksPerMicrosecond);
        
        // Set sequence number
        packet.sequenceNumber = m_sequenceNumber++;
        
        return true;
    }
    
    // Audio settings
    double m_sampleRate { 44100.0 };
    int m_samplesPerBlock { 512 };
    int m_numChannels { 2 };
    int m_packetSamples { 441 }; // ~10ms at 44.1kHz
    
    // Network settings
    juce::String m_targetIP { "127.0.0.1" };
    int m_targetPort { 12345 };
    
    // Components
    ThreadSafeFIFO m_fifo;
    NetworkSender m_sender;
    
    // State
    StreamState m_state { StreamState::Disconnected };
    std::atomic<bool> m_isStreaming { false };
    std::atomic<int> m_silentBlocks { 0 };  // Count of consecutive silent blocks
    uint32_t m_sequenceNumber { 0 };
    juce::int64 m_streamStartTime { 0 };
    
    // Listeners
    juce::CriticalSection m_listenerLock;
    juce::Array<StreamListener*> m_listeners;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStreamManager)
};

} // namespace streaming
} // namespace mix2go
