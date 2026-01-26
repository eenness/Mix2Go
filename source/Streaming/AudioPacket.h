#pragma once

#include <cstdint>
#include <vector>
#include <cstring>

namespace mix2go {
namespace streaming {

/**
 * Audio packet structure for network transmission over UDP.
 * 
 * Packet layout (header + payload):
 * - uint32_t magic (4 bytes) - 0x4D324730 "M2G0"
 * - uint32_t sampleRate (4 bytes)
 * - uint16_t numChannels (2 bytes)
 * - uint32_t numSamples (4 bytes) - samples per channel
 * - uint64_t timestamp (8 bytes) - microseconds since stream start
 * - uint32_t sequenceNumber (4 bytes) - for packet ordering/loss detection
 * - float[] audioData (variable) - interleaved samples
 */
struct AudioPacket
{
    static constexpr uint32_t MAGIC = 0x4D324730; // "M2G0"
    static constexpr size_t HEADER_SIZE = 26; // bytes before audio data
    
    uint32_t magic { MAGIC };
    uint32_t sampleRate { 44100 };
    uint16_t numChannels { 2 };
    uint32_t numSamples { 0 };
    uint64_t timestamp { 0 };
    uint32_t sequenceNumber { 0 };
    std::vector<float> audioData;
    
    /** Calculate total packet size in bytes */
    [[nodiscard]] size_t getTotalSize() const noexcept
    {
        return HEADER_SIZE + (audioData.size() * sizeof(float));
    }
    
    /** Serialize packet to byte buffer for network transmission */
    [[nodiscard]] std::vector<uint8_t> serialize() const
    {
        std::vector<uint8_t> buffer(getTotalSize());
        size_t offset = 0;
        
        // Write header fields
        std::memcpy(buffer.data() + offset, &magic, sizeof(magic));
        offset += sizeof(magic);
        
        std::memcpy(buffer.data() + offset, &sampleRate, sizeof(sampleRate));
        offset += sizeof(sampleRate);
        
        std::memcpy(buffer.data() + offset, &numChannels, sizeof(numChannels));
        offset += sizeof(numChannels);
        
        std::memcpy(buffer.data() + offset, &numSamples, sizeof(numSamples));
        offset += sizeof(numSamples);
        
        std::memcpy(buffer.data() + offset, &timestamp, sizeof(timestamp));
        offset += sizeof(timestamp);
        
        std::memcpy(buffer.data() + offset, &sequenceNumber, sizeof(sequenceNumber));
        offset += sizeof(sequenceNumber);
        
        // Write audio data
        if (!audioData.empty())
        {
            std::memcpy(buffer.data() + offset, audioData.data(), 
                       audioData.size() * sizeof(float));
        }
        
        return buffer;
    }
    
    /** Deserialize packet from byte buffer */
    static bool deserialize(const uint8_t* data, size_t size, AudioPacket& outPacket)
    {
        if (size < HEADER_SIZE)
            return false;
        
        size_t offset = 0;
        
        std::memcpy(&outPacket.magic, data + offset, sizeof(outPacket.magic));
        offset += sizeof(outPacket.magic);
        
        if (outPacket.magic != MAGIC)
            return false;
        
        std::memcpy(&outPacket.sampleRate, data + offset, sizeof(outPacket.sampleRate));
        offset += sizeof(outPacket.sampleRate);
        
        std::memcpy(&outPacket.numChannels, data + offset, sizeof(outPacket.numChannels));
        offset += sizeof(outPacket.numChannels);
        
        std::memcpy(&outPacket.numSamples, data + offset, sizeof(outPacket.numSamples));
        offset += sizeof(outPacket.numSamples);
        
        std::memcpy(&outPacket.timestamp, data + offset, sizeof(outPacket.timestamp));
        offset += sizeof(outPacket.timestamp);
        
        std::memcpy(&outPacket.sequenceNumber, data + offset, sizeof(outPacket.sequenceNumber));
        offset += sizeof(outPacket.sequenceNumber);
        
        // Read audio data
        const size_t audioBytes = size - HEADER_SIZE;
        const size_t numFloats = audioBytes / sizeof(float);
        
        if (numFloats > 0)
        {
            outPacket.audioData.resize(numFloats);
            std::memcpy(outPacket.audioData.data(), data + offset, audioBytes);
        }
        
        return true;
    }
    
    /** Set audio data from JUCE buffer (interleaved) */
    void setFromBuffer(const float* const* channelData, int numChannelsIn, 
                       int numSamplesIn, uint32_t sampleRateIn)
    {
        sampleRate = sampleRateIn;
        numChannels = static_cast<uint16_t>(numChannelsIn);
        numSamples = static_cast<uint32_t>(numSamplesIn);
        
        // Interleave audio data
        audioData.resize(static_cast<size_t>(numChannelsIn) * numSamplesIn);
        
        for (int sample = 0; sample < numSamplesIn; ++sample)
        {
            for (int channel = 0; channel < numChannelsIn; ++channel)
            {
                audioData[static_cast<size_t>(sample * numChannelsIn + channel)] = 
                    channelData[channel][sample];
            }
        }
    }
};

} // namespace streaming
} // namespace mix2go
