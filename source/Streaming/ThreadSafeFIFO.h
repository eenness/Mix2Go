#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace mix2go {
namespace streaming {

// Ein FIFO Puffer der thread-safe ist
// Damit kann der Audio Thread (schnell) und der Netzwerk Thread (langsam)
// sicher Daten austauschen ohne dass es knackst.
class ThreadSafeFIFO
{
public:
    ThreadSafeFIFO(int numSamples = 65536)
        : m_fifo(numSamples), m_buffer(2, numSamples)
    {
    }
    
    // Setup für Channels und Größe
    void prepare(int numChannels, int bufferSizeInSamples)
    {
        // Puffer neu anlegen
        m_buffer.setSize(numChannels, bufferSizeInSamples, false, true, false);
        m_fifo.setTotalSize(bufferSizeInSamples);
        m_numChannels = numChannels;
    }
    
    // Daten reinschreiben (vom Audio Thread)
    bool push(const juce::AudioBuffer<float>& source)
    {
        auto numSamples = source.getNumSamples();
        auto sourceChannels = source.getNumChannels();
        
        // wir können nicht mehr channel schreiben als wir haben
        int channelsToCopy = sourceChannels;
        if (channelsToCopy > m_buffer.getNumChannels())
            channelsToCopy = m_buffer.getNumChannels();
        
        // check ob genug platz ist
        if (m_fifo.getFreeSpace() < numSamples)
        {
            m_overruns++; // fehler zählen
            return false;
        }
        
        // schreiben vorbereiten
        auto scope = m_fifo.write(numSamples);
        
        // erster block kopieren
        if (scope.blockSize1 > 0)
        {
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                m_buffer.copyFrom(ch, scope.startIndex1, 
                                  source, ch, 0, scope.blockSize1);
            }
        }
        
        // zweiter block kopieren (wrap around)
        if (scope.blockSize2 > 0)
        {
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                m_buffer.copyFrom(ch, scope.startIndex2, 
                                  source, ch, scope.blockSize1, scope.blockSize2);
            }
        }
        
        return true;
    }
    
    // Daten rauslesen (für Netzwerk)
    bool pop(juce::AudioBuffer<float>& dest, int numSamples)
    {
        int channelsToCopy = dest.getNumChannels();
        if (channelsToCopy > m_buffer.getNumChannels())
            channelsToCopy = m_buffer.getNumChannels();
        
        // check ob genug da ist
        if (m_fifo.getNumReady() < numSamples)
        {
            m_underruns++; // fehler zählen
            return false; 
        }
        
        auto scope = m_fifo.read(numSamples);
        
        // lesen block 1
        if (scope.blockSize1 > 0)
        {
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                dest.copyFrom(ch, 0, m_buffer, ch, scope.startIndex1, scope.blockSize1);
            }
        }
        
        // lesen block 2
        if (scope.blockSize2 > 0)
        {
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                dest.copyFrom(ch, scope.blockSize1, 
                              m_buffer, ch, scope.startIndex2, scope.blockSize2);
            }
        }
        
        return true;
    }
    
    // Wie viel ist drin?
    int getNumReady()
    {
        return m_fifo.getNumReady();
    }
    
    // Wie viel Platz ist noch?
    int getFreeSpace()
    {
        return m_fifo.getFreeSpace();
    }
    
    int getNumChannels()
    {
        return m_numChannels; // atomics gehen auch so
    }
    
    // Alles löschen
    void reset()
    {
        m_fifo.reset();
        m_buffer.clear();
        m_overruns = 0;
        m_underruns = 0;
    }
    
    // Getter für Stats
    uint64_t getOverrunCount() { return m_overruns; }
    uint64_t getUnderrunCount() { return m_underruns; }
    
private:
    juce::AbstractFifo m_fifo;
    juce::AudioBuffer<float> m_buffer;
    
    std::atomic<int> m_numChannels { 2 };
    
    // simple counters
    std::atomic<uint64_t> m_overruns { 0 };
    std::atomic<uint64_t> m_underruns { 0 };
    
    // JUCE macro muss sein
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreadSafeFIFO)
};

} // namespace streaming
} // namespace mix2go
