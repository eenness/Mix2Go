#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <opus.h>   // FetchContent build: headers are in include/, not include/opus/
#include <vector>
#include <functional>
#include <cmath>

namespace mix2go {
namespace streaming {

// Wraps libopus + juce::LagrangeInterpolator to encode DAW audio → Opus packets.
//
// Usage:
//   1. Call prepare(inputSampleRate, numChannels) when DAW sample rate is known.
//   2. Call pushSamples() every processBlock(). A PacketCallback fires whenever
//      a full 20 ms Opus frame (960 samples at 48 kHz) is ready.
//   3. Call reset() when the stream stops/restarts.
//
// Thread safety: all methods must be called from the same thread (network thread).
class MixOpusEncoder
{
public:
    using PacketCallback = std::function<void(const uint8_t* data, int bytes)>;

    explicit MixOpusEncoder(int frameSize = 960)
        : m_frameSize(frameSize)
    {
        m_outputBuffer.resize(4000); // > max Opus frame size (1275 bytes)
    }

    ~MixOpusEncoder()
    {
        destroyEncoder();
    }

    // Call once when DAW sample rate / channel count is known.
    void prepare(double inputSampleRate, int numChannels)
    {
        m_inputSampleRate = inputSampleRate;
        m_numChannels     = numChannels;
        // ratio < 1 for upsampling (44100→48000), = 1 if already at 48000
        m_ratio = inputSampleRate / 48000.0;

        m_resamplerL.reset();
        m_resamplerR.reset();
        m_accumL.clear();
        m_accumR.clear();

        destroyEncoder();
        createEncoder();
    }

    // Feed one DAW processBlock's worth of audio. Callback fires 0 or 1 times
    // per call depending on whether enough samples have accumulated.
    void pushSamples(const float* const* channelData,
                     int numChannels, int numSamples,
                     PacketCallback callback)
    {
        if (!m_encoder || numSamples <= 0)
            return;

        // Calculate output count for this block after resampling to 48 kHz.
        // ceil ensures we never under-produce (LagrangeInterpolator handles frac).
        const int outputCount = static_cast<int>(
            std::ceil(static_cast<double>(numSamples) / m_ratio));

        m_tempL.resize(outputCount);
        m_tempR.resize(outputCount);

        // juce::LagrangeInterpolator::process(speedRatio, src, dst, numOutputSamples)
        // speedRatio = inputRate / outputRate  (0.91875 for 44100→48000)
        m_resamplerL.process(m_ratio, channelData[0], m_tempL.data(), outputCount);

        const int chR = (numChannels > 1) ? 1 : 0;
        m_resamplerR.process(m_ratio, channelData[chR], m_tempR.data(), outputCount);

        m_accumL.insert(m_accumL.end(), m_tempL.begin(), m_tempL.end());
        m_accumR.insert(m_accumR.end(), m_tempR.begin(), m_tempR.end());

        // Encode all complete frames
        while (static_cast<int>(m_accumL.size()) >= m_frameSize)
            encodeFrame(callback);
    }

    void reset()
    {
        m_resamplerL.reset();
        m_resamplerR.reset();
        m_accumL.clear();
        m_accumR.clear();
        if (m_encoder)
            opus_encoder_ctl(m_encoder, OPUS_RESET_STATE);
    }

    int getFrameSize() const { return m_frameSize; }

    // Input samples at DAW rate needed to produce one Opus frame at 48 kHz.
    // Rounded up so the network thread pops the right amount from the FIFO.
    int getInputSamplesPerFrame() const
    {
        return static_cast<int>(std::ceil(m_frameSize * m_ratio));
    }

private:
    void createEncoder()
    {
        int error = OPUS_OK;
        m_encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
        if (error != OPUS_OK || !m_encoder)
        {
            DBG("[OpusEncoder] Failed to create encoder: " << opus_strerror(error));
            m_encoder = nullptr;
            return;
        }
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(128000));
        opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(8));
        opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));        // FEC for loss concealment
        opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(5));  // assume ~5% loss

        DBG("[OpusEncoder] Created: 48000 Hz / 2ch / 128 kbps / FEC on");
    }

    void destroyEncoder()
    {
        if (m_encoder)
        {
            opus_encoder_destroy(m_encoder);
            m_encoder = nullptr;
        }
    }

    void encodeFrame(PacketCallback& callback)
    {
        // Interleave L + R float → int16
        m_int16Buffer.resize(m_frameSize * 2);
        for (int i = 0; i < m_frameSize; ++i)
        {
            float l = (i < static_cast<int>(m_accumL.size())) ? m_accumL[i] : 0.0f;
            float r = (i < static_cast<int>(m_accumR.size())) ? m_accumR[i] : 0.0f;
            l = juce::jlimit(-1.0f, 1.0f, l);
            r = juce::jlimit(-1.0f, 1.0f, r);
            m_int16Buffer[i * 2]     = static_cast<opus_int16>(l * 32767.0f);
            m_int16Buffer[i * 2 + 1] = static_cast<opus_int16>(r * 32767.0f);
        }

        const int bytes = opus_encode(m_encoder,
                                      m_int16Buffer.data(),
                                      m_frameSize,
                                      m_outputBuffer.data(),
                                      static_cast<opus_int32>(m_outputBuffer.size()));
        if (bytes > 0)
        {
            callback(m_outputBuffer.data(), bytes);
        }
        else
        {
            DBG("[OpusEncoder] encode error: " << opus_strerror(bytes));
        }

        // Consume the encoded samples from the accumulator
        if (static_cast<int>(m_accumL.size()) > m_frameSize)
        {
            m_accumL.erase(m_accumL.begin(), m_accumL.begin() + m_frameSize);
            m_accumR.erase(m_accumR.begin(), m_accumR.begin() + m_frameSize);
        }
        else
        {
            m_accumL.clear();
            m_accumR.clear();
        }
    }

    ::OpusEncoder* m_encoder = nullptr; // libopus opaque type (global namespace)
    juce::LagrangeInterpolator m_resamplerL, m_resamplerR;

    double m_inputSampleRate = 44100.0;
    int    m_numChannels     = 2;
    double m_ratio           = 44100.0 / 48000.0;
    int    m_frameSize;

    std::vector<float>      m_accumL, m_accumR;
    std::vector<float>      m_tempL,  m_tempR;
    std::vector<opus_int16> m_int16Buffer;
    std::vector<uint8_t>    m_outputBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixOpusEncoder)
};

} // namespace streaming
} // namespace mix2go
