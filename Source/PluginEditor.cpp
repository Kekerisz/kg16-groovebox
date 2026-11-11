#include "PluginEditor.h"
#include "BinaryData.h"

juce::WebBrowserComponent::Options KG16Editor::makeOptions()
{
    using Opt = juce::WebBrowserComponent::Options;
    auto webviewData = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KG16WebView");

    return Opt {}
        .withBackend (Opt::Backend::webview2)
        .withWinWebView2Options (Opt::WinWebView2 {}.withUserDataFolder (webviewData).withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            if (url == "/" || url.startsWith ("/index.html"))
            {
                juce::WebBrowserComponent::Resource r;
                r.data.resize ((size_t) BinaryData::index_htmlSize);
                std::memcpy (r.data.data(), BinaryData::index_html, r.data.size());
                r.mimeType = "text/html";
                return r;
            }
            return std::nullopt;
        })
        // page loaded: hand it the saved state (empty on first use, then the page loads a preset)
        .withEventListener ("kgReady", [this] (const juce::var&) { sendState ("kgInit"); })
        .withEventListener ("kgState", [this] (const juce::var& v) { proc.setStateFromUI (v["json"].toString()); })
        .withEventListener ("kgAudition", [this] (const juce::var& v)
        {
            const auto id = v["id"].toString();
            for (int t = 0; t < kg::NT; ++t) if (id == kg::TRACK_IDS[t]) proc.engine.auditionDrum (t);
        })
        .withEventListener ("kgBassAudition", [this] (const juce::var& v)
        {
            kg::BassStep s; s.on = true; s.n = (int) v["n"]; s.oct = (int) v["oct"]; s.acc = (bool) v["acc"];
            proc.engine.auditionBass (s);
        })
        .withEventListener ("kgTransport", [this] (const juce::var& v) { proc.engine.internalPlay.store ((bool) v["play"]); })
        .withEventListener ("kgLoadSamples", [this] (const juce::var& v)
        {
            const auto id = v["id"].toString();
            KG16Editor* self = this;
            const juce::Component::SafePointer<KG16Editor> safe (self);
            for (int t = 0; t < kg::NT; ++t)
                if (id == kg::TRACK_IDS[t])
                    self->proc.chooseSamples (t, [safe, t]
                    {
                        if (safe == nullptr) return;
                        safe->sendState ("kgInit");          // the page picks up the new sample names
                        safe->proc.engine.auditionDrum (t);   // and the first one plays straight away
                    });
        });
}

KG16Editor::KG16Editor (KG16Processor& p)
    : AudioProcessorEditor (&p), proc (p), web (makeOptions())
{
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (720, 560, 2400, 1800);
    setSize (1180, 860);
    startTimerHz (30);
}

KG16Editor::~KG16Editor() { stopTimer(); }

void KG16Editor::resized() { web.setBounds (getLocalBounds()); }

void KG16Editor::sendState (const juce::Identifier& eventId)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("json", proc.getStateJson());
    web.emitEventIfBrowserIsVisible (eventId, juce::var (o));
}

void KG16Editor::timerCallback()
{
    const int s = proc.engine.posStep.load(), pa = proc.engine.posPat.load(), k = proc.engine.posAbs.load();
    const bool run = proc.engine.running.load();
    if (s == lastStep && k == lastAbs && pa == lastPat && run == lastRun) return;
    lastStep = s; lastAbs = k; lastPat = pa; lastRun = run;
    auto* o = new juce::DynamicObject();
    o->setProperty ("s", s); o->setProperty ("k", k); o->setProperty ("pa", pa == 1 ? "B" : "A"); o->setProperty ("playing", run);
    web.emitEventIfBrowserIsVisible ("kgPos", juce::var (o));
}
