#pragma once

#include <juce_core/juce_core.h>
#include "AudioPacket.h"

#if JUCE_WINDOWS
 extern "C" {
     __declspec(dllimport) unsigned long __stdcall timeBeginPeriod(unsigned int uPeriod);
     __declspec(dllimport) unsigned long __stdcall timeEndPeriod(unsigned int uPeriod);
 }
 #pragma comment(lib, "winmm.lib")
#endif

namespace mix2go {
namespace streaming {

class NetworkSender : public juce::Thread
{
public:
    using AudioDataCallback = std::function<bool(std::vector<uint8_t>&)>;

    NetworkSender() : juce::Thread("Mix2Go Network Sender") {}
    ~NetworkSender() override { stop(); }

    void setTarget(juce::String ipAddress, int port)
    {
        juce::ScopedLock lock(m_settingsLock);
        m_targetIP   = ipAddress;
        m_targetPort = port;
    }

    void setAudioCallback(AudioDataCallback callback) { m_audioCallback = callback; }

    void setSendInterval(double intervalMs) { m_sendIntervalMs = intervalMs; }

    bool start()
    {
        if (isThreadRunning()) return true;

        m_shouldStop     = false;
        m_lastLogTime    = juce::Time::getHighResolutionTicks();
        m_lastLogPackets = 0;
        m_lastLogBytes   = 0;

        m_socket = std::make_unique<juce::DatagramSocket>();
        if (!m_socket->bindToPort(0))
        {
            DBG("[Mix2Go] Socket bind failed");
            return false;
        }
        startThread();
        return true;
    }

    void stop()
    {
        m_shouldStop = true;
        if (isThreadRunning()) stopThread(2000);
        m_socket.reset();
    }

    bool     isActive()        { return isThreadRunning() && !m_shouldStop; }
    uint64_t getPacketsSent()  { return m_packetsSent; }
    uint64_t getBytesSent()    { return m_bytesSent; }

private:
    void run() override
    {
        juce::String targetIP;
        int          targetPort;
        {
            juce::ScopedLock lock(m_settingsLock);
            targetIP   = m_targetIP;
            targetPort = m_targetPort;
        }

        DBG("[Mix2Go] Sender started → " << targetIP << ":" << targetPort);

        #if JUCE_WINDOWS
        timeBeginPeriod(1);
        #endif

        // ── Startup delay ────────────────────────────────────────────────────
        // Give the audio thread 50 ms to push real audio into the FIFO before
        // the network thread starts consuming.
        //
        // Without this delay the FIFO starts empty.  At 44100 Hz / 512-sample
        // blocks the audio callback fires every ~11.6 ms, but the network ticks
        // every 10 ms.  "Double-pop" events (two network ticks within one audio
        // callback period) occur roughly every 7 callbacks (~84 ms).  If the
        // FIFO is nearly empty when a double-pop event arrives, the second pop
        // finds < 441 samples → silence packet sent → iOS plays silence.
        //
        // 50 ms of audio-only fill gives the FIFO ~2200 samples of headroom.
        // Simulation shows 0 underruns vs 2+ without the delay even with 3 ms
        // of audio-callback jitter.  The delay is outside the absolute-timing
        // loop so it does NOT accumulate into a timing drift.
        juce::Thread::sleep(50);
        if (threadShouldExit() || m_shouldStop)
        {
            #if JUCE_WINDOWS
            timeEndPeriod(1);
            #endif
            return;
        }

        const double  ticksPerMs    = (double)juce::Time::getHighResolutionTicksPerSecond() / 1000.0;
        const juce::int64 intervalTicks = (juce::int64)(m_sendIntervalMs * ticksPerMs);
        juce::int64   nextSendTick  = juce::Time::getHighResolutionTicks() + intervalTicks;

        std::vector<uint8_t> data;
        data.reserve(28 + 1500);

        while (!threadShouldExit() && !m_shouldStop)
        {
            data.clear();

            if (m_audioCallback && m_audioCallback(data) && !data.empty())
            {
                const int bytesSent = m_socket->write(
                    targetIP, targetPort, data.data(), (int)data.size());

                if (bytesSent > 0)
                {
                    ++m_packetsSent;
                    m_bytesSent += (uint64_t)bytesSent;

                    if ((m_packetsSent % 100) == 0)
                    {
                        const auto   now     = juce::Time::getHighResolutionTicks();
                        const double elapsed = (double)(now - m_lastLogTime) / ticksPerMs;
                        const int pps  = elapsed > 0 ? (int)((m_packetsSent - m_lastLogPackets) * 1000.0 / elapsed) : 0;
                        const int kbps = elapsed > 0 ? (int)((m_bytesSent   - m_lastLogBytes)   * 8.0    / elapsed) : 0;
                        DBG("[Mix2Go] UDP: " << (int)m_packetsSent << " pkts  "
                            << pps << " pps  " << kbps << " kbps  "
                            << bytesSent << " B  total=" << (int)(m_bytesSent/1024) << " KB"
                            << "  → " << targetIP << ":" << targetPort);
                        m_lastLogTime    = now;
                        m_lastLogPackets = m_packetsSent;
                        m_lastLogBytes   = m_bytesSent;
                    }
                }
                else
                {
                    DBG("[Mix2Go] UDP write() failed → " << targetIP << ":" << targetPort);
                }
            }

            // Absolute timing: sleep until the next scheduled tick.
            // Errors do NOT accumulate — systematic drift is impossible.
            const auto now       = juce::Time::getHighResolutionTicks();
            const auto remaining = nextSendTick - now;
            if (remaining > 0)
            {
                const int sleepMs = (int)(remaining / ticksPerMs);
                if (sleepMs > 0) juce::Thread::sleep(sleepMs);
            }
            else if (remaining < -intervalTicks * 3)
            {
                // > 3 intervals behind (e.g. after system sleep): reset clock.
                nextSendTick = now;
            }
            nextSendTick += intervalTicks;
        }

        #if JUCE_WINDOWS
        timeEndPeriod(1);
        #endif

        DBG("[Mix2Go] Sender stopped");
    }

    std::unique_ptr<juce::DatagramSocket> m_socket;
    AudioDataCallback m_audioCallback;

    juce::CriticalSection m_settingsLock;
    juce::String m_targetIP   = "127.0.0.1";
    int          m_targetPort = 12345;
    bool         m_shouldStop = false;
    double       m_sendIntervalMs = 10.0;

    std::atomic<uint64_t> m_packetsSent { 0 };
    std::atomic<uint64_t> m_bytesSent   { 0 };

    juce::int64  m_lastLogTime    = 0;
    uint64_t     m_lastLogPackets = 0;
    uint64_t     m_lastLogBytes   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkSender)
};

} // namespace streaming
} // namespace mix2go
