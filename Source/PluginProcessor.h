#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "State.h"

class KG16Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    KG16Processor();
    ~KG16Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KG-16 Groovebox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- called from the editor (message thread)
    juce::String getStateJson() const { return stateJson; }
    void setStateFromUI (const juce::String& json);
    void chooseSamples (int track, std::function<void()> onDone);
    kg::Engine engine;

private:
    void timerCallback() override;
    void rebuildSnapshot();
    void reloadSamplesFromState();
    void loadSampleFiles (int track, const juce::Array<juce::File>& files);

    juce::String stateJson;
    juce::var state;
    std::vector<std::shared_ptr<const kg::SampleData>> samples[kg::NT];
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::SpinLock snapLock;
    std::shared_ptr<const kg::Snapshot> pending, audioSnap;
    std::vector<std::shared_ptr<const kg::Snapshot>> graveyard;
    kg::Snapshot defaultSnap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KG16Processor)
};
