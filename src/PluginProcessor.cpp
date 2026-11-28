#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String enumLabel(const rs::SliderInfo& s, double v) {
    juce::StringArray items;
    items.addTokens(juce::String::fromUTF8(s.enums), "|", "");
    const double step = s.step > 0 ? s.step : 1.0;
    const int i = (int)std::floor((v - s.lo) / step + 0.5);
    return juce::isPositiveAndBelow(i, items.size()) ? items[i] : juce::String(v);
}

RoomServiceProcessor::RoomServiceProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::create5point1(), true)
                                      .withOutput("Output", juce::AudioChannelSet::create5point1(), true)) {
    paramBySlider.assign(257, nullptr);
    lastPushed.reset(new std::atomic<float>[257]);
    for (int i = 0; i < 257; i++) lastPushed[i] = 0.0f;
    touching.assign(257, 0);
    pending.reset(new std::atomic<int>[257]);
    for (int i = 0; i < 257; i++) pending[i] = 0;
    for (int i = 0; i < rs::kNumSliders; i++) {
        const rs::SliderInfo& s = rs::kSliders[i];
        juce::NormalisableRange<float> range((float)s.lo, (float)s.hi, (float)juce::jmax(0.0, s.step));
        auto attr = juce::AudioParameterFloatAttributes();
        // Stereo | 5.1 (and the fixed ITU layout) are set up once, not automated: the host must not switch the format during playback
        if (s.idx == 136 || s.idx == 137) attr = attr.withAutomatable(false);
        if (s.enums) {
            const rs::SliderInfo* sp = &s;
            attr = attr.withStringFromValueFunction([sp](float v, int) { return enumLabel(*sp, v); });
        }
        auto* p = new juce::AudioParameterFloat(juce::ParameterID("s" + juce::String(s.idx), 1),
                                                juce::String::fromUTF8(s.label), range, (float)s.def, attr);
        addParameter(p);
        paramBySlider[(size_t)s.idx] = p;
        lastPushed[s.idx] = p->convertFrom0to1(p->convertTo0to1((float)s.def));
    }
    engine.onAutomate = [this](int idx, int mode) { scriptAutomate(idx, mode); };
    engine.onChanged = [this](int idx) { scriptChanged(idx); };
    startTimerHz(30);
}

RoomServiceProcessor::~RoomServiceProcessor() { stopTimer(); }

// RoomservicePro: stereo in / stereo out (the 1.0 path), or a 5.1 track - 5.1 (or stereo, or mono) in, 5.1 out. The script
// reads the source from channels 1 / 2 and writes six channels when its Output format says 5.1 and six are there (num_ch).
bool RoomServiceProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::create5point1();
    if (out == juce::AudioChannelSet::create5point1()) return inOk;
    if (out == juce::AudioChannelSet::stereo()) return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    return false;
}

// a state that arrived before the host told us the sample rate: read now, after @init at the real rate
// (@init first, then @serialize - the order REAPER uses)
void RoomServiceProcessor::applyHeldState() {
    if (heldSer.empty()) return;
    engine.ensureInit();
    engine.loadState(heldSer, false);
    heldSer.clear(); heldAny = false;
}

void RoomServiceProcessor::initForEditor() {
    const juce::ScopedLock l(engineLock);
    engine.ensureInit();
    applyHeldState();
}

void RoomServiceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    int lat = 0;
    {
        const juce::ScopedLock sl(engineLock);
        engine.setSampleRate(sampleRate);
        engine.setBlockSize(samplesPerBlock);
        // @init runs here, not inside the first audio block: the host gets the latency before it starts processing
        // (no restart request out of the first block), and the first block carries no start-up work
        engine.ensureInit();
        prepared = true;
        applyHeldState();
        lat = engine.latency();
    }
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples(lat); }
}

// host / automation -> script: only real host changes are pushed (a value the script wrote itself is not sent back,
// so its double precision survives)
void RoomServiceProcessor::syncParamsToEngine() {
    for (int i = 0; i < rs::kNumSliders; i++) {
        const int n = rs::kSliders[i].idx;
        auto* p = paramBySlider[(size_t)n];
        const float v = p->convertFrom0to1(p->getValue());
        const float last = lastPushed[n].load();
        if (v != last) {
            lastPushed[n] = v;
            // a value the script wrote itself comes back from the host as float (VST3 echoes edits): within float
            // precision of the script's own double it is the same value - keep the double
            const double cur = engine.getSlider(n);
            const double tol = 4.0e-7 * juce::jmax(1.0, std::fabs(cur), std::fabs(rs::kSliders[i].hi - rs::kSliders[i].lo));
            if (std::fabs((double)v - cur) > tol) engine.setSlider(n, (double)v);
        }
    }
}

// script (slider_automate) -> host. Hosts expect parameter edits on the message thread (Steinberg hosts in particular),
// and the script also automates from the audio thread (Motion moves the source every block): those are noted here and
// delivered by the timer.
void RoomServiceProcessor::scriptAutomate(int idx, int mode) {
    if (idx < 1 || idx > 256 || !paramBySlider[(size_t)idx]) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) { notifyAutomate(idx, mode); return; }
    pending[idx].fetch_or(mode == 0 ? 3 : (mode == 1 ? 5 : 1));
    pendingAny = true;
}

void RoomServiceProcessor::notifyAutomate(int idx, int mode) {
    auto* p = paramBySlider[(size_t)idx];
    const float v = (float)engine.getSlider(idx);
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    if (mode == 0 && !touching[(size_t)idx]) { p->beginChangeGesture(); touching[(size_t)idx] = 1; }
    lastPushed[idx] = p->convertFrom0to1(p->convertTo0to1(v));
    p->setValueNotifyingHost(p->convertTo0to1(v));
    if (mode == 1 && touching[(size_t)idx]) { p->endChangeGesture(); touching[(size_t)idx] = 0; }
    const double dt = juce::Time::getMillisecondCounterHiRes() - t0;
    diag.notifyCount++; diag.notifyMs = diag.notifyMs + dt; if (dt > diag.notifyMaxMs) diag.notifyMaxMs = dt;
}

// script (sliderchange) -> host display
void RoomServiceProcessor::scriptChanged(int idx) {
    if (juce::MessageManager::existsAndIsCurrentThread()) { notifyChanged(idx); return; }
    if (idx < 0) { for (int i = 0; i < rs::kNumSliders; i++) pending[rs::kSliders[i].idx].fetch_or(8); }
    else if (idx <= 256) pending[idx].fetch_or(8);
    pendingAny = true;
}

void RoomServiceProcessor::notifyChanged(int idx) {
    for (int i = 0; i < rs::kNumSliders; i++) {
        const int n = rs::kSliders[i].idx;
        if (idx >= 0 && n != idx) continue;
        auto* p = paramBySlider[(size_t)n];
        const float v = (float)engine.getSlider(n);
        if (p->convertFrom0to1(p->getValue()) != v) {
            lastPushed[n] = p->convertFrom0to1(p->convertTo0to1(v));
            p->setValueNotifyingHost(p->convertTo0to1(v));
        }
    }
}

void RoomServiceProcessor::timerCallback() {
    if (!pendingAny.exchange(false)) return;
    for (int i = 0; i < rs::kNumSliders; i++) {
        const int n = rs::kSliders[i].idx;
        const int f = pending[n].exchange(0);
        if (f == 0 || !paramBySlider[(size_t)n]) continue;
        if (f & 7) {
            if ((f & 2) && !touching[(size_t)n]) notifyAutomate(n, 0); else notifyAutomate(n, -1);
            if ((f & 4) && touching[(size_t)n]) notifyAutomate(n, 1);
        } else notifyChanged(n);
    }
}

template <class Real>
void RoomServiceProcessor::process(juce::AudioBuffer<Real>& buffer) {
    // no ScopedNoDenormals: the script handles denormals itself (assignments flush them, as in EEL2), and FTZ/DAZ
    // would change the arithmetic against the JSFX
    const juce::ScopedLock sl(engineLock);
    hasRun = true;
    syncParamsToEngine();
    rs::TimeInfo ti;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto bpm = pos->getBpm()) ti.tempo = *bpm;
            if (auto t = pos->getTimeInSeconds()) ti.position = *t;
            if (auto b = pos->getPpqPosition()) ti.beat = *b;
            if (auto sig = pos->getTimeSignature()) { ti.tsNum = sig->numerator; ti.tsDenom = sig->denominator; }
            ti.playState = pos->getIsRecording() ? 5 : (pos->getIsPlaying() ? 1 : 0);
        }
    }
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels(), n = buffer.getNumSamples();
    // the script's channel order is REAPER's (L R C LFE Ls Rs) - JUCE's 5.1 set is in the same order.
    // mono input: feed the one channel to both script inputs; a stereo input on a 5.1 track: channels 3 - 6 silent
    const Real* ins[6]; Real* outs[6];
    const int ni = juce::jmin(6, juce::jmax(2, nIn)), no = juce::jmin(6, nOut);
    for (int c = 0; c < ni; c++) ins[c] = buffer.getReadPointer(c < nIn ? c : 0);
    for (int c = 0; c < no; c++) outs[c] = buffer.getWritePointer(c);
    engine.inputChannels = nIn;
    engine.processT<Real>(ins, outs, ni, no, n, ti);
    const int lat = engine.latency();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples(lat); }
}

void RoomServiceProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process(b); }
void RoomServiceProcessor::processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process(b); }

// state: magic, slider values (double, exact), the @serialize stream (float32, as REAPER stores it)
void RoomServiceProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const juce::ScopeGuard sg { [&] { diag.stateCount++; diag.stateMs = diag.stateMs + (juce::Time::getMillisecondCounterHiRes() - t0); } };
    juce::MemoryOutputStream os(dest, false);
    std::vector<float> ser;
    std::vector<double> sl((size_t)rs::kNumSliders);
    {
        const juce::ScopedLock l(engineLock);
        for (int i = 0; i < rs::kNumSliders; i++) sl[(size_t)i] = engine.getSlider(rs::kSliders[i].idx);
        if (!heldSer.empty()) ser = heldSer; else ser = engine.saveState();
    }
    os.writeInt(0x31565352);   // "RSV1"
    os.writeInt(rs::kNumSliders);
    for (int i = 0; i < rs::kNumSliders; i++) { os.writeInt(rs::kSliders[i].idx); os.writeDouble(sl[(size_t)i]); }
    os.writeInt((int)ser.size());
    for (float f : ser) os.writeFloat(f);
}

void RoomServiceProcessor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream is(data, (size_t)size, false);
    if (is.readInt() != 0x31565352) return;
    const int ns = is.readInt();
    std::vector<std::pair<int, double>> sl;
    for (int i = 0; i < ns && !is.isExhausted(); i++) { const int n = is.readInt(); sl.push_back({n, is.readDouble()}); }
    const int nf = is.readInt();
    std::vector<float> ser;
    for (int i = 0; i < nf && !is.isExhausted(); i++) ser.push_back(is.readFloat());
    {
        const juce::ScopedLock l(engineLock);
        // project load into an instance that has not run yet: start from a fresh script, so @init sees the stored
        // values (REAPER's order: values, @init, @serialize). A host that prepares the plug-in before it sends the
        // state would otherwise leave an @init with the default values behind.
        if (!hasRun.load()) engine.reset();
        for (auto& [n, v] : sl) engine.setSlider(n, v);
        // no @serialize data: nothing to read (running it would zero its variables)
        const bool preset = hasRun.load() && juce::Time::getMillisecondCounterHiRes() - createdMs > 1500.0;
        if (!ser.empty()) {
            if (prepared) engine.loadState(ser, preset);
            else { heldSer = ser; heldAny = true; }   // sample rate not known yet: read in prepareToPlay
        }
    }
    for (auto& [n, v] : sl) {
        if (n < 1 || n > 256 || !paramBySlider[(size_t)n]) continue;
        auto* p = paramBySlider[(size_t)n];
        lastPushed[n] = p->convertFrom0to1(p->convertTo0to1((float)v));
        p->setValueNotifyingHost(p->convertTo0to1((float)v));
    }
}

juce::AudioProcessorEditor* RoomServiceProcessor::createEditor() { hasRun = true; return new RoomServiceEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RoomServiceProcessor(); }
