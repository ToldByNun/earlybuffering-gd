#include "desktop_menu_scale.hpp"
#if defined(EB_DESKTOP_IMGUI)
#include "desktop_menu_bind.hpp"
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
class EbDesktopMenuScaleHooks {
public:
    static float& scale() {
        static float value = 1.f;
        return value;
    }
    static unsigned& lastEvent() {
        static unsigned value = 0;
        return value;
    }
    static void begin(ImGuiContext* context, ImGuiContextHook*) {
        auto& io = context->IO;
        auto choice = EbDesktopMenuBind::getString("desktop-ui-scale", "100%");
        float requested = choice == "75%"    ? 0.75f
                          : choice == "125%" ? 1.25f
                          : choice == "150%" ? 1.5f
                                             : 1.f;
        float fit = std::min((io.DisplaySize.x - 24.f) / 900.f, (io.DisplaySize.y - 24.f) / 600.f);
        float next = std::max(0.1f, std::min(requested, fit));
        float ratio = scale() / next;
        if (io.MousePos.x > -FLT_MAX / 2) {
            io.MousePos.x *= ratio;
            io.MousePos.y *= ratio;
        }
        io.MousePosPrev.x *= ratio;
        io.MousePosPrev.y *= ratio;
        for (auto& point : io.MouseClickedPos) {
            point.x *= ratio;
            point.y *= ratio;
        }
        for (auto& event : context->InputEventsQueue) {
            if (event.EventId <= lastEvent()) {
                if (event.Type == ImGuiInputEventType_MousePos) {
                    event.MousePos.PosX *= ratio;
                    event.MousePos.PosY *= ratio;
                }
                continue;
            }
            if (event.Type == ImGuiInputEventType_MousePos) {
                event.MousePos.PosX /= next;
                event.MousePos.PosY /= next;
            }
            lastEvent() = event.EventId;
        }
        scale() = next;
        io.DisplaySize.x /= next;
        io.DisplaySize.y /= next;
    }
    static void render(ImGuiContext*, ImGuiContextHook*) {
        auto* data = ImGui::GetDrawData();
        if (!data)
            return;
        float s = scale();
        for (auto* list : data->CmdLists) {
            for (auto& vertex : list->VtxBuffer) {
                vertex.pos.x *= s;
                vertex.pos.y *= s;
            }
            for (auto& command : list->CmdBuffer) {
                command.ClipRect.x *= s;
                command.ClipRect.y *= s;
                command.ClipRect.z *= s;
                command.ClipRect.w *= s;
            }
        }
        data->DisplaySize.x *= s;
        data->DisplaySize.y *= s;
        data->DisplayPos.x *= s;
        data->DisplayPos.y *= s;
        auto& io = ImGui::GetIO();
        io.DisplaySize.x *= s;
        io.DisplaySize.y *= s;
    }

private:
};
void EbDesktopMenuScale::install() {
    auto* context = ImGui::GetCurrentContext();
    if (!context)
        return;
    for (auto const& hook : context->Hooks)
        if (hook.Callback == EbDesktopMenuScaleHooks::begin)
            return;
    EbDesktopMenuScaleHooks::scale() = 1.f;
    EbDesktopMenuScaleHooks::lastEvent() = 0;
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = EbDesktopMenuScaleHooks::begin;
    ImGui::AddContextHook(context, &hook);
    hook.Type = ImGuiContextHookType_RenderPost;
    hook.Callback = EbDesktopMenuScaleHooks::render;
    ImGui::AddContextHook(context, &hook);
}
#else
void EbDesktopMenuScale::install() {}
#endif
