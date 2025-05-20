/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <array>
#define MIN_FREQUENCY 20.f
#define MAX_FREQUENCY 20000.f
#define NEGATIVE_INFINITY -72.f
#define MAX_DECIBELS 12.f
#define MIN_THRESHOLD -60.f
namespace Params
{
    enum Names
    {
        Compressor_Ratio,
        Compressor_Threshold,
        Compressor_Attack,
        Compressor_Release,
        Compressor_Bypassed,
        
        Low_Cut_Frequency,
        Low_Cut_Slope,
        Low_Cut_Bypassed,

        Peak_Frequency,
        Peak_Gain,
        Peak_Q,
        Peak_Bypassed,

        High_Shelf_Frequency,
        High_Shelf_Gain,
        High_Shelf_Q,
        High_Shelf_Bypassed,

        Analyzer_Enabled,

        Input_Gain,
        Output_Gain,

        Gate_Threshold,
        Gate_Ratio,
        Gate_Attack,
        Gate_Release,
        Gate_Bypassed
    };
    inline const std::map<Names, juce::String>& GetParams()
    {
        static std::map<Names, juce::String> params =
        {
            {Compressor_Ratio,"Compressor Ratio"},
            {Compressor_Threshold, "Compressor Threshold"},
            {Compressor_Attack,"Compressor_Attack"},
            {Compressor_Release,"Compressor_Release"},
            {Compressor_Bypassed,"Compressor_Bypassed"},

            {Low_Cut_Frequency,"Low Cut Frequency"},
            {Low_Cut_Slope,"Low Cut Slope"},
            {Low_Cut_Bypassed,"Low Cut Bypassed"},

            {Peak_Frequency,"Peak Frequency"},
            {Peak_Gain,"Peak Gain"},
            {Peak_Q,"Peak Q"},
            {Peak_Bypassed,"Peak Bypassed"},

            {High_Shelf_Frequency,"High Shelf Frequency"},
            {High_Shelf_Gain,"High Shelf Gain"},
            {High_Shelf_Q,"High Shelf Q"},
            {High_Shelf_Bypassed,"High Shelf Bypassed"},

            {Analyzer_Enabled, "Analyzer Enabled"},

            {Input_Gain,"Input Gain"},
            {Output_Gain, "Output Gain"},

            {Gate_Threshold,"Gate Threshold"},
            {Gate_Ratio,"Gate Ratio"},
            {Gate_Attack,"Gate Attack"},
            {Gate_Release,"Gate Release"},
            {Gate_Bypassed,"Gate Bypassed"}
        };
        return params;
    }
};

template<typename T>
struct Fifo
{
    void prepare(int numChannels, int numSamples)
    {
        static_assert(std::is_same_v<T, juce::AudioBuffer<float>>,
            "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for (auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                numSamples,
                false,   //clear everything?
                true,    //including the extra space?
                true);   //avoid reallocating if you can?
            buffer.clear();
        }
    }

    void prepare(size_t numElements)
    {
        static_assert(std::is_same_v<T, std::vector<float>>,
            "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
        for (auto& buffer : buffers)
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }

    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 > 0)
        {
            buffers[write.startIndex1] = t;
            return true;
        }

        return false;
    }

    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 > 0)
        {
            t = buffers[read.startIndex1];
            return true;
        }

        return false;
    }

    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo{ Capacity };
};

enum Channel
{
    Right, //effectively 0
    Left //effectively 1
};

template<typename BlockType>
struct SingleChannelSampleFifo
{
    SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }

    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse);
        auto* channelPtr = buffer.getReadPointer(channelToUse);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            pushNextSampleIntoFifo(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);

        bufferToFill.setSize(1,             //channel
            bufferSize,    //num samples
            false,         //keepExistingContent
            true,          //clear extra space
            true);         //avoid reallocating
        audioBufferFifo.prepare(1, bufferSize);
        fifoIndex = 0;
        prepared.set(true);
    }
    //==============================================================================
    int getNumCompleteBuffersAvailable() const { return audioBufferFifo.getNumAvailableForReading(); }
    bool isPrepared() const { return prepared.get(); }
    int getSize() const { return size.get(); }
    //==============================================================================
    bool getAudioBuffer(BlockType& buf) { return audioBufferFifo.pull(buf); }
private:
    Channel channelToUse;
    int fifoIndex = 0;
    Fifo<BlockType> audioBufferFifo;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;

    void pushNextSampleIntoFifo(float sample)
    {
        if (fifoIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferFifo.push(bufferToFill);

            juce::ignoreUnused(ok);

            fifoIndex = 0;
        }

        bufferToFill.setSample(0, fifoIndex, sample);
        ++fifoIndex;
    }
};

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct ChainSettings
{
    float peakFreq{ 0 }, peakGainInDecibels{ 0 }, peakQuality{ 1.f };
    float lowCutFreq{ 0 };
    float highShelfFreq{ 0 }, highShelfGainInDecibels{ 0 }, highShelfQuality{ 1.f };;

    Slope lowCutSlope{ Slope::Slope_48 };

    bool lowCutBypassed{ false }, peakBypassed{ false }, highShelfBypassed{ false };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>;

using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, Filter>;

enum ChainPositions
{
    LowCut,
    Peak,
    HighShelf
};

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);
Coefficients makeHighShelfFilter(const ChainSettings& chainSettings, double sampleRate);

template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& coefficients)
{
    updateCoefficients(chain.template get<Index>().coefficients, coefficients[Index]);
    chain.template setBypassed<Index>(false);
}

template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& chain,
    const CoefficientType& coefficients,
    const Slope& slope)
{
    chain.template setBypassed<0>(true);
    chain.template setBypassed<1>(true);
    chain.template setBypassed<2>(true);
    chain.template setBypassed<3>(true);

    switch (slope)
    {
    case Slope_48:
    {
        update<3>(chain, coefficients);
    }
    case Slope_36:
    {
        update<2>(chain, coefficients);
    }
    case Slope_24:
    {
        update<1>(chain, coefficients);
    }
    case Slope_12:
    {
        update<0>(chain, coefficients);
    }
    }
}

inline auto makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq,
        sampleRate,
        2 * (chainSettings.lowCutSlope + 1));
}


//==============================================================================
/**
*/
class FastVoxAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    FastVoxAudioProcessor();
    ~FastVoxAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts{ *this, nullptr, "Parameters", createParameterLayout() };

    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleFifo<BlockType> leftChannelFifo{ Channel::Left };
    SingleChannelSampleFifo<BlockType> rightChannelFifo{ Channel::Right };

    float getRMSOutputLevel()const { return rmsOutputLevelDb; }
    float getRMSInputLevel()const { return rmsInputLevelDb; }

    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> inputGain;
    juce::dsp::Gain<float> outputGain;

    juce::AudioParameterFloat* compAttack{ nullptr };
    juce::AudioParameterFloat* compRelease{ nullptr };
    juce::AudioParameterFloat* compThreshold{ nullptr };
    juce::AudioParameterChoice* compRatio{ nullptr };
    juce::AudioParameterBool* compBypassed{ nullptr };

    juce::AudioParameterFloat* inputGainValue{ nullptr };
    juce::AudioParameterFloat* outputGainValue{ nullptr };

    juce::AudioParameterFloat* gateAttack{ nullptr };
    juce::AudioParameterFloat* gateRelease{ nullptr };
    juce::AudioParameterFloat* gateThreshold{ nullptr };
    juce::AudioParameterChoice* gateRatio{ nullptr };
    juce::AudioParameterBool* gateBypassed{ nullptr };

    std::atomic<float> rmsInputLevelDb{ NEGATIVE_INFINITY };
    std::atomic<float> rmsOutputLevelDb{ NEGATIVE_INFINITY };
    
private:
    MonoChain leftChain, rightChain;

    void updatePeakFilter(const ChainSettings& chainSettings);




    void updateLowCutFilters(const ChainSettings& chainSettings);
    void updateHighShelfFilters(const ChainSettings& chainSettings);

    void updateFilters();

    juce::dsp::Oscillator<float> osc;

    

    template<typename T>
    float computeRMSLevel(const T& buffer)
    {
        int numChannels = static_cast<int>(buffer.getNumChannels());
        int numSamples = static_cast<int>(buffer.getNumSamples());
        auto rms = 0.f;
        for (int chan = 0; chan < numChannels; ++chan)
        {
            rms += buffer.getRMSLevel(chan, 0, numSamples);
        }
        rms /= static_cast<float>(numChannels);
        return rms;
    };
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FastVoxAudioProcessor)
};
