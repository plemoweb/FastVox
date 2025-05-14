/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FastVoxAudioProcessor::FastVoxAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    using namespace Params;
    const auto& params = GetParams();

    auto floatHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
        {
            param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(params.at(paramName)));
            jassert(param != nullptr);
        };

    floatHelper(compAttack, Names::Compressor_Attack);
    floatHelper(compRelease, Names::Compressor_Release);
    floatHelper(compThreshold, Names::Compressor_Threshold);

    auto choiceHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
        {
            param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(params.at(paramName)));
            jassert(param != nullptr);
        };

    choiceHelper(compRatio, Names::Compressor_Ratio);
    
    auto boolHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
        {
            param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(params.at(paramName)));
            jassert(param != nullptr);
        };
    
    boolHelper(compBypassed, Names::Compressor_Bypassed);
}

FastVoxAudioProcessor::~FastVoxAudioProcessor()
{
}

//==============================================================================
const juce::String FastVoxAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FastVoxAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool FastVoxAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool FastVoxAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double FastVoxAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FastVoxAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int FastVoxAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FastVoxAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String FastVoxAudioProcessor::getProgramName(int index)
{
    return {};
}

void FastVoxAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void FastVoxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    juce::dsp::ProcessSpec spec;

    spec.maximumBlockSize = samplesPerBlock;

    spec.numChannels = 1;

    spec.sampleRate = sampleRate;

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    updateFilters();

    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);

    osc.initialise([](float x) { return std::sin(x); });

    spec.numChannels = getTotalNumOutputChannels();
    osc.prepare(spec);
    osc.setFrequency(440);

    compressor.prepare(spec);
}

void FastVoxAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FastVoxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (//layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void FastVoxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateFilters();

    juce::dsp::AudioBlock<float> block(buffer);

    //    buffer.clear();
    //
    //    for( int i = 0; i < buffer.getNumSamples(); ++i )
    //    {
    //        buffer.setSample(0, i, osc.processSample(0));
    //    }
    //
    //    juce::dsp::ProcessContextReplacing<float> stereoContext(block);
    //    osc.process(stereoContext);

    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);

    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);

    compressor.setAttack(compAttack->get());
    compressor.setRelease(compRelease->get());
    compressor.setThreshold(compThreshold->get());
    compressor.setRatio(compRatio->getCurrentChoiceName().getFloatValue());

    
    auto context = juce::dsp::ProcessContextReplacing<float>(block);

    context.isBypassed = compBypassed->get();
    compressor.process(context);


}

//==============================================================================
bool FastVoxAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* FastVoxAudioProcessor::createEditor()
{
    return new FastVoxAudioProcessorEditor(*this);
    //    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void FastVoxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void FastVoxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
        updateFilters();
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    using namespace Params;
    const auto& params = GetParams();
    settings.lowCutFreq = apvts.getRawParameterValue(params.at(Names::Low_Cut_Frequency))->load();
    //settings.highShelfFreq = apvts.getRawParameterValue("HighShelf Freq")->load();
    settings.peakFreq = apvts.getRawParameterValue(params.at(Names::Peak_Frequency))->load();
    settings.peakGainInDecibels = apvts.getRawParameterValue(params.at(Names::Peak_Gain))->load();
    settings.peakQuality = apvts.getRawParameterValue(params.at(Names::Peak_Q))->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue(params.at(Names::Low_Cut_Slope))->load());
    settings.highShelfFreq = apvts.getRawParameterValue(params.at(Names::High_Shelf_Frequency))->load();
    settings.highShelfGainInDecibels = apvts.getRawParameterValue(params.at(Names::High_Shelf_Gain))->load();
    settings.highShelfQuality = apvts.getRawParameterValue(params.at(Names::High_Shelf_Q))->load();
    //settings.highShelfSlope = static_cast<Slope>(apvts.getRawParameterValue("HighShelf Slope")->load());

    settings.lowCutBypassed = apvts.getRawParameterValue(params.at(Names::Low_Cut_Bypassed))->load() > 0.5f;
    settings.peakBypassed = apvts.getRawParameterValue(params.at(Names::Peak_Bypassed))->load() > 0.5f;
    settings.highShelfBypassed = apvts.getRawParameterValue(params.at(Names::High_Shelf_Frequency))->load() > 0.5f;

    return settings;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
        chainSettings.peakFreq,
        chainSettings.peakQuality,
        juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));
}

Coefficients makeHighShelfFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate,
        chainSettings.highShelfFreq,
        chainSettings.highShelfQuality,
        juce::Decibels::decibelsToGain(chainSettings.highShelfGainInDecibels));
}

void FastVoxAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());

    leftChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    rightChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);

    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}

void FastVoxAudioProcessor::updateHighShelfFilters(const ChainSettings& chainSettings)
{
    auto highShelfCoefficients = makeHighShelfFilter(chainSettings, getSampleRate());

    leftChain.setBypassed<ChainPositions::HighShelf>(chainSettings.highShelfBypassed);
    rightChain.setBypassed<ChainPositions::HighShelf>(chainSettings.highShelfBypassed);

    updateCoefficients(leftChain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
}

void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
    *old = *replacements;
}

void FastVoxAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings)
{
    auto cutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();

    leftChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    rightChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);

    updateCutFilter(rightLowCut, cutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(leftLowCut, cutCoefficients, chainSettings.lowCutSlope);
}



void FastVoxAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);

    updateLowCutFilters(chainSettings);
    updatePeakFilter(chainSettings);
    updateHighShelfFilters(chainSettings);
}

juce::AudioProcessorValueTreeState::ParameterLayout FastVoxAudioProcessor::createParameterLayout()
{
    using namespace Params;
    const auto& params = GetParams();
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Low_Cut_Frequency),
        params.at(Names::Low_Cut_Frequency),
        juce::NormalisableRange<float>(MIN_FREQUENCY, MAX_FREQUENCY, 1.f, 0.25f),
        20.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::High_Shelf_Frequency),
        params.at(Names::High_Shelf_Frequency),
        juce::NormalisableRange<float>(MIN_FREQUENCY, MAX_FREQUENCY, 1.f, 0.25f),
        20000.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::High_Shelf_Gain),
        params.at(Names::High_Shelf_Gain),
        juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::High_Shelf_Q),
        params.at(Names::High_Shelf_Q),
        juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
        1.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Peak_Frequency),
        params.at(Names::Peak_Frequency),
        juce::NormalisableRange<float>(MIN_FREQUENCY, MAX_FREQUENCY, 1.f, 0.25f),
        750.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Peak_Gain),
        params.at(Names::Peak_Gain),
        juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Peak_Q),
        params.at(Names::Peak_Q),
        juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
        1.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Compressor_Threshold),
        params.at(Names::Compressor_Threshold),
        juce::NormalisableRange<float>(MIN_THRESHOLD, MAX_DECIBELS, 1, 1),
        0));

    auto attackReleaseRange = juce::NormalisableRange<float>(5, 500, 1, 1);

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Compressor_Attack),
        params.at(Names::Compressor_Attack),
        attackReleaseRange,
        50));

    layout.add(std::make_unique<juce::AudioParameterFloat>(params.at(Names::Compressor_Release),
        params.at(Names::Compressor_Release),
        attackReleaseRange,
        250));

    auto choices = std::vector<double>{ 1,1.5,2,3,4,5,6,7,8,9,10,15,20,50 };
    juce::StringArray sa;
    for (auto choice:choices)
    {
        sa.add(juce::String(choice, 1));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(params.at(Names::Compressor_Ratio), 
        params.at(Names::Compressor_Ratio), sa, 3));

    juce::StringArray stringArray;
    for (int i = 0; i < 4; ++i)
    {
        juce::String str;
        str << (12 + i * 12);
        str << " db/Oct";
        stringArray.add(str);
    }

    layout.add(std::make_unique<juce::AudioParameterChoice>(params.at(Names::Low_Cut_Slope),
        params.at(Names::Low_Cut_Slope), stringArray, 0));
    //layout.add(std::make_unique<juce::AudioParameterChoice>("HighShelf Slope", "HighShelf Slope", stringArray, 0));

    layout.add(std::make_unique<juce::AudioParameterBool>(params.at(Names::Low_Cut_Bypassed),
        params.at(Names::Low_Cut_Bypassed), false));
    layout.add(std::make_unique<juce::AudioParameterBool>(params.at(Names::Peak_Bypassed),
        params.at(Names::Peak_Bypassed), false));
    layout.add(std::make_unique<juce::AudioParameterBool>(params.at(Names::High_Shelf_Bypassed),
        params.at(Names::High_Shelf_Bypassed), false));
    layout.add(std::make_unique<juce::AudioParameterBool>(params.at(Names::Analyzer_Enabled), 
        params.at(Names::Analyzer_Enabled), true));
    layout.add(std::make_unique<juce::AudioParameterBool>(params.at(Names::Compressor_Bypassed), 
        params.at(Names::Compressor_Bypassed), false));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FastVoxAudioProcessor();
}
