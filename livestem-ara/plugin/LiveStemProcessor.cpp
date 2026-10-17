#include "LiveStemProcessor.h"

#include "LiveStemDocumentController.h"
#include "LiveStemEditor.h"

LiveStemProcessor::LiveStemProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LiveStemProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
}

void LiveStemProcessor::releaseResources()
{
    releaseResourcesForARA();
}

bool LiveStemProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void LiveStemProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Without ARA (or if rendering failed) the input passes through unchanged.
    if (! processBlockForARA (buffer, isRealtime(), getPlayHead()))
        processBlockBypassed (buffer, midi);
}

double LiveStemProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    getTailLengthSecondsForARA (tail);
    return tail;
}

juce::AudioProcessorEditor* LiveStemProcessor::createEditor()
{
    return new LiveStemEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LiveStemProcessor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<LiveStemDocumentController>();
}
