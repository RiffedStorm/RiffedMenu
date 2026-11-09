#include "menu.h"
#include "data.h"
#include "gamedata.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "combat.h"
#include "config.h"
#include "gps.h"
#include "i18n.h"
#include "missions_data.h"
#include "update.h"
#include "version.h"
#include "wheel.h"
#include "imgui.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

static bool g_open = false;

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------
struct Toast { std::string text; double until; };
static std::deque<Toast> g_toasts;

static void Notify(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    g_toasts.push_back({ buf, ImGui::GetTime() + 2.5 });
    if (g_toasts.size() > 5) g_toasts.pop_front();
}

static void DrawToasts()
{
    double now = ImGui::GetTime();
    while (!g_toasts.empty() && g_toasts.front().until < now) g_toasts.pop_front();
    if (g_toasts.empty()) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16, vp->WorkPos.y + vp->WorkSize.y - 16), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##riffed_toasts", nullptr, f)) {
        for (auto& t : g_toasts) {
            float a = (float)std::min(1.0, (t.until - now) / 0.4);
            ImGui::TextColored(ImVec4(1, 1, 1, a), "%s", t.text.c_str());
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------
static const ImVec4 kAccent      = ImVec4(0.96f, 0.55f, 0.16f, 1.00f);
static const ImVec4 kAccentHover = ImVec4(1.00f, 0.65f, 0.28f, 1.00f);
static const ImVec4 kAccentDim   = ImVec4(0.96f, 0.55f, 0.16f, 0.35f);

void Menu::ApplyStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding = 8; s.ChildRounding = 6; s.FrameRounding = 5; s.PopupRounding = 6;
    s.GrabRounding = 4; s.TabRounding = 5; s.ScrollbarRounding = 6;
    s.WindowPadding = ImVec2(14, 12); s.FramePadding = ImVec2(9, 5); s.ItemSpacing = ImVec2(8, 7);
    s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.TabBorderSize = 0;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.93f, 0.93f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = kAccentHover;
    c[ImGuiCol_Button]               = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = kAccentDim;
    c[ImGuiCol_ButtonActive]         = kAccent;
    c[ImGuiCol_Header]               = kAccentDim;
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.96f, 0.55f, 0.16f, 0.50f);
    c[ImGuiCol_HeaderActive]         = kAccent;
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered]           = kAccentDim;
    c[ImGuiCol_TabSelected]          = ImVec4(0.96f, 0.55f, 0.16f, 0.80f);
    c[ImGuiCol_TabSelectedOverline]  = kAccent;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.08f, 0.10f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_PlotHistogram]        = kAccent;
    c[ImGuiCol_NavCursor]            = kAccent;
    s.ScaleAllSizes(scale);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
static void SectionTitle(const char* text)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

static bool Toggle(const char* label, bool* v, const char* hint = nullptr)
{
    bool changed = ImGui::Checkbox(label, v);
    if (hint && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", hint);
    if (changed) Notify("%s %s", label, *v ? T("ON") : T("OFF"));
    return changed;
}

static bool ContainsI(const char* hay, const char* needle)
{
    if (!*needle) return true;
    for (const char* h = hay; *h; ++h) {
        const char* a = h; const char* b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

// Star outline/filled, used by the vehicle favorites.
static bool StarButton(bool on)
{
    float h = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##star", ImVec2(h, h));
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f), pts[10];
    float R = h * 0.46f, r = R * 0.42f;
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.6283185f, l = i % 2 ? r : R;
        pts[i] = ImVec2(c.x + cosf(a) * l, c.y + sinf(a) * l);
    }
    ImU32 col = on ? ImGui::GetColorU32(kAccent) : hot ? IM_COL32(230, 230, 230, 255) : IM_COL32(120, 120, 125, 255);
    if (on) dl->AddConcavePolyFilled(pts, 10, col);
    else dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.3f);
    return clicked;
}

// Click, then press the key to bind. Escape cancels.
static bool Keybind(const char* label, int* vk)
{
    static int* waiting = nullptr;
    bool changed = false;
    char name[48] = "";
    if (waiting == vk) {
        snprintf(name, sizeof(name), "%s", T("Press a key..."));
        for (int k = 1; k < 255; ++k) {
            if (k == VK_LBUTTON || k == VK_RBUTTON) continue;
            if (GetAsyncKeyState(k) & 0x8000) {
                if (k != VK_ESCAPE) { *vk = k; changed = true; }
                waiting = nullptr;
                break;
            }
        }
    } else if (*vk == VK_XBUTTON1) snprintf(name, sizeof(name), "%s", T("Mouse side button 1"));
    else if (*vk == VK_XBUTTON2) snprintf(name, sizeof(name), "%s", T("Mouse side button 2"));
    else if (*vk == VK_MBUTTON) snprintf(name, sizeof(name), "%s", T("Middle mouse button"));
    else {
        UINT sc = MapVirtualKeyA(*vk, MAPVK_VK_TO_VSC);
        LONG lp = sc << 16;
        if (*vk == VK_INSERT || *vk == VK_DELETE || *vk == VK_HOME || *vk == VK_END || *vk == VK_PRIOR || *vk == VK_NEXT) lp |= 1 << 24;
        if (!GetKeyNameTextA(lp, name, sizeof(name))) snprintf(name, sizeof(name), "#%d", *vk);
    }
    ImGui::PushID(vk);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Button(name, ImVec2(-1, 0))) waiting = vk;
    ImGui::PopID();
    return changed;
}

static float HalfWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }
static float ThirdWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f; }

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
static void TabPlayer()
{
    void* ped = Game::PlayerPed();

    SectionTitle(T("Toggles"));
    if (ImGui::BeginTable("##ptoggles", 2)) {
        ImGui::TableNextColumn(); Toggle(T("God mode"), &g_toggles.godMode, T("Bullet/fire/explosion/melee/collision proof + health refill"));
        ImGui::TableNextColumn(); Toggle(T("Infinite armour"), &g_toggles.infiniteArmour);
        ImGui::TableNextColumn(); Toggle(T("Infinite ammo"), &g_toggles.infiniteAmmo, T("Keeps magazines full - no reloading"));
        ImGui::TableNextColumn(); Toggle(T("Never wanted"), &g_toggles.neverWanted);
        ImGui::TableNextColumn(); Toggle(T("Noclip"), &g_toggles.noclip, T("Fly through walls: WASD, Space up, Ctrl down, Shift faster.\nWorks on foot and in vehicles. Close the menu to move."));
        ImGui::EndTable();
    }
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::SliderFloat("##ncspeed", &g_toggles.noclipSpeed, 5.0f, 100.0f, T("Noclip speed %.0f m/s"));
    ImGui::SameLine();
    if (ImGui::Button(T("Equip jetpack"), ImVec2(-1, 0))) {
        if (Game::PlayerVehicle()) Notify(T("Get out of the vehicle first"));
        else { Features::Queue([] { Game::GiveJetpack(); }); Notify(T("Jetpack equipped")); }
    }

    SectionTitle(T("Health & armour"));
    if (ped) {
        float hp = Game::PedHealth(ped), maxHp = Game::PedMaxHealth(ped), ar = Game::PedArmour(ped);
        char ov[32];
        snprintf(ov, sizeof(ov), "%.0f / %.0f", hp, maxHp);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.25f, 0.25f, 1));
        ImGui::ProgressBar(maxHp > 0 ? hp / maxHp : 0, ImVec2(HalfWidth(), 0), ov);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        snprintf(ov, sizeof(ov), T("Armour %.0f"), ar);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.6f, 0.95f, 1));
        ImGui::ProgressBar(ar / 100.0f, ImVec2(-1, 0), ov);
        ImGui::PopStyleColor();
    }
    if (ImGui::Button(T("Full health"), ImVec2(HalfWidth(), 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedHealth(p) = Game::PedMaxHealth(p); });
    ImGui::SameLine();
    if (ImGui::Button(T("Full armour"), ImVec2(-1, 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedArmour(p) = 100.0f; });

    SectionTitle(T("Wanted level"));
    int wanted = Game::WantedLevel();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##wanted", &wanted, 0, 6, "%d stars"))
        Features::Queue([wanted] { Game::SetWantedLevel(wanted); });

    SectionTitle(T("Money"));
    static int moneyInput = 1000000;
    ImGui::Text(T("Current: $%d"), ped ? Game::Money() : 0);
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::InputInt("##money", &moneyInput, 10000, 100000);
    ImGui::SameLine();
    if (ImGui::Button(T("Set money"), ImVec2(-1, 0))) { int m = moneyInput; Features::Queue([m] { Game::Money() = m; }); Notify(T("Money set to $%d"), m); }
    float w4 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    const int adds[] = { 10000, 100000, 1000000, 10000000 };
    const char* addLabels[] = { "+$10K", "+$100K", "+$1M", "+$10M" };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(addLabels[i], ImVec2(w4, 0))) {
            int a = adds[i];
            Features::Queue([a] { int& m = Game::Money(); m = (m > 999999999 - a) ? 999999999 : m + a; });
            Notify(T("Added $%d"), a);
        }
    }
}

static void SpawnFromMenu(const VehicleEntry& v)
{
    if (Game::ActiveInterior() != 0) { Notify(T("Can't spawn vehicles indoors - go outside first")); return; }
    int id = v.id;
    bool inside = g_toggles.spawnInside;
    std::string plate = g_toggles.plate;
    Features::Queue([id, inside, plate] { Game::SpawnVehicle(id, inside, plate.c_str()); });
    Notify(T("Spawned %s"), v.name);
}

static void TabVehicle()
{
    void* veh = Game::PlayerVehicle();

    SectionTitle(T("Current vehicle"));
    if (veh) {
        float hp = Game::VehicleHealth(veh);
        char ov[48];
        snprintf(ov, sizeof(ov), T("Model %d  -  %.0f HP"), Game::VehicleModel(veh), hp);
        ImGui::ProgressBar(std::clamp(hp / 1000.0f, 0.0f, 1.0f), ImVec2(-1, 0), ov);
    } else {
        ImGui::TextDisabled(T("Not in a vehicle"));
    }
    if (ImGui::BeginTable("##vtoggles", 2)) {
        ImGui::TableNextColumn(); Toggle(T("Vehicle god mode"), &g_toggles.vehicleGod, T("Proofs + health lock on the vehicle you're in"));
        ImGui::TableNextColumn(); Toggle(T("Auto repair"), &g_toggles.autoRepair, T("Fixes visual damage as soon as it happens"));
        ImGui::EndTable();
    }
    SectionTitle(T("Nitrous"));
    if (ImGui::BeginTable("##nitro", 3)) {
        ImGui::TableNextColumn(); Toggle(T("Infinite"), &g_toggles.nitro.infinite, T("The bottle never runs out or disappears."));
        ImGui::TableNextColumn(); Toggle(T("No cooldown"), &g_toggles.nitro.noCooldown, T("Use it again the moment it ends."));
        ImGui::TableNextColumn(); Toggle(T("Always on"), &g_toggles.nitro.continuous, T("Nitrous stays on the whole time you drive. Fits a bottle if the car has none."));
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(!veh);
    if (ImGui::Button(T("Repair"), ImVec2(ThirdWidth(), 0))) { Features::Queue([] { Game::RepairVehicle(); }); Notify(T("Vehicle repaired")); }
    ImGui::SameLine();
    if (ImGui::Button(T("Flip upright"), ImVec2(ThirdWidth() * 1.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, 0)))
        Features::Queue([] { Game::FlipVehicle(); });
    ImGui::SameLine();
    if (ImGui::Button(T("Boost"), ImVec2(-1, 0))) Features::Queue([] { Game::BoostVehicle(1.6f); });
    if (veh) {
        int c1 = Game::VehicleColor(veh, 0), c2 = Game::VehicleColor(veh, 1);
        ImGui::SetNextItemWidth(HalfWidth());
        bool recolor = ImGui::SliderInt("##c1", &c1, 0, 126, T("Primary colour %d"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        recolor |= ImGui::SliderInt("##c2", &c2, 0, 126, T("Secondary colour %d"));
        if (recolor) Features::Queue([c1, c2] { Game::SetVehicleColors(c1, c2); });
    }
    ImGui::EndDisabled();

    SectionTitle(T("Spawner"));
    static char filter[64] = "";
    static int category = 0;      // 0 All, 1 Favorites, 2+ kVehicleCategories[n-1]
    static int selected = -1;
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::InputTextWithHint("##vfilter", T("Search vehicles..."), filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    {
        auto catName = [](int i) { return i == 0 ? T("All") : i == 1 ? T("Favorites") : T(kVehicleCategories[i - 1]); };
        if (ImGui::BeginCombo("##vcat", catName(category))) {
            int n = (int)IM_ARRAYSIZE(kVehicleCategories) + 1;
            for (int i = 0; i < n; ++i) if (ImGui::Selectable(catName(i), i == category)) category = i;
            ImGui::EndCombo();
        }
    }
    ImGui::Checkbox(T("Spawn inside"), &g_toggles.spawnInside);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Puts you in the driver's seat. Doors are always unlocked."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##plate", T("Custom plate (8 chars, cars only)"), g_toggles.plate, sizeof(g_toggles.plate));

    auto& favs = g_toggles.favorites;
    auto isFav = [&](int id) { return std::find(favs.begin(), favs.end(), id) != favs.end(); };
    auto matches = [&](const VehicleEntry& v) {
        if (category >= 2 && strcmp(v.category, kVehicleCategories[category - 1]) != 0) return false;
        return ContainsI(v.name, filter);
    };
    const VehicleEntry* byId[700] = {};
    int indexOf[700] = {};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kVehicles); ++i) { byId[kVehicles[i].id] = &kVehicles[i]; indexOf[kVehicles[i].id] = i; }

    // Starred vehicles first, in the user's order, then the rest of the list.
    std::vector<int> shownFavs, rest;
    for (int id : favs) if (id >= 0 && id < 700 && byId[id] && matches(*byId[id])) shownFavs.push_back(id);
    if (category != 1)
        for (const VehicleEntry& v : kVehicles) if (!isFav(v.id) && matches(v)) rest.push_back(v.id);

    float footer = ImGui::GetFrameHeightWithSpacing();
    int toggleFav = -1, swapA = -1, swapB = -1;
    if (ImGui::BeginChild("##vlist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        float idCol = ImGui::CalcTextSize("Industrial 000").x;
        auto row = [&](int id, int favIndex) {
            const VehicleEntry& v = *byId[id];
            ImGui::PushID(id);
            if (StarButton(favIndex >= 0)) toggleFav = id;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Add to or remove from favorites"));
            ImGui::SameLine();
            char label[96];
            snprintf(label, sizeof(label), "%s##row", v.name);
            int idx = indexOf[id];
            if (ImGui::Selectable(label, selected == idx, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap)) {
                selected = idx;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) SpawnFromMenu(v);
            }
            if (favIndex >= 0 && ImGui::IsItemActive() && !ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
                int next = favIndex + (ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 3.0f).y < 0 ? -1 : 1);
                if (next >= 0 && next < (int)shownFavs.size()) { swapA = id; swapB = shownFavs[next]; ImGui::ResetMouseDragDelta(); }
            }
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - idCol);
            ImGui::TextDisabled("%s %d", T(v.category), v.id);
            ImGui::PopID();
        };
        for (int i = 0; i < (int)shownFavs.size(); ++i) row(shownFavs[i], i);
        if (!shownFavs.empty() && !rest.empty()) ImGui::Separator();
        for (int id : rest) row(id, -1);
        if (category == 1 && shownFavs.empty()) ImGui::TextDisabled("%s", T("Click the star next to a vehicle to add it here."));
    }
    ImGui::EndChild();
    if (toggleFav >= 0) {
        auto it = std::find(favs.begin(), favs.end(), toggleFav);
        if (it != favs.end()) favs.erase(it); else favs.push_back(toggleFav);
    }
    if (swapA >= 0) std::iter_swap(std::find(favs.begin(), favs.end(), swapA), std::find(favs.begin(), favs.end(), swapB));

    ImGui::BeginDisabled(selected < 0);
    if (ImGui::Button(selected >= 0 ? T("Spawn selected") : T("Select a vehicle (double-click spawns)"), ImVec2(-1, 0)) && selected >= 0) {
        SpawnFromMenu(kVehicles[selected]);
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// Editors (vehicle customizer, outfit). Both preview live on the player and can
// switch to an orbiting camera (g_preview, applied by the game tick).
// ---------------------------------------------------------------------------
static void PreviewControls(const char* label, bool onVehicle, float defDist, float defHeight)
{
    static bool enabled[2] = { true, true };
    static bool spin[2] = { true, false };
    static float yaw[2] = { 35.0f, 0.0f };
    static float zoom[2] = { 1.0f, 1.0f };
    int k = onVehicle ? 0 : 1;

    ImGui::Checkbox(label, &enabled[k]);
    ImGui::SameLine();
    ImGui::Checkbox(T("Auto-rotate"), &spin[k]);
    if (enabled[k]) {
        if (spin[k]) yaw[k] = fmodf(yaw[k] + ImGui::GetIO().DeltaTime * 25.0f, 360.0f);
        ImGui::SetNextItemWidth(HalfWidth());
        ImGui::SliderFloat("##yaw", &yaw[k], 0.0f, 360.0f, T("Angle %.0f deg"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##zoom", &zoom[k], 0.5f, 2.5f, T("Distance x%.1f"));
        g_preview = { true, onVehicle, yaw[k], defDist * zoom[k], defHeight * zoom[k] };
    }
}

static ImVec4 Swatch(int i)
{
    const auto& pal = GameData::CarColours();
    if (i < 0 || i >= (int)pal.size()) return ImVec4(0.5f, 0.5f, 0.5f, 1);
    return ImVec4(pal[i].r / 255.0f, pal[i].g / 255.0f, pal[i].b / 255.0f, 1);
}

static void PaintPicker(void* veh)
{
    static int target = 0;   // 0 primary, 1 secondary
    int c[2] = { Game::VehicleColor(veh, 0), Game::VehicleColor(veh, 1) };
    ImGui::RadioButton(T("Primary"), &target, 0);
    ImGui::SameLine(); ImGui::ColorButton("##p", Swatch(c[0]), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight()));
    ImGui::SameLine(0, 24);
    ImGui::RadioButton(T("Secondary"), &target, 1);
    ImGui::SameLine(); ImGui::ColorButton("##s", Swatch(c[1]), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight()));

    int count = (int)GameData::CarColours().size();
    float sz = ImGui::GetFrameHeight();
    float sp = 3.0f;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + sp) / (sz + sp)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
    for (int i = 0; i < count; ++i) {
        if (i % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        bool current = c[target] == i;
        if (current) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
        if (current) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        if (ImGui::ColorButton("##c", Swatch(i), ImGuiColorEditFlags_NoTooltip, ImVec2(sz, sz))) {
            c[target] = i;
            Features::Queue([c0 = c[0], c1 = c[1]] { Game::SetVehicleColors(c0, c1); });
        }
        if (current) { ImGui::PopStyleVar(); ImGui::PopStyleColor(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Colour %d"), i);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

static void TabCustomize()
{
    void* veh = Game::PlayerVehicle();
    if (!veh) {
        ImGui::Spacing();
        ImGui::TextWrapped(T("Get into a vehicle to customise it. Tip: spawn one from the Vehicle tab with \"Spawn inside\" on."));
        return;
    }
    int model = Game::VehicleModel(veh);

    SectionTitle(T("Preview"));
    PreviewControls(T("Showroom camera"), true, 6.5f, 1.8f);

    SectionTitle(T("Paint"));
    PaintPicker(veh);

    int jobs = Game::PaintjobCount(model);
    if (jobs > 0) {
        SectionTitle(T("Paintjob"));
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * jobs) / (jobs + 1);
        if (ImGui::Button(T("None"), ImVec2(w, 0))) Features::Queue([] { Game::SetPaintjob(-1); });
        for (int j = 0; j < jobs; ++j) {
            ImGui::SameLine();
            char l[16]; snprintf(l, sizeof(l), T("Paintjob %d"), j + 1);
            if (ImGui::Button(l, ImVec2(w, 0))) Features::Queue([j] { Game::SetPaintjob(j); });
        }
    }

    SectionTitle(T("Parts"));
    // Parts fitted through this menu, per vehicle: category -> part id (the game keeps its own list too).
    static std::map<void*, std::map<std::string, int>> fitted;
    static int cachedModel = -1;
    static std::vector<TuningPart> parts;
    if (cachedModel != model) { parts = GameData::PartsFor(model); cachedModel = model; }
    if (parts.empty()) {
        ImGui::TextDisabled(T("This vehicle has no tuning parts (only cars a mod shop accepts do)."));
        return;
    }
    auto& mine = fitted[veh];
    if (ImGui::BeginTable("##parts", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("cat", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Front bumper__").x);
        ImGui::TableSetupColumn("pick", ImGuiTableColumnFlags_WidthStretch);
        for (size_t i = 0; i < parts.size();) {
            const std::string& cat = parts[i].category;
            size_t j = i;
            while (j < parts.size() && parts[j].category == cat) ++j;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(T(cat.c_str()));
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
            int cur = mine.count(cat) ? mine[cat] : -1;
            std::string preview = T("Stock");
            for (size_t k = i; k < j; ++k) if (parts[k].id == cur) preview = parts[k].name;
            ImGui::PushID(cat.c_str());
            if (ImGui::BeginCombo("##c", preview.c_str())) {
                auto apply = [&](int newId) {
                    int old = cur;
                    Features::Queue([old, newId] {
                        if (old > 0) Game::RemovePart(old, GameData::LinkedPart(old));
                        if (newId > 0) Game::AddPart(newId, GameData::LinkedPart(newId));
                    });
                    if (newId > 0) mine[cat] = newId; else mine.erase(cat);
                };
                if (ImGui::Selectable(T("Stock"), cur == -1)) apply(-1);
                for (size_t k = i; k < j; ++k) {
                    char l[64]; snprintf(l, sizeof(l), "%s##%d", parts[k].name.c_str(), parts[k].id);
                    if (ImGui::Selectable(l, cur == parts[k].id)) apply(parts[k].id);
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            i = j;
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(T("Remove all parts fitted here"), ImVec2(-1, 0))) {
        std::vector<int> ids;
        for (auto& [c, id] : mine) ids.push_back(id);
        mine.clear();
        Features::Queue([ids] { for (int id : ids) Game::RemovePart(id, GameData::LinkedPart(id)); });
        Notify(T("Parts removed"));
    }
}

static void TabOutfit()
{
    SectionTitle(T("Preview"));
    PreviewControls(T("Mirror camera"), false, 2.6f, 0.4f);
    if (ImGui::Button(T("Save outfit"), ImVec2(HalfWidth(), 0))) { Features::Queue([] { Game::SaveOutfit(); }); Notify(T("Outfit saved")); }
    ImGui::SameLine();
    if (ImGui::Button(T("Revert to saved outfit"), ImVec2(-1, 0))) Features::Queue([] { Game::RestoreOutfit(); });

    SectionTitle(T("Clothes, hair & tattoos"));
    bool canDress = Game::CanChangeClothes();
    if (!canDress) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s", T("Clothes need CJ's own skin, on foot."));
    const auto& items = GameData::Clothes();
    static int part = 0;
    static char filter[32] = "";
    // Body parts that actually have items, in a sensible order.
    static const int order[] = { 0, 2, 3, 1, 16, 15, 13, 14, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17 };
    ImGui::SetNextItemWidth(HalfWidth());
    if (ImGui::BeginCombo("##part", T(GameData::BodyPartName(part)))) {
        for (int p : order) {
            bool any = false;
            for (auto& it : items) any |= it.part == p;
            if (any && ImGui::Selectable(T(GameData::BodyPartName(p)), p == part)) part = p;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##cf", T("Search..."), filter, sizeof(filter));

    bool removable = part >= 4;   // torso/hair/legs/shoes always need something; pick a basic item instead
    float listH = ImGui::GetTextLineHeightWithSpacing() * 9;
    if (ImGui::BeginChild("##clothes", ImVec2(0, listH), ImGuiChildFlags_Borders)) {
        if (removable && canDress && ImGui::Selectable(T("(none - remove)"))) { int p = part; Features::Queue([p] { Game::RemoveClothes(p); }); }
        for (auto& it : items) {
            if (it.part != part || !ContainsI(it.texture.c_str(), filter)) continue;
            char l[64]; snprintf(l, sizeof(l), "%s##%s%s", it.texture.c_str(), it.texture.c_str(), it.model.c_str());
            std::string risk = "clothes:" + it.texture;
            bool bad = Features::RiskIsBad(risk);
            ImGui::BeginDisabled(bad || !canDress);
            if (ImGui::Selectable(l)) {
                std::string t = it.texture, m = it.model; int p = it.part;
                Features::Queue([t, m, p, risk] { Features::RiskBegin(risk); Game::GiveClothes(t.c_str(), m.c_str(), p); });
            }
            ImGui::EndDisabled();
            if (!it.model.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(T("model: %s"), it.model.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled(T("Click an item to put it on. Clothes only show on CJ's own skin."));

    SectionTitle(T("Skin"));
    static char skinFilter[32] = "";
    if (ImGui::Button(T("Back to CJ"), ImVec2(HalfWidth(), 0))) Features::Queue([] { Game::SetSkin(0); });
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##sf", T("Search skins..."), skinFilter, sizeof(skinFilter));
    if (ImGui::BeginChild("##skins", ImVec2(0, listH), ImGuiChildFlags_Borders)) {
        for (auto& s : GameData::Skins()) {
            if (!ContainsI(s.name.c_str(), skinFilter)) continue;
            char l[64]; snprintf(l, sizeof(l), "%-12s  %d##sk%d", s.name.c_str(), s.id, s.id);
            std::string risk = "skin:" + std::to_string(s.id);
            ImGui::BeginDisabled(Features::RiskIsBad(risk));
            if (ImGui::Selectable(l)) {
                if (Game::PlayerVehicle()) Notify(T("Get out of the vehicle first"));
                else { int id = s.id; Features::Queue([id, risk] { Features::RiskBegin(risk); Game::SetSkin(id); }); }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::EndChild();
}

static void TabWeapons()
{
    int& ammoMul = g_toggles.ammoMul;
    SectionTitle(T("Options"));
    Toggle(T("Infinite ammo / no reload"), &g_toggles.infiniteAmmo);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##ammomul", &ammoMul, 1, 10, T("Ammo x%d"));

    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##dmg", &g_toggles.damageMult, 0.1f, 10.0f, T("Weapon damage x%.1f"), ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(T("Applies to every weapon for everyone - enemies too."));
    const float presets[] = { 0.5f, 1.0f, 2.0f, 5.0f, 100.0f };
    const char* presetLabels[] = { "x0.5", T("Normal"), "x2", "x5", T("One-hit") };
    float pw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(presetLabels[i], ImVec2(pw, 0))) {
            g_toggles.damageMult = presets[i];
            Notify(T("Weapon damage x%g"), presets[i]);
        }
    }

    if (ImGui::Button(T("Gangster pack"), ImVec2(HalfWidth(), 0))) {
        Features::Queue([m = ammoMul] { for (int t : {5, 24, 26, 28, 30, 18}) Game::GiveWeapon(t, 500 * m); });
        Notify(T("Gangster pack given"));
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Military pack"), ImVec2(-1, 0))) {
        Features::Queue([m = ammoMul] { for (int t : {8, 24, 27, 29, 31, 34, 35, 16}) Game::GiveWeapon(t, 500 * m); });
        Notify(T("Military pack given"));
    }

    if (ImGui::BeginChild("##wlist")) {
        for (const char* group : kWeaponGroups) {
            SectionTitle(T(group));
            float bw = ThirdWidth();
            int n = 0;
            for (const WeaponEntry& w : kWeapons) {
                if (strcmp(w.group, group) != 0) continue;
                if (n++ % 3) ImGui::SameLine();
                if (ImGui::Button(w.name, ImVec2(bw, 0))) {
                    int id = w.id, ammo = w.ammo * ammoMul;
                    Features::Queue([id, ammo] { Game::GiveWeapon(id, ammo); });
                    Notify(T("Given %s"), w.name);
                }
            }
        }
    }
    ImGui::EndChild();
}

static void TabTeleport()
{
    void* ped = Game::PlayerPed();
    static float custom[3] = { 2495.0f, -1687.0f, 13.5f };

    SectionTitle(T("Quick"));
    if (ImGui::Button(T("Teleport to map marker"), ImVec2(-1, 0))) {
        CVector p;
        if (Game::WaypointPosition(p)) { Features::Queue([p] { Game::Teleport(p, true); }); Notify(T("Teleported to marker")); }
        else Notify(T("No marker placed on the map"));
    }

    SectionTitle(T("Coordinates"));
    if (ped) {
        CVector p = Game::PlayerPosition();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(T("Current: %.2f, %.2f, %.2f"), p.x, p.y, p.z);
        ImGui::SameLine();
        if (ImGui::SmallButton(T("Copy to fields"))) { custom[0] = p.x; custom[1] = p.y; custom[2] = p.z; }
    }
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Go").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputFloat3("##coords", custom, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button(T("Go"))) { CVector p{ custom[0], custom[1], custom[2] }; Features::Queue([p] { Game::Teleport(p, false); }); }

    SectionTitle(T("Locations"));
    if (ImGui::BeginChild("##tlist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const char* lastRegion = "";
        for (const TeleportEntry& t : kTeleports) {
            if (strcmp(lastRegion, t.region) != 0) {
                lastRegion = t.region;
                ImGui::TextDisabled("%s", t.region);
            }
            ImGui::Indent();
            if (ImGui::Selectable(t.name)) { CVector p = t.pos; Features::Queue([p] { Game::Teleport(p, true); }); Notify(T("Teleported: %s"), t.name); }
            ImGui::Unindent();
        }
    }
    ImGui::EndChild();
}

static void TabWorld()
{
    SectionTitle(T("Time"));
    int h = 12, m = 0;
    if (Game::InGame()) Game::GetTime(h, m);
    if (g_toggles.freezeTime) { h = g_toggles.frozenHour; m = g_toggles.frozenMinute; }
    ImGui::SetNextItemWidth(HalfWidth());
    bool ch = ImGui::SliderInt("##hour", &h, 0, 23, T("Hour %02d"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ch |= ImGui::SliderInt("##min", &m, 0, 59, T("Minute %02d"));
    if (ch) {
        g_toggles.frozenHour = h; g_toggles.frozenMinute = m;
        Features::Queue([h, m] { Game::SetTime(h, m); });
    }
    if (Toggle(T("Freeze time"), &g_toggles.freezeTime)) { g_toggles.frozenHour = h; g_toggles.frozenMinute = m; }

    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Reset").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SliderFloat("##speed", &g_toggles.gameSpeed, 0.1f, 3.0f, T("Game speed %.2fx"));
    ImGui::SameLine();
    if (ImGui::Button(T("Reset"))) g_toggles.gameSpeed = 1.0f;

    SectionTitle(T("Weather"));
    ImGui::SetNextItemWidth(HalfWidth());
    if (ImGui::Combo("##weather", &g_toggles.weather, [](void*, int i) { return T(kWeatherNames[i]); }, nullptr, IM_ARRAYSIZE(kWeatherNames)))
        Features::Queue([w = g_toggles.weather] { Game::ForceWeather(w); });
    ImGui::SameLine();
    Toggle(T("Lock weather"), &g_toggles.lockWeather);

    if (!Game::HasGravity()) return;
    SectionTitle(T("Physics"));
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##grav", &g_toggles.gravity, 0.001f, 0.02f, T("Gravity %.4f"));
    if (ImGui::Button(T("Moon gravity"), ImVec2(HalfWidth(), 0))) g_toggles.gravity = 0.0013f;
    ImGui::SameLine();
    if (ImGui::Button(T("Normal gravity"), ImVec2(-1, 0))) g_toggles.gravity = 0.008f;
}

static void StatSliders(const char* id, const StatEntry* stats, int count)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Combat shotgun__").x);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    for (int i = 0; i < count; ++i) {
        const StatEntry& s = stats[i];
        float v = Game::InGame() ? Game::GetStat(s.id) : 0.0f;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(T(s.name));
        ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
        ImGui::PushID(s.id);
        if (ImGui::SliderFloat("##v", &v, 0, s.max, "%.0f")) Features::Queue([sid = s.id, v] { Game::SetStat(sid, v); });
        if ((s.id == 21 || s.id == 23) && ImGui::IsItemDeactivatedAfterEdit()) Features::Queue([] { Features::RefreshBody(); });   // on release: a rebuild is too heavy for every drag step
        ImGui::PopID();
    }
    ImGui::EndTable();
}

static void MaxStats(const StatEntry* stats, int count)
{
    std::vector<std::pair<uint16_t, float>> list;
    for (int i = 0; i < count; ++i) list.push_back({ stats[i].id, stats[i].max });
    Features::Queue([list] { for (auto& s : list) Game::SetStat(s.first, s.second); });
}

static void TabStats()
{
    if (ImGui::BeginChild("##statscroll")) {
        SectionTitle(T("Body"));
        StatSliders("##body", kBodyStats, IM_ARRAYSIZE(kBodyStats));
        if (ImGui::Button(T("Athlete (no fat, max stamina & muscle)"), ImVec2(-1, 0))) {
            Features::Queue([] { Game::SetStat(21, 0); Game::SetStat(22, 1000); Game::SetStat(23, 1000); Features::RefreshBody(); });
            Notify(T("Athlete stats applied"));
        }

        SectionTitle(T("Driving"));
        StatSliders("##drive", kDrivingStats, IM_ARRAYSIZE(kDrivingStats));
        if (ImGui::Button(T("Max driving skills"), ImVec2(-1, 0))) { MaxStats(kDrivingStats, IM_ARRAYSIZE(kDrivingStats)); Notify(T("Driving skills maxed")); }

        SectionTitle(T("Weapon skills"));
        StatSliders("##wskill", kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills));
        if (ImGui::Button(T("Hitman all weapons"), ImVec2(-1, 0))) { MaxStats(kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills)); Notify(T("All weapon skills maxed")); }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Gameplay
// ---------------------------------------------------------------------------
static void SliderF(const char* id, float* v, float lo, float hi, const char* fmt) { ImGui::SetNextItemWidth(-1); ImGui::SliderFloat(id, v, lo, hi, fmt); }
static void SliderI(const char* id, int* v, int lo, int hi, const char* fmt) { ImGui::SetNextItemWidth(-1); ImGui::SliderInt(id, v, lo, hi, fmt); }

static void TabGameplay()
{
    GameplayCfg& p = g_toggles.play;
    if (!ImGui::BeginChild("##playscroll")) { ImGui::EndChild(); return; }

    SectionTitle(T("Rules"));
    if (ImGui::BeginTable("##rules", 2)) {
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!Game::CanPreventBusted());
        Toggle(T("Never get busted"), &p.neverBusted, T("Cops can't arrest you."));
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!Game::CanFreeDriveBy());
        Toggle(T("Free-look drive-by"), &p.freeDriveBy, T("In the driver's seat with a submachine gun: hold right mouse to aim anywhere, left mouse to shoot."));
        ImGui::EndDisabled();
        ImGui::TableNextColumn(); Toggle(T("Stay on bikes"), &p.noFallOffBike, T("CJ doesn't fall off bikes and motorbikes."));
        ImGui::TableNextColumn(); Toggle(T("Unlimited stamina"), &p.unlimitedStamina);
        ImGui::TableNextColumn(); Toggle(T("Unlimited breath"), &p.infiniteBreath);
        ImGui::TableNextColumn(); Toggle(T("Fast reload"), &p.fastReload);
        ImGui::TableNextColumn(); Toggle(T("Slow motion while aiming"), &p.slowmoAiming);
        ImGui::BeginDisabled(!Game::FireInstantHitAddress());
        ImGui::TableNextColumn(); Toggle(T("No bullet spread"), &p.noSpread, T("Every shot goes exactly where the crosshair points, shotguns included."));
        ImGui::EndDisabled();
        ImGui::TableNextColumn(); Toggle(T("Move while aiming"), &p.moveWhileAiming, T("Walk while aiming and shooting with any firearm, at any weapon skill. Scoped and heavy weapons are left as they are."));
        ImGui::BeginDisabled(!Game::AnimGroupsAddress());
        ImGui::TableNextColumn(); Toggle(T("Sprint with rifles and shotguns"), &p.armedSprint, T("Your sprint key also works while carrying an M4, AK-47, rifle or shotgun."));
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    if (p.freeDriveBy) SliderF("##dbsens", &p.driveBySens, 0.1f, 3.0f, T("Drive-by mouse speed x%.2f"));
    if (p.slowmoAiming) SliderF("##slowaim", &p.slowmoScale, 0.1f, 0.9f, T("Aiming speed %.2fx"));

    SectionTitle(T("Exploding bullets"));
    Toggle(T("Exploding bullets"), &p.explodingBullets, T("Every bullet you fire explodes where it lands."));
    if (p.explodingBullets) {
        const char* types[] = { T("Grenade"), T("Molotov"), T("Rocket"), T("Weak rocket"), T("Car"), T("Quick car"), T("Boat"), T("Aircraft"), T("Mine"), T("Object"), T("Tank"), T("Small") };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##exptype", &p.explosionType, types, IM_ARRAYSIZE(types));
    }

    SectionTitle(T("Custom health, armour and ammo"));
    Toggle(T("Custom max health"), &p.customHealth);
    if (p.customHealth) SliderI("##maxhp", &p.maxHealth, 10, 1000, T("Max health %d"));
    Toggle(T("Custom max armour"), &p.customArmour);
    if (p.customArmour) SliderI("##maxar", &p.maxArmour, 10, 1000, T("Max armour %d"));
    Toggle(T("Custom reserve ammo"), &p.customAmmo, T("Tops up every gun to this much spare ammo."));
    if (p.customAmmo) SliderI("##res", &p.reserveAmmo, 1, 9999, T("Reserve ammo %d"));
    SliderF("##clip", &p.clipMult, 0.5f, 10.0f, T("Magazine size x%.1f"));
    SliderI("##dmgtaken", &p.damageTaken, 0, 300, T("Damage you take %d%%"));

    SectionTitle(T("Regeneration"));
    Toggle(T("Health regeneration"), &p.healthRegen);
    if (p.healthRegen) {
        SliderF("##hpa", &p.healthAmount, 0.5f, 50.0f, T("+%.1f health"));
        SliderF("##hpe", &p.healthEvery, 0.1f, 10.0f, T("every %.1f s"));
        SliderF("##hpd", &p.healthDelay, 0.0f, 30.0f, T("starts %.1f s after a hit"));
    }
    Toggle(T("Armour regeneration"), &p.armourRegen);
    if (p.armourRegen) {
        SliderF("##ara", &p.armourAmount, 0.5f, 50.0f, T("+%.1f armour"));
        SliderF("##are", &p.armourEvery, 0.1f, 10.0f, T("every %.1f s"));
        SliderF("##ard", &p.armourDelay, 0.0f, 30.0f, T("starts %.1f s after a hit"));
    }
    Toggle(T("Ammo regeneration"), &p.ammoRegen, T("Refills the weapon in your hands."));
    if (p.ammoRegen) {
        SliderI("##amma", &p.ammoAmount, 1, 100, T("+%d rounds"));
        SliderF("##amme", &p.ammoEvery, 0.1f, 10.0f, T("every %.1f s"));
    }

    SectionTitle(T("Rewards for kills"));
    Toggle(T("Lifesteal"), &p.lifesteal, T("Heals you when you kill someone."));
    if (p.lifesteal) {
        SliderF("##lsh", &p.healthPerKill, 0.0f, 200.0f, T("+%.0f health per kill"));
        SliderF("##lsa", &p.armourPerKill, 0.0f, 200.0f, T("+%.0f armour per kill"));
    }
    SliderI("##mpk", &p.moneyPerKill, 0, 10000, T("$%d per kill"));
    ImGui::TextDisabled(T("Kills this session: %d"), Combat::Kills());
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
static void AdvancedToggle(bool* v)
{
    ImGui::PushID(v);        // the same button appears in both sections
    if (ImGui::SmallButton(*v ? T("Hide advanced") : T("Advanced..."))) *v = !*v;
    ImGui::PopID();
}

static void TabCombat()
{
    if (g_multiplayer) { ImGui::TextWrapped("%s", T("Disabled while a multiplayer client is loaded.")); return; }
    AimbotCfg& a = g_toggles.aim;
    EspCfg& e = g_toggles.esp;
    if (!ImGui::BeginChild("##combatscroll")) { ImGui::EndChild(); return; }

    SectionTitle(T("Aimbot"));
    Toggle(T("Enable aimbot"), &a.enabled, T("Hold right mouse to aim: the camera locks on to cops and enemies."));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.72f);
    AdvancedToggle(&a.advanced);
    ImGui::BeginDisabled(!g_toggles.play.freeDriveBy);
    Toggle(T("Aim during free-look drive-by"), &a.driveBy, T("Also locks on from the driver's seat. Needs Free-look drive-by on the Gameplay tab."));
    ImGui::EndDisabled();
    if (a.advanced) {
        const char* modes[] = { T("While aiming (right mouse)"), T("While holding a key"), T("Whenever a weapon is aimed") };
        ImGui::SetNextItemWidth(-1); ImGui::Combo("##amode", &a.mode, modes, IM_ARRAYSIZE(modes));
        if (a.mode == 1) Keybind(T("Aim key"), &a.key);
        SliderF("##afov", &a.fov, 1.0f, 90.0f, T("Field of view %.0f deg"));
        SliderF("##asm", &a.smooth, 0.0f, 0.95f, T("Smoothing %.2f (0 = instant)"));
        SliderF("##adist", &a.maxDist, 10.0f, 500.0f, T("Max distance %.0f m"));
        const char* bones[] = { T("Head"), T("Chest"), T("Pelvis") };
        ImGui::SetNextItemWidth(HalfWidth()); ImGui::Combo("##abone", &a.bone, bones, IM_ARRAYSIZE(bones));
        ImGui::SameLine();
        const char* prio[] = { T("Closest to crosshair"), T("Nearest to you"), T("Lowest health") };
        ImGui::SetNextItemWidth(-1); ImGui::Combo("##aprio", &a.priority, prio, IM_ARRAYSIZE(prio));
        ImGui::TextDisabled("%s", T("Target"));
        if (ImGui::BeginTable("##atargets", 3)) {
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Cops"), &a.cops);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Gangs"), &a.enemies);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Mission targets"), &a.missionTargets);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Attackers"), &a.attackers);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Civilians"), &a.civilians);
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("##aopts", 2)) {
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Only through clear line of sight"), &a.visibleOnly);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Stay on the same target"), &a.sticky);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Lead moving targets"), &a.prediction);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Ignore people in vehicles"), &a.ignoreInVehicles);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Show field of view circle"), &a.drawFov);
            ImGui::EndTable();
        }
    }

    SectionTitle(T("Wallhack"));
    Toggle(T("Enable wallhack"), &e.enabled, T("Draws boxes with health around cops and enemies."));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.72f);
    AdvancedToggle(&e.advanced);
    Toggle(T("Only show who the aimbot can hit"), &e.onlyAimbot, T("A box then means the aimbot can snap to that person right now."));
    if (e.advanced) {
        if (ImGui::BeginTable("##eopts", 3)) {
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Boxes"), &e.box);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Health bar"), &e.health);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Armour bar"), &e.armour);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Distance"), &e.distance);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Labels"), &e.label);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Lines"), &e.snapline);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("%s", T("Who to show"));
        if (ImGui::BeginTable("##ewho", 4)) {
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Cops"), &e.cops);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Enemies"), &e.enemies);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Civilians"), &e.civilians);
            ImGui::TableNextColumn(); ImGui::Checkbox(T("Friends"), &e.friends);
            ImGui::EndTable();
        }
        SliderF("##edist", &e.maxDist, 10.0f, 500.0f, T("Max distance %.0f m"));
        SliderF("##ethick", &e.thickness, 0.5f, 5.0f, T("Line thickness %.1f"));
        ImGuiColorEditFlags f = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar;
        ImGui::ColorEdit4(T("Cops"), e.colCop, f); ImGui::SameLine();
        ImGui::ColorEdit4(T("Enemies"), e.colEnemy, f); ImGui::SameLine();
        ImGui::ColorEdit4(T("Civilians"), e.colCiv, f); ImGui::SameLine();
        ImGui::ColorEdit4(T("Friends"), e.colFriend, f);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Missions
// ---------------------------------------------------------------------------
static void TabMissions()
{
    if (!Game::HasMissions()) { ImGui::TextWrapped("%s", T("Mission tools aren't available on this version of the game.")); return; }
    bool on = Game::OnMission();

    SectionTitle(T("Current mission"));
    if (on) { std::string n = Game::MissionName(); ImGui::Text(T("On a mission: %s"), n.empty() ? "?" : n.c_str()); }
    else ImGui::TextDisabled("%s", T("Not on a mission"));

    ImGui::BeginDisabled(!on);
    if (ImGui::Button(T("Complete mission"), ImVec2(HalfWidth(), 0)))
        Features::Queue([] { Notify("%s", Game::MissionPass() ? T("Mission completed") : T("This mission can't be skipped")); });
    ImGui::SameLine();
    if (ImGui::Button(T("Fail mission"), ImVec2(-1, 0)))
        Features::Queue([] { Notify("%s", Game::MissionFail() ? T("Mission failed") : T("This mission has no fail routine")); });
    if (ImGui::Button(T("Kill targets"), ImVec2(HalfWidth(), 0)))
        Features::Queue([] {
            std::vector<Blip> blips; Game::GetBlips(blips);
            int n = 0;
            for (const Blip& b : blips) {
                if (b.colour != 0 && b.colour != 5) continue;
                if (b.type == 2) { if (void* p = Game::PedFromHandle(b.handle)) if (!Game::PedDead(p)) { Game::KillPed(p); ++n; } }
                else if (b.type == 1) { if (void* v = Game::VehicleFromHandle(b.handle)) { Game::ExplodeVehicle(v); ++n; } }
            }
            Notify(T("%d target(s) eliminated"), n);
        });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(T("Teleport to objective"), ImVec2(-1, 0)))
        Features::Queue([] {
            CVector p;
            if (GPS::Objective(p)) { Game::Teleport(p, true); Notify("%s", T("Teleported to objective")); }
            else Notify("%s", T("No objective on the map"));
        });
    if (ImGui::Button(T("Fix stuck black screen"), ImVec2(-1, 0))) Features::Queue([] { Game::RecoverScreen(); });
    Toggle(T("Protect allies"), &g_toggles.mission.protectAllies, T("People marked green or blue on the radar can't be hurt."));

    SectionTitle(T("Play any mission"));
    ImGui::Checkbox(T("Don't change story progress"), &g_toggles.mission.preserveStory);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Your save carries on as if the replay never happened. Rewards still count."));
    if (!g_toggles.mission.preserveStory) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s", T("Off: passing a mission out of order can break your story progress."));
    static int group = -1, picked = -1;
    static char filter[32] = "";
    ImGui::SetNextItemWidth(HalfWidth());
    if (ImGui::BeginCombo("##mg", group < 0 ? T("All") : T(kMissionGroups[group]))) {
        if (ImGui::Selectable(T("All"), group < 0)) group = -1;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kMissionGroups); ++i) if (ImGui::Selectable(T(kMissionGroups[i]), group == i)) group = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mf", T("Search..."), filter, sizeof(filter));

    auto start = [](const MissionEntry& m) {
        int idx = m.index; bool keep = g_toggles.mission.preserveStory; std::string title = m.title;
        Features::Queue([idx, keep, title] {
            if (Game::StartMission(idx, keep)) Notify(T("Starting: %s"), title.c_str());
            else Notify("%s", T("Finish the current mission first"));
        });
    };
    float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##mlist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        int last = -1;
        for (const MissionEntry& m : kMissions) {
            if ((group >= 0 && m.group != group) || !ContainsI(m.title, filter)) continue;
            if (m.group != last) { last = m.group; ImGui::TextDisabled("%s", T(kMissionGroups[m.group])); }
            char l[96]; snprintf(l, sizeof(l), "%s##m%d", m.title, m.index);
            ImGui::Indent();
            if (ImGui::Selectable(l, picked == m.index, ImGuiSelectableFlags_AllowDoubleClick)) {
                picked = m.index;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) start(m);
            }
            ImGui::Unindent();
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(picked < 0 || on);
    if (ImGui::Button(on ? T("Finish the current mission first") : T("Start selected mission"), ImVec2(-1, 0)))
        for (const MissionEntry& m : kMissions) if (m.index == picked) start(m);
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
static bool g_cfgDirty = true, g_resetLayout = false;
static std::string g_cfgSelected;

static void ConfigSection()
{
    SectionTitle(T("Settings files"));
    if (ImGui::Checkbox(T("Save settings automatically"), &Config::AutoSaveEnabled())) Config::SaveMeta();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Saves when you close the menu."));
    if (ImGui::Checkbox(T("Use multiple configs"), &Config::MultiEnabled())) Config::SaveMeta();
    if (!Config::MultiEnabled()) {
        if (Config::AutoSaveEnabled()) ImGui::TextDisabled("%s", T("Your settings save automatically when you close the menu."));
        else {
            if (ImGui::Button(T("Save settings"), ImVec2(-1, 0))) { Config::SaveDefault(); Notify("%s", T("Settings saved")); }
            ImGui::TextDisabled("%s", T("Changes are lost when you quit unless you save them."));
        }
        return;
    }
    static char name[48] = "";
    std::string& selected = g_cfgSelected;
    bool& dirty = g_cfgDirty;
    static std::vector<std::string> list;
    if (dirty) { list = Config::List(); dirty = false; }

    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(T("Save")).x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##cfgname", T("New config name"), name, sizeof(name));
    ImGui::SameLine();
    if (ImGui::Button(T("Save"))) {
        std::string n = Config::Sanitize(name);
        if (n.empty()) Notify("%s", T("Type a name first"));
        else if (Config::Save(n)) { Notify(T("Saved '%s'"), n.c_str()); selected = n; name[0] = 0; dirty = true; }
    }
    float h = ImGui::GetTextLineHeightWithSpacing() * 6;
    auto load = [](const std::string& n) { if (Config::Load(n)) Notify(T("Loaded '%s'"), n.c_str()); };
    if (ImGui::BeginChild("##cfglist", ImVec2(0, h), ImGuiChildFlags_Borders)) {
        for (const std::string& n : list) {
            ImGui::PushID(n.c_str());
            if (ImGui::Selectable(n.c_str(), selected == n, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected = n;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) load(n);
            }
            bool isAuto = Config::Autoload() == n;
            if (isAuto || Config::Current() == n) {
                const char* tag = isAuto ? T("AUTOLOAD") : T("loaded");
                float w = ImGui::CalcTextSize(tag).x;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - w - 4);
                ImGui::TextColored(isAuto ? kAccent : ImVec4(0.55f, 0.55f, 0.6f, 1), "%s", tag);
            }
            ImGui::PopID();
        }
        if (list.empty()) ImGui::TextDisabled("%s", T("No configs yet."));
    }
    ImGui::EndChild();
    bool has = !selected.empty();
    ImGui::BeginDisabled(!has);
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    if (ImGui::Button(T("Load"), ImVec2(w, 0))) load(selected);
    ImGui::SameLine();
    if (ImGui::Button(T("Overwrite"), ImVec2(w, 0))) { if (Config::Save(selected)) Notify(T("Saved '%s'"), selected.c_str()); }
    ImGui::SameLine();
    bool isAuto = has && Config::Autoload() == selected;
    if (ImGui::Button(isAuto ? T("No autoload") : T("Autoload"), ImVec2(w, 0))) Config::SetAutoload(isAuto ? "" : selected);
    ImGui::SameLine();
    if (ImGui::Button(T("Delete"), ImVec2(-1, 0))) { if (Config::Delete(selected)) { selected.clear(); dirty = true; } }
    ImGui::EndDisabled();
}

static void TabSettings()
{
    if (!ImGui::BeginChild("##setscroll")) { ImGui::EndChild(); return; }
    ConfigSection();

    SectionTitle(T("Interface"));
    const char* langs[] = { "English", "Polski" };
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##lang", &g_toggles.language, langs, 2);
    ImGuiIO& io = ImGui::GetIO();
    SliderF("##scale", &g_toggles.textScale, 0.7f, 2.0f, T("Text scale %.2f"));
    SliderF("##alpha", &g_toggles.opacity, 0.4f, 1.0f, T("Opacity %.2f"));

    SectionTitle(T("Keys"));
    Keybind(T("Open menu"), &g_toggles.menuKey);
    Keybind(T("Noclip"), &g_toggles.noclipKey);
    Keybind(T("Weapon wheel"), &g_toggles.wheel.key);

    SectionTitle(T("Weapon wheel"));
    Toggle(T("Enable weapon wheel"), &g_toggles.wheel.enabled, T("Hold the key, point with the mouse, release to equip."));
    ImGui::SameLine();
    ImGui::Checkbox(T("Slow time while open"), &g_toggles.wheel.slowmo);
    SliderF("##wscale", &g_toggles.wheel.scale, 0.6f, 1.4f, T("Size x%.2f"));

    SectionTitle(T("GPS"));
    Toggle(T("Route on the minimap"), &g_toggles.gps.enabled, T("Draws the road route to your map marker."));
    ImGui::SameLine();
    ImGui::Checkbox(T("Also to mission objectives"), &g_toggles.gps.toObjective);
    ImGui::Checkbox(T("Show route on foot"), &g_toggles.gps.onFoot);
    ImGui::ColorEdit4(T("Map marker route"), g_toggles.gps.colWaypoint, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(0, 24);
    ImGui::ColorEdit4(T("Mission route"), g_toggles.gps.colMission, ImGuiColorEditFlags_NoInputs);
    SliderF("##gpsw", &g_toggles.gps.width, 1.0f, 8.0f, T("Line width %.1f"));
    ImGui::TextDisabled(T("Status: %s"), T(GPS::Status()));

    SectionTitle(T("Updates"));
    ImGui::Checkbox(T("Tell me when a new version is out"), &g_toggles.updateNotify);
    if (Update::Available()) {
        ImGui::TextColored(kAccent, T("Version %s is available (you have %s)."), Update::Latest().c_str(), RIFFED_VERSION);
        if (ImGui::Button(T("Open download page"), ImVec2(-1, 0))) Update::OpenReleasePage();
    } else {
        ImGui::TextDisabled(T("RiffedMenu %s"), RIFFED_VERSION);
    }

    SectionTitle(T("Reset"));
    if (ImGui::Button(T("Reset to defaults"), ImVec2(HalfWidth(), 0))) ImGui::OpenPopup("###resetconfirm");
    ImGui::SameLine();
    if (ImGui::Button(T("Clear everything"), ImVec2(-1, 0))) ImGui::OpenPopup("###clearconfirm");

    auto confirm = [](const char* id, const char* heading, const char* question, const char* detail, const char* action, auto run) {
        char title[96];
        snprintf(title, sizeof(title), "%s###%s", heading, id);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
        ImGui::TextUnformatted(question);
        ImGui::TextDisabled("%s", detail);
        ImGui::Spacing();
        float w = ImGui::GetFontSize() * 8;
        if (ImGui::Button(action, ImVec2(w, 0))) { run(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(T("Cancel"), ImVec2(w, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    };
    confirm("resetconfirm", T("Reset to defaults"), T("Turn every option back to its default?"),
            T("Your saved configs, favorites and language stay."), T("Reset"), [] {
        Config::ResetOptions();
        Notify("%s", T("Settings reset to defaults"));
    });
    confirm("clearconfirm", T("Clear everything"), T("Put RiffedMenu back as if it was just installed?"),
            T("This deletes all your settings, favorites and saved configs. It can't be undone."), T("Clear everything"), [] {
        Config::FactoryReset();
        Features::RiskClear();
        g_cfgDirty = true;
        g_cfgSelected.clear();
        g_resetLayout = true;
        Notify("%s", T("Everything cleared"));
    });

    SectionTitle(T("Diagnostics"));
    ImGui::Text(T("Game version: %s"), Game::VersionName(Game::Version()));
    ImGui::Text(T("Memory features: %s"), Game::Supported() ? T("enabled") : T("disabled"));
    ImGui::Text(T("In game: %s"), Game::InGame() ? T("yes") : T("no"));
    ImGui::Text(T("FPS: %.0f"), io.Framerate);
    ImGui::TextWrapped(T("Log: %sRiffedMenu.log"), GameDir().c_str());
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------

static void UpdateNotice()
{
    static bool shown = false;
    if (shown || !g_toggles.updateNotify || !Update::Available() || !Game::InGame()) return;
    shown = true;
    Notify(T("RiffedMenu %s is available - see Settings"), Update::Latest().c_str());
}

bool Menu::IsOpen() { return g_open; }
void Features::NotifyPush(const char* text) { Notify("%s", text); }
void Menu::Toggle() { g_open = !g_open; Log("Menu %s", g_open ? "opened" : "closed"); }

void Menu::Render(void* device)
{
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = g_toggles.textScale;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = g_toggles.opacity;
    Combat::Render();
    GPS::Render();
    Wheel::Render(device);
    UpdateNotice();
    DrawToasts();
    g_preview.active = false;   // editors re-request it below while their tab is visible
    if (!g_open) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float sc = ImGui::GetFontSize() / 16.0f;
    ImGuiCond layoutCond = g_resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    g_resetLayout = false;
    ImGui::SetNextWindowSize(ImVec2(640 * sc, 680 * sc), layoutCond);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 60 * sc, vp->WorkPos.y + 60 * sc), layoutCond);
    ImGui::SetNextWindowSizeConstraints(ImVec2(460 * sc, 400 * sc), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("RiffedMenu", &g_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) { ImGui::End(); return; }

    // Header: title, subtitle, close button
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted("RiffedMenu");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("GTA San Andreas trainer"));
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight());
    if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0))) g_open = false;

    if (!Game::Supported()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.12f, 0.12f, 1));
        ImGui::BeginChild("##warn", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::TextWrapped(T("Unsupported executable: %s. RiffedMenu supports GTA:SA v1.0 US and the 2014 Steam re-release. Game features are disabled so nothing gets corrupted."), Game::VersionName(Game::Version()));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else if (!Game::InGame()) {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1), "%s", T("Load a save to use the trainer."));
    }

    bool live = Game::InGame();
    auto gameTab = [live](const char* name, void (*draw)()) {
        if (!ImGui::BeginTabItem(name)) return;
        ImGui::BeginDisabled(!live);
        draw();
        ImGui::EndDisabled();
        ImGui::EndTabItem();
    };
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        gameTab(T("Player"), TabPlayer);
        gameTab(T("Vehicle"), TabVehicle);
        gameTab(T("Customize"), TabCustomize);
        gameTab(T("Weapons"), TabWeapons);
        gameTab(T("Gameplay"), TabGameplay);
        gameTab(T("Combat"), TabCombat);
        gameTab(T("Missions"), TabMissions);
        gameTab(T("Outfit"), TabOutfit);
        gameTab(T("Teleport"), TabTeleport);
        gameTab(T("World"), TabWorld);
        gameTab(T("Stats"), TabStats);
        if (ImGui::BeginTabItem(T("Settings"))) { TabSettings(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
