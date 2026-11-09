#include "Vst3Host.h"

#include <algorithm>
#include <filesystem>
#include <mutex>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

namespace Steinberg {
DEF_CLASS_IID(IPlugFrame)
}

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

FUnknown* HostContext() {
    static HostApplication* app = [] {
        auto* created = new HostApplication();  // lives for the whole process
        PluginContextFactory::instance().setPluginContext(created);
        return created;
    }();
    return app;
}

// Receives parameter edits from the plugin's window for the audio thread to apply.
class ComponentHandler final : public IComponentHandler {
public:
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        std::lock_guard<std::mutex> guard(lock);
        pending.push_back({id, value});
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }  // owned by the instrument
    uint32 PLUGIN_API release() override { return 1; }

    std::mutex lock;
    std::vector<std::pair<ParamID, ParamValue>> pending;
};

// A floating window that hosts the plugin's own interface.
class EditorWindow final : public wxFrame, public IPlugFrame {
public:
    EditorWindow(wxWindow* parent, const wxString& title, IPtr<IPlugView> view,
                 std::function<void()> onClosed)
        : wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
          view_(std::move(view)),
          onClosed_(std::move(onClosed)) {
        view_->setFrame(this);
        view_->attached(reinterpret_cast<void*>(GetHWND()), kPlatformTypeHWND);

        ViewRect rect;
        if (view_->getSize(&rect) == kResultTrue) ApplySize(rect);

        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            if (!resizing_ && view_ && view_->canResize() == kResultTrue) {
                const wxSize size = GetClientSize();
                ViewRect r(0, 0, size.x, size.y);
                view_->onSize(&r);
            }
            e.Skip();
        });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
            Detach();
            Destroy();
        });
    }

    // Disconnects from the plugin; safe to call more than once.
    void Detach() {
        if (!view_) return;
        view_->setFrame(nullptr);
        view_->removed();
        view_ = nullptr;
        if (onClosed_) onClosed_();
    }

    void ClearCallback() { onClosed_ = nullptr; }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* rect) override {
        if (!rect) return kInvalidArgument;
        ApplySize(*rect);
        if (view) view->onSize(rect);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }  // wx owns this window
    uint32 PLUGIN_API release() override { return 1; }

private:
    void ApplySize(const ViewRect& rect) {
        resizing_ = true;
        SetClientSize(rect.getWidth(), rect.getHeight());
        resizing_ = false;
    }

    IPtr<IPlugView> view_;
    std::function<void()> onClosed_;
    bool resizing_ = false;
};

}  // namespace

struct Vst3Instrument::Impl {
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    ComponentHandler handler;
    HostProcessData data;
    EventList events{512};
    ParameterChanges changes;
    ProcessContext context{};
    bool processing = false, active = false, prepared = false;
    double sampleRate = 48000;
    int64_t projectSamples = 0;
    EditorWindow* editor = nullptr;

    ~Impl() {
        if (editor) {
            editor->ClearCallback();
            editor->Detach();
            editor->Destroy();
        }
        if (processor && processing) processor->setProcessing(false);
        if (component && active) component->setActive(false);
        if (prepared) data.unprepare();
        if (controller) controller->setComponentHandler(nullptr);
        processor = nullptr;
        controller = nullptr;
        component = nullptr;
        provider = nullptr;  // terminates the plugin; the module must outlive this
        module = nullptr;
    }
};

std::vector<PluginInfo> ScanVst3Plugins() {
    std::vector<PluginInfo> plugins;
    for (const std::string& path : VST3::Hosting::Module::getModulePaths()) {
        std::wstring stem = std::filesystem::u8path(path).stem().wstring();
        const std::wstring suffix = L" (64 Bit)";
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
            stem.resize(stem.size() - suffix.size());
        plugins.push_back({wxString(stem), path});
    }
    std::sort(plugins.begin(), plugins.end(),
              [](const PluginInfo& a, const PluginInfo& b) { return a.name.CmpNoCase(b.name) < 0; });
    return plugins;
}

Vst3Instrument::Vst3Instrument() : impl_(std::make_unique<Impl>()) {}

Vst3Instrument::~Vst3Instrument() = default;

std::unique_ptr<Vst3Instrument> Vst3Instrument::Load(const PluginInfo& plugin, double sampleRate,
                                                     int blockSize, wxString* error) {
    const auto fail = [&](const wxString& why) {
        if (error) *error = why;
        return nullptr;
    };

    std::unique_ptr<Vst3Instrument> result(new Vst3Instrument());
    Impl& p = *result->impl_;
    p.sampleRate = sampleRate;

    FUnknown* context = HostContext();
    std::string moduleError;
    p.module = VST3::Hosting::Module::create(plugin.path, moduleError);
    if (!p.module) return fail("Could not load the plugin: " + wxString::FromUTF8(moduleError));

    VST3::Hosting::PluginFactory factory = p.module->getFactory();
    factory.setHostContext(context);

    // Prefer an instrument class if the module also contains effects.
    VST3::Hosting::ClassInfo chosen;
    bool found = false;
    for (const VST3::Hosting::ClassInfo& info : factory.classInfos()) {
        if (info.category() != kVstAudioEffectClass) continue;
        const std::string sub = info.subCategoriesString();
        const bool instrument = sub.find("Instrument") != std::string::npos;
        if (!found || instrument) {
            chosen = info;
            found = true;
        }
        if (instrument) break;
    }
    if (!found) return fail("The plugin has no audio component");

    p.provider = owned(new PlugProvider(factory, chosen, true));
    if (!p.provider->initialize()) return fail("The plugin failed to initialise");

    p.component = p.provider->getComponentPtr();
    p.controller = p.provider->getControllerPtr();
    p.processor = FUnknownPtr<IAudioProcessor>(p.component);
    if (!p.processor) return fail("The plugin cannot process audio");
    if (p.component->getBusCount(kAudio, kOutput) < 1)
        return fail("That plugin has no audio output");
    if (p.controller) p.controller->setComponentHandler(&p.handler);

    // Ask for stereo on the first output; leave every other bus switched off.
    std::vector<SpeakerArrangement> in(p.component->getBusCount(kAudio, kInput), SpeakerArr::kEmpty);
    std::vector<SpeakerArrangement> out(p.component->getBusCount(kAudio, kOutput), SpeakerArr::kEmpty);
    for (size_t i = 0; i < in.size(); ++i) p.processor->getBusArrangement(kInput, int32(i), in[i]);
    for (size_t i = 0; i < out.size(); ++i) p.processor->getBusArrangement(kOutput, int32(i), out[i]);
    out[0] = SpeakerArr::kStereo;
    p.processor->setBusArrangements(in.empty() ? nullptr : in.data(), int32(in.size()), out.data(),
                                    int32(out.size()));

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    if (p.processor->setupProcessing(setup) != kResultOk)
        return fail("The plugin rejected the audio settings");

    for (int32 i = 0; i < int32(in.size()); ++i) p.component->activateBus(kAudio, kInput, i, false);
    for (int32 i = 0; i < int32(out.size()); ++i) p.component->activateBus(kAudio, kOutput, i, i == 0);
    if (p.component->getBusCount(kEvent, kInput) > 0) p.component->activateBus(kEvent, kInput, 0, true);

    if (p.component->setActive(true) != kResultOk) return fail("The plugin could not be activated");
    p.active = true;

    if (!p.data.prepare(*p.component, blockSize, kSample32))
        return fail("Could not allocate the plugin's audio buffers");
    p.prepared = true;
    p.changes.setMaxParameters(p.controller ? std::max<int32>(p.controller->getParameterCount(), 1) : 1);

    p.processor->setProcessing(true);
    p.processing = true;
    return result;
}

void Vst3Instrument::Process(const MidiEvent* events, int count, float* left, float* right,
                             int frames) {
    Impl& p = *impl_;

    p.events.clear();
    for (int i = 0; i < count; ++i) {
        Event e{};
        e.busIndex = 0;
        e.sampleOffset = events[i].offset;
        if (events[i].on) {
            e.type = Event::kNoteOnEvent;
            e.noteOn.channel = 0;
            e.noteOn.pitch = int16(events[i].pitch);
            e.noteOn.velocity = events[i].velocity;
            e.noteOn.noteId = -1;
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = 0;
            e.noteOff.pitch = int16(events[i].pitch);
            e.noteOff.velocity = 0;
            e.noteOff.noteId = -1;
        }
        p.events.addEvent(e);
    }

    p.changes.clearQueue();
    {
        std::unique_lock<std::mutex> guard(p.handler.lock, std::try_to_lock);
        if (guard.owns_lock()) {
            for (const auto& change : p.handler.pending) {
                int32 index = 0, point = 0;
                if (IParamValueQueue* queue = p.changes.addParameterData(change.first, index))
                    queue->addPoint(0, change.second, point);
            }
            p.handler.pending.clear();
        }
    }

    p.context = ProcessContext{};
    p.context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
    p.context.sampleRate = p.sampleRate;
    p.context.tempo = 96;
    p.context.projectTimeSamples = p.projectSamples;
    p.projectSamples += frames;

    p.data.numSamples = frames;
    p.data.inputEvents = &p.events;
    p.data.inputParameterChanges = &p.changes;
    p.data.processContext = &p.context;

    if (p.processor->process(p.data) == kResultOk && p.data.numOutputs > 0 &&
        p.data.outputs[0].numChannels > 0) {
        const AudioBusBuffers& bus = p.data.outputs[0];
        std::copy_n(bus.channelBuffers32[0], frames, left);
        std::copy_n(bus.channelBuffers32[bus.numChannels > 1 ? 1 : 0], frames, right);
    } else {
        std::fill_n(left, frames, 0.f);
        std::fill_n(right, frames, 0.f);
    }
}

bool Vst3Instrument::OpenEditor(wxWindow* parent, const wxString& title) {
    Impl& p = *impl_;
    if (p.editor) {
        p.editor->Raise();
        return true;
    }
    if (!p.controller) return false;

    IPtr<IPlugView> view = owned(p.controller->createView(ViewType::kEditor));
    if (!view || view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) return false;

    p.editor = new EditorWindow(parent, title, view, [&p] { p.editor = nullptr; });
    p.editor->Show();
    return true;
}
