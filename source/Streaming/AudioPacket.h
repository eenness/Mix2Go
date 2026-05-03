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
    // Header layout (28 bytes, 4-byte aligned):
    //   uint32 magic       @ 0
    //   uint32 sampleRate  @ 4
    //   uint16 numChannels @ 8
    //   uint16 flags       @ 10   (reserved, was padding gap)
    //   uint32 numSamples  @ 12
    //   uint64 timestamp   @ 16   (8-byte aligned)
    //   uint32 seqNumber   @ 24
    //                        28
    static const size_t HEADER_SIZE = 28;

    // Daten Felder
    uint32_t magic = MAGIC;
    uint32_t sampleRate = 44100;
    uint16_t numChannels = 2;
    uint16_t flags = 0;          // reserved, must be 0
    uint32_t numSamples = 0;
    uint64_t timestamp = 0;
    uint32_t sequenceNumber = 0;

    // PCM16 audio payload — interleaved stereo (L R L R ...)
    std::vector<int16_t> pcmData;

    // Größe berechnen
    size_t getTotalSize()
    {
        return HEADER_SIZE + (pcmData.size() * sizeof(int16_t));
    }

    // In Bytes umwandeln zum versenden
    std::vector<uint8_t> serialize()
    {
        std::vector<uint8_t> buffer(getTotalSize());
        size_t offset = 0;

        std::memcpy(buffer.data() + offset, &magic, sizeof(magic));
        offset += sizeof(magic);

        std::memcpy(buffer.data() + offset, &sampleRate, sizeof(sampleRate));
        offset += sizeof(sampleRate);

        std::memcpy(buffer.data() + offset, &numChannels, sizeof(numChannels));
        offset += sizeof(numChannels);

        std::memcpy(buffer.data() + offset, &flags, sizeof(flags));
        offset += sizeof(flags);

        std::memcpy(buffer.data() + offset, &numSamples, sizeof(numSamples));
        offset += sizeof(numSamples);

        std::memcpy(buffer.data() + offset, &timestamp, sizeof(timestamp));
        offset += sizeof(timestamp);

        std::memcpy(buffer.data() + offset, &sequenceNumber, sizeof(sequenceNumber));
        offset += sizeof(sequenceNumber);

        // PCM16 audio daten kopieren
        if (!pcmData.empty())
        {
            std::memcpy(buffer.data() + offset, pcmData.data(),
                       pcmData.size() * sizeof(int16_t));
        }

        return buffer;
    }

    // Aus Bytes wieder Paket machen
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

        std::memcpy(&outPacket.flags, data + offset, sizeof(outPacket.flags));
        offset += sizeof(outPacket.flags);

        std::memcpy(&outPacket.numSamples, data + offset, sizeof(outPacket.numSamples));
        offset += sizeof(outPacket.numSamples);

        std::memcpy(&outPacket.timestamp, data + offset, sizeof(outPacket.timestamp));
        offset += sizeof(outPacket.timestamp);

        std::memcpy(&outPacket.sequenceNumber, data + offset, sizeof(outPacket.sequenceNumber));
        offset += sizeof(outPacket.sequenceNumber);

        // PCM16 samples holen
        size_t audioBytes = size - HEADER_SIZE;
        size_t numInt16s = audioBytes / sizeof(int16_t);

        if (numInt16s > 0)
        {
            outPacket.pcmData.resize(numInt16s);
            std::memcpy(outPacket.pcmData.data(), data + offset, audioBytes);
        }

        return true;
    }

    // Helper um JUCE Buffer daten zu übernehmen
    // Konvertiert float [-1.0, 1.0] → int16 PCM mit Clipping-Schutz
    void setFromBuffer(const float* const* channelData, int numChannelsIn,
                       int numSamplesIn, uint32_t sampleRateIn)
    {
        sampleRate = sampleRateIn;
        numChannels = (uint16_t)numChannelsIn;
        numSamples = (uint32_t)numSamplesIn;

        pcmData.resize(numChannelsIn * numSamplesIn);

        // Interleaved speichern (L R L R ...) mit float → int16 Konvertierung
        // Formel: clamp(s, -1, 1) * 32767  →  range [-32767, 32767]
        for (int sample = 0; sample < numSamplesIn; ++sample)
        {
            for (int channel = 0; channel < numChannelsIn; ++channel)
            {
                float s = channelData[channel][sample];
                // Clamp to [-1.0, 1.0] to prevent int16 overflow
                if (s > 1.0f)  s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                pcmData[sample * numChannelsIn + channel] = static_cast<int16_t>(s * 32767.0f);
            }
        }
    }
};

} // namespace streaming
} // namespace mix2go
