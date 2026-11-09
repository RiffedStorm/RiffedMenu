#include "combat.h"
#include "features.h"
#include "game.h"
#include "i18n.h"
#include "log.h"
#include "imgui.h"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {

enum Class { Cop, Enemy, Civilian, Friend };

struct Target {
    void* ped;
    Class cls;
    bool  missionTarget, attacker;
    CVector head, chest, pelvis, feet;
    float health, armour, dist;
    bool  visible;
    bool  eligible;      // the aimbot would accept this ped right now
    float aimAngle;      // degrees from the crosshair (FLT-ish large when behind the camera)
};

std::vector<Target> g_targets;
std::map<void*, DWORD> g_attackers;
struct Vis { DWORD when; bool clear; };
std::map<void*, Vis> g_visCache;
std::map<void*, bool> g_alive;
void* g_locked = nullptr;
int   g_kills = 0;

float Dist(const CVector& a, const CVector& b)
{
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

bool FirstPersonCam(int mode) { return mode != 53 && mode != 55; }
bool AimCam(int mode)
{
    switch (mode) {
    case 7: case 8: case 34: case 45: case 46: case 51: case 53: case 55: return true;
    default: return false;
    }
}

void Crosshair(float w, float h, float& cx, float& cy)
{
    if (FirstPersonCam(Game::CameraMode())) { cx = w * 0.5f; cy = h * 0.5f; }
    else { cx = w * 0.53f; cy = h * 0.40f; }             // the game's fixed third-person reticle position
}

// Angle corrections (radians) that bring a world point under the crosshair.
bool AimError(const CVector& p, float& dBeta, float& dAlpha)
{
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    float sx, sy, cx, cy, fx, fy;
    if (!Game::WorldToScreen(p, d.x, d.y, sx, sy)) return false;
    Crosshair(d.x, d.y, cx, cy);
    Game::ProjectionScale(fx, fy);
    if (fx < 1e-4f || fy < 1e-4f) return false;
    dBeta  = -atanf((sx - cx) / (d.x * fx));
    dAlpha = -atanf((sy - cy) / (d.y * fy));
    return true;
}

bool Visible(void* ped, const CVector& from, const CVector& to, bool cars)
{
    DWORD now = GetTickCount();
    auto it = g_visCache.find(ped);
    if (it != g_visCache.end() && now - it->second.when < 120) return it->second.clear;
    bool clear = Game::LineOfSight(from, to, cars);
    g_visCache[ped] = { now, clear };
    return clear;
}

bool ClassAllowed(const Target& t)
{
    const AimbotCfg& a = g_toggles.aim;
    if (t.cls == Friend) return false;
    if (t.missionTarget) return a.missionTargets;
    if (t.attacker && a.attackers) return true;
    switch (t.cls) {
    case Cop:      return a.cops;
    case Enemy:    return a.enemies;
    case Civilian: return a.civilians;
    default:       return false;
    }
}

void Scan()
{
    g_targets.clear();
    void* me = Game::PlayerPed();
    if (!Game::HasCamera() || !Game::PedPoolSize()) return;

    DWORD now = GetTickCount();
    if (void* who = Game::PedLastDamager(me)) g_attackers[who] = now;
    for (auto it = g_attackers.begin(); it != g_attackers.end();)
        it = now - it->second > 15000 ? g_attackers.erase(it) : std::next(it);
    if (g_visCache.size() > 400) g_visCache.clear();

    std::vector<Blip> blips;
    Game::GetBlips(blips);
    std::vector<void*> reds, allies;
    for (const Blip& b : blips) {
        if (b.type == 2) {
            void* p = Game::PedFromHandle(b.handle);
            if (p) ((b.colour == 1 || b.colour == 2 || b.colour == 6) ? allies : reds).push_back(p);
        } else if (b.type == 1 && (b.colour == 0 || b.colour == 5)) {
            if (void* v = Game::VehicleFromHandle(b.handle)) reds.push_back(v);
        }
    }
    auto has = [](const std::vector<void*>& v, void* p) { return std::find(v.begin(), v.end(), p) != v.end(); };

    const AimbotCfg& a = g_toggles.aim;
    CVector cam = Game::CameraPos(), myPos = Game::EntityPos(me);
    void* myCar = Game::PlayerVehicle();
    int n = Game::PedPoolSize();
    std::map<void*, bool> alive;
    for (int i = 0; i < n; ++i) {
        void* ped = Game::PedAt(i);
        if (!ped || ped == me) continue;
        bool dead = Game::PedDead(ped);
        alive[ped] = !dead;
        if (dead) {
            auto was = g_alive.find(ped);
            if (was != g_alive.end() && was->second && Game::PedLastDamager(ped) == me) {
                ++g_kills;
                GameplayCfg& p = g_toggles.play;
                if (p.lifesteal) {
                    float mx = Game::PedMaxHealth(me);
                    Game::PedHealth(me) = std::min(mx, Game::PedHealth(me) + p.healthPerKill);
                    Game::PedArmour(me) = std::min(p.customArmour ? (float)p.maxArmour : 100.0f, Game::PedArmour(me) + p.armourPerKill);
                }
                if (p.moneyPerKill) Game::Money() += p.moneyPerKill;
            }
            continue;
        }

        Target t{};
        t.ped = ped;
        void* car = Game::PedCar(ped);
        int type = Game::PedType(ped);
        t.missionTarget = has(reds, ped) || (car && has(reds, car));
        t.attacker = g_attackers.count(ped) != 0;
        bool ally = has(allies, ped) || (car && has(allies, car));
        if (ally || type == 8)                 t.cls = Friend;
        else if (type == 6)                    t.cls = Cop;
        else if (t.missionTarget || t.attacker || (type >= 7 && type <= 16)) t.cls = Enemy;
        else                                   t.cls = Civilian;

        CVector root = Game::EntityPos(ped);
        t.head = t.chest = t.pelvis = root;
        t.head.z += 0.65f; t.chest.z += 0.25f;
        Game::PedBone(ped, 8, t.head);
        Game::PedBone(ped, 3, t.chest);
        Game::PedBone(ped, 2, t.pelvis);
        t.feet = root; t.feet.z -= 1.0f;
        t.health = Game::PedHealth(ped);
        t.armour = Game::PedArmour(ped);
        t.dist = Dist(myPos, root);

        const CVector& aimAt = a.bone == 0 ? t.head : a.bone == 1 ? t.chest : t.pelvis;
        float db = 0, da = 0;
        t.aimAngle = AimError(aimAt, db, da) ? hypotf(db, da) * 57.2958f : 999.0f;
        t.visible = (t.dist <= a.maxDist && t.aimAngle <= a.fov && ClassAllowed(t) && a.visibleOnly) ? Visible(ped, cam, aimAt, car == nullptr && !myCar) : true;   // someone in a car is "behind" that car, and our own car sits in front of the camera
        t.eligible = ClassAllowed(t) && t.dist <= a.maxDist && t.aimAngle <= a.fov && (!a.visibleOnly || t.visible) &&
                     !(a.ignoreInVehicles && car) && !(myCar && car == myCar);
        g_targets.push_back(t);
    }
    g_alive.swap(alive);
}

void RunAimbot()
{
    const AimbotCfg& a = g_toggles.aim;
    void* myCar = Game::PlayerVehicle();
    if (!a.enabled || g_multiplayer || !g_gameFocused || (myCar && !(a.driveBy && Features::DriveByAiming()))) { g_locked = nullptr; return; }
    int w = Game::CurrentWeapon();
    if (w < 22 || w > 38) { g_locked = nullptr; return; }       // firearms only

    int mode = Game::CameraMode();
    if (myCar && mode != 55) { g_locked = nullptr; return; }    // the drive-by camera hasn't taken over yet
    bool active = false;
    if (a.mode == 0) active = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) && AimCam(mode);
    else if (a.mode == 1) active = (GetAsyncKeyState(a.key) & 0x8000) != 0;
    else active = AimCam(mode);
    if (!active) { g_locked = nullptr; return; }

    const Target* best = nullptr;
    float bestScore = 1e9f;
    for (const Target& t : g_targets) {
        if (!t.eligible) continue;
        if (a.sticky && t.ped == g_locked) { best = &t; break; }
        float score = a.priority == 0 ? t.aimAngle : a.priority == 1 ? t.dist : t.health + t.armour;
        if (score < bestScore) { bestScore = score; best = &t; }
    }
    if (!best) { g_locked = nullptr; return; }
    g_locked = best->ped;

    CVector p = a.bone == 0 ? best->head : a.bone == 1 ? best->chest : best->pelvis;
    if (a.prediction) { CVector v = Game::EntitySpeed(best->ped); p.x += v.x * 1.5f; p.y += v.y * 1.5f; p.z += v.z * 1.5f; }

    if (myCar) {
        // The camera moves with the car before the next shot: aim for where the target will be relative to it.
        void* theirCar = Game::PedCar(best->ped);
        CVector them = Game::EntitySpeed(theirCar ? theirCar : best->ped), us = Game::EntitySpeed(myCar);
        float steps = 50.0f * std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
        p.x += (them.x - us.x) * steps; p.y += (them.y - us.y) * steps; p.z += (them.z - us.z) * steps;
    }

    float db, da;
    if (!AimError(p, db, da)) return;
    float k = 1.0f - std::clamp(a.smooth, 0.0f, 0.97f);
    float& beta = Game::CameraBeta();
    float& alpha = Game::CameraAlpha();
    beta += db * k;
    alpha = std::clamp(alpha + da * k, -1.4f, 1.4f);
    if (beta > 3.14159265f) beta -= 6.2831853f;
    if (beta < -3.14159265f) beta += 6.2831853f;
}

// --- exploding bullets --------------------------------------------------------------------
using Impact_t = void(__fastcall*)(void*, void*, void*, void*, CVector*, CVector*, void*, int);
Impact_t oImpact = nullptr;
std::vector<CVector> g_pending;
DWORD g_lastBlast = 0;

void __fastcall hkImpact(void* self, void* edx, void* firedBy, void* victim, CVector* start, CVector* end, void* colPoint, int incremental)
{
    oImpact(self, edx, firedBy, victim, start, end, colPoint, incremental);
    if (g_toggles.play.explodingBullets && !g_multiplayer && firedBy && firedBy == Game::PlayerPed() && colPoint && g_pending.size() < 16)
        g_pending.push_back(*(CVector*)colPoint);      // CColPoint starts with the hit position
}

// Lets the cop play his arrest animation but never marks the player as arrested, so the game
// has nothing to react to: no Busted screen, and CJ can still get up.
using ArrestStart_t = void(__fastcall*)(void*, void*, void*);
ArrestStart_t oArrestStart = nullptr;

void __fastcall hkArrestStart(void* task, void* edx, void* cop)
{
    void* victim = *(void**)((uint8_t*)task + 8);
    bool guard = g_toggles.play.neverBusted && victim && victim == Game::PlayerPed();
    int state = guard ? Game::PedState(victim) : 0;
    oArrestStart(task, edx, cop);
    if (guard) Game::UndoArrest(victim, state);
}

// No bullet spread. Only the player's shots are touched, because everything shared goes back after the call.
using Fire_t = char(__fastcall*)(void*, void*, void*, CVector*, CVector*, void*, CVector*, CVector*, int, int);
Fire_t oFire = nullptr;

char __fastcall hkFire(void* weapon, void* edx, void* firedBy, CVector* origin, CVector* muzzle, void* target, CVector* targetPos, CVector* start, int a7, int a8)
{
    if (!g_toggles.play.noSpread || g_multiplayer || !firedBy || firedBy != Game::PlayerPed())
        return oFire(weapon, edx, firedBy, origin, muzzle, target, targetPos, start, a7, a8);

    Game::SteadyAim(firedBy);
    int type = *(int*)weapon;
    float* accuracy = type >= 25 && type <= 27 ? Game::WeaponAccuracy(firedBy, type) : nullptr;      // shotguns: the pellet cone
    float saved = accuracy ? *accuracy : 0.0f;
    if (accuracy) *accuracy = 1000000.0f;

    CVector line;
    bool freeAim = !target && !targetPos && origin && Game::OnCrosshairLine(start ? *start : *origin, line);   // free aim only, a lock-on has its own target
    char result = oFire(weapon, edx, firedBy, origin, muzzle, target, targetPos, freeAim ? &line : start, a7, a8);

    if (accuracy) *accuracy = saved;
    return result;
}

// The game puts two-handed guns on a movement set whose sprint is just the run, and re-picks the set
// every frame. Hiding the gun from that one decision gives CJ his normal set while the sprint key is held.
using AnimGroups_t = void(__fastcall*)(void*, void*);
AnimGroups_t oAnimGroups = nullptr;

void __fastcall hkAnimGroups(void* ped, void* edx)
{
    if (g_toggles.play.armedSprint && !g_multiplayer && ped == Game::PlayerPed() && Game::WantsArmedSprint(ped)) {
        void*& held = Game::PedWeaponObject(ped);
        void* saved = held;
        held = nullptr;
        oAnimGroups(ped, edx);
        held = saved;
        return;
    }
    oAnimGroups(ped, edx);
}

using GunMove_t = char(__fastcall*)(void*, void*, float*);
GunMove_t oGunMove = nullptr;

char __fastcall hkGunMove(void* task, void* edx, float* wanted)
{
    char result = oGunMove(task, edx, wanted);
    if (g_toggles.play.moveWhileAiming && !g_multiplayer) Game::StartGunMove(task, wanted);
    return result;
}

ImU32 Col(const float* c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alpha)); }

} // namespace

namespace Combat {

void Tick()
{
    if (!Game::HasCamera()) return;
    Scan();
    RunAimbot();
}

int Kills() { return g_kills; }

bool InstallBulletHook()
{
    void* target = Game::BulletImpactAddress();
    if (!target) return false;
    if (MH_CreateHook(target, (void*)hkImpact, (void**)&oImpact) != MH_OK || MH_EnableHook(target) != MH_OK) {
        Log("Bullet impact hook failed");
        return false;
    }
    Log("Hooked CWeapon::DoBulletImpact at %p", target);
    if (void* a = Game::ArrestStartAddress()) {
        if (MH_CreateHook(a, (void*)hkArrestStart, (void**)&oArrestStart) == MH_OK && MH_EnableHook(a) == MH_OK)
            Log("Hooked arrest task at %p", a);
        else Log("Arrest hook failed");
    }
    if (void* f = Game::FireInstantHitAddress()) {
        if (MH_CreateHook(f, (void*)hkFire, (void**)&oFire) == MH_OK && MH_EnableHook(f) == MH_OK)
            Log("Hooked CWeapon::FireInstantHit at %p", f);
        else Log("Fire hook failed");
    }
    if (void* g = Game::AnimGroupsAddress()) {
        if (MH_CreateHook(g, (void*)hkAnimGroups, (void**)&oAnimGroups) == MH_OK && MH_EnableHook(g) == MH_OK)
            Log("Hooked CPlayerPed::ProcessAnimGroups at %p", g);
        else Log("Movement set hook failed");
    }
    if (void* m = Game::GunMoveAddress()) {
        if (MH_CreateHook(m, (void*)hkGunMove, (void**)&oGunMove) == MH_OK && MH_EnableHook(m) == MH_OK)
            Log("Hooked CTaskSimpleUseGun::ControlGunMove at %p", m);
        else Log("Gun move hook failed");
    }
    return true;
}

void SpawnPendingExplosions()
{
    if (g_pending.empty()) return;
    DWORD now = GetTickCount();
    if (now - g_lastBlast >= 60) {               // a shotgun blast is many pellets: one explosion is plenty
        Game::AddExplosion(g_pending.back(), g_toggles.play.explosionType);
        g_lastBlast = now;
    }
    g_pending.clear();
}

void Render()
{
    if (!Game::InGame() || !Game::HasCamera() || Game::PauseMenuOpen() || g_multiplayer) return;
    const EspCfg& e = g_toggles.esp;
    const AimbotCfg& a = g_toggles.aim;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (a.enabled && a.drawFov) {
        float cx, cy, fx, fy;
        Crosshair(d.x, d.y, cx, cy);
        Game::ProjectionScale(fx, fy);
        float r = tanf(a.fov * 0.0174533f) * d.x * fx;
        dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(255, 255, 255, 110), 64, 1.5f);
    }
    if (!e.enabled) return;

    float scale = ImGui::GetFontSize() / 17.0f;
    for (const Target& t : g_targets) {
        if (e.onlyAimbot ? !t.eligible : false) continue;
        if (t.dist > e.maxDist) continue;
        if (!e.onlyAimbot) {
            bool show = t.cls == Cop ? e.cops : t.cls == Enemy ? e.enemies : t.cls == Civilian ? e.civilians : e.friends;
            if (!show) continue;
        }
        float tx, ty, fx, fy;
        CVector top = t.head; top.z += 0.20f;
        if (!Game::WorldToScreen(top, d.x, d.y, tx, ty) || !Game::WorldToScreen(t.feet, d.x, d.y, fx, fy)) continue;
        float h = fy - ty;
        if (h < 2.0f) continue;
        float w = h * 0.42f, x0 = (tx + fx) * 0.5f - w * 0.5f, x1 = x0 + w;

        const float* c = t.cls == Cop ? e.colCop : t.cls == Enemy ? e.colEnemy : t.cls == Civilian ? e.colCiv : e.colFriend;
        ImU32 col = Col(c);
        float thick = e.thickness * scale;
        if (e.box) {
            dl->AddRect(ImVec2(x0 - 1, ty - 1), ImVec2(x1 + 1, fy + 1), IM_COL32(0, 0, 0, 170), 0, 0, thick + 1.5f);
            dl->AddRect(ImVec2(x0, ty), ImVec2(x1, fy), col, 0, 0, thick);
        }
        float barW = std::max(2.0f, 3.0f * scale);
        if (e.health) {
            float f = std::clamp(t.health / 100.0f, 0.0f, 1.0f);
            float bx = x0 - barW - 3 * scale;
            dl->AddRectFilled(ImVec2(bx - 1, ty - 1), ImVec2(bx + barW + 1, fy + 1), IM_COL32(0, 0, 0, 170));
            ImU32 hc = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f - f, f, 0.15f, 1));
            dl->AddRectFilled(ImVec2(bx, fy - h * f), ImVec2(bx + barW, fy), hc);
        }
        if (e.armour && t.armour > 0) {
            float f = std::clamp(t.armour / 100.0f, 0.0f, 1.0f);
            float bx = x1 + 3 * scale;
            dl->AddRectFilled(ImVec2(bx - 1, ty - 1), ImVec2(bx + barW + 1, fy + 1), IM_COL32(0, 0, 0, 170));
            dl->AddRectFilled(ImVec2(bx, fy - h * f), ImVec2(bx + barW, fy), IM_COL32(90, 150, 255, 255));
        }
        if (e.snapline) dl->AddLine(ImVec2(d.x * 0.5f, d.y), ImVec2((x0 + x1) * 0.5f, fy), col, 1.0f * scale);
        float textY = fy + 2 * scale;
        char buf[48];
        if (e.label) {
            const char* name = t.missionTarget ? T("Target") : t.cls == Cop ? T("Cop") : t.cls == Enemy ? T("Enemy") : t.cls == Civilian ? T("Civilian") : T("Friend");
            ImVec2 sz = ImGui::CalcTextSize(name);
            dl->AddText(ImVec2((x0 + x1 - sz.x) * 0.5f, ty - sz.y - 2 * scale), col, name);
        }
        if (e.distance) {
            snprintf(buf, sizeof(buf), "%.0f m", t.dist);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2((x0 + x1 - sz.x) * 0.5f, textY), IM_COL32(235, 235, 235, 255), buf);
        }
    }
}

} // namespace Combat
