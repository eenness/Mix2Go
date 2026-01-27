#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioPacket.h"
#include "ThreadSafeFIFO.h"
#include "NetworkSender.h"

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
        // Callback setzen
        m_sender.setAudioCallback([this](AudioPacket& packet) {
            return fillPacketFromFIFO(packet);
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
        
        // FIFO Größe für ca 1 Sekunde Puffer
        int fifoSize = (int)sampleRate * 2;
        m_fifo.prepare(numChannels, fifoSize);
        
        // Packet Größe: ca 10ms Audio pro Paket senden
        m_packetSamples = (int)(sampleRate * 0.01);
        
        DBG("Manager Prepared: SR=" << sampleRate 
            << " PacketSamples=" << m_packetSamples);
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
            
        setState(StreamState::Connecting);
        
        m_fifo.reset();
        m_sequenceNumber = 0;
        m_streamStartTime = juce::Time::getHighResolutionTicks();
        
        if (!m_sender.start())
        {
            setState(StreamState::Error);
            return false;
        }
        
        m_isStreaming = true;
        setState(StreamState::Streaming);
        
        DBG("Streaming started!");
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
    
    bool isStreaming()
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
        
        // Check ob Stille ist (Magnitude)
        float maxLevel = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float chLevel = buffer.getMagnitude(ch, 0, buffer.getNumSamples());
            if (chLevel > maxLevel) maxLevel = chLevel;
        }
        
        // Wenn zu leise, dann nix senden
        if (maxLevel < 0.001f) // -60dB ca
        {
            m_silentBlocks++;
            return; 
        }
        
        m_silentBlocks = 0;
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
    
    // Wird vom Network Thread aufgerufen
    bool fillPacketFromFIFO(AudioPacket& packet)
    {
        if (m_fifo.getNumReady() < m_packetSamples)
            return false;
            
        // Temp Buffer
        juce::AudioBuffer<float> tempBuffer(m_numChannels, m_packetSamples);
        
        if (!m_fifo.pop(tempBuffer, m_packetSamples))
            return false;
        
        // Paket füllen
        packet.setFromBuffer(tempBuffer.getArrayOfReadPointers(), 
                            m_numChannels, m_packetSamples,
                            (uint32_t)m_sampleRate);
        
        // Zeitstempel berechnen
        double ticksPerMicrosecond = juce::Time::getHighResolutionTicksPerSecond() / 1000000.0;
        auto ticksSinceStart = juce::Time::getHighResolutionTicks() - m_streamStartTime;
        
        packet.timestamp = (uint64_t)(ticksSinceStart / ticksPerMicrosecond);
        packet.sequenceNumber = m_sequenceNumber++;
        
        return true;
    }
    
    double m_sampleRate = 44100.0;
    int m_samplesPerBlock = 512;
    int m_numChannels = 2;
    int m_packetSamples = 441;
    
    juce::String m_targetIP = "127.0.0.1";
    int m_targetPort = 12345;
    
    ThreadSafeFIFO m_fifo;
    NetworkSender m_sender;
    
    StreamState m_state = StreamState::Disconnected;
    std::atomic<bool> m_isStreaming { false };
    std::atomic<int> m_silentBlocks { 0 };
    uint32_t m_sequenceNumber = 0;
    juce::int64 m_streamStartTime = 0;
    
    juce::CriticalSection m_listenerLock;
    juce::Array<StreamListener*> m_listeners;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStreamManager)
};

} // namespace streaming
} // namespace mix2go
