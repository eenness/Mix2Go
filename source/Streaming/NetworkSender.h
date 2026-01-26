#pragma once

#include <juce_core/juce_core.h>
#include "AudioPacket.h"

namespace mix2go {
namespace streaming {

/**
 * Network sender for UDP audio streaming.
 * 
 * Runs in a separate thread, reads from a callback and sends
 * packets to the configured target address.
 */
class NetworkSender : public juce::Thread
{
public:
    /** Callback to get audio data for sending */
    using AudioDataCallback = std::function<bool(AudioPacket&)>;
    
    NetworkSender()
        : juce::Thread("Mix2Go Network Sender")
    {
    }
    
    ~NetworkSender() override
    {
        stop();
    }
    
    /** Configure target address and port */
    void setTarget(const juce::String& ipAddress, int port)
    {
        juce::ScopedLock lock(m_settingsLock);
        m_targetIP = ipAddress;
        m_targetPort = port;
    }
    
    /** Set the callback that provides audio packets */
    void setAudioCallback(AudioDataCallback callback)
    {
        m_audioCallback = std::move(callback);
    }
    
    /** Set how often to send packets (in milliseconds) */
    void setSendInterval(int intervalMs)
    {
        m_sendIntervalMs.store(intervalMs, std::memory_order_relaxed);
    }
    
    /** Start the sender thread */
    bool start()
    {
        if (isThreadRunning())
            return true;
            
        m_shouldStop.store(false);
        
        // Create UDP socket
        m_socket = std::make_unique<juce::DatagramSocket>();
        
        if (!m_socket->bindToPort(0)) // Bind to any available port
        {
            DBG("NetworkSender: Failed to bind socket");
            m_lastError = "Failed to bind socket";
            return false;
        }
        
        startThread(juce::Thread::Priority::normal);
        return true;
    }
    
    /** Stop the sender thread */
    void stop()
    {
        m_shouldStop.store(true);
        
        if (isThreadRunning())
        {
            stopThread(2000); // Wait up to 2 seconds
        }
        
        m_socket.reset();
    }
    
    /** Check if currently connected/sending */
    [[nodiscard]] bool isActive() const noexcept
    {
        return isThreadRunning() && !m_shouldStop.load();
    }
    
    /** Get last error message */
    [[nodiscard]] juce::String getLastError() const
    {
        juce::ScopedLock lock(m_settingsLock);
        return m_lastError;
    }
    
    /** Get total packets sent */
    [[nodiscard]] uint64_t getPacketsSent() const noexcept
    {
        return m_packetsSent.load(std::memory_order_relaxed);
    }
    
    /** Get total bytes sent */
    [[nodiscard]] uint64_t getBytesSent() const noexcept
    {
        return m_bytesSent.load(std::memory_order_relaxed);
    }
    
private:
    void run() override
    {
        juce::String targetIP;
        int targetPort;
        
        {
            juce::ScopedLock lock(m_settingsLock);
            targetIP = m_targetIP;
            targetPort = m_targetPort;
        }
        
        DBG("NetworkSender: Starting to send to " << targetIP << ":" << targetPort);
        
        while (!threadShouldExit() && !m_shouldStop.load())
        {
            AudioPacket packet;
            
            // Get audio data from callback
            if (m_audioCallback && m_audioCallback(packet))
            {
                // Serialize and send
                auto data = packet.serialize();
                
                const int bytesSent = m_socket->write(
                    targetIP, targetPort,
                    data.data(), static_cast<int>(data.size())
                );
                
                if (bytesSent > 0)
                {
                    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
                    m_bytesSent.fetch_add(static_cast<uint64_t>(bytesSent), 
                                          std::memory_order_relaxed);
                }
                else
                {
                    juce::ScopedLock lock(m_settingsLock);
                    m_lastError = "Send failed";
                }
            }
            
            // Sleep to control send rate
            const int sleepMs = m_sendIntervalMs.load(std::memory_order_relaxed);
            if (sleepMs > 0)
            {
                juce::Thread::sleep(sleepMs);
            }
        }
        
        DBG("NetworkSender: Stopped");
    }
    
    std::unique_ptr<juce::DatagramSocket> m_socket;
    AudioDataCallback m_audioCallback;
    
    mutable juce::CriticalSection m_settingsLock;
    juce::String m_targetIP { "127.0.0.1" };
    int m_targetPort { 12345 };
    juce::String m_lastError;
    
    std::atomic<bool> m_shouldStop { false };
    std::atomic<int> m_sendIntervalMs { 10 }; // ~100 packets/sec default
    std::atomic<uint64_t> m_packetsSent { 0 };
    std::atomic<uint64_t> m_bytesSent { 0 };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkSender)
};

} // namespace streaming
} // namespace mix2go
