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
#include "imgui_internal.h"
#include "theme.h"
#include "widgets.h"
#include "icons.h"
#include "blur.h"
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
using Theme::S;

// ---------------------------------------------------------------------------
// Toasts: slide in at the bottom right, stack upwards, fade out.
// ---------------------------------------------------------------------------
struct Toast { std::string text; int state; double start; float y; };   // state: -1 message, 0 off, 1 on
static std::deque<Toast> g_toasts;
static const double kToastLife = 2.8;

static void PushToast(const std::string& text, int state)
{
    double now = ImGui::GetTime();
    // Flipping the same option again replaces its toast instead of stacking a new one.
    for (auto it = g_toasts.begin(); it != g_toasts.end(); ++it)
        if (it->text == text && (state >= 0 || it->state < 0)) { it->state = state; it->start = now - 0.3; return; }   // already slid in
    g_toasts.push_back({ text, state, now, -1.0f });
    if (g_toasts.size() > 5) g_toasts.pop_front();
}

static void Notify(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    PushToast(buf, -1);
}

static void NotifyToggle(const char* label, bool on) { PushToast(label, on ? 1 : 0); }

static void DrawToasts()
{
    using namespace Theme;
    double now = ImGui::GetTime();
    while (!g_toasts.empty() && now - g_toasts.front().start > kToastLife + 0.35) g_toasts.pop_front();
    if (g_toasts.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float h = S(46), gap = S(8), maxW = std::min(S(420), ds.x * 0.4f);
    float slot = 0;
    for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
        Toast& t = g_toasts[i];
        float age = (float)(now - t.start), life = (float)kToastLife;
        float in = UI::AnimationsOn() ? std::clamp(age / 0.28f, 0.0f, 1.0f) : 1.0f;
        float out = std::clamp((life + 0.35f - age) / 0.35f, 0.0f, 1.0f);
        in = 1.0f - powf(1.0f - in, 3.0f);
        if (t.y < 0) t.y = slot;
        UI::Ease(t.y, slot, 12.0f);
        slot += h + gap;

        const char* pill = t.state < 0 ? nullptr : t.state ? T("ON") : T("OFF");
        float pillW = pill ? ImGui::CalcTextSize(pill).x + S(18) : 0;
        float textW = std::min(ImGui::CalcTextSize(t.text.c_str()).x, maxW - S(56) - pillW);
        float w = textW + S(56) + pillW;
        float x1 = ds.x - S(22) + (1.0f - in) * (w + S(40)), x0 = x1 - w;
        float y1 = ds.y - S(26) - t.y, y0 = y1 - h;
        float a = std::min(in, out);
        auto C = [&](const ImVec4& c, float k = 1.0f) { return ImGui::GetColorU32(Alpha(c, k * a)); };

        for (int k = 1; k <= 4; ++k) dl->AddRect(ImVec2(x0 - k, y0 - k + S(3)), ImVec2(x1 + k, y1 + k + S(3)), C(P.shadow, 0.10f), S(12) + k, 0, S(2));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), C(ImVec4(0.09f, 0.08f, 0.07f, 0.95f)), S(12));
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), C(ImVec4(1, 1, 1, 0.08f)), S(12), 0, 1.0f);
        ImVec4 mark = t.state == 0 ? P.textDim : P.accent;
        dl->AddRectFilled(ImVec2(x0 + S(10), y0 + S(12)), ImVec2(x0 + S(13), y1 - S(12)), C(mark), S(2));
        Icons::Draw(dl, t.state < 0 ? Icons::Bell : Icons::Check, ImVec2(x0 + S(29), (y0 + y1) * 0.5f), S(16), C(mark));
        UI::TextFit(dl, ImVec2(x0 + S(44), (y0 + y1) * 0.5f - ImGui::GetFontSize() * 0.5f), C(P.text), t.text.c_str(), nullptr, textW);
        if (pill) {
            ImVec2 pa(x1 - pillW - S(10), y0 + S(12)), pb(x1 - S(10), y1 - S(12));
            dl->AddRectFilled(pa, pb, C(t.state ? P.accent : ImVec4(1, 1, 1, 0.12f)), (pb.y - pa.y) * 0.5f);
            ImGui::PushFont(Small);
            ImVec2 ts = ImGui::CalcTextSize(pill);
            dl->AddText(ImVec2((pa.x + pb.x - ts.x) * 0.5f, (pa.y + pb.y - ts.y) * 0.5f), C(t.state ? P.onAccent : P.textMuted), pill);
            ImGui::PopFont();
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers shared by the tabs (thin wrappers over the custom widgets)
// ---------------------------------------------------------------------------
static void SectionTitle(const char* text) { UI::Section(text); }

static bool Toggle(const char* label, bool* v, const char* hint = nullptr)
{
    bool changed = UI::Checkbox(label, v, hint);
    if (changed) NotifyToggle(label, *v);
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
    ImU32 col = on ? Theme::Col(Theme::P.accent) : hot ? Theme::Col(Theme::P.text) : Theme::Col(Theme::P.textDim);
    if (on) dl->AddConcavePolyFilled(pts, 10, col);
    else dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, S(1.3f));
    return clicked;
}

// Click the chip, then press the key to bind. Escape cancels.
static bool Keybind(const char* label, int* vk)
{
    char name[48] = "";
    if (*vk == VK_XBUTTON1) snprintf(name, sizeof(name), "%s", T("Mouse side button 1"));
    else if (*vk == VK_XBUTTON2) snprintf(name, sizeof(name), "%s", T("Mouse side button 2"));
    else if (*vk == VK_MBUTTON) snprintf(name, sizeof(name), "%s", T("Middle mouse button"));
    else {
        UINT sc = MapVirtualKeyA(*vk, MAPVK_VK_TO_VSC);
        LONG lp = sc << 16;
        if (*vk == VK_INSERT || *vk == VK_DELETE || *vk == VK_HOME || *vk == VK_END || *vk == VK_PRIOR || *vk == VK_NEXT) lp |= 1 << 24;
        if (!GetKeyNameTextA(lp, name, sizeof(name))) snprintf(name, sizeof(name), "#%d", *vk);
    }
    return UI::KeyChip(label, vk, T("Press a key..."), name);
}

static float HalfWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }
static float ThirdWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f; }

// Width of the widest label, so a name column fits English and Polish alike.
static float LabelColumn(const char* const* names, int count, float maxShare = 0.5f)
{
    float w = 0;
    for (int i = 0; i < count; ++i) w = std::max(w, ImGui::CalcTextSize(T(names[i])).x);
    return std::min(w + S(14), ImGui::GetContentRegionAvail().x * maxShare);
}

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
    UI::SliderFloat("##ncspeed", &g_toggles.noclipSpeed, 5.0f, 100.0f, T("Noclip speed %.0f m/s"));
    ImGui::SameLine();
    if (UI::Button(T("Equip jetpack"), ImVec2(-1, 0))) {
        if (Game::PlayerVehicle()) Notify(T("Get out of the vehicle first"));
        else { Features::Queue([] { Game::GiveJetpack(); }); Notify(T("Jetpack equipped")); }
    }

    SectionTitle(T("Health & armour"));
    if (ped) {
        float hp = Game::PedHealth(ped), maxHp = Game::PedMaxHealth(ped), ar = Game::PedArmour(ped);
        char ov[32];
        snprintf(ov, sizeof(ov), "%.0f / %.0f", hp, maxHp);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.25f, 0.25f, 1));
        UI::ProgressBar(maxHp > 0 ? hp / maxHp : 0, ImVec2(HalfWidth(), 0), ov);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        snprintf(ov, sizeof(ov), T("Armour %.0f"), ar);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.6f, 0.95f, 1));
        UI::ProgressBar(ar / 100.0f, ImVec2(-1, 0), ov);
        ImGui::PopStyleColor();
    }
    if (UI::Button(T("Full health"), ImVec2(HalfWidth(), 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedHealth(p) = Game::PedMaxHealth(p); });
    ImGui::SameLine();
    if (UI::Button(T("Full armour"), ImVec2(-1, 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedArmour(p) = 100.0f; });

    SectionTitle(T("Wanted level"));
    int wanted = Game::WantedLevel();
    ImGui::BeginDisabled(g_toggles.freezeWanted);
    ImGui::SetNextItemWidth(-1);
    if (UI::SliderInt("##wanted", &wanted, 0, 6, T("%d stars")))
        Features::Queue([wanted] { Game::SetWantedLevel(wanted); });
    ImGui::EndDisabled();
    ImGui::BeginDisabled(g_toggles.neverWanted);
    if (Toggle(T("Freeze wanted level"), &g_toggles.freezeWanted, T("Keeps your wanted level at the chosen number of stars.")) && g_toggles.freezeWanted)
        g_toggles.frozenWanted = wanted;
    UI::BeginSub(g_toggles.freezeWanted);
    ImGui::SetNextItemWidth(-1);
    UI::SliderInt("##frozenwanted", &g_toggles.frozenWanted, 0, 6, T("%d stars"));
    UI::EndSub();
    ImGui::BeginDisabled(g_toggles.freezeWanted);
    Toggle(T("Max wanted level"), &g_toggles.limitWanted, T("Your wanted level can still rise and fall, but never above this many stars."));
    UI::BeginSub(g_toggles.limitWanted);
    ImGui::SetNextItemWidth(-1);
    UI::SliderInt("##maxwanted", &g_toggles.maxWanted, 0, 6, T("%d stars"));
    UI::EndSub();
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    SectionTitle(T("Money"));
    static int moneyInput = 1000000;
    ImGui::Text(T("Current: $%d"), ped ? Game::Money() : 0);
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::InputInt("##money", &moneyInput, 10000, 100000);
    ImGui::SameLine();
    if (UI::Button(T("Set money"), ImVec2(-1, 0))) { int m = moneyInput; Features::Queue([m] { Game::Money() = m; }); Notify(T("Money set to $%d"), m); }
    float w4 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    const int adds[] = { 10000, 100000, 1000000, 10000000 };
    const char* addLabels[] = { "+$10K", "+$100K", "+$1M", "+$10M" };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (UI::Button(addLabels[i], ImVec2(w4, 0))) {
            int a = adds[i];
            Features::Queue([a] { int& m = Game::Money(); m = (m > 999999999 - a) ? 999999999 : m + a; });
            Notify(T("Added $%d"), a);
        }
    }
    if (UI::Button(T("Set to maximum"), ImVec2(-1, 0))) {
        Features::Queue([] { Game::Money() = 999999999; });        // the game's own cap
        Notify(T("Money set to $%d"), 999999999);
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

static ImVec4 Swatch(int i);

// Car colour: a chip showing the colour (click it for the whole palette) and a slider to step through it.
static bool ColourSlider(const char* id, const char* fmt, int* c, float width)
{
    using namespace Theme;
    bool changed = false;
    ImGui::PushID(id);
    const float h = ImGui::GetFrameHeight(), chipW = h * 1.6f, gap = S(8);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##chip", ImVec2(chipW, h))) ImGui::OpenPopup("##palette");
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetTooltip("%s", T("Pick from the palette"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + chipW, p.y + h), Col(Swatch(*c)), S(8));
    dl->AddRect(p, ImVec2(p.x + chipW, p.y + h), Col(hov ? P.accent : ImVec4(1, 1, 1, 0.25f)), S(8), 0, hov ? S(2) : 1.0f);
    Icons::Draw(dl, Icons::ChevronDown, ImVec2(p.x + chipW - S(9), p.y + h * 0.5f), S(9), IM_COL32(255, 255, 255, 200));
    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(std::max(S(60), width - chipW - gap));
    changed |= UI::SliderInt("##v", c, 0, 126, fmt);

    if (ImGui::BeginPopup("##palette")) {
        int count = (int)GameData::CarColours().size();
        float sz = h * 0.95f, sp = S(4);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
        for (int i = 0; i < count; ++i) {
            if (i % 16) ImGui::SameLine();
            ImGui::PushID(i);
            ImVec2 q = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##col", ImVec2(sz, sz))) { *c = i; changed = true; ImGui::CloseCurrentPopup(); }
            bool on = ImGui::IsItemHovered(), cur = *c == i;
            if (on) ImGui::SetTooltip(T("Colour %d"), i);
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            pdl->AddRectFilled(q, ImVec2(q.x + sz, q.y + sz), Col(Swatch(i)), S(5));
            if (cur || on) pdl->AddRect(ImVec2(q.x - S(1.5f), q.y - S(1.5f)), ImVec2(q.x + sz + S(1.5f), q.y + sz + S(1.5f)),
                                        Col(cur ? P.text : P.accent), S(6), 0, S(2));
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

static void TabVehicle()
{
    void* veh = Game::PlayerVehicle();

    SectionTitle(T("Current vehicle"));
    if (veh) {
        float hp = Game::VehicleHealth(veh);
        char ov[48];
        snprintf(ov, sizeof(ov), T("Model %d  -  %.0f HP"), Game::VehicleModel(veh), hp);
        UI::ProgressBar(std::clamp(hp / 1000.0f, 0.0f, 1.0f), ImVec2(-1, 0), ov);
    } else {
        ImGui::TextDisabled(T("Not in a vehicle"));
    }
    if (ImGui::BeginTable("##vtoggles", 2)) {
        ImGui::TableNextColumn(); Toggle(T("Vehicle god mode"), &g_toggles.vehicleGod, T("Proofs + health lock on the vehicle you're in"));
        ImGui::TableNextColumn(); Toggle(T("Auto repair"), &g_toggles.autoRepair, T("Fixes visual damage as soon as it happens"));
        ImGui::TableNextColumn(); Toggle(T("Auto flip"), &g_toggles.autoFlip, T("Turns your car back onto its wheels when it ends up on its roof."));
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
    if (UI::Button(T("Repair"), ImVec2(ThirdWidth(), 0))) { Features::Queue([] { Game::RepairVehicle(); }); Notify(T("Vehicle repaired")); }
    ImGui::SameLine();
    if (UI::Button(T("Flip upright"), ImVec2(ThirdWidth() * 1.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, 0)))
        Features::Queue([] { Game::FlipVehicle(); });
    ImGui::SameLine();
    if (UI::Button(T("Boost"), ImVec2(-1, 0))) Features::Queue([] { Game::BoostVehicle(1.6f); });
    if (veh) {
        int c1 = Game::VehicleColor(veh, 0), c2 = Game::VehicleColor(veh, 1);
        bool recolor = ColourSlider("##c1", T("Primary colour %d"), &c1, HalfWidth());
        ImGui::SameLine();
        recolor |= ColourSlider("##c2", T("Secondary colour %d"), &c2, ImGui::GetContentRegionAvail().x);
        if (recolor) Features::Queue([c1, c2] { Game::SetVehicleColors(c1, c2); });
    }
    ImGui::EndDisabled();

    SectionTitle(T("Spawner"));
    static char filter[64] = "";
    static int category = 0;      // 0 All, 1 Favorites, 2+ kVehicleCategories[n-1]
    static int selected = -1;
    ImGui::SetNextItemWidth(HalfWidth());
    UI::SearchBox("##vfilter", T("Search vehicles..."), filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    {
        auto catName = [](int i) { return i == 0 ? T("All") : i == 1 ? T("Favorites") : T(kVehicleCategories[i - 1]); };
        if (ImGui::BeginCombo("##vcat", catName(category))) {
            int n = (int)IM_ARRAYSIZE(kVehicleCategories) + 1;
            for (int i = 0; i < n; ++i) if (UI::Selectable(catName(i), i == category)) category = i;
            ImGui::EndCombo();
        }
    }
    UI::Checkbox(T("Spawn inside"), &g_toggles.spawnInside);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Puts you in the driver's seat. Doors are always unlocked."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    UI::InputTextWithHint("##plate", T("Custom plate (8 chars, cars only)"), g_toggles.plate, sizeof(g_toggles.plate));

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
    if (UI::BeginChild("##vlist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
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
            if (UI::Selectable(label, selected == idx, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap)) {
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
    UI::EndChild();
    if (toggleFav >= 0) {
        auto it = std::find(favs.begin(), favs.end(), toggleFav);
        if (it != favs.end()) favs.erase(it); else favs.push_back(toggleFav);
    }
    if (swapA >= 0) std::iter_swap(std::find(favs.begin(), favs.end(), swapA), std::find(favs.begin(), favs.end(), swapB));

    ImGui::BeginDisabled(selected < 0);
    if (UI::Button(selected >= 0 ? T("Spawn selected") : T("Select a vehicle (double-click spawns)"), ImVec2(-1, 0)) && selected >= 0) {
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

    UI::Checkbox(label, &enabled[k]);
    ImGui::SameLine();
    UI::Checkbox(T("Auto-rotate"), &spin[k]);
    if (enabled[k]) {
        if (spin[k]) yaw[k] = fmodf(yaw[k] + ImGui::GetIO().DeltaTime * 25.0f, 360.0f);
        ImGui::SetNextItemWidth(HalfWidth());
        UI::SliderFloat("##yaw", &yaw[k], 0.0f, 360.0f, T("Angle %.0f deg"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        UI::SliderFloat("##zoom", &zoom[k], 0.5f, 2.5f, T("Distance x%.1f"));
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
    UI::RadioButton(T("Primary"), &target, 0);
    ImGui::SameLine(); ImGui::ColorButton("##p", Swatch(c[0]), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight()));
    ImGui::SameLine(0, 24);
    UI::RadioButton(T("Secondary"), &target, 1);
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
        if (UI::Button(T("None"), ImVec2(w, 0))) Features::Queue([] { Game::SetPaintjob(-1); });
        for (int j = 0; j < jobs; ++j) {
            ImGui::SameLine();
            char l[16]; snprintf(l, sizeof(l), T("Paintjob %d"), j + 1);
            if (UI::Button(l, ImVec2(w, 0))) Features::Queue([j] { Game::SetPaintjob(j); });
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
        std::vector<const char*> cats;
        for (const TuningPart& tp : parts) cats.push_back(tp.category.c_str());
        ImGui::TableSetupColumn("cat", ImGuiTableColumnFlags_WidthFixed, LabelColumn(cats.data(), (int)cats.size(), 0.4f));
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
                if (UI::Selectable(T("Stock"), cur == -1)) apply(-1);
                for (size_t k = i; k < j; ++k) {
                    char l[64]; snprintf(l, sizeof(l), "%s##%d", parts[k].name.c_str(), parts[k].id);
                    if (UI::Selectable(l, cur == parts[k].id)) apply(parts[k].id);
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            i = j;
        }
        ImGui::EndTable();
    }
    if (UI::Button(T("Remove all parts fitted here"), ImVec2(-1, 0))) {
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
    if (UI::Button(T("Save outfit"), ImVec2(HalfWidth(), 0))) { Features::Queue([] { Game::SaveOutfit(); }); Notify(T("Outfit saved")); }
    ImGui::SameLine();
    if (UI::Button(T("Revert to saved outfit"), ImVec2(-1, 0))) Features::Queue([] { Game::RestoreOutfit(); });

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
            if (any && UI::Selectable(T(GameData::BodyPartName(p)), p == part)) part = p;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    UI::SearchBox("##cf", T("Search..."), filter, sizeof(filter));

    bool removable = part >= 4;   // torso/hair/legs/shoes always need something; pick a basic item instead
    float listH = ImGui::GetTextLineHeightWithSpacing() * 9;
    if (UI::BeginChild("##clothes", ImVec2(0, listH), ImGuiChildFlags_Borders)) {
        if (removable && canDress && UI::Selectable(T("(none - remove)"))) { int p = part; Features::Queue([p] { Game::RemoveClothes(p); }); }
        for (auto& it : items) {
            if (it.part != part || !ContainsI(it.texture.c_str(), filter)) continue;
            char l[64]; snprintf(l, sizeof(l), "%s##%s%s", it.texture.c_str(), it.texture.c_str(), it.model.c_str());
            std::string risk = "clothes:" + it.texture;
            bool bad = Features::RiskIsBad(risk);
            ImGui::BeginDisabled(bad || !canDress);
            if (UI::Selectable(l)) {
                std::string t = it.texture, m = it.model; int p = it.part;
                Features::Queue([t, m, p, risk] { Features::RiskBegin(risk); Game::GiveClothes(t.c_str(), m.c_str(), p); });
            }
            ImGui::EndDisabled();
            if (!it.model.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(T("model: %s"), it.model.c_str());
        }
    }
    UI::EndChild();
    ImGui::TextDisabled(T("Click an item to put it on. Clothes only show on CJ's own skin."));

    SectionTitle(T("Skin"));
    static char skinFilter[32] = "";
    if (UI::Button(T("Back to CJ"), ImVec2(HalfWidth(), 0))) Features::Queue([] { Game::SetSkin(0); });
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    UI::SearchBox("##sf", T("Search skins..."), skinFilter, sizeof(skinFilter));
    if (UI::BeginChild("##skins", ImVec2(0, listH), ImGuiChildFlags_Borders)) {
        for (auto& s : GameData::Skins()) {
            if (!ContainsI(s.name.c_str(), skinFilter)) continue;
            char l[64]; snprintf(l, sizeof(l), "%-12s  %d##sk%d", s.name.c_str(), s.id, s.id);
            std::string risk = "skin:" + std::to_string(s.id);
            ImGui::BeginDisabled(Features::RiskIsBad(risk));
            if (UI::Selectable(l)) {
                if (Game::PlayerVehicle()) Notify(T("Get out of the vehicle first"));
                else { int id = s.id; Features::Queue([id, risk] { Features::RiskBegin(risk); Game::SetSkin(id); }); }
            }
            ImGui::EndDisabled();
        }
    }
    UI::EndChild();
}

static void TabWeapons()
{
    int& ammoMul = g_toggles.ammoMul;
    SectionTitle(T("Options"));
    Toggle(T("Infinite ammo / no reload"), &g_toggles.infiniteAmmo);
    ImGui::SetNextItemWidth(-1);
    UI::SliderInt("##ammomul", &ammoMul, 1, 10, T("Ammo x%d"));

    ImGui::SetNextItemWidth(-1);
    UI::SliderFloat("##dmg", &g_toggles.damageMult, 0.1f, 10.0f, T("Weapon damage x%.1f"), ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(T("Applies to every weapon for everyone - enemies too."));
    const float presets[] = { 0.5f, 1.0f, 2.0f, 5.0f, 100.0f };
    const char* presetLabels[] = { "x0.5", T("Normal"), "x2", "x5", T("One-hit") };
    float pw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine();
        if (UI::Button(presetLabels[i], ImVec2(pw, 0))) {
            g_toggles.damageMult = presets[i];
            Notify(T("Weapon damage x%g"), presets[i]);
        }
    }

    if (Toggle(T("All guns"), &g_toggles.wheel.allGuns, T("Every gun can be picked on the weapon wheel with the scroll wheel, collected or not. Empty categories come with ammo.")) && g_toggles.wheel.allGuns)
        g_toggles.wheel.switchGuns = true;
    if (UI::Button(T("Remove all weapons"), ImVec2(-1, 0))) {
        Features::Queue([] { Game::RemoveAllWeapons(); Wheel::ForgetCollected(); });
        Notify(T("All weapons removed"));
    }

    if (UI::Button(T("Gangster pack"), ImVec2(HalfWidth(), 0))) {
        Features::Queue([m = ammoMul] { for (int t : {5, 24, 26, 28, 30, 18}) Game::GiveWeapon(t, 500 * m); });
        Notify(T("Gangster pack given"));
    }
    ImGui::SameLine();
    if (UI::Button(T("Military pack"), ImVec2(-1, 0))) {
        Features::Queue([m = ammoMul] { for (int t : {8, 24, 27, 29, 31, 34, 35, 16}) Game::GiveWeapon(t, 500 * m); });
        Notify(T("Military pack given"));
    }

    if (UI::BeginChild("##wlist")) {
        for (const char* group : kWeaponGroups) {
            SectionTitle(T(group));
            float bw = ThirdWidth();
            int n = 0;
            for (const WeaponEntry& w : kWeapons) {
                if (strcmp(w.group, group) != 0) continue;
                if (n++ % 3) ImGui::SameLine();
                if (UI::Button(w.name, ImVec2(bw, 0))) {
                    int id = w.id, ammo = w.ammo * ammoMul;
                    Features::Queue([id, ammo] { Game::GiveWeapon(id, ammo); });
                    Notify(T("Given %s"), w.name);
                }
            }
        }
    }
    UI::EndChild();
}

static void TabTeleport()
{
    void* ped = Game::PlayerPed();
    static float custom[3] = { 2495.0f, -1687.0f, 13.5f };

    SectionTitle(T("Quick"));
    if (UI::Button(T("Teleport to map marker"), ImVec2(-1, 0))) {
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
        if (UI::SmallButton(T("Copy to fields"))) { custom[0] = p.x; custom[1] = p.y; custom[2] = p.z; }
    }
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Go").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputFloat3("##coords", custom, "%.1f");
    ImGui::SameLine();
    if (UI::Button(T("Go"))) { CVector p{ custom[0], custom[1], custom[2] }; Features::Queue([p] { Game::Teleport(p, false); }); }

    SectionTitle(T("Locations"));
    if (UI::BeginChild("##tlist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const char* lastRegion = "";
        for (const TeleportEntry& t : kTeleports) {
            if (strcmp(lastRegion, t.region) != 0) {
                lastRegion = t.region;
                ImGui::TextDisabled("%s", t.region);
            }
            ImGui::Indent();
            if (UI::Selectable(t.name)) { CVector p = t.pos; Features::Queue([p] { Game::Teleport(p, true); }); Notify(T("Teleported: %s"), t.name); }
            ImGui::Unindent();
        }
    }
    UI::EndChild();
}

static void TabWorld()
{
    SectionTitle(T("Time"));
    int h = 12, m = 0;
    if (Game::InGame()) Game::GetTime(h, m);
    if (g_toggles.freezeTime) { h = g_toggles.frozenHour; m = g_toggles.frozenMinute; }
    ImGui::SetNextItemWidth(HalfWidth());
    bool ch = UI::SliderInt("##hour", &h, 0, 23, T("Hour %02d"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ch |= UI::SliderInt("##min", &m, 0, 59, T("Minute %02d"));
    if (ch) {
        g_toggles.frozenHour = h; g_toggles.frozenMinute = m;
        Features::Queue([h, m] { Game::SetTime(h, m); });
    }
    if (Toggle(T("Freeze time"), &g_toggles.freezeTime)) { g_toggles.frozenHour = h; g_toggles.frozenMinute = m; }

    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Reset").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    UI::SliderFloat("##speed", &g_toggles.gameSpeed, 0.1f, 3.0f, T("Game speed %.2fx"));
    ImGui::SameLine();
    if (UI::Button(T("Reset"))) g_toggles.gameSpeed = 1.0f;

    SectionTitle(T("Weather"));
    ImGui::SetNextItemWidth(HalfWidth());
    if (UI::Combo("##weather", &g_toggles.weather, [](void*, int i) { return T(kWeatherNames[i]); }, nullptr, IM_ARRAYSIZE(kWeatherNames)))
        Features::Queue([w = g_toggles.weather] { Game::ForceWeather(w); });
    ImGui::SameLine();
    Toggle(T("Lock weather"), &g_toggles.lockWeather);

    SectionTitle(T("Map"));
    {
        const char* open[] = { T("As in the story"), T("Open San Fierro"), T("Open the whole map") };
        ImGui::SetNextItemWidth(-1);
        UI::Combo("##mapopen", &g_toggles.mapOpen, open, IM_ARRAYSIZE(open));
        ImGui::TextDisabled("%s", T("Removes the roadblocks and the wanted stars for entering closed areas."));
        ImGui::BeginDisabled(!Game::CanRevealMap());
        ImGui::TextUnformatted(T("Reveal on the pause map"));
        const char* areas[] = { T("Whole map"), T("Los Santos"), T("San Fierro"), T("Las Venturas") };
        if (ImGui::BeginTable("##reveal", 4)) {
            for (int i = 0; i < 4; ++i) {
                ImGui::TableNextColumn();
                if (UI::Button(areas[i], ImVec2(-1, 0))) { Features::Queue([i] { Game::RevealMap(i); }); Notify(T("Map revealed")); }
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
    }

    if (!Game::HasGravity()) return;
    SectionTitle(T("Physics"));
    ImGui::SetNextItemWidth(-1);
    UI::SliderFloat("##grav", &g_toggles.gravity, 0.001f, 0.02f, T("Gravity %.4f"));
    if (UI::Button(T("Moon gravity"), ImVec2(HalfWidth(), 0))) g_toggles.gravity = 0.0013f;
    ImGui::SameLine();
    if (UI::Button(T("Normal gravity"), ImVec2(-1, 0))) g_toggles.gravity = 0.008f;
}

static void StatSliders(const char* id, const StatEntry* stats, int count)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return;
    std::vector<const char*> names;
    for (int i = 0; i < count; ++i) names.push_back(stats[i].name);
    float nameW = LabelColumn(names.data(), count, 0.45f);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, nameW);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    for (int i = 0; i < count; ++i) {
        const StatEntry& s = stats[i];
        float v = Game::InGame() ? Game::GetStat(s.id) : 0.0f;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
        UI::TextFit(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), Theme::Col(Theme::P.text), T(s.name), nullptr, nameW - S(8));
        ImGui::Dummy(ImVec2(nameW - S(8), ImGui::GetTextLineHeight()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(s.name));
        ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
        ImGui::PushID(s.id);
        if (UI::SliderFloat("##v", &v, 0, s.max, "%.0f")) Features::Queue([sid = s.id, v] { Game::SetStat(sid, v); });
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
    if (UI::BeginChild("##statscroll")) {
        SectionTitle(T("Body"));
        StatSliders("##body", kBodyStats, IM_ARRAYSIZE(kBodyStats));
        if (UI::Button(T("Athlete (no fat, max stamina & muscle)"), ImVec2(-1, 0))) {
            Features::Queue([] { Game::SetStat(21, 0); Game::SetStat(22, 1000); Game::SetStat(23, 1000); Features::RefreshBody(); });
            Notify(T("Athlete stats applied"));
        }

        SectionTitle(T("Driving"));
        StatSliders("##drive", kDrivingStats, IM_ARRAYSIZE(kDrivingStats));
        if (UI::Button(T("Max driving skills"), ImVec2(-1, 0))) { MaxStats(kDrivingStats, IM_ARRAYSIZE(kDrivingStats)); Notify(T("Driving skills maxed")); }

        SectionTitle(T("Weapon skills"));
        StatSliders("##wskill", kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills));
        if (UI::Button(T("Hitman all weapons"), ImVec2(-1, 0))) { MaxStats(kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills)); Notify(T("All weapon skills maxed")); }
    }
    UI::EndChild();
}

// ---------------------------------------------------------------------------
// Gameplay
// ---------------------------------------------------------------------------
static void SliderF(const char* id, float* v, float lo, float hi, const char* fmt) { ImGui::SetNextItemWidth(-1); UI::SliderFloat(id, v, lo, hi, fmt); }
static void SliderI(const char* id, int* v, int lo, int hi, const char* fmt) { ImGui::SetNextItemWidth(-1); UI::SliderInt(id, v, lo, hi, fmt); }

static void TabGameplay()
{
    GameplayCfg& p = g_toggles.play;
    if (!UI::BeginChild("##playscroll")) { UI::EndChild(); return; }

    SectionTitle(T("Rules"));
    if (ImGui::BeginTable("##rules", 2)) {
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!Game::CanPreventBusted());
        Toggle(T("Never get busted"), &p.neverBusted, T("Cops can't arrest you."));
        ImGui::EndDisabled();
        ImGui::TableNextColumn(); Toggle(T("Stay on bikes"), &p.noFallOffBike, T("CJ doesn't fall off bikes and motorbikes."));
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!Game::CanFreeDriveBy());
        Toggle(T("Free-look drive-by"), &p.freeDriveBy, T("In the driver's seat with a submachine gun: hold right mouse to aim anywhere, left mouse to shoot."));
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!Game::CanFreeDriveBy() || !p.freeDriveBy);
        ImGui::TableNextColumn(); Toggle(T("Use any weapon in vehicles"), &p.anyVehicleWeapon, T("Free-look drive-by fires the weapon you carried into the car or picked on the wheel. Melee weapons and rocket launchers can't be used. Grenades can hurt your own car. Needs Free-look drive-by."));
        ImGui::EndDisabled();
        ImGui::TableNextColumn(); Toggle(T("Unlimited stamina"), &p.unlimitedStamina);
        ImGui::TableNextColumn(); Toggle(T("Unlimited breath"), &p.infiniteBreath);
        ImGui::TableNextColumn(); Toggle(T("Always fed"), &p.alwaysFed, T("CJ never gets hungry, so he doesn't lose fat, muscle or health from not eating."));
        ImGui::TableNextColumn(); Toggle(T("Fast reload"), &p.fastReload);
        ImGui::TableNextColumn(); Toggle(T("Keep weapons when wasted or busted"), &p.keepWeapons, T("You still pay the hospital or police fee."));
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!p.keepWeapons);
        Toggle(T("Keep ammo"), &p.keepAmmo, T("When off, you keep the guns but they stay empty until you find ammo for them."));
        ImGui::EndDisabled();
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
    UI::BeginSub(p.freeDriveBy); SliderF("##dbsens", &p.driveBySens, 0.1f, 3.0f, T("Drive-by mouse speed x%.2f")); UI::EndSub();
    UI::BeginSub(p.slowmoAiming); SliderF("##slowaim", &p.slowmoScale, 0.1f, 0.9f, T("Aiming speed %.2fx")); UI::EndSub();

    SectionTitle(T("Exploding bullets"));
    Toggle(T("Exploding bullets"), &p.explodingBullets, T("Every bullet you fire explodes where it lands."));
    {
        UI::BeginSub(p.explodingBullets);
        const char* types[] = { T("Grenade"), T("Molotov"), T("Rocket"), T("Weak rocket"), T("Car"), T("Quick car"), T("Boat"), T("Aircraft"), T("Mine"), T("Object"), T("Tank"), T("Small") };
        ImGui::SetNextItemWidth(-1);
        UI::Combo("##exptype", &p.explosionType, types, IM_ARRAYSIZE(types));
        UI::EndSub();
    }

    SectionTitle(T("Custom health, armour and ammo"));
    Toggle(T("Custom max health"), &p.customHealth);
    UI::BeginSub(p.customHealth); SliderI("##maxhp", &p.maxHealth, 10, 1000, T("Max health %d")); UI::EndSub();
    Toggle(T("Custom max armour"), &p.customArmour);
    UI::BeginSub(p.customArmour); SliderI("##maxar", &p.maxArmour, 10, 1000, T("Max armour %d")); UI::EndSub();
    Toggle(T("Custom reserve ammo"), &p.customAmmo, T("Tops up every gun to this much spare ammo."));
    UI::BeginSub(p.customAmmo); SliderI("##res", &p.reserveAmmo, 1, 9999, T("Reserve ammo %d")); UI::EndSub();
    SliderF("##clip", &p.clipMult, 0.5f, 10.0f, T("Magazine size x%.1f"));
    SliderI("##dmgtaken", &p.damageTaken, 0, 300, T("Damage you take %d%%"));

    SectionTitle(T("Regeneration"));
    Toggle(T("Health regeneration"), &p.healthRegen);
    UI::BeginSub(p.healthRegen);
    SliderF("##hpa", &p.healthAmount, 0.5f, 50.0f, T("+%.1f health"));
    SliderF("##hpe", &p.healthEvery, 0.1f, 10.0f, T("every %.1f s"));
    SliderF("##hpd", &p.healthDelay, 0.0f, 30.0f, T("starts %.1f s after a hit"));
    UI::EndSub();
    Toggle(T("Armour regeneration"), &p.armourRegen);
    UI::BeginSub(p.armourRegen);
    SliderF("##ara", &p.armourAmount, 0.5f, 50.0f, T("+%.1f armour"));
    SliderF("##are", &p.armourEvery, 0.1f, 10.0f, T("every %.1f s"));
    SliderF("##ard", &p.armourDelay, 0.0f, 30.0f, T("starts %.1f s after a hit"));
    UI::EndSub();
    Toggle(T("Ammo regeneration"), &p.ammoRegen, T("Refills the weapon in your hands."));
    UI::BeginSub(p.ammoRegen);
    SliderI("##amma", &p.ammoAmount, 1, 100, T("+%d rounds"));
    SliderF("##amme", &p.ammoEvery, 0.1f, 10.0f, T("every %.1f s"));
    UI::EndSub();

    SectionTitle(T("Rewards for kills"));
    Toggle(T("Lifesteal"), &p.lifesteal, T("Heals you when you kill someone."));
    UI::BeginSub(p.lifesteal);
    SliderF("##lsh", &p.healthPerKill, 0.0f, 200.0f, T("+%.0f health per kill"));
    SliderF("##lsa", &p.armourPerKill, 0.0f, 200.0f, T("+%.0f armour per kill"));
    UI::EndSub();
    SliderI("##mpk", &p.moneyPerKill, 0, 10000, T("$%d per kill"));
    ImGui::TextDisabled(T("Kills this session: %d"), Combat::Kills());
    UI::EndChild();
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
// Main toggle on the left, "Advanced" button on the right; the toggle's label never runs under it.
static void AdvancedRow(const char* label, bool* on, const char* hint, bool* advanced)
{
    const char* btn = *advanced ? T("Hide advanced") : T("Advanced...");
    ImGui::PushFont(Theme::Small);
    float bw = ImGui::CalcTextSize(btn).x + S(18);
    ImGui::PopFont();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail - bw - S(12));
    Toggle(label, on, hint);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - bw));
    ImGui::PushID(advanced);
    if (UI::SmallButton(btn)) *advanced = !*advanced;
    ImGui::PopID();
}

static void TabCombat()
{
    if (g_multiplayer) { ImGui::TextWrapped("%s", T("Disabled while a multiplayer client is loaded.")); return; }
    AimbotCfg& a = g_toggles.aim;
    EspCfg& e = g_toggles.esp;
    if (!UI::BeginChild("##combatscroll")) { UI::EndChild(); return; }

    SectionTitle(T("Aimbot"));
    AdvancedRow(T("Enable aimbot"), &a.enabled, T("Hold right mouse to aim: the camera locks on to cops and enemies."), &a.advanced);
    ImGui::BeginDisabled(!g_toggles.play.freeDriveBy);
    Toggle(T("Aim during free-look drive-by"), &a.driveBy, T("Also locks on from the driver's seat. Needs Free-look drive-by on the Gameplay tab."));
    ImGui::EndDisabled();
    if (a.advanced) {
        const char* modes[] = { T("While aiming (right mouse)"), T("While holding a key"), T("Whenever a weapon is aimed") };
        ImGui::SetNextItemWidth(-1); UI::Combo("##amode", &a.mode, modes, IM_ARRAYSIZE(modes));
        if (a.mode == 1) Keybind(T("Aim key"), &a.key);
        SliderF("##afov", &a.fov, 1.0f, 90.0f, T("Field of view %.0f deg"));
        SliderF("##asm", &a.smooth, 0.0f, 0.95f, T("Smoothing %.2f (0 = instant)"));
        SliderF("##adist", &a.maxDist, 10.0f, 500.0f, T("Max distance %.0f m"));
        const char* bones[] = { T("Head"), T("Chest"), T("Pelvis") };
        ImGui::SetNextItemWidth(HalfWidth()); UI::Combo("##abone", &a.bone, bones, IM_ARRAYSIZE(bones));
        ImGui::SameLine();
        const char* prio[] = { T("Closest to crosshair"), T("Nearest to you"), T("Lowest health") };
        ImGui::SetNextItemWidth(-1); UI::Combo("##aprio", &a.priority, prio, IM_ARRAYSIZE(prio));
        ImGui::TextDisabled("%s", T("Target"));
        if (ImGui::BeginTable("##atargets", 3)) {
            ImGui::TableNextColumn(); UI::Checkbox(T("Cops"), &a.cops);
            ImGui::TableNextColumn(); UI::Checkbox(T("Gangs"), &a.enemies);
            ImGui::TableNextColumn(); UI::Checkbox(T("Mission targets"), &a.missionTargets);
            ImGui::TableNextColumn(); UI::Checkbox(T("Attackers"), &a.attackers);
            ImGui::TableNextColumn(); UI::Checkbox(T("Civilians"), &a.civilians);
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("##aopts", 2)) {
            ImGui::TableNextColumn(); UI::Checkbox(T("Only through clear line of sight"), &a.visibleOnly);
            ImGui::TableNextColumn(); UI::Checkbox(T("Stay on the same target"), &a.sticky);
            ImGui::TableNextColumn(); UI::Checkbox(T("Lead moving targets"), &a.prediction);
            ImGui::TableNextColumn(); UI::Checkbox(T("Ignore people in vehicles"), &a.ignoreInVehicles);
            ImGui::TableNextColumn(); UI::Checkbox(T("Show field of view circle"), &a.drawFov);
            ImGui::EndTable();
        }
    }

    SectionTitle(T("Wallhack"));
    AdvancedRow(T("Enable wallhack"), &e.enabled, T("Draws boxes with health around cops and enemies."), &e.advanced);
    Toggle(T("Only show who the aimbot can hit"), &e.onlyAimbot, T("A box then means the aimbot can snap to that person right now."));
    if (e.advanced) {
        if (ImGui::BeginTable("##eopts", 3)) {
            ImGui::TableNextColumn(); UI::Checkbox(T("Boxes"), &e.box);
            ImGui::TableNextColumn(); UI::Checkbox(T("Health bar"), &e.health);
            ImGui::TableNextColumn(); UI::Checkbox(T("Armour bar"), &e.armour);
            ImGui::TableNextColumn(); UI::Checkbox(T("Distance"), &e.distance);
            ImGui::TableNextColumn(); UI::Checkbox(T("Labels"), &e.label);
            ImGui::TableNextColumn(); UI::Checkbox(T("Lines"), &e.snapline);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("%s", T("Who to show"));
        if (ImGui::BeginTable("##ewho", 4)) {
            ImGui::TableNextColumn(); UI::Checkbox(T("Cops"), &e.cops);
            ImGui::TableNextColumn(); UI::Checkbox(T("Enemies"), &e.enemies);
            ImGui::TableNextColumn(); UI::Checkbox(T("Civilians"), &e.civilians);
            ImGui::TableNextColumn(); UI::Checkbox(T("Friends"), &e.friends);
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
    UI::EndChild();
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
    if (UI::Button(T("Complete mission"), ImVec2(HalfWidth(), 0)))
        Features::Queue([] { Notify("%s", Game::MissionPass() ? T("Mission completed") : T("This mission can't be skipped")); });
    ImGui::SameLine();
    if (UI::Button(T("Fail mission"), ImVec2(-1, 0)))
        Features::Queue([] { Notify("%s", Game::MissionFail() ? T("Mission failed") : T("This mission has no fail routine")); });
    if (UI::Button(T("Kill targets"), ImVec2(HalfWidth(), 0)))
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
    if (UI::Button(T("Teleport to objective"), ImVec2(-1, 0)))
        Features::Queue([] {
            CVector p;
            if (GPS::Objective(p)) { Game::Teleport(p, true); Notify("%s", T("Teleported to objective")); }
            else Notify("%s", T("No objective on the map"));
        });
    if (UI::Button(T("Fix stuck black screen"), ImVec2(-1, 0))) Features::Queue([] { Game::RecoverScreen(); });
    Toggle(T("Protect allies"), &g_toggles.mission.protectAllies, T("People marked green or blue on the radar can't be hurt."));

    SectionTitle(T("Play any mission"));
    UI::Checkbox(T("Don't change story progress"), &g_toggles.mission.preserveStory);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Your save carries on as if the replay never happened. Rewards still count."));
    if (!g_toggles.mission.preserveStory) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s", T("Off: passing a mission out of order can break your story progress."));
    static int group = -1, picked = -1;
    static char filter[32] = "";
    ImGui::SetNextItemWidth(HalfWidth());
    if (ImGui::BeginCombo("##mg", group < 0 ? T("All") : T(kMissionGroups[group]))) {
        if (UI::Selectable(T("All"), group < 0)) group = -1;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kMissionGroups); ++i) if (UI::Selectable(T(kMissionGroups[i]), group == i)) group = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    UI::SearchBox("##mf", T("Search..."), filter, sizeof(filter));

    auto start = [](const MissionEntry& m) {
        int idx = m.index; bool keep = g_toggles.mission.preserveStory; std::string title = m.title;
        Features::Queue([idx, keep, title] {
            if (Game::StartMission(idx, keep)) Notify(T("Starting: %s"), title.c_str());
            else Notify("%s", T("Finish the current mission first"));
        });
    };
    float footer = ImGui::GetFrameHeightWithSpacing();
    if (UI::BeginChild("##mlist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        int last = -1;
        for (const MissionEntry& m : kMissions) {
            if ((group >= 0 && m.group != group) || !ContainsI(m.title, filter)) continue;
            if (m.group != last) { last = m.group; ImGui::TextDisabled("%s", T(kMissionGroups[m.group])); }
            char l[96]; snprintf(l, sizeof(l), "%s##m%d", m.title, m.index);
            ImGui::Indent();
            if (UI::Selectable(l, picked == m.index, ImGuiSelectableFlags_AllowDoubleClick)) {
                picked = m.index;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) start(m);
            }
            ImGui::Unindent();
        }
    }
    UI::EndChild();
    ImGui::BeginDisabled(picked < 0 || on);
    if (UI::Button(on ? T("Finish the current mission first") : T("Start selected mission"), ImVec2(-1, 0)))
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
    if (UI::Checkbox(T("Save settings automatically"), &Config::AutoSaveEnabled())) Config::SaveMeta();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T("Saves when you close the menu."));
    if (UI::Checkbox(T("Use multiple configs"), &Config::MultiEnabled())) Config::SaveMeta();
    if (!Config::MultiEnabled()) {
        if (Config::AutoSaveEnabled()) ImGui::TextDisabled("%s", T("Your settings save automatically when you close the menu."));
        else {
            if (UI::Button(T("Save settings"), ImVec2(-1, 0))) { Config::SaveDefault(); Notify("%s", T("Settings saved")); }
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
    UI::InputTextWithHint("##cfgname", T("New config name"), name, sizeof(name));
    ImGui::SameLine();
    if (UI::Button(T("Save"))) {
        std::string n = Config::Sanitize(name);
        if (n.empty()) Notify("%s", T("Type a name first"));
        else if (Config::Save(n)) { Notify(T("Saved '%s'"), n.c_str()); selected = n; name[0] = 0; dirty = true; }
    }
    float h = ImGui::GetTextLineHeightWithSpacing() * 6;
    auto load = [](const std::string& n) { if (Config::Load(n)) Notify(T("Loaded '%s'"), n.c_str()); };
    if (UI::BeginChild("##cfglist", ImVec2(0, h), ImGuiChildFlags_Borders)) {
        for (const std::string& n : list) {
            ImGui::PushID(n.c_str());
            if (UI::Selectable(n.c_str(), selected == n, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected = n;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) load(n);
            }
            bool isAuto = Config::Autoload() == n;
            if (isAuto || Config::Current() == n) {
                const char* tag = isAuto ? T("AUTOLOAD") : T("loaded");
                float w = ImGui::CalcTextSize(tag).x;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - w - 4);
                ImGui::TextColored(isAuto ? Theme::P.accent : ImVec4(0.55f, 0.55f, 0.6f, 1), "%s", tag);
            }
            ImGui::PopID();
        }
        if (list.empty()) ImGui::TextDisabled("%s", T("No configs yet."));
    }
    UI::EndChild();
    bool has = !selected.empty();
    ImGui::BeginDisabled(!has);
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    if (UI::Button(T("Load"), ImVec2(w, 0))) load(selected);
    ImGui::SameLine();
    if (UI::Button(T("Overwrite"), ImVec2(w, 0))) { if (Config::Save(selected)) Notify(T("Saved '%s'"), selected.c_str()); }
    ImGui::SameLine();
    bool isAuto = has && Config::Autoload() == selected;
    if (UI::Button(isAuto ? T("No autoload") : T("Autoload"), ImVec2(w, 0))) Config::SetAutoload(isAuto ? "" : selected);
    ImGui::SameLine();
    if (UI::Button(T("Delete"), ImVec2(-1, 0))) { if (Config::Delete(selected)) { selected.clear(); dirty = true; } }
    ImGui::EndDisabled();
}

// Accent presets as round swatches; the chosen one gets a ring and a check mark.
static void AccentPicker()
{
    using namespace Theme;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T("Accent colour"));
    float d = ImGui::GetFrameHeight();
    for (int i = 0; i < PresetCount(); ++i) {
        ImGui::SameLine(0, i ? S(10) : S(16));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##accent", ImVec2(d, d))) g_toggles.accent = i;
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) ImGui::SetTooltip("%s", T(PresetAt(i).name));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
        bool cur = g_toggles.accent == i;
        if (cur || hov) dl->AddCircle(c, d * 0.5f, Col(cur ? P.text : P.textDim), 24, S(1.5f));
        dl->AddCircleFilled(c, d * 0.36f, Col(PresetAt(i).colour), 24);
        if (cur) Icons::Draw(dl, Icons::Check, c, d * 0.36f, IM_COL32(25, 18, 10, 230));
    }
    ImGui::SameLine(0, S(14));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", T(PresetAt(g_toggles.accent).name));
}

static void TabSettings()
{
    if (!UI::BeginChild("##setscroll")) { UI::EndChild(); return; }
    ConfigSection();

    SectionTitle(T("Interface"));
    const char* langs[] = { "English", "Polski" };
    ImGui::SetNextItemWidth(-1);
    UI::Combo("##lang", &g_toggles.language, langs, 2);
    ImGuiIO& io = ImGui::GetIO();
    SliderF("##scale", &g_toggles.textScale, 0.7f, 2.0f, T("Text scale %.2f"));
    SliderF("##alpha", &g_toggles.opacity, 0.4f, 1.0f, T("Opacity %.2f"));
    AccentPicker();
    if (ImGui::BeginTable("##look", 2)) {
        ImGui::TableNextColumn(); Toggle(T("Animations"), &g_toggles.animations, T("Smooth movement for switches, pages and the menu itself."));
        ImGui::TableNextColumn(); Toggle(T("Frosted glass"), &g_toggles.blur, T("Blurs the game behind the menu. Turn it off if the menu costs you frames."));
        ImGui::EndTable();
    }

    SectionTitle(T("Game fixes"));
    Toggle(T("Keep my mouse sensitivity"), &g_toggles.keepMouseSens, T("Stops the game resetting your mouse sensitivity to the default. Uses the value from your own game settings."));

    SectionTitle(T("Keys"));
    Keybind(T("Open menu"), &g_toggles.menuKey);
    Keybind(T("Noclip"), &g_toggles.noclipKey);
    Keybind(T("Weapon wheel"), &g_toggles.wheel.key);

    SectionTitle(T("Weapon wheel"));
    Toggle(T("Enable weapon wheel"), &g_toggles.wheel.enabled, T("Hold the key, point with the mouse, release to equip."));
    UI::BeginSub(g_toggles.wheel.enabled);
    if (ImGui::BeginTable("##wheelopts", 2)) {
        ImGui::TableNextColumn(); Toggle(T("Slow time while open"), &g_toggles.wheel.slowmo);
        ImGui::TableNextColumn(); Toggle(T("Use in vehicles"), &g_toggles.wheel.inVehicles);
        ImGui::EndTable();
    }
    Toggle(T("Switch weapons with the scroll wheel"), &g_toggles.wheel.switchGuns, T("Point at a weapon in the wheel and scroll to swap it for another gun of that type you have collected. Your ammo carries over."));
    SliderF("##wscale", &g_toggles.wheel.scale, 0.6f, 1.4f, T("Size x%.2f"));
    UI::EndSub();

    SectionTitle(T("GPS"));
    Toggle(T("Route on the minimap"), &g_toggles.gps.enabled, T("Draws the road route to your map marker."));
    UI::BeginSub(g_toggles.gps.enabled);
    if (ImGui::BeginTable("##gpsopts", 2)) {
        ImGui::TableNextColumn(); Toggle(T("Also to mission objectives"), &g_toggles.gps.toObjective);
        ImGui::TableNextColumn(); Toggle(T("Show route on foot"), &g_toggles.gps.onFoot);
        ImGui::TableNextColumn(); ImGui::ColorEdit4(T("Map marker route"), g_toggles.gps.colWaypoint, ImGuiColorEditFlags_NoInputs);
        ImGui::TableNextColumn(); ImGui::ColorEdit4(T("Mission route"), g_toggles.gps.colMission, ImGuiColorEditFlags_NoInputs);
        ImGui::EndTable();
    }
    SliderF("##gpsw", &g_toggles.gps.width, 1.0f, 8.0f, T("Line width %.1f"));
    UI::EndSub();
    ImGui::TextDisabled(T("Status: %s"), T(GPS::Status()));

    SectionTitle(T("Updates"));
    UI::Checkbox(T("Tell me when a new version is out"), &g_toggles.updateNotify);
    UI::BeginSub(g_toggles.updateNotify);
    UI::Checkbox(T("Check every 30 minutes while playing"), &g_toggles.updatePeriodic);
    UI::EndSub();
    if (Update::Available()) {
        ImGui::TextColored(Theme::P.accent, T("Version %s is available (you have %s)."), Update::Latest().c_str(), RIFFED_VERSION);
        if (UI::Button(T("Open download page"), ImVec2(-1, 0))) Update::OpenReleasePage();
    } else {
        ImGui::TextDisabled(T("RiffedMenu %s"), RIFFED_VERSION);
    }

    SectionTitle(T("Reset"));
    if (UI::Button(T("Reset to defaults"), ImVec2(HalfWidth(), 0))) ImGui::OpenPopup("###resetconfirm");
    ImGui::SameLine();
    if (UI::Button(T("Clear everything"), ImVec2(-1, 0))) ImGui::OpenPopup("###clearconfirm");

    auto confirm = [](const char* id, const char* heading, const char* question, const char* detail, const char* action, auto run) {
        char title[96];
        snprintf(title, sizeof(title), "%s###%s", heading, id);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
        ImGui::TextUnformatted(question);
        ImGui::TextDisabled("%s", detail);
        ImGui::Spacing();
        float w = ImGui::GetFontSize() * 8;
        if (UI::Button(action, ImVec2(w, 0))) { run(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (UI::Button(T("Cancel"), ImVec2(w, 0))) ImGui::CloseCurrentPopup();
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
    UI::EndChild();
}

// ---------------------------------------------------------------------------

static void UpdateNotice()
{
    static std::string shown;                    // once per version, so a release during play still gets a notice
    if (!g_toggles.updateNotify || !Update::Available() || !Game::InGame()) return;
    std::string latest = Update::Latest();
    if (latest == shown) return;
    shown = latest;
    Notify(T("RiffedMenu %s is available - see Settings"), latest.c_str());
}

bool Menu::IsOpen() { return g_open; }
void Features::NotifyPush(const char* text) { Notify("%s", text); }
void Features::NotifyToggle(const char* label, bool on) { ::NotifyToggle(label, on); }
void Menu::Toggle() { g_open = !g_open; Log("Menu %s", g_open ? "opened" : "closed"); }

// ---------------------------------------------------------------------------
// Menu shell: frosted panel, sidebar, header with search, animated pages.
// ---------------------------------------------------------------------------
struct Page { const char* name; Icons::Id icon; void (*draw)(); bool needsGame; };
static const Page kPages[] = {
    { "Player",    Icons::Player,    TabPlayer,    true },
    { "Vehicle",   Icons::Vehicle,   TabVehicle,   true },
    { "Customize", Icons::Customize, TabCustomize, true },
    { "Weapons",   Icons::Weapons,   TabWeapons,   true },
    { "Gameplay",  Icons::Gameplay,  TabGameplay,  true },
    { "Combat",    Icons::Combat,    TabCombat,    true },
    { "Missions",  Icons::Missions,  TabMissions,  true },
    { "Outfit",    Icons::Outfit,    TabOutfit,    true },
    { "Teleport",  Icons::Teleport,  TabTeleport,  true },
    { "World",     Icons::World,     TabWorld,     true },
    { "Stats",     Icons::Stats,     TabStats,     true },
    { "Settings",  Icons::Settings,  TabSettings,  false },
};
static const int kPageCount = IM_ARRAYSIZE(kPages);

static int   g_page = 0;
static float g_pageAnim = 1.0f;
static char  g_search[64] = "";
static int   g_indexLang = -1;
static bool  g_homeValid = false;
static ImVec2 g_home;

static void GoTo(int page)
{
    if (page == g_page) return;
    g_page = page;
    g_pageAnim = 0.0f;
}

// Runs every tab once, invisibly, so the search can find options on tabs that aren't open.
static void BuildIndex()
{
    PreviewCam keep = g_preview;
    UI::ClearIndex();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(S(720), S(900)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##riffed_index", nullptr, f)) {
        for (int i = 0; i < kPageCount; ++i) {
            UI::IndexBegin(i);
            ImGui::PushID(i);
            if (UI::BeginChild("##ip", ImVec2(S(700), S(4000)))) kPages[i].draw();
            UI::EndChild();
            ImGui::PopID();
            UI::IndexEnd();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    g_preview = keep;
    g_indexLang = g_toggles.language;
}

static void DrawSearchResults()
{
    using namespace Theme;
    std::vector<const UI::Entry*> hits;
    for (const UI::Entry& e : UI::Index())
        if (ContainsI(e.label.c_str(), g_search) || ContainsI(e.section.c_str(), g_search)) hits.push_back(&e);
    std::stable_sort(hits.begin(), hits.end(), [](const UI::Entry* a, const UI::Entry* b) { return a->page < b->page; });
    if (hits.empty()) { ImGui::TextDisabled("%s", T("No matches")); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    int lastPage = -1;
    for (size_t i = 0; i < hits.size() && i < 200; ++i) {
        const UI::Entry& e = *hits[i];
        if (e.page != lastPage) {
            lastPage = e.page;
            ImGui::Dummy(ImVec2(0, S(4)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            Icons::Draw(dl, kPages[e.page].icon, ImVec2(p.x + S(9), p.y + S(9)), S(16), Col(P.accent));
            ImGui::PushFont(Heading);
            dl->AddText(ImVec2(p.x + S(26), p.y), Col(P.accent), Upper(T(kPages[e.page].name)));
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            ImGui::PopFont();
        }
        ImGui::PushID((int)i);
        float w = ImGui::GetContentRegionAvail().x, h = S(36);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##hit", ImVec2(w, h))) {
            GoTo(e.page);
            UI::FocusOn(e.page, e.label);
            g_search[0] = 0;
        }
        float hv = UI::Anim(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(Mix(P.card, Alpha(P.accent, 0.16f), hv)), S(9));
        float labelW = e.section.empty() ? w - S(28) : (w - S(28)) * 0.58f;
        float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
        UI::TextFit(dl, ImVec2(p.x + S(14), ty), Col(P.text), e.label.c_str(), nullptr, labelW);
        if (!e.section.empty()) {
            ImGui::PushFont(Small);
            float sw = w - S(28) - labelW - S(12);
            float tw = std::min(ImGui::CalcTextSize(e.section.c_str()).x, sw);
            UI::TextFit(dl, ImVec2(p.x + w - S(14) - tw, p.y + (h - ImGui::GetFontSize()) * 0.5f), Col(P.textMuted), e.section.c_str(), nullptr, sw);
            ImGui::PopFont();
        }
        ImGui::PopID();
    }
}

static float SidebarWidth()
{
    float w = 0;
    for (const Page& p : kPages) w = std::max(w, ImGui::CalcTextSize(T(p.name)).x);
    w += S(78);
    ImGui::PushFont(Theme::Small);                     // room for the subtitle under the logo too
    w = std::max(w, ImGui::CalcTextSize(T("GTA San Andreas trainer")).x + S(44));
    ImGui::PopFont();
    return std::clamp(w, S(176), S(260));
}

static void DrawPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float sideW)
{
    using namespace Theme;
    const float R = S(18);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    dl->PushClipRectFullScreen();
    for (int i = 1; i <= 7; ++i) {                     // drop shadow, a little lower than the panel
        float e = S(2.5f) * i;
        dl->AddRect(ImVec2(a.x - e, a.y - e + S(8)), ImVec2(b.x + e, b.y + e + S(8)), Col(P.shadow, 0.07f * (8 - i) / 7.0f), R + e, 0, S(2.8f));
    }
    for (int i = 1; i <= 5; ++i) {                     // soft accent glow
        float e = S(1.6f) * i;
        dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), Col(P.accent, 0.045f * (6 - i) / 5.0f), R + e, 0, S(2.0f));
    }
    Blur::Request();
    float tint = g_toggles.opacity;
    if (void* tex = Blur::Texture()) {
        dl->AddImageRounded((ImTextureID)tex, a, b, ImVec2(a.x / ds.x, a.y / ds.y), ImVec2(b.x / ds.x, b.y / ds.y), Col(ImVec4(1, 1, 1, 1)), R);
        tint *= 0.78f;                                 // let the frosted glass show through
    }
    dl->AddRectFilled(a, b, Col(P.panel, tint), R);
    dl->AddRectFilled(a, ImVec2(a.x + sideW, b.y), Col(P.sidebar), R, ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(a.x + sideW, a.y + S(16)), ImVec2(a.x + sideW, b.y - S(16)), Col(P.hairline), 1.0f);
    dl->AddRect(a, b, Col(ImVec4(1, 1, 1, 0.09f)), R, 0, 1.0f);
    dl->AddLine(ImVec2(a.x + R, a.y + 0.5f), ImVec2(b.x - R, a.y + 0.5f), Col(ImVec4(1, 1, 1, 0.10f)), 1.0f);
    for (int i = 0; i < 3; ++i) {                      // resize grip
        float o = S(6) + i * S(4);
        dl->AddLine(ImVec2(b.x - o, b.y - S(5)), ImVec2(b.x - S(5), b.y - o), Col(P.textDim, 0.6f), 1.0f);
    }
    dl->PopClipRect();
}

static void DrawSidebar(ImDrawList* dl, ImVec2 a, ImVec2 b, float sideW)
{
    using namespace Theme;
    ImGui::PushFont(Title);
    UI::TextFit(dl, ImVec2(a.x + S(20), a.y + S(18)), Col(P.accent), "RiffedMenu", nullptr, sideW - S(32), Title);
    float titleH = ImGui::GetFontSize();
    ImGui::PopFont();
    // Subtitle wraps onto a second line rather than being cut when the sidebar can't grow enough.
    const char* sub = T("GTA San Andreas trainer");
    float wrapW = sideW - S(36);
    ImVec2 subSize = Small->CalcTextSizeA(Small->FontSize, FLT_MAX, wrapW, sub);
    dl->AddText(Small, Small->FontSize, ImVec2(a.x + S(21), a.y + S(18) + titleH), Col(P.textMuted), sub, nullptr, wrapW);

    const float top = a.y + S(28) + titleH + subSize.y + S(14);
    const float itemH = std::clamp((b.y - S(34) - top) / kPageCount - S(4), S(26), S(40));   // shrink rows in a short window
    static float indY = -1;
    float wantY = top + g_page * (itemH + S(4));
    if (indY < 0) indY = wantY;
    UI::Ease(indY, wantY, 16.0f);
    ImVec2 ia(a.x + S(10), indY), ib(a.x + sideW - S(10), indY + itemH);
    dl->AddRectFilled(ia, ib, Col(P.accent, 0.15f), S(10));
    dl->AddRectFilled(ImVec2(ia.x, ia.y + S(9)), ImVec2(ia.x + S(3), ib.y - S(9)), Col(P.accent), S(2));

    for (int i = 0; i < kPageCount; ++i) {
        ImVec2 p(a.x + S(10), top + i * (itemH + S(4)));
        if (p.y + itemH > b.y - S(34)) break;              // very small window: keep clear of the version line
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##nav", ImVec2(sideW - S(20), itemH))) { GoTo(i); g_search[0] = 0; }
        bool hov = ImGui::IsItemHovered();
        float hv = UI::Anim(ImGui::GetItemID(), hov ? 1.0f : 0.0f);
        ImGui::PopID();
        bool cur = i == g_page && !g_search[0];
        if (!cur && hv > 0.01f) dl->AddRectFilled(p, ImVec2(p.x + sideW - S(20), p.y + itemH), Col(ImVec4(1, 1, 1, 0.045f * hv)), S(10));
        ImVec4 col = cur ? P.accentHi : Mix(P.textMuted, P.text, hv);
        Icons::Draw(dl, kPages[i].icon, ImVec2(p.x + S(22), p.y + itemH * 0.5f), S(18), Col(col));
        const char* name = T(kPages[i].name);
        float maxW = sideW - S(20) - S(50);
        if (UI::TextFit(dl, ImVec2(p.x + S(42), p.y + (itemH - ImGui::GetFontSize()) * 0.5f), Col(cur ? P.text : col), name, nullptr, maxW) && hov)
            ImGui::SetTooltip("%s", name);
    }
    ImGui::PushFont(Small);
    char ver[32];
    snprintf(ver, sizeof(ver), "v%s", RIFFED_VERSION);
    dl->AddText(ImVec2(a.x + S(20), b.y - S(14) - ImGui::GetFontSize()), Col(P.textDim), ver);
    ImGui::PopFont();
}

static void DrawMenu(float openT)
{
    using namespace Theme;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ease = 1.0f - powf(1.0f - openT, 3.0f);
    const float sideW = SidebarWidth();

    if (g_search[0] && (UI::Index().empty() || g_indexLang != g_toggles.language)) BuildIndex();

    ImGuiCond layoutCond = g_resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImVec2 defPos(vp->WorkPos.x + S(60), vp->WorkPos.y + S(60));
    ImGui::SetNextWindowSize(ImVec2(S(880), S(660)), layoutCond);
    if (g_resetLayout) { ImGui::SetNextWindowPos(defPos, ImGuiCond_Always); g_homeValid = false; }
    g_resetLayout = false;
    if (openT < 0.999f) {                            // slide in and out from a little lower
        if (!g_homeValid) {
            ImGuiWindow* w = ImGui::FindWindowByName("RiffedMenu");
            g_home = w ? w->Pos : defPos;
            g_homeValid = true;
        }
        ImGui::SetNextWindowPos(ImVec2(g_home.x, g_home.y + (1.0f - ease) * S(22)), ImGuiCond_Always);
    } else {
        g_homeValid = false;
        ImGui::SetNextWindowPos(defPos, ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(sideW + S(460), S(540)), ImVec2(FLT_MAX, FLT_MAX));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    if (!g_open) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool visible = ImGui::Begin("RiffedMenu", nullptr, flags);
    ImGui::PopStyleVar();
    if (!visible) { ImGui::End(); ImGui::PopStyleVar(); return; }

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImVec2 a = win->Pos, b(a.x + win->Size.x, a.y + win->Size.y);
    ImDrawList* dl = win->DrawList;
    DrawPanel(dl, a, b, sideW);
    DrawSidebar(dl, a, b, sideW);

    // Header: page title, status, search, close.
    const float x0 = a.x + sideW + S(26), x1 = b.x - S(22);
    const bool searching = g_search[0] != 0;
    float closeW = S(30), searchW = std::clamp((x1 - x0) * 0.4f, S(170), S(300));
    ImGui::SetCursorScreenPos(ImVec2(x1 - closeW, a.y + S(20)));
    if (UI::IconButton("##close", Icons::Close, closeW)) g_open = false;
    ImGui::SetCursorScreenPos(ImVec2(x1 - closeW - S(10) - searchW, a.y + S(20) + (closeW - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(searchW);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) ImGui::SetKeyboardFocusHere();
    UI::SearchBox("##search", T("Search features"), g_search, sizeof(g_search));

    float titleMaxW = x1 - closeW - S(24) - searchW - x0;
    ImGui::PushFont(Title);
    const char* title = searching ? T("Search") : T(kPages[g_page].name);
    UI::TextFit(dl, ImVec2(x0, a.y + S(16)), Col(P.text), Upper(title), nullptr, titleMaxW, Title);
    float titleH = ImGui::GetFontSize();
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const char* status = nullptr;
    ImVec4 statusCol = P.warning;
    if (!Game::Supported()) { status = T("Unsupported game version - game features are off."); statusCol = P.danger; }
    else if (!Game::InGame() && kPages[g_page].needsGame && !searching) status = T("Load a save to use the trainer.");
    if (status) UI::TextFit(dl, ImVec2(x0, a.y + S(16) + titleH), Col(statusCol), status, nullptr, x1 - x0, Small);
    ImGui::PopFont();

    // Page area
    float top = a.y + S(16) + titleH + S(24);
    ImGui::SetCursorScreenPos(ImVec2(x0 - S(6), top));
    UI::Ease(g_pageAnim, 1.0f, 10.0f);
    float pe = 1.0f - powf(1.0f - g_pageAnim, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6), S(4)));
    if (UI::BeginChild("##page", ImVec2(b.x - x0 - S(10), b.y - top - S(16)), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground)) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.25f + 0.75f * pe));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - pe) * S(14));
        if (searching) {
            DrawSearchResults();
        } else {
            const Page& pg = kPages[g_page];
            UI::SetPage(g_page);
            bool live = Game::InGame();
            ImGui::BeginDisabled(pg.needsGame && !live);
            pg.draw();
            ImGui::EndDisabled();
            UI::CloseCards();
        }
        ImGui::PopStyleVar();
    }
    UI::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();
}

static void DrawCursor()
{
    using namespace Theme;
    ImGuiIO& io = ImGui::GetIO();
    if (io.MousePos.x < 0) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 m = io.MousePos;
    float k = S(1.0f);
    if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput) {
        float h = S(9);
        dl->AddLine(ImVec2(m.x, m.y - h), ImVec2(m.x, m.y + h), IM_COL32(0, 0, 0, 200), S(3));
        dl->AddLine(ImVec2(m.x, m.y - h), ImVec2(m.x, m.y + h), IM_COL32(255, 255, 255, 255), S(1.4f));
        return;
    }
    ImVec2 pts[7] = { ImVec2(0, 0), ImVec2(0, 17), ImVec2(4.4f, 13.2f), ImVec2(7.3f, 19.6f), ImVec2(10.1f, 18.4f), ImVec2(7.3f, 12.1f), ImVec2(12.6f, 12.1f) };
    for (ImVec2& p : pts) p = ImVec2(m.x + p.x * k, m.y + p.y * k);
    ImVec2 sh[7];
    for (int i = 0; i < 7; ++i) sh[i] = ImVec2(pts[i].x + S(1.5f), pts[i].y + S(2));
    dl->AddConcavePolyFilled(sh, 7, IM_COL32(0, 0, 0, 90));
    dl->AddConcavePolyFilled(pts, 7, ImGui::GetColorU32(P.text));
    dl->AddPolyline(pts, 7, ImGui::GetColorU32(ImVec4(0.08f, 0.06f, 0.05f, 1)), ImDrawFlags_Closed, S(1.2f));
    dl->AddCircleFilled(ImVec2(m.x + S(3), m.y + S(9)), S(1.6f), ImGui::GetColorU32(P.accent));
}

void Menu::Render(void* device)
{
    Combat::Render();
    GPS::Render();
    Wheel::Render(device);
    UpdateNotice();
    g_preview.active = false;   // editors re-request it below while their tab is visible

    static float openT = 0.0f;
    UI::Ease(openT, g_open ? 1.0f : 0.0f, g_open ? 11.0f : 15.0f);
    if (openT > 0.003f) DrawMenu(openT);
    DrawToasts();
    if (g_open) DrawCursor();
}
