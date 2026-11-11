#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// The plugin window shows the same page as the browser prototype; the page talks to the C++ engine
// through WebView events instead of WebAudio.
class KG16Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KG16Editor (KG16Processor&);
    ~KG16Editor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xffd8d7d0)); }

private:
    void timerCallback() override;
    void sendState (const juce::Identifier& eventId);
    juce::WebBrowserComponent::Options makeOptions();

    KG16Processor& proc;
    juce::WebBrowserComponent web;
    int lastStep = -2, lastAbs = -1, lastPat = -1; bool lastRun = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KG16Editor)
};
