#include "wheel.h"
#include "features.h"
#include "game.h"
#include "gamedata.h"
#include "i18n.h"
#include "log.h"
#include "config.h"
#include "data.h"
#include "imgui.h"
#include "MinHook.h"
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

// Hold the key to open a radial selector with the game slowed down, point with the mouse, release
// to equip. Icons are the game's own weapon icons, read from gta3.img.
namespace {

struct Sector { const char* name; int slots[3]; };
const Sector kSectors[] = {
    { "Melee",      { 1, 0, -1 } },
    { "Handguns",   { 2, -1, -1 } },
    { "SMGs",       { 4, -1, -1 } },
    { "Shotguns",   { 3, -1, -1 } },
    { "Assault",    { 5, -1, -1 } },
    { "Rifles",     { 6, -1, -1 } },
    { "Heavy",      { 7, -1, -1 } },
    { "Thrown",     { 8, -1, -1 } },
    { "Tools",      { 9, 12, -1 } },
    { "Gear",       { 11, 10, -1 } },
};
const int kCount = IM_ARRAYSIZE(kSectors);

bool  g_open = false;
int   g_hover = -1;

struct Item { int slot, type, clip, total; };

// The game keeps one weapon per slot, so everything else the player has held is remembered here,
// and stored next to each save slot by the hooks below.
const int kTypes = 47;
bool g_have[kTypes] = {};
int  g_shown[kCount] = {};   // gun the scroll wheel landed on this time, applied when the wheel closes
int  g_pick[kCount] = {};    // last gun chosen in sectors that span two slots
bool g_hadGuns = false, g_pedGone = false, g_justLoaded = false;
unsigned g_lastClock = 0;

// Detonator (40) is left out: the game hands it out together with satchel charges.
const std::vector<int> kSlotGuns[13] = {
    { 1 }, { 2, 3, 4, 5, 6, 7, 8, 9 }, { 22, 23, 24 }, { 25, 26, 27 }, { 28, 29, 32 }, { 30, 31 }, { 33, 34 },
    { 35, 36, 37, 38 }, { 16, 17, 18, 39 }, { 41, 42, 43 }, { 10, 11, 12, 13, 14, 15 }, { 44, 45, 46 }, {},
};

int SlotOf(int type)
{
    for (int s = 0; s < 13; ++s)
        if (std::find(kSlotGuns[s].begin(), kSlotGuns[s].end(), type) != kSlotGuns[s].end()) return s;
    return -1;
}

int CatalogAmmo(int type)
{
    for (const WeaponEntry& w : kWeapons) if (w.id == type) return w.ammo;
    return 0;
}

bool Switching() { return g_toggles.wheel.switchGuns; }
bool AllGuns()   { return g_toggles.wheel.switchGuns && g_toggles.wheel.allGuns; }

std::vector<int> GunsFor(int sector)
{
    std::vector<int> out;
    for (int s : kSectors[sector].slots) {
        if (s < 0) continue;
        for (int t : kSlotGuns[s]) if (g_have[t] || (AllGuns() && CatalogAmmo(t) > 0)) out.push_back(t);
    }
    return out;
}

bool Usable(int slot, int type, int total) { return type != 0 && (total > 0 || slot <= 1 || slot >= 10); }

void ForgetGuns()
{
    memset(g_have, 0, sizeof(g_have));
    memset(g_shown, 0, sizeof(g_shown));
    memset(g_pick, 0, sizeof(g_pick));
    g_hadGuns = false;
}

std::string SlotFile(int slot) { return Config::Dir() + "saves\\slot" + std::to_string(slot) + ".txt"; }

// "...GTASAsf3.b" -> 3
int SlotFromPath(const char* p)
{
    if (!p) return -1;
    const char* dot = strrchr(p, '.');
    if (!dot) return -1;
    const char* q = dot;
    while (q > p && q[-1] >= '0' && q[-1] <= '9') --q;
    return q == dot ? -1 : atoi(q);
}

void Track()
{
    // A new game starts the clock again. Loads are handled by the hook, which sets g_justLoaded.
    unsigned now = Game::ClockMs();
    bool restarted = g_pedGone || now + 10000 < g_lastClock;
    g_lastClock = now;
    g_pedGone = false;
    if (g_justLoaded) g_justLoaded = false;
    else if (restarted) { ForgetGuns(); Log("Wheel: new game, collected guns cleared"); }

    bool guns = false;
    for (int s = 0; s < 13; ++s) {
        int t, c, a;
        Game::WeaponSlot(s, t, c, a);
        if (t > 0 && t < kTypes) g_have[t] = true;
        if (s >= 2 && s <= 9 && t != 0) guns = true;
    }
    if (g_hadGuns && !guns) { ForgetGuns(); Log("Wheel: all guns lost, collected guns cleared"); }   // wasted or busted
    g_hadGuns = guns;
}

using SaveFn = char(__cdecl*)(int);
using LoadFn = char(__cdecl*)(char*);
SaveFn oSave = nullptr;
LoadFn oLoad = nullptr;

char __cdecl hkSave(int a)
{
    if (Game::PlayerPed()) Track();   // include anything picked up since the last tick
    char ok = oSave(a);
    int slot = SlotFromPath(Game::SaveFilePath());
    if (ok && slot >= 0) {
        CreateDirectoryA((Config::Dir() + "saves").c_str(), nullptr);
        std::ofstream f(SlotFile(slot), std::ios::trunc);
        for (int t = 1; t < kTypes; ++t) if (g_have[t]) f << t << ' ';
        Log("Wheel: stored collected guns with save slot %d", slot);
    }
    return ok;
}

char __cdecl hkLoad(char* out)
{
    char ok = oLoad(out);
    int slot = SlotFromPath(Game::LoadFilePath());
    if (ok) {
        ForgetGuns();
        g_justLoaded = true;
        int n = 0, t;
        std::ifstream f(SlotFile(slot));
        while (f >> t) if (t > 0 && t < kTypes) { g_have[t] = true; ++n; }
        Log("Wheel: save slot %d has %d collected guns", slot, n);
    }
    return ok;
}

// Replaces the game's call to ClearWeapons when the player is wasted or busted.
void __fastcall hkWeaponPenalty(void* ped, void*)
{
    const GameplayCfg& p = g_toggles.play;
    if (!p.keepWeapons || g_multiplayer) { Game::ClearPedWeapons(ped); return; }
    if (p.keepAmmo) return;
    for (int s = 2; s <= 9; ++s) { Game::SetSlotAmmo(s, 0); Game::SetSlotClip(s, 0); }
    Features::Queue([] { Game::SetCurrentWeapon(0); });
}

bool ItemFor(int sector, Item& it)
{
    if (int type = g_shown[sector]) {
        int slot = SlotOf(type), t, c, a;
        Game::WeaponSlot(slot, t, c, a);
        if (!Usable(slot, t, a)) { c = 0; a = CatalogAmmo(type); }   // only All guns lands on an empty slot
        it = { slot, type, c, std::max(a, 1) };
        return true;
    }
    if (int pick = Switching() ? g_pick[sector] : 0) {
        int slot = SlotOf(pick), t, c, a;
        Game::WeaponSlot(slot, t, c, a);
        if (t == pick && Usable(slot, t, a)) { it = { slot, t, c, a }; return true; }
    }
    for (int s : kSectors[sector].slots) {
        if (s < 0) continue;
        int t, c, a;
        Game::WeaponSlot(s, t, c, a);
        if (Usable(s, t, a)) { it = { s, t, c, a }; return true; }
    }
    if (AllGuns()) {
        auto guns = GunsFor(sector);
        if (!guns.empty()) { it = { SlotOf(guns[0]), guns[0], 0, CatalogAmmo(guns[0]) }; return true; }
    }
    return false;
}

// Makes the game hold what the wheel shows for this sector. The category's ammo moves to the new gun.
void ApplyChoice(int sector)
{
    Item it;
    if (!ItemFor(sector, it)) return;
    int t, c, a;
    Game::WeaponSlot(it.slot, t, c, a);
    bool shown = g_shown[sector] != 0;
    g_shown[sector] = 0;
    if (shown) g_pick[sector] = it.type;
    if (t == it.type && Usable(it.slot, t, a)) return;
    Game::GiveWeapon(it.type, it.total);
    Game::SetSlotAmmo(it.slot, it.total);    // picking up over a gun of this class would add the two
    int nt, nc, na;
    Game::WeaponSlot(it.slot, nt, nc, na);
    if (nc > it.total) Game::SetSlotClip(it.slot, it.total);
    if (Game::PlayerVehicle() && !Features::DriveByAiming()) Game::HideHeldWeapon();   // giving puts it in his hands
}

void Scroll(int sector, int dir)
{
    auto guns = GunsFor(sector);
    if (guns.size() < 2) return;
    Item it;
    if (!ItemFor(sector, it)) return;       // nothing held and no All guns: no ammo to carry over
    auto at = std::find(guns.begin(), guns.end(), it.type);
    int i = at == guns.end() ? 0 : (int)(at - guns.begin());
    g_shown[sector] = guns[(i + dir + (int)guns.size()) % (int)guns.size()];
}

std::map<int, IDirect3DTexture9*> g_icons;

// Finds the 64x64 "<name>icon" DXT3 texture in a RenderWare TXD.
IDirect3DTexture9* LoadIcon(IDirect3DDevice9* dev, const std::string& txdName)
{
    std::string b = GameData::ImgRead(txdName + ".txd");
    if (b.size() < 64) return nullptr;
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, b.data() + o, 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, b.data() + o, 2); return v; };
    int count = u16(24);
    size_t p = 28;
    for (int i = 0; i < count && p + 120 < b.size(); ++i) {
        uint32_t size = u32(p + 4);
        size_t q = p + 24;
        std::string name(b.data() + q + 8, strnlen(b.data() + q + 8, 32));
        int w = u16(q + 80), h = u16(q + 82), mips = (uint8_t)b[q + 85];
        bool dxt3 = memcmp(b.data() + q + 76, "DXT3", 4) == 0;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, "icon") == 0 && dxt3 && mips >= 1) {
            uint32_t dataSize = u32(q + 88);
            IDirect3DTexture9* tex = nullptr;
            if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_DXT3, D3DPOOL_MANAGED, &tex, nullptr))) return nullptr;
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
                memcpy(lr.pBits, b.data() + q + 92, std::min<size_t>(dataSize, (size_t)w * h));
                tex->UnlockRect(0);
                return tex;
            }
            tex->Release();
            return nullptr;
        }
        p += 12 + size;
    }
    return nullptr;
}

IDirect3DTexture9* Icon(IDirect3DDevice9* dev, int type)
{
    auto it = g_icons.find(type);
    if (it != g_icons.end()) return it->second;
    IDirect3DTexture9* tex = nullptr;
    std::string txd = GameData::WeaponTxd(type);
    if (!txd.empty()) tex = LoadIcon(dev, txd);
    g_icons[type] = tex;
    return tex;
}

void Equip(int sector)
{
    auto select = Game::PlayerVehicle() ? Game::SetVehicleWeapon : Game::SetCurrentWeapon;
    Item it;
    for (int i = 0; i < kCount; ++i) if (g_shown[i] && i != sector) ApplyChoice(i);
    if (sector >= 0 && Switching()) ApplyChoice(sector);
    if (sector >= 0 && ItemFor(sector, it)) select(it.type);
    else if (sector < 0)                    select(0);
}

} // namespace

namespace Wheel {

bool IsOpen() { return g_open; }

void PlayerGone() { g_pedGone = true; }
void ForgetCollected() { ForgetGuns(); }

void InstallSaveHooks()
{
    Log("Wheel: keep weapons %s", Game::RedirectWeaponPenalties((void*)hkWeaponPenalty) ? "installed" : "unavailable");
    void* s = Game::SaveGameAddress(), *l = Game::LoadGameAddress();
    if (!s || !l) return;
    bool ok = MH_CreateHook(s, (void*)hkSave, (void**)&oSave) == MH_OK && MH_EnableHook(s) == MH_OK;
    ok = MH_CreateHook(l, (void*)hkLoad, (void**)&oLoad) == MH_OK && MH_EnableHook(l) == MH_OK && ok;
    Log("Wheel: save tracking %s", ok ? "installed" : "failed");
}

void ReleaseTextures()
{
    for (auto& [k, t] : g_icons) if (t) t->Release();
    g_icons.clear();
}

void Tick()
{
    Track();
    bool want = g_toggles.wheel.enabled && (g_gameFocused || g_open);
    want = want && (GetAsyncKeyState(g_toggles.wheel.key) & 0x8000) && (g_toggles.wheel.inVehicles || !Game::PlayerVehicle()) &&
           !Game::PauseMenuOpen() && !Game::WidescreenOn();

    if (want && !g_open) {
        RECT rc; HWND w = GetForegroundWindow();
        if (GetClientRect(w, &rc)) { POINT c{ rc.right / 2, rc.bottom / 2 }; ClientToScreen(w, &c); SetCursorPos(c.x, c.y); }
        g_open = true;                              // after centring: our cursor hook blocks moves while open
        g_hover = -2;
    } else if (!want && g_open) {
        g_open = false;
        if (g_hover != -2) Equip(g_hover);          // -2: the mouse never left the centre, keep the weapon
        memset(g_shown, 0, sizeof(g_shown));
    }
}

void Render(void* device)
{
    if (!g_open) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
    float R = std::min(ds.x, ds.y) * 0.34f * g_toggles.wheel.scale, r = R * 0.42f;

    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 90));
    ImVec2 m = ImGui::GetIO().MousePos;
    float mx = m.x - c.x, my = m.y - c.y, md = hypotf(mx, my);
    const float step = 6.2831853f / kCount;
    if (md > r * 0.6f) {
        float a = atan2f(my, mx) + 1.5707963f + step * 0.5f;
        if (a < 0) a += 6.2831853f;
        g_hover = ((int)(a / step)) % kCount;
    } else if (md > 6.0f && g_hover != -2) {
        g_hover = -1;
    } else if (md > r * 0.25f) {
        g_hover = -1;
    }

    float scroll = ImGui::GetIO().MouseWheel;
    if (Switching() && g_hover >= 0 && scroll != 0.0f) Scroll(g_hover, scroll < 0 ? 1 : -1);

    auto* dev = (IDirect3DDevice9*)device;
    const float gap = 0.02f;
    for (int i = 0; i < kCount; ++i) {
        float a0 = -1.5707963f - step * 0.5f + i * step + gap, a1 = a0 + step - gap * 2;
        Item it;
        bool have = ItemFor(i, it), hot = g_hover == i;
        ImU32 fill = hot ? IM_COL32(245, 140, 41, 215) : have ? IM_COL32(18, 18, 22, 190) : IM_COL32(18, 18, 22, 110);
        dl->PathClear();
        dl->PathArcTo(c, R, a0, a1, 24);
        dl->PathArcTo(c, r, a1, a0, 18);
        dl->PathFillConcave(fill);

        float am = (a0 + a1) * 0.5f, rm = (R + r) * 0.5f;
        ImVec2 p(c.x + cosf(am) * rm, c.y + sinf(am) * rm);
        float s = (R - r) * 0.34f;
        if (have) {
            IDirect3DTexture9* tex = it.type ? Icon(dev, it.type) : nullptr;
            if (tex) dl->AddImage((ImTextureID)tex, ImVec2(p.x - s, p.y - s), ImVec2(p.x + s, p.y + s), ImVec2(0, 0), ImVec2(1, 1), have ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
            else {
                const char* t = T("Fists");
                ImVec2 z = ImGui::CalcTextSize(t);
                dl->AddText(ImVec2(p.x - z.x * 0.5f, p.y - z.y * 0.5f), IM_COL32_WHITE, t);
            }
        } else {
            const char* t = T(kSectors[i].name);
            ImVec2 z = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p.x - z.x * 0.5f, p.y - z.y * 0.5f), IM_COL32(170, 170, 170, 150), t);
        }
    }

    dl->AddCircleFilled(c, r * 0.94f, IM_COL32(12, 12, 15, 225), 48);
    char line1[64] = "", line2[64] = "";
    Item it;
    if (g_hover >= 0 && ItemFor(g_hover, it)) {
        snprintf(line1, sizeof(line1), "%s", GameData::WeaponDisplayName(it.type));
        if (it.total > 0 && it.slot >= 2 && it.slot <= 9) snprintf(line2, sizeof(line2), "%d / %d", it.clip, it.total - it.clip);
    } else if (g_hover >= 0) {
        snprintf(line1, sizeof(line1), "%s", T(kSectors[g_hover].name));
        snprintf(line2, sizeof(line2), "%s", T("Empty"));
    } else if (g_hover == -1) {
        snprintf(line1, sizeof(line1), "%s", T("Unarmed"));
    } else {
        snprintf(line1, sizeof(line1), "%s", T("Point at a weapon"));
    }
    ImVec2 z1 = ImGui::CalcTextSize(line1), z2 = ImGui::CalcTextSize(line2);
    dl->AddText(ImVec2(c.x - z1.x * 0.5f, c.y - z1.y - 1), IM_COL32_WHITE, line1);
    dl->AddText(ImVec2(c.x - z2.x * 0.5f, c.y + 2), IM_COL32(200, 200, 200, 255), line2);
    if (Switching() && g_hover >= 0 && ItemFor(g_hover, it)) {
        auto guns = GunsFor(g_hover);
        auto at = std::find(guns.begin(), guns.end(), it.type);
        if (guns.size() > 1 && at != guns.end()) {
            char line3[32];
            snprintf(line3, sizeof(line3), T("Scroll %d/%d"), (int)(at - guns.begin()) + 1, (int)guns.size());
            ImVec2 z3 = ImGui::CalcTextSize(line3);
            dl->AddText(ImVec2(c.x - z3.x * 0.5f, c.y + z2.y + 4), IM_COL32(245, 140, 41, 255), line3);
        }
    }
    dl->AddCircleFilled(m, 4.0f, IM_COL32(255, 255, 255, 230));
}

} // namespace Wheel
