#include "menu.h"
#include "data.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "imgui.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
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
    if (changed) Notify("%s %s", label, *v ? "ON" : "OFF");
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

static float HalfWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }
static float ThirdWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f; }

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
static void TabPlayer()
{
    void* ped = Game::PlayerPed();

    SectionTitle("Toggles");
    if (ImGui::BeginTable("##ptoggles", 2)) {
        ImGui::TableNextColumn(); Toggle("God mode", &g_toggles.godMode, "Bullet/fire/explosion/melee/collision proof + health refill");
        ImGui::TableNextColumn(); Toggle("Infinite armour", &g_toggles.infiniteArmour);
        ImGui::TableNextColumn(); Toggle("Infinite ammo", &g_toggles.infiniteAmmo, "Keeps magazines full - no reloading");
        ImGui::TableNextColumn(); Toggle("Never wanted", &g_toggles.neverWanted);
        ImGui::TableNextColumn(); Toggle("Noclip  (F7)", &g_toggles.noclip, "Fly through walls: WASD, Space up, Ctrl down, Shift faster.\nWorks on foot and in vehicles. Close the menu to move.");
        ImGui::EndTable();
    }
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::SliderFloat("##ncspeed", &g_toggles.noclipSpeed, 5.0f, 100.0f, "Noclip speed %.0f m/s");
    ImGui::SameLine();
    if (ImGui::Button("Equip jetpack", ImVec2(-1, 0))) {
        if (Game::PlayerVehicle()) Notify("Get out of the vehicle first");
        else { Features::Queue([] { Game::GiveJetpack(); }); Notify("Jetpack equipped"); }
    }

    SectionTitle("Health & armour");
    if (ped) {
        float hp = Game::PedHealth(ped), maxHp = Game::PedMaxHealth(ped), ar = Game::PedArmour(ped);
        char ov[32];
        snprintf(ov, sizeof(ov), "%.0f / %.0f", hp, maxHp);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.25f, 0.25f, 1));
        ImGui::ProgressBar(maxHp > 0 ? hp / maxHp : 0, ImVec2(HalfWidth(), 0), ov);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        snprintf(ov, sizeof(ov), "Armour %.0f", ar);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.6f, 0.95f, 1));
        ImGui::ProgressBar(ar / 100.0f, ImVec2(-1, 0), ov);
        ImGui::PopStyleColor();
    }
    if (ImGui::Button("Full health", ImVec2(HalfWidth(), 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedHealth(p) = Game::PedMaxHealth(p); });
    ImGui::SameLine();
    if (ImGui::Button("Full armour", ImVec2(-1, 0)))
        Features::Queue([] { if (void* p = Game::PlayerPed()) Game::PedArmour(p) = 100.0f; });

    SectionTitle("Wanted level");
    int wanted = Game::WantedLevel();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##wanted", &wanted, 0, 6, "%d stars"))
        Features::Queue([wanted] { Game::SetWantedLevel(wanted); });

    SectionTitle("Money");
    static int moneyInput = 1000000;
    ImGui::Text("Current: $%d", ped ? Game::Money() : 0);
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::InputInt("##money", &moneyInput, 10000, 100000);
    ImGui::SameLine();
    if (ImGui::Button("Set money", ImVec2(-1, 0))) { int m = moneyInput; Features::Queue([m] { Game::Money() = m; }); Notify("Money set to $%d", m); }
    float w4 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    const int adds[] = { 10000, 100000, 1000000, 10000000 };
    const char* addLabels[] = { "+$10K", "+$100K", "+$1M", "+$10M" };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(addLabels[i], ImVec2(w4, 0))) {
            int a = adds[i];
            Features::Queue([a] { int& m = Game::Money(); m = (m > 999999999 - a) ? 999999999 : m + a; });
            Notify("Added $%d", a);
        }
    }
}

static void SpawnFromMenu(const VehicleEntry& v)
{
    if (Game::ActiveInterior() != 0) { Notify("Can't spawn vehicles indoors - go outside first"); return; }
    int id = v.id;
    Features::Queue([id] { Game::SpawnVehicle(id); });
    Notify("Spawned %s", v.name);
}

static void TabVehicle()
{
    void* veh = Game::PlayerVehicle();

    SectionTitle("Current vehicle");
    if (veh) {
        float hp = Game::VehicleHealth(veh);
        char ov[48];
        snprintf(ov, sizeof(ov), "Model %d  -  %.0f HP", Game::VehicleModel(veh), hp);
        ImGui::ProgressBar(std::clamp(hp / 1000.0f, 0.0f, 1.0f), ImVec2(-1, 0), ov);
    } else {
        ImGui::TextDisabled("Not in a vehicle");
    }
    if (ImGui::BeginTable("##vtoggles", 2)) {
        ImGui::TableNextColumn(); Toggle("Vehicle god mode", &g_toggles.vehicleGod, "Proofs + health lock on the vehicle you're in");
        ImGui::TableNextColumn(); Toggle("Auto repair", &g_toggles.autoRepair, "Fixes visual damage as soon as it happens");
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(!veh);
    if (ImGui::Button("Repair", ImVec2(ThirdWidth(), 0))) { Features::Queue([] { Game::RepairVehicle(); }); Notify("Vehicle repaired"); }
    ImGui::SameLine();
    if (ImGui::Button("Flip upright", ImVec2(ThirdWidth() * 1.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, 0)))
        Features::Queue([] { Game::FlipVehicle(); });
    ImGui::SameLine();
    if (ImGui::Button("Boost", ImVec2(-1, 0))) Features::Queue([] { Game::BoostVehicle(1.6f); });
    if (veh) {
        int c1 = Game::VehicleColor(veh, 0), c2 = Game::VehicleColor(veh, 1);
        ImGui::SetNextItemWidth(HalfWidth());
        bool recolor = ImGui::SliderInt("##c1", &c1, 0, 126, "Primary colour %d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        recolor |= ImGui::SliderInt("##c2", &c2, 0, 126, "Secondary colour %d");
        if (recolor) Features::Queue([c1, c2] { Game::SetVehicleColors(c1, c2); });
    }
    ImGui::EndDisabled();

    SectionTitle("Spawner");
    static char filter[64] = "";
    static int category = 0;
    static int selected = -1;
    ImGui::SetNextItemWidth(HalfWidth());
    ImGui::InputTextWithHint("##vfilter", "Search vehicles...", filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##vcat", &category, kVehicleCategories, IM_ARRAYSIZE(kVehicleCategories));

    float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##vlist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        float idCol = ImGui::CalcTextSize("Industrial 000").x;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kVehicles); ++i) {
            const VehicleEntry& v = kVehicles[i];
            if (category && strcmp(v.category, kVehicleCategories[category]) != 0) continue;
            if (!ContainsI(v.name, filter)) continue;
            char label[96];
            snprintf(label, sizeof(label), "%s##%d", v.name, i);
            if (ImGui::Selectable(label, selected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) SpawnFromMenu(v);
            }
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - idCol);
            ImGui::TextDisabled("%s %d", v.category, v.id);
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(selected < 0);
    if (ImGui::Button(selected >= 0 ? "Spawn selected" : "Select a vehicle (double-click spawns)", ImVec2(-1, 0)) && selected >= 0) {
        SpawnFromMenu(kVehicles[selected]);
    }
    ImGui::EndDisabled();
}

static void TabWeapons()
{
    static int ammoMul = 1;
    SectionTitle("Options");
    Toggle("Infinite ammo / no reload", &g_toggles.infiniteAmmo);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##ammomul", &ammoMul, 1, 10, "Ammo x%d");

    if (ImGui::Button("Gangster pack", ImVec2(HalfWidth(), 0))) {
        Features::Queue([m = ammoMul] { for (int t : {5, 24, 26, 28, 30, 18}) Game::GiveWeapon(t, 500 * m); });
        Notify("Gangster pack given");
    }
    ImGui::SameLine();
    if (ImGui::Button("Military pack", ImVec2(-1, 0))) {
        Features::Queue([m = ammoMul] { for (int t : {8, 24, 27, 29, 31, 34, 35, 16}) Game::GiveWeapon(t, 500 * m); });
        Notify("Military pack given");
    }

    if (ImGui::BeginChild("##wlist")) {
        for (const char* group : kWeaponGroups) {
            SectionTitle(group);
            float bw = ThirdWidth();
            int n = 0;
            for (const WeaponEntry& w : kWeapons) {
                if (strcmp(w.group, group) != 0) continue;
                if (n++ % 3) ImGui::SameLine();
                if (ImGui::Button(w.name, ImVec2(bw, 0))) {
                    int id = w.id, ammo = w.ammo * ammoMul;
                    Features::Queue([id, ammo] { Game::GiveWeapon(id, ammo); });
                    Notify("Given %s", w.name);
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

    SectionTitle("Quick");
    if (ImGui::Button("Teleport to map marker", ImVec2(-1, 0))) {
        CVector p;
        if (Game::WaypointPosition(p)) { Features::Queue([p] { Game::Teleport(p, true); }); Notify("Teleported to marker"); }
        else Notify("No marker placed on the map");
    }

    SectionTitle("Coordinates");
    if (ped) {
        CVector p = Game::PlayerPosition();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Current: %.2f, %.2f, %.2f", p.x, p.y, p.z);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy to fields")) { custom[0] = p.x; custom[1] = p.y; custom[2] = p.z; }
    }
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Go").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputFloat3("##coords", custom, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Go")) { CVector p{ custom[0], custom[1], custom[2] }; Features::Queue([p] { Game::Teleport(p, false); }); }

    SectionTitle("Locations");
    if (ImGui::BeginChild("##tlist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const char* lastRegion = "";
        for (const TeleportEntry& t : kTeleports) {
            if (strcmp(lastRegion, t.region) != 0) {
                lastRegion = t.region;
                ImGui::TextDisabled("%s", t.region);
            }
            ImGui::Indent();
            if (ImGui::Selectable(t.name)) { CVector p = t.pos; Features::Queue([p] { Game::Teleport(p, true); }); Notify("Teleported: %s", t.name); }
            ImGui::Unindent();
        }
    }
    ImGui::EndChild();
}

static void TabWorld()
{
    SectionTitle("Time");
    int h = 12, m = 0;
    if (Game::InGame()) Game::GetTime(h, m);
    if (g_toggles.freezeTime) { h = g_toggles.frozenHour; m = g_toggles.frozenMinute; }
    ImGui::SetNextItemWidth(HalfWidth());
    bool ch = ImGui::SliderInt("##hour", &h, 0, 23, "Hour %02d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ch |= ImGui::SliderInt("##min", &m, 0, 59, "Minute %02d");
    if (ch) {
        g_toggles.frozenHour = h; g_toggles.frozenMinute = m;
        Features::Queue([h, m] { Game::SetTime(h, m); });
    }
    if (Toggle("Freeze time", &g_toggles.freezeTime)) { g_toggles.frozenHour = h; g_toggles.frozenMinute = m; }

    static float speed = 1.0f;
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Reset").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::SliderFloat("##speed", &speed, 0.1f, 3.0f, "Game speed %.2fx"))
        Features::Queue([s = speed] { Game::SetTimeScale(s); });
    ImGui::SameLine();
    if (ImGui::Button("Reset")) { speed = 1.0f; Features::Queue([] { Game::SetTimeScale(1.0f); }); }

    SectionTitle("Weather");
    ImGui::SetNextItemWidth(HalfWidth());
    if (ImGui::Combo("##weather", &g_toggles.weather, kWeatherNames, IM_ARRAYSIZE(kWeatherNames)))
        Features::Queue([w = g_toggles.weather] { Game::ForceWeather(w); });
    ImGui::SameLine();
    if (Toggle("Lock weather", &g_toggles.lockWeather) && !g_toggles.lockWeather) Features::Queue([] { Game::ReleaseWeather(); });

    if (!Game::HasGravity()) return;
    SectionTitle("Physics");
    static float gravity = 0.008f;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##grav", &gravity, 0.001f, 0.02f, "Gravity %.4f"))
        Features::Queue([g = gravity] { Game::SetGravity(g); });
    if (ImGui::Button("Moon gravity", ImVec2(HalfWidth(), 0))) { gravity = 0.0013f; Features::Queue([] { Game::SetGravity(0.0013f); }); }
    ImGui::SameLine();
    if (ImGui::Button("Normal gravity", ImVec2(-1, 0))) { gravity = 0.008f; Features::Queue([] { Game::SetGravity(0.008f); }); }
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
        ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(s.name);
        ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
        ImGui::PushID(s.id);
        if (ImGui::SliderFloat("##v", &v, 0, s.max, "%.0f")) Features::Queue([sid = s.id, v] { Game::SetStat(sid, v); });
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
        SectionTitle("Body");
        StatSliders("##body", kBodyStats, IM_ARRAYSIZE(kBodyStats));
        if (ImGui::Button("Athlete (no fat, max stamina & muscle)", ImVec2(-1, 0))) {
            Features::Queue([] { Game::SetStat(21, 0); Game::SetStat(22, 1000); Game::SetStat(23, 1000); });
            Notify("Athlete stats applied");
        }

        SectionTitle("Driving");
        StatSliders("##drive", kDrivingStats, IM_ARRAYSIZE(kDrivingStats));
        if (ImGui::Button("Max driving skills", ImVec2(-1, 0))) { MaxStats(kDrivingStats, IM_ARRAYSIZE(kDrivingStats)); Notify("Driving skills maxed"); }

        SectionTitle("Weapon skills");
        StatSliders("##wskill", kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills));
        if (ImGui::Button("Hitman all weapons", ImVec2(-1, 0))) { MaxStats(kWeaponSkills, IM_ARRAYSIZE(kWeaponSkills)); Notify("All weapon skills maxed"); }
    }
    ImGui::EndChild();
}

static void TabSettings()
{
    SectionTitle("Interface");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##scale", &io.FontGlobalScale, 0.7f, 2.0f, "Text scale %.2f");
    static float alpha = 0.96f;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##alpha", &alpha, 0.4f, 1.0f, "Opacity %.2f")) ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = alpha;
    ImGui::TextDisabled("Insert: open / close RiffedMenu");

    SectionTitle("Diagnostics");
    ImGui::Text("Game version: %s", Game::VersionName(Game::Version()));
    ImGui::Text("Memory features: %s", Game::Supported() ? "enabled" : "disabled");
    ImGui::Text("In game: %s", Game::InGame() ? "yes" : "no");
    ImGui::Text("FPS: %.0f", io.Framerate);
    ImGui::TextWrapped("Log: %sRiffedMenu.log", GameDir().c_str());
}

// ---------------------------------------------------------------------------

bool Menu::IsOpen() { return g_open; }
void Menu::Toggle() { g_open = !g_open; Log("Menu %s", g_open ? "opened" : "closed"); }

void Menu::Render()
{
    DrawToasts();
    if (!g_open) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float sc = ImGui::GetFontSize() / 16.0f;
    ImGui::SetNextWindowSize(ImVec2(580 * sc, 620 * sc), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 60 * sc, vp->WorkPos.y + 60 * sc), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(460 * sc, 400 * sc), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("RiffedMenu", &g_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) { ImGui::End(); return; }

    // Header: title, subtitle, close button
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted("RiffedMenu");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("GTA San Andreas trainer");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight());
    if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0))) g_open = false;

    if (!Game::Supported()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.12f, 0.12f, 1));
        ImGui::BeginChild("##warn", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::TextWrapped("Unsupported executable: %s. RiffedMenu supports GTA:SA v1.0 US and the 2014 Steam "
                           "re-release. Game features are disabled so nothing gets corrupted.", Game::VersionName(Game::Version()));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else if (!Game::InGame()) {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1), "Load a save to use the trainer.");
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
        gameTab("Player", TabPlayer);
        gameTab("Vehicle", TabVehicle);
        gameTab("Weapons", TabWeapons);
        gameTab("Teleport", TabTeleport);
        gameTab("World", TabWorld);
        gameTab("Stats", TabStats);
        if (ImGui::BeginTabItem("Settings")) { TabSettings(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
