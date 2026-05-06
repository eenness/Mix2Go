#pragma once

#include <juce_core/juce_core.h>
#include "AudioPacket.h"

#if JUCE_WINDOWS
 // Forward-declare winmm timer functions to avoid Windows header conflicts
 extern "C" {
     __declspec(dllimport) unsigned long __stdcall timeBeginPeriod(unsigned int uPeriod);
     __declspec(dllimport) unsigned long __stdcall timeEndPeriod(unsigned int uPeriod);
 }
 #pragma comment(lib, "winmm.lib")
#endif

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
    
    // Interval ändern (double für sample-genaue Berechnung, z.B. 4.9887ms)
    void setSendInterval(double intervalMs)
    {
        m_sendIntervalMs = intervalMs;
    }
    
    bool start()
    {
        if (isThreadRunning())
            return true;

        m_shouldStop = false;
        m_lastLogTime     = juce::Time::getHighResolutionTicks();
        m_lastLogPackets  = 0;
        m_lastLogBytes    = 0;

        // Socket bauen
        m_socket = std::make_unique<juce::DatagramSocket>();

        if (!m_socket->bindToPort(0)) // random port ist ok
        {
            DBG("[Mix2Go] Fehler: Socket Bind geht nicht");
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

        // Windows-Timer auf 1ms stellen damit sleep() genau ist
        #if JUCE_WINDOWS
        timeBeginPeriod(1);
        #endif

        const double ticksPerSec = (double)juce::Time::getHighResolutionTicksPerSecond();
        const double ticksPerMs  = ticksPerSec / 1000.0;

        // Absolutes Scheduling: nächsten Send-Zeitpunkt vorausberechnen
        // Fehler akkumulieren sich NICHT über Zeit → kein systematischer Drift
        const juce::int64 intervalTicks = (juce::int64)(m_sendIntervalMs * ticksPerMs);
        juce::int64 nextSendTick = juce::Time::getHighResolutionTicks() + intervalTicks;

        while (!threadShouldExit() && !m_shouldStop)
        {
            AudioPacket packet;

            if (m_audioCallback && m_audioCallback(packet))
            {
                auto data = packet.serialize();

                int bytesSent = m_socket->write(
                    targetIP, targetPort,
                    data.data(), (int)data.size()
                );

                if (bytesSent > 0)
                {
                    m_packetsSent++;
                    m_bytesSent += bytesSent;

                    if ((m_packetsSent % 100) == 0)
                    {
                        auto now         = juce::Time::getHighResolutionTicks();
                        double elapsedMs = (double)(now - m_lastLogTime) / ticksPerMs;
                        int pps  = (elapsedMs > 0) ? (int)((m_packetsSent - m_lastLogPackets) * 1000.0 / elapsedMs) : 0;
                        int kbps = (elapsedMs > 0) ? (int)((m_bytesSent   - m_lastLogBytes)   * 8.0    / elapsedMs) : 0;

                        DBG("[Mix2Go] UDP: " << (int)m_packetsSent << " pkts"
                            << "  " << pps  << " pkt/s"
                            << "  " << kbps << " kbps"
                            << "  pktSize=" << bytesSent << " bytes"
                            << "  total=" << (int)(m_bytesSent / 1024) << " KB"
                            << "  -> " << targetIP << ":" << targetPort);

                        m_lastLogTime    = now;
                        m_lastLogPackets = m_packetsSent;
                        m_lastLogBytes   = m_bytesSent;
                    }
                }
                else
                {
                    DBG("[Mix2Go] UDP write() failed (bytesSent=" << bytesSent
                        << ") -> " << targetIP << ":" << targetPort);
                }
            }

            // Absolutes Timing: schlafen bis zum nächsten geplanten Zeitpunkt
            auto now      = juce::Time::getHighResolutionTicks();
            auto remaining = nextSendTick - now;
            if (remaining > 0)
            {
                int sleepMs = (int)(remaining / ticksPerMs);
                if (sleepMs > 0)
                    juce::Thread::sleep(sleepMs);
            }
            else if (remaining < -intervalTicks * 3)
            {
                // Mehr als 3 Intervalle hinter Zeitplan: Uhr zurücksetzen
                // (passiert z.B. beim Start oder nach System-Sleep)
                nextSendTick = now;
            }
            nextSendTick += intervalTicks;
        }

        #if JUCE_WINDOWS
        timeEndPeriod(1);
        #endif

        DBG("Sender gestoppt");
    }
    
    std::unique_ptr<juce::DatagramSocket> m_socket;
    AudioDataCallback m_audioCallback;
    
    juce::CriticalSection m_settingsLock;
    juce::String m_targetIP = "127.0.0.1";
    int m_targetPort = 12345;
    
    // simple state variables
    bool m_shouldStop = false;
    double m_sendIntervalMs = 10.0;
    
    // stats
    std::atomic<uint64_t> m_packetsSent { 0 };
    std::atomic<uint64_t> m_bytesSent   { 0 };

    // rate tracking for periodic log
    juce::int64  m_lastLogTime    = 0;
    uint64_t     m_lastLogPackets = 0;
    uint64_t     m_lastLogBytes   = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkSender)
};

} // namespace streaming
} // namespace mix2go
