#include "PluginEditor.h"
#include <cstdlib>

RoomServiceEditor::RoomServiceEditor(RoomServiceProcessor& p)
    : AudioProcessorEditor(&p), proc(p), gfx(*p.engine.gen) {
    setOpaque(true);
    setWantsKeyboardFocus(true);    // options:want_all_kb
    setResizable(true, true);
    setResizeLimits(480, 360, 4000, 3000);
    setSize(760, 574);              // @gfx 760 574
    gfx.isKeyDown = [](int c) {
        using K = juce::KeyPress;
        switch (c) {
            case 1818584692: return K::isKeyCurrentlyDown(K::leftKey);    // 'left'
            case 1919379572: return K::isKeyCurrentlyDown(K::rightKey);   // 'rght'
            case 30064:      return K::isKeyCurrentlyDown(K::upKey);      // 'up'
            case 1685026670: return K::isKeyCurrentlyDown(K::downKey);    // 'down'
            default: break;
        }
        if (c > 0 && c < 256) {
            return K::isKeyCurrentlyDown(c) || K::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase((juce::juce_wchar)c));
        }
        return false;
    };
    gfx.onCursor = [this](int id) {
        if (id == lastCursor) return;   // the script sets the cursor every frame; only real changes go to the OS
        lastCursor = id;
        using C = juce::MouseCursor;
        C::StandardCursorType t = C::NormalCursor;
        switch (id) {
            case 32513: t = C::IBeamCursor; break;
            case 32642: t = C::BottomRightCornerResizeCursor; break;
            case 32643: t = C::BottomLeftCornerResizeCursor; break;
            case 32644: t = C::LeftRightResizeCursor; break;
            case 32645: t = C::UpDownResizeCursor; break;
            case 32646: t = C::UpDownLeftRightResizeCursor; break;
            case 32649: t = C::PointingHandCursor; break;
            default: break;
        }
        setMouseCursor(t);
    };
    diagFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("ahh-debug.txt");
    if (!diagFile.existsAsFile()) diagFile = juce::File();
    else diagFile.appendText("\n--- Ahh editor opened " + juce::Time::getCurrentTime().toString(true, true) + "\n");
    startTimerHz(30);
}

void RoomServiceEditor::diagTick() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (diagT0 == 0) diagT0 = now;
    if (now - diagT0 < 1000.0) return;
    auto& d = proc.diag;
    const int nc = d.notifyCount.exchange(0), sc = d.stateCount.exchange(0);
    const double nm = d.notifyMs.exchange(0), nmax = d.notifyMaxMs.exchange(0), sm = d.stateMs.exchange(0);
    juce::String line;
    line << juce::Time::getCurrentTime().toString(false, true, true) << "  frames " << dFrames
         << "  gfx avg " << juce::String(dFrames ? dGfx / dFrames : 0, 1) << " max " << juce::String(dGfxMax, 1)
         << " ms  frame gap avg " << juce::String(dFrames ? dGap / dFrames : 0, 1) << " max " << juce::String(dGapMax, 1)
         << " ms  paints " << dPaints << " avg " << juce::String(dPaints ? dPaint / dPaints : 0, 1) << " max " << juce::String(dPaintMax, 1)
         << " ms  mouse events " << dEvents << "  host notify " << nc << " x, " << juce::String(nm, 1) << " ms (max " << juce::String(nmax, 1)
         << ")  state " << sc << " x, " << juce::String(sm, 1) << " ms  scale " << juce::String(scale(), 2)
         << "  size " << gfx.screen().getWidth() << "x" << gfx.screen().getHeight() << "\n";
    diagFile.appendText(line);
    diagT0 = now; dFrames = dPaints = dEvents = 0; dGfx = dGfxMax = dGap = dGapMax = dPaint = dPaintMax = 0;
}

RoomServiceEditor::~RoomServiceEditor() { stopTimer(); }

double RoomServiceEditor::scale() const {
    const auto* g = proc.engine.gen.get();
    return g->v_gfx_ext_retina > 0 ? (double)lastPaintScale : 1.0;
}

void RoomServiceEditor::resized() { repaint(); }

void RoomServiceEditor::timerCallback() {
    if (!isShowing() || getWidth() <= 0 || getHeight() <= 0) return;   // closed or hidden window: no frames
    // a frame that took long is followed by a pause at least as long: the host's own window, mouse and keyboard
    // handling always get half of the time, however large the window is (the picture slows down, nothing freezes)
    if (juce::Time::getMillisecondCounterHiRes() < nextFrameMs) return;
    // WHILE THE WINDOW IS BEING RESIZED: no new frames - the last one is stretched to the window (paint), which costs nothing.
    // Every new size means new fonts and every text drawn anew; doing that 30 times a second made the resize stutter.
    // 150 ms after the last size change the frame is drawn once at the final size.
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (getWidth() != lastW || getHeight() != lastH) { if (lastW > 0) sizeChangeMs = nowMs; lastW = getWidth(); lastH = getHeight(); }   // (the first size: no wait)
        if (nowMs - sizeChangeMs < 150.0) { repaint(); return; }
    }
    // the script expects @init before @gfx (REAPER guarantees it); a host may show the editor before the first block
    if (proc.engine.needsInit() || proc.hasHeldState()) proc.initForEditor();
    const double s = scale();
    const int w = juce::roundToInt(getWidth() * s), h = juce::roundToInt(getHeight() * s);
    auto* g = proc.engine.gen.get();
    if (g->v_gfx_ext_retina > 0) g->v_gfx_ext_retina = s;
    g->v_mouse_cap = liveCap | latchedButtons;   // a press seen since the last frame counts for this frame
    // a newer release on GitHub: the script shows a small button in the footer (updav, string 1000 = the tag)
    if (updState != 1) {
        updState = rs::UpdateCheck::state(&updTag, &updUrl);
        if (updState == 1) g->setString(1000, updTag.c_str());
    }
    // updav: 0 nothing, 1 "x is out", 2 downloading (updprog 0..1, -1 = size unknown), 3 downloaded, 4 failed; string 1001 = the hint
    {
        double prog = 0.0; std::string path, err;
        int dl = rs::UpdateCheck::downloadState(&prog, &path, &err);
        int phase = updState != 1 ? 0 : (dl == 1 ? 2 : (dl == 2 ? 3 : (dl == 3 ? 4 : 1)));
       #if JUCE_LINUX
        if (const char* t = std::getenv("AHH_TEST_UPDPHASE")) { if (updState == 1) { phase = juce::jlimit(1, 4, std::atoi(t)); prog = 0.42; err = "the connection broke off"; } }
       #endif
        if (phase != updPhase) {
            updPhase = phase;
            const bool direct = !rs::UpdateCheck::assetUrl().empty();
            const std::string v = updTag.size() > 1 && (updTag[0] == 'v' || updTag[0] == 'V') ? updTag.substr(1) : updTag;
            std::string tip;
            if (phase == 1) tip = direct ? "Ahh " + v + " is out. Click to download and install it - quit your DAW before you click Install. Right-click: download only, the download folder, what's new."
                                         : "Ahh " + v + " is out. Click to open the download page.";
            else if (phase == 2) tip = "Downloading Ahh " + v + " to " + rs::UpdateCheck::folder().getFullPathName().toStdString() + ". The installer opens when it is there.";
            else if (phase == 3) tip = "Ahh " + v + " is downloaded. Click to install - quit your DAW before you click Install. Right-click: show the file.";
            else if (phase == 4) tip = "Ahh " + v + " could not be downloaded (" + err + "). Click to try again, right-click to open the page instead.";
            g->setString(1001, tip.c_str());
            g->setString(1000, v.c_str());
        }
        g->g_updav = (double)phase;
        g->g_updprog = prog;
    }
    // Interface "System": the computer's light / dark setting, asked about once a second (cheap, and the OS rarely changes it)
    if (sysDarkAge-- <= 0) {
        sysDarkAge = 30;
        if (auto* e = std::getenv("AHH_TEST_SYSDARK")) sysDark = e[0] == '1';
        else sysDark = juce::Desktop::getInstance().isDarkModeActive();
    }
    g->g_sysdark = sysDark ? 1.0 : 0.0;
    // a newly opened editor has empty images: the script must draw a whole frame, whatever it believes is still on screen
    // (it only knew a reopened window by a pause of 0.25 s - a quick close and open in the host left the window blank)
    if (freshEditor) { g->g_fbw = 0.0; freshEditor = false; }
    { static const bool layoutTest = std::getenv("AHH_TEST_TEXTLOG") != nullptr; if (layoutTest) g->g_fbw = 0.0; }   // layout test: every frame drawn in full
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    gfx.beginFrame(w, h);
    proc.engine.runGfx(&gfx, w, h);
    gfx.endFrame();
    if (diagFile != juce::File()) {
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        dFrames++; dGfx += t1 - t0; dGfxMax = juce::jmax(dGfxMax, t1 - t0);
        if (lastTick > 0) { dGap += t0 - lastTick; dGapMax = juce::jmax(dGapMax, t0 - lastTick); }
        lastTick = t0;
        diagTick();
    }
    {
        const double t1 = juce::Time::getMillisecondCounterHiRes(), busy = (t1 - t0) + lastPaintMs;
        // (the script draws a full frame after a gap of 0.25 s: up to 200 ms the pause keeps the frames closer than that)
        const double pause = busy < 200.0 ? juce::jmax(busy * 0.33, juce::jmin(busy, 200.0 - busy)) : juce::jmin(busy, 250.0);
        nextFrameMs = busy > 16.0 ? t1 + pause : 0.0;
    }
    latchedButtons = 0;
    if (g->g_updgo != 0.0) {   // the footer note: 1 = click, 2 = right-click (the menu)
        const bool menu = g->g_updgo >= 2.0;
        g->g_updgo = 0.0;
        if (menu) updateMenu(); else updateClick();
    }
    if (g->g_mango != 0.0) {   // Help: "Read manual"
        g->g_mango = 0.0;
        if (std::getenv("AHH_TEST_NOBROWSER") == nullptr) juce::URL("https://dmajorsound.com/software/ahh/manual.html").launchInDefaultBrowser();
    }
    g->v_mouse_cap = liveCap;                     // the next frame sees the release
    // only what changed goes to the screen: nothing when the picture stands still, a meter's own area when it moves
    gfx.takeDirty(dirty);
    for (const auto& r : dirty) {
        const int x0 = (int)std::floor(r.getX() / s) - 1, y0 = (int)std::floor(r.getY() / s) - 1;
        const int x1 = (int)std::ceil(r.getRight() / s) + 1, y1 = (int)std::ceil(r.getBottom() / s) + 1;
        repaint(x0, y0, x1 - x0, y1 - y0);
    }
}

void RoomServiceEditor::paint(juce::Graphics& g) {
    const double pt0 = juce::Time::getMillisecondCounterHiRes();
    const juce::ScopeGuard sg { [&] { lastPaintMs = juce::Time::getMillisecondCounterHiRes() - pt0; if (diagFile != juce::File()) { const double dt = juce::Time::getMillisecondCounterHiRes() - pt0; dPaints++; dPaint += dt; dPaintMax = juce::jmax(dPaintMax, dt); } } };
    const float ps = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs(ps - lastPaintScale) > 0.01f) {
        if (std::abs(ps - candidateScale) <= 0.01f) lastPaintScale = ps; else candidateScale = ps;
    } else candidateScale = lastPaintScale;
    const juce::Image& img = gfx.screen();
    if (!img.isValid()) { g.fillAll(juce::Colours::black); return; }
    const int W = img.getWidth(), H = img.getHeight();
    if (W == getWidth() && H == getHeight()) {
        // one image pixel per point: copy the invalid part only
        const auto r = g.getClipBounds().getIntersection(img.getBounds());
        if (!r.isEmpty()) g.drawImageAt(img.getClippedImage(r), r.getX(), r.getY());
        return;
    }
    if (W == juce::roundToInt(getWidth() * ps) && H == juce::roundToInt(getHeight() * ps)) {
        // HiDPI: the frame was drawn at the screen's pixel size - it goes out 1:1 in device pixels, no resampling
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::scale(1.0f / ps));
        const auto r = g.getClipBounds().expanded(1).getIntersection(img.getBounds());
        if (!r.isEmpty()) g.drawImageAt(img.getClippedImage(r), r.getX(), r.getY());
        return;
    }
    // the frame size is still the one of the previous scale or window size (one frame): stretched for now
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(img, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void RoomServiceEditor::mouse(const juce::MouseEvent& e, bool up) {
    dEvents++;
    auto* g = proc.engine.gen.get();
    const double s = scale();
    g->v_mouse_x = std::floor(e.position.x * s);
    g->v_mouse_y = std::floor(e.position.y * s);
    const auto m = e.mods;
    int cap = 0;
    if (!up) {
        if (m.isLeftButtonDown()) cap |= 1;
        if (m.isRightButtonDown()) cap |= 2;
        if (m.isMiddleButtonDown()) cap |= 64;
    }
#if JUCE_MAC
    if (m.isCommandDown()) cap |= 4;
    if (m.isCtrlDown()) cap |= 32;
#else
    if (m.isCtrlDown()) cap |= 4;
#endif
    if (m.isShiftDown()) cap |= 8;
    if (m.isAltDown()) cap |= 16;
    liveCap = cap;
    latchedButtons |= cap & (1 | 2 | 64);
    g->v_mouse_cap = cap | latchedButtons;
}

void RoomServiceEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    mouse(e);
    auto* g = proc.engine.gen.get();
    g->v_mouse_wheel += w.deltaY * (w.isReversed ? -1.0 : 1.0) * 857.0;   // one notch (~0.14) = 120 as in REAPER
    g->v_mouse_hwheel += w.deltaX * 857.0;
}

bool RoomServiceEditor::keyPressed(const juce::KeyPress& k) {
    using K = juce::KeyPress;
    const int kc = k.getKeyCode();
    int c = 0;
    if (kc == K::returnKey) c = 13;
    else if (kc == K::escapeKey) c = 27;
    else if (kc == K::backspaceKey) c = 8;
    else if (kc == K::tabKey) c = 9;
    else if (kc == K::deleteKey) c = 6579564;        // 'del'
    else if (kc == K::leftKey) c = 1818584692;
    else if (kc == K::rightKey) c = 1919379572;
    else if (kc == K::upKey) c = 30064;
    else if (kc == K::downKey) c = 1685026670;
    else if (kc == K::homeKey) c = 1752132965;       // 'home'
    else if (kc == K::endKey) c = 6647396;           // 'end'
    else if (kc == K::pageUpKey) c = 1885828464;     // 'pgup'
    else if (kc == K::pageDownKey) c = 1885824110;   // 'pgdn'
    else if (kc == K::insertKey) c = 6909555;        // 'ins'
    else {
        const juce::juce_wchar t = k.getTextCharacter();
        const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
        if (ctrl && ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z'))) c = (int)(juce::CharacterFunctions::toLowerCase(t) - 'a' + 1);
        else if (t >= 32) c = (int)t;
    }
    if (c) { gfx.keyQueue.push_back(c); if (gfx.keyQueue.size() > 64) gfx.keyQueue.pop_front(); }
    return true;   // all keys go to the plugin while it has the focus
}

// ---- the update note in the footer (as in OLAAF) ----
// a click: download and install; when the file is there: install; without an installer for this system: the release page
void RoomServiceEditor::updateClick() {
    if (updState != 1) return;
    std::string path;
    const int dl = rs::UpdateCheck::downloadState(nullptr, &path, nullptr);
    if (rs::UpdateCheck::assetUrl().empty()) rs::UpdateCheck::openPage(updUrl);
    else if (dl == 2) rs::UpdateCheck::installStart(path);
    else if (dl != 1) rs::UpdateCheck::downloadStart(true);
}

// a right-click: download and install, download only, where to, what's new
void RoomServiceEditor::updateMenu() {
    if (updState != 1) return;
    std::string path;
    const int dl = rs::UpdateCheck::downloadState(nullptr, &path, nullptr);
    const bool direct = !rs::UpdateCheck::assetUrl().empty();
    const juce::File dir = rs::UpdateCheck::folder();
    juce::PopupMenu m;
    if (dl == 2) {
        m.addItem(3, "Install now");
       #if JUCE_MAC
        m.addItem(4, "Show in Finder");
       #else
        m.addItem(4, "Show in Explorer");
       #endif
        m.addSeparator();
    } else {
        m.addItem(1, "Download and install", direct && dl != 1);
        m.addItem(2, "Download only (install later)", direct && dl != 1);
        m.addSeparator();
    }
    m.addItem(5, "Download to: " + dir.getFileName() + juce::String::fromUTF8(" \xe2\x80\xa6"), dl != 1);
    m.addItem(6, "What's new (release page)");
    juce::Component::SafePointer<RoomServiceEditor> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [self, path, dir](int cmd) {
        if (self == nullptr) return;
        if (cmd == 1 || cmd == 2) rs::UpdateCheck::downloadStart(cmd == 1);
        else if (cmd == 3) rs::UpdateCheck::installStart(path);
        else if (cmd == 4) rs::UpdateCheck::reveal(path);
        else if (cmd == 6) rs::UpdateCheck::openPage(self->updUrl);
        else if (cmd == 5) {
            self->folderChooser = std::make_unique<juce::FileChooser>("Ahh: choose where updates are downloaded to", dir);
            self->folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [](const juce::FileChooser& fc) { const juce::File f = fc.getResult(); if (f.isDirectory()) rs::UpdateCheck::setFolder(f, true); });
        }
    });
}
