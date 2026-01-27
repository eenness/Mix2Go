#pragma once

#include <juce_core/juce_core.h>
#include "AudioPacket.h"

namespace mix2go {
namespace streaming {

// Thread der das Netzwerk zeug macht
// Sendet UDP Pakete im Hintergrund
class NetworkSender : public juce::Thread
{
public:
    using AudioDataCallback = std::function<bool(AudioPacket&)>;
    
    NetworkSender()
        : juce::Thread("Mix2Go Network Sender")
    {
    }
    
    ~NetworkSender() override
    {
        stop();
    }
    
    // Zieladresse setzen
    void setTarget(juce::String ipAddress, int port)
    {
        juce::ScopedLock lock(m_settingsLock);
        m_targetIP = ipAddress;
        m_targetPort = port;
    }
    
    // Callback speichern
    void setAudioCallback(AudioDataCallback callback)
    {
        m_audioCallback = callback;
    }
    
    // Interval ändern
    void setSendInterval(int intervalMs)
    {
        m_sendIntervalMs = intervalMs;
    }
    
    bool start()
    {
        if (isThreadRunning())
            return true;
            
        m_shouldStop = false;
        
        // Socket bauen
        m_socket = std::make_unique<juce::DatagramSocket>();
        
        if (!m_socket->bindToPort(0)) // random port ist ok
        {
            DBG("Fehler: Socket Bind geht nicht");
            return false;
        }
        
        startThread();
        return true;
    }
    
    void stop()
    {
        m_shouldStop = true;
        
        if (isThreadRunning())
        {
            stopThread(2000);
        }
        
        m_socket.reset();
    }
    
    bool isActive()
    {
        return isThreadRunning() && !m_shouldStop;
    }
    
    uint64_t getPacketsSent()
    {
        return m_packetsSent;
    }
    
    uint64_t getBytesSent()
    {
        return m_bytesSent;
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
        
        DBG("Sender läuft. Ziel: " << targetIP << ":" << targetPort);
        
        while (!threadShouldExit() && !m_shouldStop)
        {
            AudioPacket packet;
            
            // Daten holen
            if (m_audioCallback && m_audioCallback(packet))
            {
                // Packet fertig machen und senden
                auto data = packet.serialize();
                
                int bytesSent = m_socket->write(
                    targetIP, targetPort,
                    data.data(), (int)data.size()
                );
                
                if (bytesSent > 0)
                {
                    m_packetsSent++;
                    m_bytesSent += bytesSent;
                }
                else
                {
                    DBG("Send failed");
                }
            }
            
            // Kurz warten damit wir nicht 100% CPU brauchen
            int sleepMs = m_sendIntervalMs;
            if (sleepMs > 0)
            {
                juce::Thread::sleep(sleepMs);
            }
        }
        
        DBG("Sender gestoppt");
    }
    
    std::unique_ptr<juce::DatagramSocket> m_socket;
    AudioDataCallback m_audioCallback;
    
    juce::CriticalSection m_settingsLock;
    juce::String m_targetIP = "127.0.0.1";
    int m_targetPort = 12345;
    
    // simple state variables
    bool m_shouldStop = false;
    int m_sendIntervalMs = 10;
    
    // stats
    std::atomic<uint64_t> m_packetsSent { 0 };
    std::atomic<uint64_t> m_bytesSent { 0 };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkSender)
};

} // namespace streaming
} // namespace mix2go
