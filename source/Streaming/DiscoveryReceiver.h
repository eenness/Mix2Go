#pragma once

#include <juce_core/juce_core.h>

namespace mix2go::streaming {

class AudioStreamManager; // forward declaration

// ────────────────────────────────────────────────────────────────────────────
// DiscoveryReceiver
//
// Listens for Mix2Go app broadcast announcements on UDP port [kDiscoveryPort].
// When an announcement arrives the AudioStreamManager is notified on the
// JUCE message thread so it can update its streaming target.
//
// Discovery packet format (ASCII, received over UDP):
//   "MIX2GO:2:<audioPort>"    e.g.  "MIX2GO:2:43210"
//
// The sender's IP address is taken directly from the UDP datagram source.
// This removes all manual IP/port configuration from both the app and the VST.
//
// Heartbeat: the app broadcasts every 1 s.  If no packet arrives within
// [kHeartbeatTimeoutMs] ms, onDeviceLost() is called so the VST can stop
// streaming to a device that is no longer listening.
// ────────────────────────────────────────────────────────────────────────────

class DiscoveryReceiver : private juce::Thread
{
public:
    /// UDP port the VST listens on.  Must match DiscoveryAnnouncer.kDiscoveryPort
    /// in the Flutter app.
    static constexpr int kDiscoveryPort      = 40051;

    /// Milliseconds without a heartbeat before we consider the app gone.
    static constexpr int kHeartbeatTimeoutMs = 3000;

    // ── Callbacks set by AudioStreamManager ─────────────────────────────────

    /// Called on the JUCE message thread when a new device is discovered or
    /// when the discovered device IP / port changes.
    std::function<void(juce::String ip, int audioPort)> onDiscovered;

    /// Called on the JUCE message thread when the heartbeat times out.
    std::function<void()> onLost;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    DiscoveryReceiver() : juce::Thread("Mix2Go-Discovery") {}

    ~DiscoveryReceiver() override { stopListening(); }

    /// Tries to bind to [kDiscoveryPort] (fallback: +1, +2, … up to +8).
    /// Returns the port that was actually bound, or 0 on failure.
    int startListening()
    {
        if (isThreadRunning()) return m_boundPort;

        // Try a small port range to survive the rare case where the primary
        // port is already in use on this machine.
        for (int delta = 0; delta < 9; ++delta)
        {
            const int port = kDiscoveryPort + delta;
            // allowBroadcast = true so macOS delivers 255.255.255.255 packets
            // to this socket even on the receiving side.
            m_socket = std::make_unique<juce::DatagramSocket>(true);

            if (m_socket->bindToPort(port))
            {
                m_boundPort = port;
                startThread();
                DBG("[Discovery] Listening on UDP port " << port);
                return port;
            }

            m_socket = nullptr;
            DBG("[Discovery] Port " << port << " in use, trying next…");
        }

        DBG("[Discovery] Could not bind any port in range "
            << kDiscoveryPort << "-" << (kDiscoveryPort + 8));
        return 0;
    }

    void stopListening()
    {
        signalThreadShouldExit();
        if (m_socket) m_socket->shutdown();
        stopThread(2000);
        m_socket    = nullptr;
        m_boundPort = 0;
    }

    int  boundPort() const { return m_boundPort; }
    bool isListening() const { return isThreadRunning() && m_boundPort > 0; }

    juce::String lastSeenIP()   const { return m_lastSeenIP; }
    int          lastSeenPort() const { return m_lastSeenPort; }

private:

    void run() override
    {
        juce::int64 lastHeartbeat = juce::Time::currentTimeMillis();
        bool deviceConnected = false;

        while (!threadShouldExit())
        {
            // Poll with a short timeout so we can check the heartbeat timer.
            if (!m_socket->waitUntilReady(true, 400))
            {
                // Timeout — check whether the heartbeat expired.
                if (deviceConnected &&
                    juce::Time::currentTimeMillis() - lastHeartbeat > kHeartbeatTimeoutMs)
                {
                    deviceConnected = false;
                    DBG("[Discovery] Heartbeat timeout — device lost");
                    juce::MessageManager::callAsync([this]() {
                        if (onLost) onLost();
                    });
                }
                continue;
            }

            char buf[256] = {};
            juce::String senderIP;
            int          senderPort = 0;
            const int bytes = m_socket->read(buf, (int)sizeof(buf) - 1,
                                             false, senderIP, senderPort);
            if (bytes <= 0) continue;

            // Parse "MIX2GO:2:<audioPort>"
            const juce::String msg(buf, bytes);
            const auto tokens = juce::StringArray::fromTokens(msg, ":", "");

            if (tokens.size() < 3
                || tokens[0] != "MIX2GO"
                || tokens[1].getIntValue() != 2)
                continue;

            const int audioPort = tokens[2].getIntValue();
            if (audioPort <= 0 || audioPort > 65535) continue;

            lastHeartbeat = juce::Time::currentTimeMillis();

            // Only notify when the endpoint actually changes (or on first connection).
            if (!deviceConnected
                || m_lastSeenIP   != senderIP
                || m_lastSeenPort != audioPort)
            {
                deviceConnected = true;
                m_lastSeenIP    = senderIP;
                m_lastSeenPort  = audioPort;

                DBG("[Discovery] Found Mix2Go app at "
                    << senderIP << ":" << audioPort);

                // Capture by value — senderIP / audioPort live on this thread's stack.
                juce::MessageManager::callAsync([this, senderIP, audioPort]() {
                    if (onDiscovered) onDiscovered(senderIP, audioPort);
                });
            }
        }
    }

    std::unique_ptr<juce::DatagramSocket> m_socket;
    int          m_boundPort   = 0;
    juce::String m_lastSeenIP;
    int          m_lastSeenPort = 0;
};

} // namespace mix2go::streaming
