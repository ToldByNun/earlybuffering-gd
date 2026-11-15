#include "desktop_menu_sidebar_wave_gpu.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_background.hpp"
#include "desktop_menu_bind.hpp"
#include "desktop_menu_theme.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <cmath>
#include <imgui.h>

// macOS Cocos pulls OpenGL/gl.h + glext (VAO via APPLE), not gl3.h — so
// GL_VERTEX_ARRAY_BINDING is missing while glBindVertexArray*APPLE exists.
#if !defined(GL_VERTEX_ARRAY_BINDING)
#if defined(GL_VERTEX_ARRAY_BINDING_APPLE)
#define GL_VERTEX_ARRAY_BINDING GL_VERTEX_ARRAY_BINDING_APPLE
#else
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#endif

// GPU wave via offscreen FBO → ImGui AddImage.
// Must NOT use ImDrawCallback: gd-imgui-cocos calls every UserCallback as a
// function pointer (ResetRenderState == -1 crashes) and custom GL mid-batch
// corrupts the ImGui VAO so later widgets vanish.

class EbDesktopMenuSidebarWaveGpuState {
public:
    static int& targetSlot() {
        static int value = 0;
        return value;
    }

    static GLuint& backgroundTexture() {
        static GLuint value = 0;
        return value;
    }
    static int& backgroundFrame() {
        static int value = -1;
        return value;
    }

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
        static GLuint values[2] = {0, 0};
        return values[targetSlot()];
    }

    static int& texW() {
        static int values[2] = {0, 0};
        return values[targetSlot()];
    }

    static int& texH() {
        static int values[2] = {0, 0};
        return values[targetSlot()];
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
        // FBO coordinates start at the bottom. Local material coordinates start
        // at the top; the final ImGui image flips texture UVs exactly once.
        return R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif

uniform vec4 uBackdropDim;
uniform sampler2D uBackdrop;
uniform sampler2D uBackground;
uniform float uUseBackground;
uniform sampler2D uHorizontal;
uniform float uPass;
uniform vec2 uTargetSize;
uniform float uPadding;
uniform vec4 uCapture;
uniform vec2 uFramebuffer;
uniform float uGlass;
uniform float uRefraction;
uniform float uHighlight;
uniform float uEdgeWidth;
uniform float uBlur;
uniform float uClassic;
uniform float uRadius;
uniform float uSamples;
uniform float uTint;
uniform float uOpacity;
uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBaseColor;
uniform vec3 uLightColor;
uniform float uRounding;
uniform vec4 uCorners;
varying vec2 v_uv;

// Distance-field refraction inspired by OverShifted/LiquidGlass,
// upstream 3797fa541c1f026c521a75885ee271f03bdf9f0f (MIT).
float glassDistance(vec2 local) {
    float radius = local.x < uResolution.x*0.5 ? min(uRounding,uResolution.x*0.5) : 0.0;
    vec2 q = abs(local-uResolution*0.5)-(uResolution*0.5-vec2(radius));
    return length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-radius;
}
vec3 material(vec2 local) {
    vec3 backdropColor = vec3(0.0);
    if(uClassic < 0.9999) {
        vec2 screen = uCapture.xy + vec2(local.x, uResolution.y-local.y)*uCapture.zw;
        vec2 screenUv=clamp(screen/uFramebuffer,vec2(0.0),vec2(1.0));
        backdropColor = uUseBackground > 0.5 ? texture2D(uBackground,screenUv).rgb :
            mix(texture2D(uBackdrop,screenUv).rgb,uBackdropDim.rgb,uBackdropDim.a);
    }
    vec2 uv = (2.0*local-uResolution)/min(uResolution.x,uResolution.y);
    for(float i=1.0;i<6.0;i++) {
        uv.x += 0.6/i*cos(i*2.5*uv.y+uTime/18.0);
        uv.y += 0.6/i*cos(i*2.5*uv.x+uTime/18.0);
    }
    return mix(backdropColor,mix(uBaseColor,uLightColor,0.5+0.5*sin(uTime-uv.y-uv.x)),uClassic);
}
void main() {
    vec2 local = vec2(v_uv.x, 1.0 - v_uv.y) * (uPass < 1.5 && uPass > 0.5 ? uTargetSize : uResolution);
    if(uPass > 0.5 && uPass < 1.5) local -= vec2(uPadding);
    float r = uRounding;
    if (r > 0.5 && uPass != 1.0) {
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

    float edgeLight = 0.0;
    if(uGlass > 0.5 && uPass != 1.0) {
        float distance = glassDistance(local);
        vec2 gradient = vec2(glassDistance(local+vec2(0.5,0.0))-glassDistance(local-vec2(0.5,0.0)),
                             glassDistance(local+vec2(0.0,0.5))-glassDistance(local-vec2(0.0,0.5)));
        vec2 normal = gradient/max(length(gradient),0.0001);
        float edge = exp(-max(-distance,0.0)/max(uEdgeWidth,1.0));
        local -= normal*uRefraction*edge;
        edgeLight = edge*uHighlight*dot(normal,normalize(vec2(-0.6,-0.8)));
    }
    vec3 color = material(local);
    if(uBlur > 0.5 && uRadius > 0.0) {
        vec3 sum = vec3(0.0);
        float total = 0.0;
        for(int i=-32;i<=32;i++) {
            if(abs(float(i)) > uSamples) continue;
            float offset = float(i)*uRadius/uSamples;
            float sigma=uSamples/3.0;
            float weight = exp(-0.5*float(i*i)/(sigma*sigma));
            vec3 sampleColor;
            if(uPass < 1.5) sampleColor = material(local+vec2(offset,0.0));
            else {
                vec2 uv = (local+vec2(uPadding)+vec2(0.0,offset))/uTargetSize;
                sampleColor = texture2D(uHorizontal,vec2(uv.x,1.0-uv.y)).rgb;
            }
            sum += sampleColor*weight;
            total += weight;
        }
        vec3 blurred = sum/total;
        color = uPass < 1.5 ? blurred : mix(color,blurred,uOpacity);

    }
    if(uPass > 1.5) color = mix(color,uBaseColor,uTint*(1.0-uClassic));
    if(uGlass > 0.5 && uPass != 1.0) color = clamp(color*(1.0+edgeLight)+vec3(max(edgeLight,0.0)*0.15),0.0,1.0);
    gl_FragColor = vec4(color,1.0);
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
        EbDesktopMenuSidebarWaveGpuState::locPosition() =
            glGetAttribLocation(program, "a_position");
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

    static bool renderToTexture(EbDesktopMenuSidebarWaveGpu::PaintRequest request) {
        EbDesktopMenuSidebarWaveGpuState::targetSlot() = request.background ? 1 : 0;
        auto quality = EbDesktopMenuBind::getString("ui-render-quality", "High");
        request.renderScale *= quality == "Low" ? 0.5f : quality == "Balanced" ? 0.75f : 1.f;
        int const width = static_cast<int>(request.max.x - request.min.x + 0.5f);
        int const height = static_cast<int>(request.max.y - request.min.y + 0.5f);
        if (width < 1 || height < 1) {
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

        bool const targetReady = ensureTextureSize(std::max(1, int(width * request.renderScale)),
                                                   std::max(1, int(height * request.renderScale)));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(lastFbo));
        glActiveTexture(static_cast<GLenum>(lastActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
        if (!targetReady)
            return false;

        GLfloat clearColor[4];
        GLboolean colorMask[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // Capture before ImGui submission: foreground UI cannot enter this source.
        static GLuint backdrop = 0;
        glActiveTexture(GL_TEXTURE0);
        GLint unitZeroTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &unitZeroTexture);
        if (!backdrop)
            glGenTextures(1, &backdrop);
        glBindTexture(GL_TEXTURE_2D, backdrop);
        if (request.blur && request.classic < 0.9999) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            static int captureWidth = 0, captureHeight = 0;
            if (captureWidth != lastViewport[2] || captureHeight != lastViewport[3]) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, lastViewport[2], lastViewport[3], 0, GL_RGB,
                             GL_UNSIGNED_BYTE, nullptr);
                captureWidth = lastViewport[2];
                captureHeight = lastViewport[3];
            }
            static int capturedFrame = -1;
            if (capturedFrame != ImGui::GetFrameCount()) {
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, lastViewport[0], lastViewport[1],
                                    lastViewport[2], lastViewport[3]);
                capturedFrame = ImGui::GetFrameCount();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, EbDesktopMenuSidebarWaveGpuState::fbo());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               EbDesktopMenuSidebarWaveGpuState::colorTex(), 0);
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
        auto const dim = EbDesktopMenuBackground::dim(EbDesktopMenuTheme::styleAlpha());
        glUniform4f(glGetUniformLocation(program, "uBackdropDim"), dim.x, dim.y, dim.z, dim.w);
        glUniform1i(glGetUniformLocation(program, "uBackdrop"), 0);
        glActiveTexture(GL_TEXTURE2);
        GLint unitTwoTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &unitTwoTexture);
        bool const useBackground =
            !request.background &&
            EbDesktopMenuSidebarWaveGpuState::backgroundFrame() == ImGui::GetFrameCount();
        glBindTexture(GL_TEXTURE_2D, EbDesktopMenuSidebarWaveGpuState::backgroundTexture());
        glUniform1i(glGetUniformLocation(program, "uBackground"), 2);
        glUniform1f(glGetUniformLocation(program, "uUseBackground"), useBackground);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(glGetUniformLocation(program, "uGlass"), request.glass);
        glUniform1f(glGetUniformLocation(program, "uRefraction"), request.refraction);
        glUniform1f(glGetUniformLocation(program, "uHighlight"), request.highlight);
        glUniform1f(glGetUniformLocation(program, "uEdgeWidth"), request.edgeWidth);
        glUniform1f(glGetUniformLocation(program, "uBlur"), request.blur);
        glUniform1f(glGetUniformLocation(program, "uClassic"), request.classic);
        glUniform1f(glGetUniformLocation(program, "uSamples"), quality == "Low"        ? 8.f
                                                               : quality == "Balanced" ? 16.f
                                                                                       : 32.f);
        glUniform1f(glGetUniformLocation(program, "uRadius"), request.blurRadius);
        glUniform1f(glGetUniformLocation(program, "uOpacity"), request.opacity);
        glUniform1f(glGetUniformLocation(program, "uTint"), request.tint);
        auto const& io = ImGui::GetIO();
        float sx = lastViewport[2] / io.DisplaySize.x, sy = lastViewport[3] / io.DisplaySize.y;
        glUniform4f(glGetUniformLocation(program, "uCapture"), request.min.x * sx,
                    lastViewport[3] - request.max.y * sy, sx, sy);
        glUniform2f(glGetUniformLocation(program, "uFramebuffer"), float(lastViewport[2]),
                    float(lastViewport[3]));

        float const resW = float(width);
        float const resH = float(height);
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

        GLint unitOneTexture = 0;
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &unitOneTexture);
        static GLuint textures[2] = {0, 0}, fbos[2] = {0, 0};
        static int widths[2] = {0, 0}, heights[2] = {0, 0};
        int const slot = request.background ? 1 : 0;
        auto& horizontal = textures[slot];
        auto& horizontalFbo = fbos[slot];
        auto& horizontalWidth = widths[slot];
        auto& horizontalHeight = heights[slot];
        int padding =
            int(std::ceil(request.blurRadius + (request.glass ? request.refraction : 0.f))) + 2;
        int paddedLogicalWidth = width + padding * 2, paddedLogicalHeight = height + padding * 2;
        int paddedWidth = std::max(1, int(paddedLogicalWidth * request.renderScale)),
            paddedHeight = std::max(1, int(paddedLogicalHeight * request.renderScale));
        glBindVertexArray(EbDesktopMenuSidebarWaveGpuState::vao());
        glUniform1f(glGetUniformLocation(program, "uPass"), 0);
        if (request.blur) {
            if (!horizontal)
                glGenTextures(1, &horizontal);
            if (!horizontalFbo)
                glGenFramebuffers(1, &horizontalFbo);
            glBindTexture(GL_TEXTURE_2D, horizontal);
            if (horizontalWidth != paddedWidth || horizontalHeight != paddedHeight) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, paddedWidth, paddedHeight, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, nullptr);
                horizontalWidth = paddedWidth;
                horizontalHeight = paddedHeight;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, horizontalFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, horizontal,
                                   0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glUniform2f(glGetUniformLocation(program, "uTargetSize"), float(paddedLogicalWidth),
                            float(paddedLogicalHeight));
                glUniform1f(glGetUniformLocation(program, "uPadding"), float(padding));
                glUniform1f(glGetUniformLocation(program, "uPass"), 1);
                glViewport(0, 0, paddedWidth, paddedHeight);
                glActiveTexture(GL_TEXTURE0);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glUniform1i(glGetUniformLocation(program, "uHorizontal"), 1);
                glUniform1f(glGetUniformLocation(program, "uPass"), 2);
            } else {
                glUniform1f(glGetUniformLocation(program, "uBlur"), 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, EbDesktopMenuSidebarWaveGpuState::fbo());
            glViewport(0, 0, EbDesktopMenuSidebarWaveGpuState::texW(),
                       EbDesktopMenuSidebarWaveGpuState::texH());
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (request.background) {
            auto& texture = EbDesktopMenuSidebarWaveGpuState::backgroundTexture();
            if (!texture)
                glGenTextures(1, &texture);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, texture);
            static int oldWidth = 0, oldHeight = 0;
            int w = EbDesktopMenuSidebarWaveGpuState::texW(),
                h = EbDesktopMenuSidebarWaveGpuState::texH();
            if (w != oldWidth || h != oldHeight) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                oldWidth = w;
                oldHeight = h;
            }
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
            EbDesktopMenuSidebarWaveGpuState::backgroundFrame() = ImGui::GetFrameCount();
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitTwoTexture));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitOneTexture));

        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(lastFbo));
        glBindVertexArray(static_cast<GLuint>(lastVao));
        glUseProgram(static_cast<GLuint>(lastProgram));
        glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitZeroTexture));
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
    GLint fbo = 0, vao = 0, buffer = 0, texture = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    EbDesktopMenuSidebarWaveGpuState::available() = EbDesktopMenuSidebarWaveGpuGl::init();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
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
    ImTextureID const tex = EbDesktopMenuSidebarWaveGpuGl::toImGuiTexture(
        request.background ? EbDesktopMenuSidebarWaveGpuState::backgroundTexture()
                           : EbDesktopMenuSidebarWaveGpuState::colorTex());
    // Discarded corners stay alpha-0 from the FBO clear — AddImage blends them out.
    // Tint alpha follows Style.Alpha so the GPU wave fades with shell open/close.
    float const fade = EbDesktopMenuTheme::styleAlpha() * request.alpha;
    int const a = static_cast<int>(fade * 255.0f + 0.5f);
    drawList->AddImage(tex, request.min, request.max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
                       IM_COL32(255, 255, 255, a < 0 ? 0 : (a > 255 ? 255 : a)));
}

#endif
