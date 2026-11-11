#include "desktop_menu_icons.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "desktop_menu_tooltip.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <imgui.h>
#include <algorithm>

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

    // Hit slot is square; glyph is aspect-fitted inside with padding.
    static constexpr float kModeSlotSize = 40.0f;
    static constexpr float kModeGlyphMax = 32.0f;
    static constexpr float kModeGap = 26.0f;
    static constexpr float kModeRowPadY = 6.0f;
    static constexpr float kModeIdleAlpha = 0.75f;
    static constexpr float kModeActiveAlpha = 1.0f;

    class ModeGlyph {
    public:
        cocos2d::CCTexture2D* texture = nullptr;
        float contentW = 1.0f;
        float contentH = 1.0f;
        ImVec2 uv0 = ImVec2(0.0f, 0.0f);
        ImVec2 uv1 = ImVec2(1.0f, 1.0f);
    };

    static ModeGlyph* modeGlyphs() {
        static ModeGlyph values[EbDesktopMenuIcons::kGamemodeCount] = {};
        return values;
    }

    static constexpr char const* kModeSprites[EbDesktopMenuIcons::kGamemodeCount] = {
        "eb_mode_cube.png", "eb_mode_ship.png",  "eb_mode_ball.png",   "eb_mode_ufo.png",
        "eb_mode_wave.png", "eb_mode_robot.png", "eb_mode_spider.png", "eb_mode_swing.png",
    };

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
        float const heat = EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered, 0.05f);

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

    static bool modeIconButton(int index, bool selected,
                               EbDesktopMenuIconsState::ModeGlyph const& glyph) {
        char const* title = EbDesktopMenuIcons::gamemodeTitle(index);
        if (!title || !glyph.texture) {
            return false;
        }

        float const slot = EbDesktopMenuIconsState::kModeSlotSize;
        ImGui::PushID(index);
        bool const pressed = ImGui::InvisibleButton("##mode", ImVec2(slot, slot));
        EbDesktopMenuTooltip::show(title);
        bool const hovered = ImGui::IsItemHovered();
        float const heat = EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered, 0.05f);
        float const select =
            EbDesktopMenuMotion::smoothToward(ImGui::GetItemID() ^ 0x5a5a5a5au, selected, 0.07f);

        ImVec2 const slotMin = ImGui::GetItemRectMin();
        ImVec2 const slotMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Aspect-fit glyph inside the square slot (no stretch).
        float const maxGlyph = EbDesktopMenuIconsState::kModeGlyphMax;
        float const aspect = glyph.contentW / std::max(glyph.contentH, 1.0f);
        float drawW = maxGlyph;
        float drawH = maxGlyph;
        if (aspect >= 1.0f) {
            drawH = maxGlyph / aspect;
        } else {
            drawW = maxGlyph * aspect;
        }
        ImVec2 const imgMin(slotMin.x + (slot - drawW) * 0.5f, slotMin.y + (slot - drawH) * 0.5f);
        ImVec2 const imgMax(imgMin.x + drawW, imgMin.y + drawH);

        if (heat > 0.01f && !selected) {
            ImVec4 plate(1.0f, 1.0f, 1.0f, 0.08f * heat);
            drawList->AddRectFilled(slotMin, slotMax, EbDesktopMenuTheme::toU32(plate), 8.0f);
        }
        if (select > 0.01f) {
            EbDesktopMenuTheme::drawAccentGlow(drawList, imgMin, imgMax, 6.0f, 0.45f * select);
        }

        float const alpha = EbDesktopMenuMotion::lerp4(
                                ImVec4(1.0f, 1.0f, 1.0f, EbDesktopMenuIconsState::kModeIdleAlpha),
                                ImVec4(1.0f, 1.0f, 1.0f, EbDesktopMenuIconsState::kModeActiveAlpha),
                                select)
                                .w;
        float const hoverBoost = (!selected) ? (0.10f * heat) : 0.0f;
        ImVec4 tint(1.0f, 1.0f, 1.0f, std::min(1.0f, alpha + hoverBoost));
        drawList->AddImage(toImGui(glyph.texture), imgMin, imgMax, glyph.uv0, glyph.uv1,
                           EbDesktopMenuTheme::toU32(tint));

        ImGui::PopID();
        return pressed;
    }

    static EbDesktopMenuIconsState::ModeGlyph glyphFromSprite(char const* sprPath) {
        EbDesktopMenuIconsState::ModeGlyph glyph;
        if (!sprPath) {
            return glyph;
        }
        auto* sprite = geode::prelude::CCSprite::create(sprPath);
        if (!sprite) {
            return glyph;
        }
        auto* texture = sprite->getTexture();
        if (!texture) {
            return glyph;
        }

        cocos2d::CCRect const rect = sprite->getTextureRect();
        float const texW = static_cast<float>(texture->getPixelsWide());
        float const texH = static_cast<float>(texture->getPixelsHigh());
        if (texW <= 0.0f || texH <= 0.0f || rect.size.width <= 0.0f || rect.size.height <= 0.0f) {
            return glyph;
        }

        glyph.texture = texture;
        glyph.contentW = rect.size.width;
        glyph.contentH = rect.size.height;
        glyph.uv0 = ImVec2(rect.origin.x / texW, rect.origin.y / texH);
        glyph.uv1 = ImVec2((rect.origin.x + rect.size.width) / texW,
                           (rect.origin.y + rect.size.height) / texH);
        return glyph;
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

    // `_spr` only works on string literals — resolve each mode sprite explicitly.
    char const* modeSpr[kGamemodeCount] = {
        "eb_mode_cube.png"_spr,   "eb_mode_ship.png"_spr,  "eb_mode_ball.png"_spr,
        "eb_mode_ufo.png"_spr,    "eb_mode_wave.png"_spr,  "eb_mode_robot.png"_spr,
        "eb_mode_spider.png"_spr, "eb_mode_swing.png"_spr,
    };
    for (int i = 0; i < kGamemodeCount; ++i) {
        EbDesktopMenuIconsState::modeGlyphs()[i] =
            EbDesktopMenuIconsDraw::glyphFromSprite(modeSpr[i]);
        if (!EbDesktopMenuIconsState::modeGlyphs()[i].texture) {
            geode::log::warn("[EarlyBuffer] desktop menu: {} missing",
                             EbDesktopMenuIconsState::kModeSprites[i]);
        }
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
                                       EbDesktopMenuIconsState::kGithubUrl,
                                       "Open GitHub repository");

    cursor.x += size + gap;
    ImGui::SetCursorScreenPos(cursor);
    EbDesktopMenuIconsDraw::iconButton("##eb-link-discord", EbDesktopMenuIconsState::discordTex(),
                                       EbDesktopMenuIconsState::kDiscordUrl, "Join the Discord");
}

bool EbDesktopMenuIcons::drawGamemodeBar(int* selected) {
    if (!selected) {
        return false;
    }
    if (*selected < 0 || *selected >= kGamemodeCount) {
        *selected = 0;
    }

    float const slot = EbDesktopMenuIconsState::kModeSlotSize;
    float const gap = EbDesktopMenuIconsState::kModeGap;
    float const rowPad = EbDesktopMenuIconsState::kModeRowPadY;
    float const total = static_cast<float>(kGamemodeCount) * slot +
                        static_cast<float>(kGamemodeCount - 1) * gap;
    float const avail = ImGui::GetContentRegionAvail().x;
    float const originX = ImGui::GetCursorScreenPos().x;
    float const originY = ImGui::GetCursorScreenPos().y;
    float const startX = originX + std::max(0.0f, (avail - total) * 0.5f);
    float const y = originY + rowPad;

    bool changed = false;
    for (int i = 0; i < kGamemodeCount; ++i) {
        float const x = startX + static_cast<float>(i) * (slot + gap);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool const selectedNow = (*selected == i);
        if (EbDesktopMenuIconsDraw::modeIconButton(i, selectedNow,
                                                   EbDesktopMenuIconsState::modeGlyphs()[i])) {
            if (!selectedNow) {
                *selected = i;
                changed = true;
            }
        }
    }

    // Advance layout past the padded icon row for following widgets.
    ImGui::SetCursorScreenPos(ImVec2(originX, originY));
    ImGui::Dummy(ImVec2(avail, slot + rowPad * 2.0f));
    return changed;
}

#endif

char const* EbDesktopMenuIcons::gamemodeTitle(int index) {
    static char const* titles[kGamemodeCount] = {"Cube", "Ship",  "Ball",   "UFO",
                                                 "Wave", "Robot", "Spider", "Swing"};
    if (index < 0 || index >= kGamemodeCount) {
        return titles[0];
    }
    return titles[index];
}

char const* EbDesktopMenuIcons::gamemodePrefix(int index) {
    static char const* prefixes[kGamemodeCount] = {"cube", "ship",  "ball",   "ufo",
                                                   "wave", "robot", "spider", "swing"};
    if (index < 0 || index >= kGamemodeCount) {
        return prefixes[0];
    }
    return prefixes[index];
}
