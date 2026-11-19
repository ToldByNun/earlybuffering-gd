#ifndef MOBILE_CLASSIC_MATERIAL_HPP
#define MOBILE_CLASSIC_MATERIAL_HPP
#if defined(EB_DESKTOP_IMGUI) && defined(EB_UI_ANDROID)
#include <Geode/Geode.hpp>
#include <imgui.h>
class EbMobileClassicGlState {
public:
    EbMobileClassicGlState();
    ~EbMobileClassicGlState();

private:
    int m_viewport[4]{}, m_scissor[4]{}, m_fbo = 0, m_program = 0;
    int m_active = 0, m_texture = 0, m_zeroTexture = 0, m_array = 0, m_elements = 0;
    int m_srcRgb = 0, m_dstRgb = 0, m_srcAlpha = 0, m_dstAlpha = 0;
    float m_clear[4]{};
    unsigned char m_mask[4]{};
    bool m_blend = false, m_scissorEnabled = false, m_depth = false, m_cull = false;
};
class EbMobileClassicMaterial {
public:
    static EbMobileClassicMaterial& get();
    bool paint(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding, float time,
               ImDrawFlags corners, float alpha = 1.f);

private:
    bool initialize();
    cocos2d::CCRenderTexture* m_target = nullptr;
    cocos2d::CCSprite* m_quad = nullptr;
    cocos2d::CCGLProgram* m_program = nullptr;
    bool m_attempted = false;
    int m_time = -1, m_base = -1, m_light = -1, m_shape = -1;
    int m_frame = -1;
    float m_lastTime = -1.f;
    ImVec4 m_lastBase{}, m_lastLight{};
    float m_lastRadius = -1.f;
};
#endif
#endif
