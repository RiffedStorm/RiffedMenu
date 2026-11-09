#include "wheel.h"
#include "features.h"
#include "game.h"
#include "gamedata.h"
#include "i18n.h"
#include "log.h"
#include "imgui.h"
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstring>
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

bool ItemFor(int sector, Item& it)
{
    for (int s : kSectors[sector].slots) {
        if (s < 0) continue;
        int t, c, a;
        Game::WeaponSlot(s, t, c, a);
        if (t != 0 && (a > 0 || s <= 1 || s >= 10)) { it = { s, t, c, a }; return true; }
    }
    return false;
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
    Item it;
    if (sector >= 0 && ItemFor(sector, it)) Game::SetCurrentWeapon(it.type);
    else if (sector < 0)                    Game::SetCurrentWeapon(0);
}

} // namespace

namespace Wheel {

bool IsOpen() { return g_open; }

void ReleaseTextures()
{
    for (auto& [k, t] : g_icons) if (t) t->Release();
    g_icons.clear();
}

void Tick()
{
    bool want = g_toggles.wheel.enabled && (g_gameFocused || g_open);
    want = want && (GetAsyncKeyState(g_toggles.wheel.key) & 0x8000) && !Game::PlayerVehicle() &&
           !Game::PauseMenuOpen() && !Game::WidescreenOn();

    if (want && !g_open) {
        RECT rc; HWND w = GetForegroundWindow();
        if (GetClientRect(w, &rc)) { POINT c{ rc.right / 2, rc.bottom / 2 }; ClientToScreen(w, &c); SetCursorPos(c.x, c.y); }
        g_open = true;                              // after centring: our cursor hook blocks moves while open
        g_hover = -2;
    } else if (!want && g_open) {
        g_open = false;
        if (g_hover != -2) Equip(g_hover);          // -2: the mouse never left the centre, keep the weapon
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
    dl->AddCircleFilled(m, 4.0f, IM_COL32(255, 255, 255, 230));
}

} // namespace Wheel
