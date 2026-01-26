#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace mix2go {
namespace streaming {

/**
 * Thread-safe, lock-free FIFO buffer for audio samples.
 * 
 * Uses JUCE's AbstractFifo for lock-free producer/consumer pattern
 * between the real-time audio thread and the network sender thread.
 */
class ThreadSafeFIFO
{
public:
    explicit ThreadSafeFIFO(int numSamples = 65536)
        : m_fifo(numSamples), m_buffer(2, numSamples)
    {
    }
    
    /** Prepare with specific channel count and buffer size */
    void prepare(int numChannels, int bufferSizeInSamples)
    {
        m_buffer.setSize(numChannels, bufferSizeInSamples, false, true, false);
        m_fifo.setTotalSize(bufferSizeInSamples);
        m_numChannels.store(numChannels);
    }
    
    /** Push audio samples from the audio thread (producer) */
    bool push(const juce::AudioBuffer<float>& source)
    {
        const auto numSamples = source.getNumSamples();
        const auto numChannels = juce::jmin(source.getNumChannels(), m_buffer.getNumChannels());
        
        if (m_fifo.getFreeSpace() < numSamples)
        {
            m_overruns.fetch_add(1, std::memory_order_relaxed);
            return false; // Buffer full, drop samples
        }
        
        const auto scope = m_fifo.write(numSamples);
        
        if (scope.blockSize1 > 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                m_buffer.copyFrom(ch, scope.startIndex1, 
                                  source, ch, 0, scope.blockSize1);
            }
        }
        
        if (scope.blockSize2 > 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                m_buffer.copyFrom(ch, scope.startIndex2, 
                                  source, ch, scope.blockSize1, scope.blockSize2);
            }
        }
        
        return true;
    }
    
    /** Pop audio samples for the network thread (consumer) */
    bool pop(juce::AudioBuffer<float>& dest, int numSamples)
    {
        const auto numChannels = juce::jmin(dest.getNumChannels(), m_buffer.getNumChannels());
        
        if (m_fifo.getNumReady() < numSamples)
        {
            m_underruns.fetch_add(1, std::memory_order_relaxed);
            return false; // Not enough samples available
        }
        
        const auto scope = m_fifo.read(numSamples);
        
        if (scope.blockSize1 > 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                dest.copyFrom(ch, 0, m_buffer, ch, scope.startIndex1, scope.blockSize1);
            }
        }
        
        if (scope.blockSize2 > 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                dest.copyFrom(ch, scope.blockSize1, 
                              m_buffer, ch, scope.startIndex2, scope.blockSize2);
            }
        }
        
        return true;
    }
    
    /** Get number of samples ready to read */
    [[nodiscard]] int getNumReady() const noexcept
    {
        return m_fifo.getNumReady();
    }
    
    /** Get available space for writing */
    [[nodiscard]] int getFreeSpace() const noexcept
    {
        return m_fifo.getFreeSpace();
    }
    
    /** Get number of channels */
    [[nodiscard]] int getNumChannels() const noexcept
    {
        return m_numChannels.load(std::memory_order_relaxed);
    }
    
    /** Clear the buffer */
    void reset()
    {
        m_fifo.reset();
        m_buffer.clear();
        m_overruns.store(0, std::memory_order_relaxed);
        m_underruns.store(0, std::memory_order_relaxed);
    }
    
    /** Get overrun count (samples dropped due to full buffer) */
    [[nodiscard]] uint64_t getOverrunCount() const noexcept
    {
        return m_overruns.load(std::memory_order_relaxed);
    }
    
    /** Get underrun count (read attempts with insufficient data) */
    [[nodiscard]] uint64_t getUnderrunCount() const noexcept
    {
        return m_underruns.load(std::memory_order_relaxed);
    }
    
private:
    juce::AbstractFifo m_fifo;
    juce::AudioBuffer<float> m_buffer;
    std::atomic<int> m_numChannels { 2 };
    std::atomic<uint64_t> m_overruns { 0 };
    std::atomic<uint64_t> m_underruns { 0 };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreadSafeFIFO)
};

} // namespace streaming
} // namespace mix2go
