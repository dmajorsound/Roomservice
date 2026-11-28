// roomservice native engine: runs the generated code with the JSFX host semantics of REAPER / ysfx
#pragma once
#include "gen.h"
#include "meta.h"
#include <memory>
#include <atomic>
#include <functional>

namespace rs {

struct TimeInfo { double tempo = 120, position = 0, beat = 0; int playState = 1; int tsNum = 4, tsDenom = 4; };

class Engine : public HostCallbacks {
public:
    Engine();
    ~Engine() override = default;

    void setSampleRate(double sr);
    void setBlockSize(int bs);
    // slider by JSFX number (1-based), from the host / GUI side of the plugin
    void setSlider(int n, double v);
    void reset();
    double getSlider(int n) const { return gen->sl[n]; }

    void process(const double* const* in, double* const* out, int nIn, int nOut, int frames, const TimeInfo& ti);
    template <class Real> void processT(const Real* const* in, Real* const* out, int nIn, int nOut, int frames, const TimeInfo& ti);

    // @init must have run before @serialize or @gfx see the script (REAPER always runs it first). A host may ask for
    // the state or open the editor before the first audio block; these make sure of it. Call them with the engine lock.
    bool needsInit() const { return mustInit; }
    void ensureInit() { if (mustInit) init(); }

    // @serialize
    std::vector<float> saveState();
    // keepView: a preset is loaded into a running instance - the view (Basic / Expert, tabs, folded modules, sizes)
    // stays as it is; everything that sounds comes from the preset. A project load restores the view too.
    void loadState(const std::vector<float>& data, bool keepView = false);

    // @gfx (message thread)
    void runGfx(GfxBackend* b, int w, int h);

    int latency() const { const double d = gen->v_pdc_delay; return d > 0 ? (int)d : 0; }
    int inputChannels = 0;   // channels of the track's input bus (1 mono, 2 stereo, 6 5.1; 0 = unknown): the script's input meters

    // notifications from the script (slider_automate / sliderchange) -> plugin
    std::function<void(int, int)> onAutomate;
    std::function<void(int)> onChanged;

    std::unique_ptr<Gen> gen;

private:
    void init();
    void sliderAutomate(int idx, int touchMode) override { if (onAutomate) onAutomate(idx, touchMode); }
    void sliderChanged(int idx) override { if (onChanged) onChanged(idx); }
    double srate = 48000; int blockSize = 256;
    bool mustInit = true, mustSlider = true, gfxMustInit = true;
};

} // namespace rs
