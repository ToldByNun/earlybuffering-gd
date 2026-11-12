#include "desktop_menu_icons.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "desktop_menu_tooltip.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <string>

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

    // Square slots; glyphs are pre-padded 160x160 PNGs (native aspect preserved).
    static constexpr float kModeSlotSize = 44.0f;
    static constexpr float kModeGlyphSize = 36.0f;
    static constexpr float kModeGap = 28.0f;
    static constexpr float kModeRowPadY = 8.0f;
    static constexpr float kModeIdleAlpha = 0.5f;
    static constexpr float kModeActiveAlpha = 1.0f;

    class ModeGlyph {
    public:
        cocos2d::CCTexture2D* texture = nullptr;
        float contentW = 1.0f;
        float contentH = 1.0f;
    };

    static ModeGlyph* modeGlyphs() {
        static ModeGlyph values[EbDesktopMenuIcons::kGamemodeCount] = {};
        return values;
    }

    static constexpr char const* kModeFiles[EbDesktopMenuIcons::kGamemodeCount] = {
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
        auto* sprite = geode::prelude::CCSprite::create(sprPath);
        if (!sprite) {
            return nullptr;
        }
        return sprite->getTexture();
    }

    // Dedicated texture from the PNG file — avoids Geode atlas UV / point-vs-pixel bugs.
    static cocos2d::CCTexture2D* textureFromPngFile(char const* fileName, float* outW, float* outH) {
        if (!fileName) {
            return nullptr;
        }
        auto* mod = geode::prelude::Mod::get();
        if (!mod) {
            return nullptr;
        }

        auto const root = mod->getResourcesDir();
        auto const id = mod->getID();
        std::filesystem::path const candidates[] = {
            root / fileName,
            root / "ui" / fileName,
            root / id / fileName,
            root / id / "ui" / fileName,
            mod->getPackagePath().parent_path() / "resources" / "ui" / fileName,
            mod->getPackagePath().parent_path() / "resources" / id / fileName,
        };

        std::filesystem::path found;
        for (auto const& candidate : candidates) {
            std::error_code error;
            if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error)) {
                found = candidate;
                break;
            }
        }
        if (found.empty()) {
            return nullptr;
        }

        auto* image = new cocos2d::CCImage();
        if (!image->initWithImageFile(found.string().c_str())) {
            delete image;
            return nullptr;
        }

        auto* texture = new cocos2d::CCTexture2D();
        if (!texture->initWithImage(image)) {
            delete texture;
            delete image;
            return nullptr;
        }
        // Keep alive for the process — ImGui samples the GL name each frame.
        texture->retain();
        if (outW) {
            *outW = static_cast<float>(image->getWidth());
        }
        if (outH) {
            *outH = static_cast<float>(image->getHeight());
        }
        delete image;
        return texture;
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
        ImTextureID const texId = toImGui(texture);

        ImVec4 tint = EbDesktopMenuMotion::lerp4(ImVec4(1.0f, 1.0f, 1.0f, 0.55f),
                                                 ImVec4(1.0f, 1.0f, 1.0f, 1.0f), heat);
        // Minimal 1px drop — same idea as sidebar text shadows.
        ImU32 const shadow =
            EbDesktopMenuTheme::toU32(ImVec4(0.0f, 0.0f, 0.0f, 0.28f * tint.w));
        drawList->AddImage(texId, ImVec2(min.x + 1.0f, min.y + 1.0f),
                           ImVec2(max.x + 1.0f, max.y + 1.0f), ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f), shadow);
        drawList->AddImage(texId, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
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
        float const glyphSize = EbDesktopMenuIconsState::kModeGlyphSize;
        ImGui::PushID(index);
        bool const pressed = ImGui::InvisibleButton("##mode", ImVec2(slot, slot));
        EbDesktopMenuTooltip::show(title);
        bool const hovered = ImGui::IsItemHovered();
        float const heat = EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered, 0.05f);
        float const select =
            EbDesktopMenuMotion::smoothToward(ImGui::GetItemID() ^ 0x5a5a5a5au, selected, 0.07f);

        ImVec2 const slotMin = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Padded square PNGs — draw as squares; native aspect is already in the alpha.
        ImVec2 const imgMin(slotMin.x + (slot - glyphSize) * 0.5f,
                            slotMin.y + (slot - glyphSize) * 0.5f);
        ImVec2 const imgMax(imgMin.x + glyphSize, imgMin.y + glyphSize);

        if (heat > 0.01f && !selected) {
            ImVec4 plate(1.0f, 1.0f, 1.0f, 0.08f * heat);
            drawList->AddRectFilled(slotMin, ImVec2(slotMin.x + slot, slotMin.y + slot),
                                    EbDesktopMenuTheme::toU32(plate), 8.0f);
        }
        if (select > 0.01f) {
            EbDesktopMenuTheme::drawAccentGlow(drawList, imgMin, imgMax, 6.0f, 0.45f * select);
        }

        float const alpha = EbDesktopMenuMotion::lerp4(
                                ImVec4(1.0f, 1.0f, 1.0f, EbDesktopMenuIconsState::kModeIdleAlpha),
                                ImVec4(1.0f, 1.0f, 1.0f, EbDesktopMenuIconsState::kModeActiveAlpha),
                                select)
                                .w;
        float const hoverBoost = (!selected) ? (0.12f * heat) : 0.0f;
        ImVec4 tint(1.0f, 1.0f, 1.0f, std::min(1.0f, alpha + hoverBoost));
        // Full UV — dedicated file textures are not atlas-packed.
        drawList->AddImage(toImGui(glyph.texture), imgMin, imgMax, ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f), EbDesktopMenuTheme::toU32(tint));

        ImGui::PopID();
        return pressed;
    }

private:
    EbDesktopMenuIconsDraw() = delete;
};

void EbDesktopMenuIcons::load() {
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

    for (int i = 0; i < kGamemodeCount; ++i) {
        float width = 0.0f;
        float height = 0.0f;
        auto& glyph = EbDesktopMenuIconsState::modeGlyphs()[i];
        // Drop previous dedicated texture on reload (fullscreen / GL refresh).
        if (glyph.texture) {
            glyph.texture->release();
            glyph.texture = nullptr;
        }
        glyph.texture = EbDesktopMenuIconsDraw::textureFromPngFile(
            EbDesktopMenuIconsState::kModeFiles[i], &width, &height);
        glyph.contentW = std::max(1.0f, width);
        glyph.contentH = std::max(1.0f, height);
        if (!glyph.texture) {
            geode::log::warn("[EarlyBuffer] desktop menu: {} missing (file load)",
                             EbDesktopMenuIconsState::kModeFiles[i]);
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
