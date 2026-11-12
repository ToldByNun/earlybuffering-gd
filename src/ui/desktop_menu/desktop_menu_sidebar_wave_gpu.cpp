#include "desktop_menu_sidebar_wave_gpu.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_theme.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <imgui.h>

// GPU wave via offscreen FBO → ImGui AddImage.
// Must NOT use ImDrawCallback: gd-imgui-cocos calls every UserCallback as a
// function pointer (ResetRenderState == -1 crashes) and custom GL mid-batch
// corrupts the ImGui VAO so later widgets vanish.

class EbDesktopMenuSidebarWaveGpuState {
public:
    static bool& tried() {
        static bool value = false;
        return value;
    }

    static bool& available() {
        static bool value = false;
        return value;
    }

    static GLuint& program() {
        static GLuint value = 0;
        return value;
    }

    static GLuint& vbo() {
        static GLuint value = 0;
        return value;
    }

    static GLuint& vao() {
        static GLuint value = 0;
        return value;
    }

    static GLuint& fbo() {
        static GLuint value = 0;
        return value;
    }

    static GLuint& colorTex() {
        static GLuint value = 0;
        return value;
    }

    static int& texW() {
        static int value = 0;
        return value;
    }

    static int& texH() {
        static int value = 0;
        return value;
    }

    static GLint& locPosition() {
        static GLint value = -1;
        return value;
    }

    static GLint& locTime() {
        static GLint value = -1;
        return value;
    }

    static GLint& locResolution() {
        static GLint value = -1;
        return value;
    }

    static GLint& locBase() {
        static GLint value = -1;
        return value;
    }

    static GLint& locLight() {
        static GLint value = -1;
        return value;
    }

    static GLint& locRounding() {
        static GLint value = -1;
        return value;
    }

    static GLint& locCorners() {
        static GLint value = -1;
        return value;
    }

private:
    EbDesktopMenuSidebarWaveGpuState() = delete;
};

class EbDesktopMenuSidebarWaveGpuGl {
public:
    static char const* vertexSource() {
        return R"GLSL(
attribute vec2 a_position;
varying vec2 v_uv;
void main() {
    v_uv = a_position * 0.5 + 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)GLSL";
    }

    static char const* fragmentSource() {
        // v_uv: bottom-left origin from NDC. Flip Y to match ImGui top-left sampling.
        return R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBaseColor;
uniform vec3 uLightColor;
uniform float uRounding;
uniform vec4 uCorners;
varying vec2 v_uv;

void main() {
    vec2 local = vec2(v_uv.x, 1.0 - v_uv.y) * uResolution;
    float r = uRounding;
    if (r > 0.5) {
        vec2 tl = local - vec2(r, r);
        vec2 tr = local - vec2(uResolution.x - r, r);
        vec2 br = local - vec2(uResolution.x - r, uResolution.y - r);
        vec2 bl = local - vec2(r, uResolution.y - r);
        if (uCorners.x > 0.5 && local.x < r && local.y < r && dot(tl, tl) > r * r) {
            discard;
        }
        if (uCorners.y > 0.5 && local.x > uResolution.x - r && local.y < r &&
            dot(tr, tr) > r * r) {
            discard;
        }
        if (uCorners.z > 0.5 && local.x > uResolution.x - r &&
            local.y > uResolution.y - r && dot(br, br) > r * r) {
            discard;
        }
        if (uCorners.w > 0.5 && local.x < r && local.y > uResolution.y - r &&
            dot(bl, bl) > r * r) {
            discard;
        }
    }

    vec2 fragCoord = local;
    vec2 uv = (2.0 * fragCoord - uResolution.xy) / min(uResolution.x, uResolution.y);
    for (float i = 1.0; i < 6.0; i++) {
        uv.x += 0.6 / i * cos(i * 2.5 * uv.y + (uTime / 18.0));
        uv.y += 0.6 / i * cos(i * 2.5 * uv.x + (uTime / 18.0));
    }
    float wave = 0.5 + 0.5 * sin(uTime - uv.y - uv.x);
    vec3 finalColor = mix(uBaseColor, uLightColor, wave);
    gl_FragColor = vec4(finalColor, 1.0);
}
)GLSL";
    }

    static bool compileShader(GLenum type, char const* source, GLuint* outShader) {
        if (!outShader || !source) {
            return false;
        }
        GLuint shader = glCreateShader(type);
        if (shader == 0) {
            return false;
        }
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char logBuf[512];
            GLsizei len = 0;
            glGetShaderInfoLog(shader, 512, &len, logBuf);
            geode::log::warn("[EarlyBuffer] sidebar wave shader compile failed: {}",
                             len > 0 ? logBuf : "(no log)");
            glDeleteShader(shader);
            return false;
        }
        *outShader = shader;
        return true;
    }

    static bool linkProgram(GLuint vert, GLuint frag, GLuint* outProgram) {
        if (!outProgram) {
            return false;
        }
        GLuint program = glCreateProgram();
        if (program == 0) {
            return false;
        }
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glBindAttribLocation(program, 0, "a_position");
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char logBuf[512];
            GLsizei len = 0;
            glGetProgramInfoLog(program, 512, &len, logBuf);
            geode::log::warn("[EarlyBuffer] sidebar wave program link failed: {}",
                             len > 0 ? logBuf : "(no log)");
            glDeleteProgram(program);
            return false;
        }
        *outProgram = program;
        return true;
    }

    static bool ensureTextureSize(int width, int height) {
        if (width < 1) {
            width = 1;
        }
        if (height < 1) {
            height = 1;
        }
        if (width > 2048) {
            width = 2048;
        }
        if (height > 2048) {
            height = 2048;
        }
        if (EbDesktopMenuSidebarWaveGpuState::colorTex() != 0 &&
            EbDesktopMenuSidebarWaveGpuState::texW() == width &&
            EbDesktopMenuSidebarWaveGpuState::texH() == height) {
            return true;
        }

        if (EbDesktopMenuSidebarWaveGpuState::colorTex() != 0) {
            GLuint tex = EbDesktopMenuSidebarWaveGpuState::colorTex();
            glDeleteTextures(1, &tex);
            EbDesktopMenuSidebarWaveGpuState::colorTex() = 0;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, EbDesktopMenuSidebarWaveGpuState::fbo());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            geode::log::warn("[EarlyBuffer] sidebar wave FBO incomplete ({})",
                             static_cast<int>(status));
            glDeleteTextures(1, &tex);
            return false;
        }

        EbDesktopMenuSidebarWaveGpuState::colorTex() = tex;
        EbDesktopMenuSidebarWaveGpuState::texW() = width;
        EbDesktopMenuSidebarWaveGpuState::texH() = height;
        return true;
    }

    static bool init() {
        GLuint vert = 0;
        GLuint frag = 0;
        if (!compileShader(GL_VERTEX_SHADER, vertexSource(), &vert)) {
            return false;
        }
        if (!compileShader(GL_FRAGMENT_SHADER, fragmentSource(), &frag)) {
            glDeleteShader(vert);
            return false;
        }
        GLuint program = 0;
        bool const linked = linkProgram(vert, frag, &program);
        glDeleteShader(vert);
        glDeleteShader(frag);
        if (!linked) {
            return false;
        }

        EbDesktopMenuSidebarWaveGpuState::program() = program;
        EbDesktopMenuSidebarWaveGpuState::locPosition() = glGetAttribLocation(program, "a_position");
        EbDesktopMenuSidebarWaveGpuState::locTime() = glGetUniformLocation(program, "uTime");
        EbDesktopMenuSidebarWaveGpuState::locResolution() =
            glGetUniformLocation(program, "uResolution");
        EbDesktopMenuSidebarWaveGpuState::locBase() = glGetUniformLocation(program, "uBaseColor");
        EbDesktopMenuSidebarWaveGpuState::locLight() = glGetUniformLocation(program, "uLightColor");
        EbDesktopMenuSidebarWaveGpuState::locRounding() =
            glGetUniformLocation(program, "uRounding");
        EbDesktopMenuSidebarWaveGpuState::locCorners() = glGetUniformLocation(program, "uCorners");

        float const quad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        GLuint vbo = 0;
        GLuint vao = 0;
        glGenBuffers(1, &vbo);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        GLint const posLoc = EbDesktopMenuSidebarWaveGpuState::locPosition();
        if (posLoc >= 0) {
            glEnableVertexAttribArray(static_cast<GLuint>(posLoc));
            glVertexAttribPointer(static_cast<GLuint>(posLoc), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        EbDesktopMenuSidebarWaveGpuState::vbo() = vbo;
        EbDesktopMenuSidebarWaveGpuState::vao() = vao;

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        EbDesktopMenuSidebarWaveGpuState::fbo() = fbo;
        if (!ensureTextureSize(196, 600)) {
            return false;
        }

        geode::log::info("[EarlyBuffer] sidebar wave GPU FBO ready");
        return true;
    }

    static bool renderToTexture(EbDesktopMenuSidebarWaveGpu::PaintRequest const& request) {
        int const width = static_cast<int>(request.max.x - request.min.x + 0.5f);
        int const height = static_cast<int>(request.max.y - request.min.y + 0.5f);
        if (width < 1 || height < 1) {
            return false;
        }
        if (!ensureTextureSize(width, height)) {
            return false;
        }

        GLint lastFbo = 0;
        GLint lastVao = 0;
        GLint lastProgram = 0;
        GLint lastViewport[4] = {0, 0, 0, 0};
        GLint lastActiveTexture = 0;
        GLint lastTexture = 0;
        GLboolean lastEnableBlend = GL_FALSE;
        GLboolean lastEnableCull = GL_FALSE;
        GLboolean lastEnableDepth = GL_FALSE;
        GLboolean lastEnableScissor = GL_FALSE;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &lastFbo);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVao);
        glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
        glGetIntegerv(GL_VIEWPORT, lastViewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &lastActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
        lastEnableBlend = glIsEnabled(GL_BLEND);
        lastEnableCull = glIsEnabled(GL_CULL_FACE);
        lastEnableDepth = glIsEnabled(GL_DEPTH_TEST);
        lastEnableScissor = glIsEnabled(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, EbDesktopMenuSidebarWaveGpuState::fbo());
        glViewport(0, 0, EbDesktopMenuSidebarWaveGpuState::texW(),
                   EbDesktopMenuSidebarWaveGpuState::texH());
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLuint const program = EbDesktopMenuSidebarWaveGpuState::program();
        glUseProgram(program);

        float const resW = static_cast<float>(EbDesktopMenuSidebarWaveGpuState::texW());
        float const resH = static_cast<float>(EbDesktopMenuSidebarWaveGpuState::texH());
        float const maxRounding = (resW < resH ? resW : resH) * 0.5f;
        float const rounding = request.rounding > maxRounding ? maxRounding : request.rounding;
        float const corners[4] = {
            (request.cornerFlags & ImDrawFlags_RoundCornersTopLeft) ? 1.0f : 0.0f,
            (request.cornerFlags & ImDrawFlags_RoundCornersTopRight) ? 1.0f : 0.0f,
            (request.cornerFlags & ImDrawFlags_RoundCornersBottomRight) ? 1.0f : 0.0f,
            (request.cornerFlags & ImDrawFlags_RoundCornersBottomLeft) ? 1.0f : 0.0f,
        };

        if (EbDesktopMenuSidebarWaveGpuState::locTime() >= 0) {
            glUniform1f(EbDesktopMenuSidebarWaveGpuState::locTime(), request.timeSeconds);
        }
        if (EbDesktopMenuSidebarWaveGpuState::locResolution() >= 0) {
            glUniform2f(EbDesktopMenuSidebarWaveGpuState::locResolution(), resW, resH);
        }
        if (EbDesktopMenuSidebarWaveGpuState::locBase() >= 0) {
            glUniform3f(EbDesktopMenuSidebarWaveGpuState::locBase(), request.baseColor.x,
                        request.baseColor.y, request.baseColor.z);
        }
        if (EbDesktopMenuSidebarWaveGpuState::locLight() >= 0) {
            glUniform3f(EbDesktopMenuSidebarWaveGpuState::locLight(), request.lightColor.x,
                        request.lightColor.y, request.lightColor.z);
        }
        if (EbDesktopMenuSidebarWaveGpuState::locRounding() >= 0) {
            glUniform1f(EbDesktopMenuSidebarWaveGpuState::locRounding(), rounding);
        }
        if (EbDesktopMenuSidebarWaveGpuState::locCorners() >= 0) {
            glUniform4f(EbDesktopMenuSidebarWaveGpuState::locCorners(), corners[0], corners[1],
                        corners[2], corners[3]);
        }

        glBindVertexArray(EbDesktopMenuSidebarWaveGpuState::vao());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(lastFbo));
        glBindVertexArray(static_cast<GLuint>(lastVao));
        glUseProgram(static_cast<GLuint>(lastProgram));
        glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        glActiveTexture(static_cast<GLenum>(lastActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
        if (lastEnableBlend) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        if (lastEnableCull) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        if (lastEnableDepth) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        if (lastEnableScissor) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        return true;
    }

    static ImTextureID toImGuiTexture(GLuint tex) {
#if IMGUI_VERSION_NUM >= 19140
        return static_cast<ImTextureID>(tex);
#else
        return reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(tex));
#endif
    }

private:
    EbDesktopMenuSidebarWaveGpuGl() = delete;
};

bool EbDesktopMenuSidebarWaveGpu::ensureReady() {
    if (EbDesktopMenuSidebarWaveGpuState::tried()) {
        return EbDesktopMenuSidebarWaveGpuState::available();
    }
    EbDesktopMenuSidebarWaveGpuState::tried() = true;
    EbDesktopMenuSidebarWaveGpuState::available() = EbDesktopMenuSidebarWaveGpuGl::init();
    if (!EbDesktopMenuSidebarWaveGpuState::available()) {
        geode::log::warn("[EarlyBuffer] sidebar wave GPU unavailable — using CPU fill");
    }
    return EbDesktopMenuSidebarWaveGpuState::available();
}

bool EbDesktopMenuSidebarWaveGpu::isAvailable() {
    return EbDesktopMenuSidebarWaveGpuState::available();
}

void EbDesktopMenuSidebarWaveGpu::paint(ImDrawList* drawList, PaintRequest const& request) {
    if (!drawList || !ensureReady()) {
        return;
    }
    if (!EbDesktopMenuSidebarWaveGpuGl::renderToTexture(request)) {
        return;
    }
    ImTextureID const tex =
        EbDesktopMenuSidebarWaveGpuGl::toImGuiTexture(EbDesktopMenuSidebarWaveGpuState::colorTex());
    // Discarded corners stay alpha-0 from the FBO clear — AddImage blends them out.
    // Tint alpha follows Style.Alpha so the GPU wave fades with shell open/close.
    float const fade = EbDesktopMenuTheme::styleAlpha();
    int const a = static_cast<int>(fade * 255.0f + 0.5f);
    drawList->AddImage(tex, request.min, request.max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                       IM_COL32(255, 255, 255, a < 0 ? 0 : (a > 255 ? 255 : a)));
}

#endif
