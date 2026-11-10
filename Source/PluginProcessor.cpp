#include "PluginProcessor.h"
#include "PluginEditor.h"

KG16Processor::KG16Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    // Before the UI has ever been opened: no pattern, every voice ready for MIDI from the piano roll.
    for (auto& t : defaultSnap.tr) t.human = 25;
    for (auto& p : defaultSnap.pat)
        for (int t = 0; t < kg::NT; ++t)
            for (int s = 0; s < kg::MAXS; ++s) { p.prob[t][s] = 100; p.rat[t][s] = 1; }
    defaultSnap.tr[kg::KICK].satType = kg::SAT_TUBE; defaultSnap.tr[kg::KICK].sat = 15;
    defaultSnap.tr[kg::SUB].satType = kg::SAT_TUBE; defaultSnap.tr[kg::SUB].sat = 30;
    startTimer (2000);
}

KG16Processor::~KG16Processor() { stopTimer(); }

void KG16Processor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatencySamples());
}

bool KG16Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void KG16Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    {
        const juce::SpinLock::ScopedTryLockType lock (snapLock);
        if (lock.isLocked() && pending != audioSnap) audioSnap = pending;
    }
    const kg::Snapshot& S = audioSnap ? *audioSnap : defaultSnap;

    kg::Engine::HostPos host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) host.ppq = *q;
            if (auto b = pos->getBpm()) host.bpm = *b;
        }

    buffer.clear();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    engine.process (S, L, R, buffer.getNumSamples(), host, midi);
    midi.clear();
}

//==============================================================================
void KG16Processor::rebuildSnapshot()
{
    std::shared_ptr<const kg::Snapshot> snap = kg::snapshotFromJson (state, samples);
    const juce::SpinLock::ScopedLockType lock (snapLock);
    if (pending) graveyard.push_back (pending);
    pending = std::move (snap);
}

void KG16Processor::timerCallback()
{
    // free retired snapshots on the message thread once the audio thread no longer holds them
    const juce::SpinLock::ScopedLockType lock (snapLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [] (const auto& p) { return p.use_count() == 1; }), graveyard.end());
}

void KG16Processor::setStateFromUI (const juce::String& json)
{
    auto parsed = juce::JSON::parse (json);
    if (! parsed.isObject()) return;
    // keep the sample file paths we own, the UI only knows their names
    const auto oldVo = state["vo"];
    if (auto* vo = parsed["vo"].getDynamicObject())
        for (int t = 0; t < kg::NT; ++t)
            if (auto* v = vo->getProperty (kg::TRACK_IDS[t]).getDynamicObject())
                if (oldVo.isObject() && ! v->hasProperty ("paths"))
                    v->setProperty ("paths", oldVo[kg::TRACK_IDS[t]]["paths"]);
    state = parsed;
    stateJson = juce::JSON::toString (state, true);
    rebuildSnapshot();
}

void KG16Processor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out (destData, false);
    out.writeString ("KG16v1");
    out.writeString (stateJson);
}

void KG16Processor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    if (in.readString() != "KG16v1") return;
    const auto json = in.readString();
    state = juce::JSON::parse (json);
    stateJson = state.isObject() ? json : juce::String();
    reloadSamplesFromState();
    rebuildSnapshot();
}

//==============================================================================
void KG16Processor::loadSampleFiles (int t, const juce::Array<juce::File>& files)
{
    std::vector<std::shared_ptr<const kg::SampleData>> list;
    for (auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) continue;
        auto sd = std::make_shared<kg::SampleData>();
        const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20));
        sd->buf.setSize ((int) std::min (2u, r->numChannels), len);
        r->read (&sd->buf, 0, len, 0, true, sd->buf.getNumChannels() > 1);
        sd->rate = r->sampleRate; sd->name = f.getFileName(); sd->path = f.getFullPathName();
        list.push_back (sd);
    }
    samples[t] = std::move (list);
}

void KG16Processor::reloadSamplesFromState()
{
    for (int t = 0; t < kg::NT; ++t)
    {
        juce::Array<juce::File> files;
        const auto paths = state["vo"][kg::TRACK_IDS[t]]["paths"];
        if (paths.isArray())
            for (auto& p : *paths.getArray())
                if (juce::File f (p.toString()); f.existsAsFile()) files.add (f);
        loadSampleFiles (t, files);
    }
}

void KG16Processor::chooseSamples (int t, std::function<void()> onDone)
{
    chooser = std::make_unique<juce::FileChooser> ("Minták kiválasztása", juce::File(), formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, t, onDone] (const juce::FileChooser& fc)
        {
            const auto files = fc.getResults();
            if (files.isEmpty()) return;
            loadSampleFiles (t, files);
            if (! state.isObject()) state = juce::var (new juce::DynamicObject());
            auto* root = state.getDynamicObject();
            if (! root->hasProperty ("vo")) root->setProperty ("vo", juce::var (new juce::DynamicObject()));
            auto* vo = root->getProperty ("vo").getDynamicObject();
            if (! vo->hasProperty (kg::TRACK_IDS[t])) vo->setProperty (kg::TRACK_IDS[t], juce::var (new juce::DynamicObject()));
            auto* v = vo->getProperty (kg::TRACK_IDS[t]).getDynamicObject();
            juce::Array<juce::var> names, paths;
            for (auto& s : samples[t]) { names.add (s->name); paths.add (s->path); }
            v->setProperty ("names", names); v->setProperty ("paths", paths); v->setProperty ("model", "USR");
            stateJson = juce::JSON::toString (state, true);
            rebuildSnapshot();
            if (onDone) onDone();
        });
}

juce::AudioProcessorEditor* KG16Processor::createEditor() { return new KG16Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KG16Processor(); }
