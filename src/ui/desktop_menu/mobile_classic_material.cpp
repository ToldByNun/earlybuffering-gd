#include "mobile_classic_material.hpp"
#if defined(EB_DESKTOP_IMGUI) && defined(EB_UI_ANDROID)
#include "desktop_menu_theme.hpp"
#include <algorithm>
#include <cmath>
EbMobileClassicGlState::EbMobileClassicGlState() {
    glGetIntegerv(GL_VIEWPORT, m_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, m_scissor);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &m_active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_zeroTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_array);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &m_elements);
    glGetIntegerv(GL_BLEND_SRC_RGB, &m_srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &m_dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &m_dstAlpha);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, m_clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, m_mask);
    m_blend = glIsEnabled(GL_BLEND);
    m_scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    m_depth = glIsEnabled(GL_DEPTH_TEST);
    m_cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
EbMobileClassicGlState::~EbMobileClassicGlState() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    glScissor(m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]);
    glClearColor(m_clear[0], m_clear[1], m_clear[2], m_clear[3]);
    glColorMask(m_mask[0], m_mask[1], m_mask[2], m_mask[3]);
    // Keep Cocos' binding cache synchronized without globally invalidating
    // renderer/matrix state while CCNode::visit is on the stack.
    cocos2d::ccGLBlendFunc(m_srcRgb, m_dstRgb);
    glBlendFuncSeparate(m_srcRgb, m_dstRgb, m_srcAlpha, m_dstAlpha);
    auto restore = [](unsigned flag, bool enabled) {
        if (enabled)
            glEnable(flag);
        else
            glDisable(flag);
    };
    restore(GL_BLEND, m_blend);
    restore(GL_SCISSOR_TEST, m_scissorEnabled);
    restore(GL_DEPTH_TEST, m_depth);
    restore(GL_CULL_FACE, m_cull);
    cocos2d::ccGLBindTexture2DN(0, m_zeroTexture);
    cocos2d::ccGLBindTexture2DN(m_active - GL_TEXTURE0, m_texture);
    glActiveTexture(m_active);
    glBindBuffer(GL_ARRAY_BUFFER, m_array);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_elements);
    cocos2d::ccGLUseProgram(m_program);
}
EbMobileClassicMaterial& EbMobileClassicMaterial::get() {
    static EbMobileClassicMaterial value;
    return value;
}
bool EbMobileClassicMaterial::initialize() {
    if (m_attempted)
        return m_target && m_quad && m_program;
    m_attempted = true;
    auto* program = new cocos2d::CCGLProgram();
    char const* vertex = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying mediump vec2 v_uv;
void main() { gl_Position = CC_MVPMatrix * a_position; v_uv = a_texCoord; }
)";
    char const* fragment = R"(
precision mediump float;
varying mediump vec2 v_uv;
uniform float u_time;
uniform vec3 u_base;
uniform vec3 u_light;
uniform vec3 u_shape;
void main() {
    vec2 p = vec2(v_uv.x, 1.0-v_uv.y);
    vec2 uv = (2.0*p-1.0)*vec2(1.0,u_shape.y);
    float t = u_time;
    for (int i=1; i<5; ++i) {
        float f=float(i);
        uv.x += 0.6/f*cos(f*2.5*uv.y+t/18.0);
        uv.y += 0.6/f*cos(f*2.5*uv.x+t/18.0);
    }
    float wave=0.5+0.5*sin(t-uv.x-uv.y);
    vec2 pixel=p*vec2(128.0,128.0*u_shape.y);
    float r=u_shape.x;
    float alpha=1.0;
    if (pixel.x<r && (pixel.y<r || pixel.y>128.0*u_shape.y-r)) {
        vec2 center=vec2(r,pixel.y<r ? r : 128.0*u_shape.y-r);
        alpha=1.0-smoothstep(r-0.8,r+0.8,length(pixel-center));
    }
    gl_FragColor=vec4(mix(u_base,u_light,wave),alpha);
}
)";
    if (!program->initWithVertexShaderByteArray(vertex, fragment)) {
        program->release();
        geode::log::warn("[EarlyBuffer] Mobile Classic shader compilation failed; static fallback");
        return false;
    }
    program->addAttribute(kCCAttributeNamePosition, cocos2d::kCCVertexAttrib_Position);
    program->addAttribute(kCCAttributeNameTexCoord, cocos2d::kCCVertexAttrib_TexCoords);
    if (!program->link()) {
        program->release();
        return false;
    }
    program->updateUniforms();
    unsigned char const white[4] = {255, 255, 255, 255};
    auto* texture = new cocos2d::CCTexture2D();
    bool const loaded =
        texture->initWithData(white, cocos2d::kCCTexture2DPixelFormat_RGBA8888, 1, 1, {1, 1});
    auto* quad = loaded ? cocos2d::CCSprite::createWithTexture(texture) : nullptr;
    texture->release();
    auto* target =
        cocos2d::CCRenderTexture::create(128, 384, cocos2d::kCCTexture2DPixelFormat_RGBA8888);
    if (!quad || !target) {
        program->release();
        return false;
    }
    target->getSprite()->getTexture()->setAntiAliasTexParameters();
    target->retain();
    quad->retain();
    quad->setAnchorPoint({0, 0});
    auto const quadSize = quad->getContentSize();
    quad->setScaleX(128.f / std::max(quadSize.width, 0.001f));
    quad->setScaleY(384.f / std::max(quadSize.height, 0.001f));
    quad->setShaderProgram(program);
    quad->setBlendFunc({GL_ONE, GL_ZERO});
    m_program = program;
    m_quad = quad;
    m_target = target;
    m_time = program->getUniformLocationForName("u_time");
    m_base = program->getUniformLocationForName("u_base");
    m_light = program->getUniformLocationForName("u_light");
    m_shape = program->getUniformLocationForName("u_shape");
    return true;
}
bool EbMobileClassicMaterial::paint(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding,
                                    float time, ImDrawFlags corners, float alpha) {
    if (!list || max.x <= min.x || max.y <= min.y)
        return false;
    if (!m_attempted) {
        EbMobileClassicGlState restoreState;
        initialize();
    }
    if (!m_target || !m_quad || !m_program)
        return false;
    // One small persistent target, no scene capture, no per-frame mesh or allocation.
    auto const base = EbDesktopMenuTheme::sidebarWaveDark();
    auto const light = EbDesktopMenuTheme::sidebarWaveLight();
    float const radius =
        (corners & ImDrawFlags_RoundCornersLeft) ? rounding * 128.f / (max.x - min.x) : 0.f;
    auto sameColor = [](ImVec4 a, ImVec4 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    bool const changed = time != m_lastTime || radius != m_lastRadius ||
                         !sameColor(base, m_lastBase) || !sameColor(light, m_lastLight);
    if (m_frame != ImGui::GetFrameCount() && changed) {
        EbMobileClassicGlState restoreState;
        m_frame = ImGui::GetFrameCount();
        m_lastTime = time;
        m_lastRadius = radius;
        m_lastBase = base;
        m_lastLight = light;
        m_target->beginWithClear(0, 0, 0, 0);
        m_program->use();
        m_program->setUniformLocationWith1f(m_time, std::fmod(time, 1130.973355f));
        m_program->setUniformLocationWith3f(m_base, base.x, base.y, base.z);
        m_program->setUniformLocationWith3f(m_light, light.x, light.y, light.z);
        m_program->setUniformLocationWith3f(m_shape, radius, 3.f, 0.f);
        m_quad->visit();
        m_target->end();
    }
    auto* texture = m_target->getSprite()->getTexture();
    auto const content = texture->getContentSizeInPixels();
    float const u = content.width / texture->getPixelsWide();
    float const v = content.height / texture->getPixelsHigh();
    list->AddImage(static_cast<ImTextureID>(texture->getName()), min, max, {0, v}, {u, 0},
                   IM_COL32(255, 255, 255,
                            static_cast<int>(255.f * EbDesktopMenuTheme::styleAlpha() *
                                             std::clamp(alpha, 0.f, 1.f))));
    return true;
}
#endif
