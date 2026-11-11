#include "desktop_menu_icons.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "desktop_menu_tooltip.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <imgui.h>

class EbDesktopMenuIconsState {
public:
    static cocos2d::CCTexture2D*& githubTex() {
        static cocos2d::CCTexture2D* value = nullptr;
        return value;
    }

    static cocos2d::CCTexture2D*& discordTex() {
        static cocos2d::CCTexture2D* value = nullptr;
        return value;
    }

    static constexpr char const* kGithubUrl = "https://github.com/ToldByNun/earlybuffering-gd";
    static constexpr char const* kDiscordUrl = "https://discord.gg/QJuTDkmPcf";
    static constexpr float kIconSize = 22.0f;
    static constexpr float kPad = 16.0f;
    static constexpr float kGap = 12.0f;

private:
    EbDesktopMenuIconsState() = delete;
};

class EbDesktopMenuIconsDraw {
public:
    static cocos2d::CCTexture2D* textureFromSprite(char const* sprPath) {
        if (!sprPath) {
            return nullptr;
        }
        // Geode sprite paths already include the mod id (via _spr at the call site).
        auto* sprite = geode::prelude::CCSprite::create(sprPath);
        if (!sprite) {
            return nullptr;
        }
        return sprite->getTexture();
    }

    static ImTextureID toImGui(cocos2d::CCTexture2D* texture) {
        if (!texture) {
            return (ImTextureID)0;
        }
        return (ImTextureID)(uintptr_t)texture->getName();
    }

    static bool iconButton(char const* id, cocos2d::CCTexture2D* texture, char const* url,
                           char const* tooltip) {
        if (!id || !texture || !url) {
            return false;
        }

        float const size = EbDesktopMenuIconsState::kIconSize;
        ImGui::PushID(id);
        bool const pressed = ImGui::InvisibleButton("##hit", ImVec2(size, size));
        EbDesktopMenuTooltip::show(tooltip);
        bool const hovered = ImGui::IsItemHovered();
        float const heat =
            EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered, 0.05f);

        ImVec2 const min = ImGui::GetItemRectMin();
        ImVec2 const max = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (heat > 0.01f) {
            ImVec4 plate(1.0f, 1.0f, 1.0f, 0.10f * heat);
            drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(plate), 6.0f);
        }

        ImVec4 tint = EbDesktopMenuMotion::lerp4(ImVec4(1.0f, 1.0f, 1.0f, 0.55f),
                                                 ImVec4(1.0f, 1.0f, 1.0f, 1.0f), heat);
        drawList->AddImage(toImGui(texture), min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                           EbDesktopMenuTheme::toU32(tint));

        if (pressed) {
            geode::utils::web::openLinkInBrowser(url);
        }
        ImGui::PopID();
        return pressed;
    }

private:
    EbDesktopMenuIconsDraw() = delete;
};

void EbDesktopMenuIcons::load() {
    // Re-resolve after fullscreen (GL context / texture cache refresh).
    EbDesktopMenuIconsState::githubTex() =
        EbDesktopMenuIconsDraw::textureFromSprite("eb_github.png"_spr);
    if (!EbDesktopMenuIconsState::githubTex()) {
        geode::log::warn("[EarlyBuffer] desktop menu: eb_github.png missing");
    }
    EbDesktopMenuIconsState::discordTex() =
        EbDesktopMenuIconsDraw::textureFromSprite("eb_discord.png"_spr);
    if (!EbDesktopMenuIconsState::discordTex()) {
        geode::log::warn("[EarlyBuffer] desktop menu: eb_discord.png missing");
    }
}

void EbDesktopMenuIcons::drawSidebarLinks(ImVec2 const& sideMin, float sideWidth,
                                          float sideHeight) {
    (void)sideWidth;
    float const size = EbDesktopMenuIconsState::kIconSize;
    float const pad = EbDesktopMenuIconsState::kPad;
    float const gap = EbDesktopMenuIconsState::kGap;
    ImVec2 cursor(sideMin.x + pad, sideMin.y + sideHeight - pad - size);

    ImGui::SetCursorScreenPos(cursor);
    EbDesktopMenuIconsDraw::iconButton("##eb-link-github", EbDesktopMenuIconsState::githubTex(),
                                       EbDesktopMenuIconsState::kGithubUrl, "Open GitHub repository");

    cursor.x += size + gap;
    ImGui::SetCursorScreenPos(cursor);
    EbDesktopMenuIconsDraw::iconButton("##eb-link-discord", EbDesktopMenuIconsState::discordTex(),
                                       EbDesktopMenuIconsState::kDiscordUrl, "Join the Discord");
}

#endif
