#include "hide_from_stream.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(EB_UI_WINDOWS)

#include <Geode/modify/CCEGLView.hpp>
#include <imgui-cocos.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <gl/GL.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_ONE
#define GL_ONE 1
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif

using GlBlendFuncSeparateFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLenum);

class EbHideFromStreamState {
public:
    bool settingOn = false;
    bool affinityOk = false;
    bool classRegistered = false;
    bool captureSafeDraw = false;
    bool pendingDestroy = false;
    HWND host = nullptr;
    HWND overlay = nullptr;
    HDC dibDc = nullptr;
    HBITMAP dibBitmap = nullptr;
    void* dibBits = nullptr;
    int dibW = 0;
    int dibH = 0;
    GLuint fbo = 0;
    GLuint colorTex = 0;
    int fboW = 0;
    int fboH = 0;
    std::string status;
    bool statusOn = false;
    std::vector<std::uint8_t> readback;

    using GlGenFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
    using GlBindFramebufferFn = void(APIENTRY*)(GLenum, GLuint);
    using GlFramebufferTexture2DFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using GlDeleteFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint const*);
    using GlCheckFramebufferStatusFn = GLenum(APIENTRY*)(GLenum);

    GlGenFramebuffersFn genFramebuffers = nullptr;
    GlBindFramebufferFn bindFramebuffer = nullptr;
    GlFramebufferTexture2DFn framebufferTexture2D = nullptr;
    GlDeleteFramebuffersFn deleteFramebuffers = nullptr;
    GlCheckFramebufferStatusFn checkFramebufferStatus = nullptr;
    GlBlendFuncSeparateFn blendFuncSeparate = nullptr;
    bool glReady = false;
};

EbHideFromStreamState ebHideFromStreamState;

class EbHideFromStreamImpl {
public:
    static void* resolveGl(char const* name) {
        void* proc = wglGetProcAddress(name);
        if (!proc)
            proc = GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name);
        return proc;
    }

    static bool loadGl() {
        auto& state = ebHideFromStreamState;
        if (state.glReady)
            return true;
        state.genFramebuffers =
            reinterpret_cast<EbHideFromStreamState::GlGenFramebuffersFn>(resolveGl("glGenFramebuffers"));
        state.bindFramebuffer =
            reinterpret_cast<EbHideFromStreamState::GlBindFramebufferFn>(resolveGl("glBindFramebuffer"));
        state.framebufferTexture2D = reinterpret_cast<EbHideFromStreamState::GlFramebufferTexture2DFn>(
            resolveGl("glFramebufferTexture2D"));
        state.deleteFramebuffers = reinterpret_cast<EbHideFromStreamState::GlDeleteFramebuffersFn>(
            resolveGl("glDeleteFramebuffers"));
        state.checkFramebufferStatus =
            reinterpret_cast<EbHideFromStreamState::GlCheckFramebufferStatusFn>(
                resolveGl("glCheckFramebufferStatus"));
        state.blendFuncSeparate =
            reinterpret_cast<GlBlendFuncSeparateFn>(resolveGl("glBlendFuncSeparate"));
        state.glReady = state.genFramebuffers && state.bindFramebuffer &&
                        state.framebufferTexture2D && state.deleteFramebuffers &&
                        state.checkFramebufferStatus;
        return state.glReady;
    }

    static void applyCaptureSafeBlend() {
        auto& state = ebHideFromStreamState;
        if (!state.captureSafeDraw)
            return;
        // Straight-alpha RGB, but keep A = srcA + dstA*(1-srcA). The default
        // cocos blend squares alpha into transparent FBOs and washes text out.
        if (state.blendFuncSeparate) {
            state.blendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                    GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    static bool ensureClass() {
        auto& state = ebHideFromStreamState;
        if (state.classRegistered)
            return true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EarlyBufferHideFromStream";
        ATOM atom = RegisterClassExW(&wc);
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        state.classRegistered = true;
        return true;
    }

    static HWND gameHwnd() {
        HDC dc = wglGetCurrentDC();
        if (!dc)
            return nullptr;
        return WindowFromDC(dc);
    }

    static void destroyDib() {
        auto& state = ebHideFromStreamState;
        if (state.dibBitmap) {
            DeleteObject(state.dibBitmap);
            state.dibBitmap = nullptr;
            state.dibBits = nullptr;
        }
        if (state.dibDc) {
            DeleteDC(state.dibDc);
            state.dibDc = nullptr;
        }
        state.dibW = 0;
        state.dibH = 0;
    }

    static void destroyFbo() {
        auto& state = ebHideFromStreamState;
        if (!state.glReady)
            return;
        if (state.fbo) {
            state.deleteFramebuffers(1, &state.fbo);
            state.fbo = 0;
        }
        if (state.colorTex) {
            glDeleteTextures(1, &state.colorTex);
            state.colorTex = 0;
        }
        state.fboW = 0;
        state.fboH = 0;
    }

    static void destroyOverlay() {
        auto& state = ebHideFromStreamState;
        destroyDib();
        destroyFbo();
        if (state.overlay) {
            DestroyWindow(state.overlay);
            state.overlay = nullptr;
        }
        state.host = nullptr;
        state.affinityOk = false;
        state.captureSafeDraw = false;
        state.pendingDestroy = false;
    }

    static void destroyOverlayIfIdle() {
        auto& state = ebHideFromStreamState;
        if (state.captureSafeDraw) {
            state.pendingDestroy = true;
            return;
        }
        destroyOverlay();
    }

    static bool ensureOverlay(HWND host) {
        auto& state = ebHideFromStreamState;
        if (!host)
            return false;
        if (!ensureClass())
            return false;
        if (state.overlay && state.host == host && IsWindow(state.overlay))
            return true;
        destroyOverlay();

        HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                      L"EarlyBufferHideFromStream", L"EarlyBuffer", WS_POPUP, 0, 0,
                                      1, 1, host, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) {
            geode::log::warn("[EarlyBuffer] Hide From Stream: couldn't create overlay window ({})",
                             GetLastError());
            return false;
        }
        if (!SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE)) {
            geode::log::warn(
                "[EarlyBuffer] Hide From Stream needs Windows 10 (2004)+ for capture exclusion ({})",
                GetLastError());
            DestroyWindow(window);
            return false;
        }
        LONG ex = GetWindowLongW(window, GWL_EXSTYLE);
        SetWindowLongW(window, GWL_EXSTYLE,
                       ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        state.overlay = window;
        state.host = host;
        state.affinityOk = true;
        geode::log::info("[EarlyBuffer] Hide From Stream: on (desktop GUI)");
        return true;
    }

    static bool ensureDib(int width, int height) {
        auto& state = ebHideFromStreamState;
        if (state.dibDc && state.dibBitmap && state.dibBits && state.dibW == width &&
            state.dibH == height)
            return true;
        destroyDib();
        state.dibDc = CreateCompatibleDC(nullptr);
        if (!state.dibDc)
            return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        state.dibBitmap =
            CreateDIBSection(state.dibDc, &info, DIB_RGB_COLORS, &state.dibBits, nullptr, 0);
        if (!state.dibBitmap || !state.dibBits) {
            destroyDib();
            return false;
        }
        SelectObject(state.dibDc, state.dibBitmap);
        state.dibW = width;
        state.dibH = height;
        return true;
    }

    static bool ensureFbo(int width, int height) {
        auto& state = ebHideFromStreamState;
        if (!loadGl())
            return false;
        if (state.fbo && state.colorTex && state.fboW == width && state.fboH == height)
            return true;
        destroyFbo();
        glGenTextures(1, &state.colorTex);
        glBindTexture(GL_TEXTURE_2D, state.colorTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        state.genFramebuffers(1, &state.fbo);
        state.bindFramebuffer(GL_FRAMEBUFFER, state.fbo);
        state.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   state.colorTex, 0);
        GLenum const status = state.checkFramebufferStatus(GL_FRAMEBUFFER);
        state.bindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            destroyFbo();
            return false;
        }
        state.fboW = width;
        state.fboH = height;
        return true;
    }

    static void presentBits(HWND host, int width, int height) {
        auto& state = ebHideFromStreamState;
        if (!state.overlay || !state.dibBits || !state.dibDc)
            return;
        POINT screen{0, 0};
        ClientToScreen(host, &screen);
        SIZE size{width, height};
        POINT src{0, 0};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        POINT dst{screen.x, screen.y};
        if (UpdateLayeredWindow(state.overlay, nullptr, &dst, &size, state.dibDc, &src, 0, &blend,
                                ULW_ALPHA)) {
            ShowWindow(state.overlay, SW_SHOWNOACTIVATE);
        }
    }

    static void clearDib() {
        auto& state = ebHideFromStreamState;
        if (!state.dibBits || state.dibW <= 0 || state.dibH <= 0)
            return;
        std::memset(state.dibBits, 0,
                    static_cast<size_t>(state.dibW) * static_cast<size_t>(state.dibH) * 4u);
    }

    static void paintStatusGdi() {
        auto& state = ebHideFromStreamState;
        if (!state.statusOn || state.status.empty() || !state.dibDc)
            return;
        SetBkMode(state.dibDc, TRANSPARENT);
        SetTextColor(state.dibDc, RGB(235, 235, 245));
        RECT rect{12, 10, state.dibW - 12, state.dibH - 10};
        DrawTextA(state.dibDc, state.status.c_str(), -1, &rect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        auto* pixels = static_cast<std::uint32_t*>(state.dibBits);
        int const count = state.dibW * state.dibH;
        for (int i = 0; i < count; ++i) {
            std::uint32_t const p = pixels[i];
            if ((p & 0x00FFFFFFu) != 0)
                pixels[i] = p | 0xFF000000u;
        }
    }

    static void writePremulBgra(std::uint8_t* dst, unsigned b, unsigned g, unsigned r,
                                unsigned a) {
        if (a == 0) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        unsigned const aa = a >= 255 ? 255 : a;
        dst[0] = static_cast<std::uint8_t>((aa * b + 127) / 255);
        dst[1] = static_cast<std::uint8_t>((aa * g + 127) / 255);
        dst[2] = static_cast<std::uint8_t>((aa * r + 127) / 255);
        dst[3] = static_cast<std::uint8_t>(aa);
    }

    static bool scaleReadbackToDib(int srcW, int srcH, int dstW, int dstH) {
        auto& state = ebHideFromStreamState;
        if (!state.dibBits || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
            return false;
        state.readback.resize(static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, srcW, srcH, GL_BGRA, GL_UNSIGNED_BYTE, state.readback.data());
        auto* dst = static_cast<std::uint8_t*>(state.dibBits);
        for (int y = 0; y < dstH; ++y) {
            int const srcY =
                (dstH <= 1) ? (srcH - 1) : (srcH - 1 - (y * (srcH - 1)) / (dstH - 1));
            for (int x = 0; x < dstW; ++x) {
                int const srcX = (dstW <= 1) ? 0 : (x * (srcW - 1)) / (dstW - 1);
                std::uint8_t const* srcPx =
                    state.readback.data() +
                    (static_cast<size_t>(srcY) * static_cast<size_t>(srcW) +
                     static_cast<size_t>(srcX)) *
                        4u;
                std::uint8_t* dstPx =
                    dst + (static_cast<size_t>(y) * static_cast<size_t>(dstW) +
                           static_cast<size_t>(x)) *
                              4u;
                writePremulBgra(dstPx, srcPx[0], srcPx[1], srcPx[2], srcPx[3]);
            }
        }
        return true;
    }

    static bool imguiWantsPaint() {
        return ImGuiCocos::get().isInitialized() && ImGuiCocos::get().isVisible();
    }

    static bool wantsOverlay() {
        auto& state = ebHideFromStreamState;
        if (!state.settingOn)
            return false;
        return imguiWantsPaint() || (state.statusOn && !state.status.empty());
    }

    static void hideWindow() {
        auto& state = ebHideFromStreamState;
        if (state.overlay && IsWindow(state.overlay))
            ShowWindow(state.overlay, SW_HIDE);
    }

    static bool clientSize(HWND host, int& width, int& height) {
        RECT client{};
        if (!GetClientRect(host, &client))
            return false;
        width = std::max(1, static_cast<int>(client.right - client.left));
        height = std::max(1, static_cast<int>(client.bottom - client.top));
        return true;
    }

    static void presentStatusOnly() {
        auto& state = ebHideFromStreamState;
        HWND host = gameHwnd();
        if (!host)
            return;
        int width = 0;
        int height = 0;
        if (!clientSize(host, width, height) || !ensureOverlay(host) || !ensureDib(width, height)) {
            hideWindow();
            return;
        }
        POINT screen{0, 0};
        ClientToScreen(host, &screen);
        SetWindowPos(state.overlay, nullptr, screen.x, screen.y, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        clearDib();
        paintStatusGdi();
        presentBits(host, width, height);
    }

    // Draw ImGui into FBO + excluded layered window. Leaves default FB bound and
    // ImGuiCocos invisible so the following swapBuffers presents game only
    // (never SwapBuffers with an FBO bound — that broke Game Capture).
    static bool presentImGuiToOverlay() {
        auto& state = ebHideFromStreamState;
        HWND host = gameHwnd();
        if (!host || !loadGl())
            return false;

        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        int const fbW = viewport[2] > 0 ? viewport[2] : 1;
        int const fbH = viewport[3] > 0 ? viewport[3] : 1;
        int clientW = 0;
        int clientH = 0;
        if (!clientSize(host, clientW, clientH) || !ensureOverlay(host) ||
            !ensureDib(clientW, clientH) || !ensureFbo(fbW, fbH)) {
            hideWindow();
            return false;
        }

        POINT screen{0, 0};
        ClientToScreen(host, &screen);
        SetWindowPos(state.overlay, nullptr, screen.x, screen.y, clientW, clientH,
                     SWP_NOACTIVATE | SWP_NOZORDER);

        GLint prevViewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        GLboolean const prevScissor = glIsEnabled(GL_SCISSOR_TEST);
        GLfloat prevClear[4] = {};
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);

        state.bindFramebuffer(GL_FRAMEBUFFER, state.fbo);
        glViewport(0, 0, fbW, fbH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        applyCaptureSafeBlend();

        state.captureSafeDraw = true;
        ImGuiCocos::get().drawFrame();
        state.captureSafeDraw = false;

        // Disabling Hide From Stream mid-draw destroys the DIB via the setting
        // listener — never touch dibBits if that happened.
        bool const stillLive = state.settingOn && state.dibBits && state.overlay;
        if (stillLive)
            scaleReadbackToDib(fbW, fbH, clientW, clientH);
        if (stillLive && state.dibBits)
            presentBits(host, clientW, clientH);

        state.bindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
        if (prevScissor)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);

        if (state.pendingDestroy || !state.settingOn)
            destroyOverlayIfIdle();
        return stillLive;
    }
};

class $modify(EbHideFromStreamCCEGLView, cocos2d::CCEGLView) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::Early)) {
            geode::log::warn("[EarlyBuffer] Hide From Stream: swapBuffers Early priority failed");
        }
    }

    void swapBuffers() {
        auto& state = ebHideFromStreamState;
        if (state.pendingDestroy && !state.captureSafeDraw)
            EbHideFromStreamImpl::destroyOverlay();
        if (!state.settingOn || !EbHideFromStreamImpl::wantsOverlay()) {
            if (!state.settingOn)
                EbHideFromStreamImpl::destroyOverlayIfIdle();
            else
                EbHideFromStreamImpl::hideWindow();
            CCEGLView::swapBuffers();
            return;
        }

        if (!EbHideFromStreamImpl::imguiWantsPaint()) {
            EbHideFromStreamImpl::presentStatusOnly();
            CCEGLView::swapBuffers();
            return;
        }

        bool const wasVisible = ImGuiCocos::get().isVisible();
        if (!EbHideFromStreamImpl::presentImGuiToOverlay()) {
            CCEGLView::swapBuffers();
            return;
        }

        // Capture flags after drawFrame (hover/click targets). Saving them before
        // present wiped WantCaptureMouse and made the menu click-through.
        auto& io = ImGui::GetIO();
        bool const wantMouse = io.WantCaptureMouse;
        bool const wantKey = io.WantCaptureKeyboard;
        bool const wantText = io.WantTextInput;

        // Skip imgui-cocos' second drawFrame (game FB only). setVisible(false)
        // clears WantCapture — put the post-draw values back afterward.
        ImGuiCocos::get().setVisible(false);
        CCEGLView::swapBuffers();
        ImGuiCocos::get().setVisible(wasVisible);
        io.WantCaptureMouse = wantMouse;
        io.WantCaptureKeyboard = wantKey;
        io.WantTextInput = wantText;
    }
};

void EbHideFromStream::install() {
    syncFromSettings();
}

void EbHideFromStream::syncFromSettings() {
    auto* mod = geode::prelude::Mod::get();
    auto& state = ebHideFromStreamState;
    bool const on = mod && mod->getSettingValue<bool>("hide-from-stream");
    state.settingOn = on;
    if (!on) {
        // Never DestroyWindow/DIB during captureSafeDraw (checkbox click inside
        // drawFrame) — that nulled dibBits and crashed in scaleReadbackToDib.
        state.pendingDestroy = true;
        EbHideFromStreamImpl::destroyOverlayIfIdle();
    }
}

bool EbHideFromStream::settingEnabled() {
    return ebHideFromStreamState.settingOn;
}

bool EbHideFromStream::active() {
    auto& state = ebHideFromStreamState;
    return state.settingOn && state.affinityOk;
}

void EbHideFromStream::setStatusText(std::string text) {
    ebHideFromStreamState.status = std::move(text);
}

void EbHideFromStream::setStatusVisible(bool visible) {
    ebHideFromStreamState.statusOn = visible;
}

std::string const& EbHideFromStream::statusText() {
    return ebHideFromStreamState.status;
}

bool EbHideFromStream::statusVisible() {
    return ebHideFromStreamState.statusOn;
}

bool EbHideFromStream::captureSafeDrawActive() {
    return ebHideFromStreamState.captureSafeDraw;
}

#else

void EbHideFromStream::install() {}

void EbHideFromStream::syncFromSettings() {}

bool EbHideFromStream::settingEnabled() {
    return false;
}

bool EbHideFromStream::active() {
    return false;
}

void EbHideFromStream::setStatusText(std::string) {}

void EbHideFromStream::setStatusVisible(bool) {}

std::string const& EbHideFromStream::statusText() {
    static std::string empty;
    return empty;
}

bool EbHideFromStream::statusVisible() {
    return false;
}

bool EbHideFromStream::captureSafeDrawActive() {
    return false;
}

#endif
