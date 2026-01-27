#pragma once

#include <cstdint>
#include <vector>
#include <cstring>

namespace mix2go {
namespace streaming {

// Einfache Struktur für das Audio Paket
// Wird so übers Netzwerk geschickt
struct AudioPacket
{
    // Konstanten
    static const uint32_t MAGIC = 0x4D324730; // "M2G0"
    static const size_t HEADER_SIZE = 26; 
    
    // Daten Felder
    uint32_t magic = MAGIC;
    uint32_t sampleRate = 44100;
    uint16_t numChannels = 2;
    uint32_t numSamples = 0;
    uint64_t timestamp = 0;
    uint32_t sequenceNumber = 0;
    std::vector<float> audioData;
    
    // Größe berechnen
    size_t getTotalSize()
    {
        return HEADER_SIZE + (audioData.size() * sizeof(float));
    }
    
    // In Bytes umwandeln zum versenden
    std::vector<uint8_t> serialize()
    {
        std::vector<uint8_t> buffer(getTotalSize());
        size_t offset = 0;
        
        // Header reinschreiben
        // memcpy ist einfachste lösung
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
        
        // Audio daten kopieren
        if (audioData.size() > 0)
        {
            std::memcpy(buffer.data() + offset, audioData.data(), 
                       audioData.size() * sizeof(float));
        }
        
        return buffer;
    }
    
    // Aus Bytes wieder Paket machen
    static bool deserialize(const uint8_t* data, size_t size, AudioPacket& outPacket)
    {
        if (size < HEADER_SIZE)
            return false;
        
        size_t offset = 0;
        
        // Alles wieder rauslesen
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
        
        // Audio Samples holen
        size_t audioBytes = size - HEADER_SIZE;
        size_t numFloats = audioBytes / sizeof(float);
        
        if (numFloats > 0)
        {
            outPacket.audioData.resize(numFloats);
            std::memcpy(outPacket.audioData.data(), data + offset, audioBytes);
        }
        
        return true;
    }
    
    // Helper um JUCE Buffer daten zu übernehmen
    void setFromBuffer(const float* const* channelData, int numChannelsIn, 
                       int numSamplesIn, uint32_t sampleRateIn)
    {
        sampleRate = sampleRateIn;
        numChannels = (uint16_t)numChannelsIn;
        numSamples = (uint32_t)numSamplesIn;
        
        // Größe anpassen
        audioData.resize(numChannelsIn * numSamplesIn);
        
        // Interleaved speichern (L R L R L R)
        for (int sample = 0; sample < numSamplesIn; ++sample)
        {
            for (int channel = 0; channel < numChannelsIn; ++channel)
            {
                int index = sample * numChannelsIn + channel;
                audioData[index] = channelData[channel][sample];
            }
        }
    }
};

} // namespace streaming
} // namespace mix2go
