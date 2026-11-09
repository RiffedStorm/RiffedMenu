#include "features.h"
#include "game.h"
#include "log.h"
#include "gamedata.h"
#include "combat.h"
#include "config.h"
#include "gps.h"
#include "hooks.h"
#include "wheel.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <vector>

Toggles g_toggles;
PreviewCam g_preview;
bool    g_gameFocused = false;
bool    g_multiplayer = false;

static SRWLOCK g_queueLock = SRWLOCK_INIT;
static std::vector<std::function<void()>> g_queue;

namespace Features {

void SelfTest(void* ped);

void Queue(std::function<void()> fn)
{
    AcquireSRWLockExclusive(&g_queueLock);
    g_queue.push_back(std::move(fn));
    ReleaseSRWLockExclusive(&g_queueLock);
}

// --- crash guard --------------------------------------------------------------------------
static bool g_bodyDirty = false;
void RefreshBody() { g_bodyDirty = true; }

static std::set<std::string> g_bad;
static std::string g_riskKey;
static DWORD g_riskSince = 0;

static std::string RiskFile()       { return Config::Dir() + "applying.txt"; }
static std::string QuarantineFile() { return Config::Dir() + "disabled-items.txt"; }

void RiskLoad()
{
    std::ifstream q(QuarantineFile());
    for (std::string l; std::getline(q, l);) if (!l.empty()) g_bad.insert(l);
    std::ifstream p(RiskFile());
    std::string key;
    if (p && std::getline(p, key) && !key.empty()) {
        p.close();
        Log("Crash guard: the game stopped while applying '%s' - disabling it", key.c_str());
        g_bad.insert(key);
        std::ofstream(QuarantineFile(), std::ios::app) << key << '\n';
        DeleteFileA(RiskFile().c_str());
    }
}

void RiskBegin(const std::string& key)
{
    std::ofstream(RiskFile(), std::ios::trunc) << key << '\n';
    g_riskKey = key;
    g_riskSince = GetTickCount();
}

bool RiskIsBad(const std::string& key) { return g_bad.count(key) != 0; }

void RiskClear()
{
    g_bad.clear();
    DeleteFileA(QuarantineFile().c_str());
}

static void RiskTick()
{
    if (!g_riskKey.empty() && GetTickCount() - g_riskSince > 4000) {  // survived: not the culprit
        DeleteFileA(RiskFile().c_str());
        g_riskKey.clear();
    }
}

// --- gameplay -----------------------------------------------------------------------------
static bool g_driveBy = false;
bool DriveByAiming() { return g_driveBy; }

static void Gameplay(void* ped, void* veh, float dt)
{
    GameplayCfg& p = g_toggles.play;

    // Things the game stores once: apply on change and shortly after a save is loaded.
    static void* lastPed = nullptr;
    static int applied[4] = { -1, -1, -1, -1 };
    if (ped != lastPed) { lastPed = ped; for (int& a : applied) a = -1; }
    auto apply = [](int& state, bool want, auto fn) { if (state != (int)want) { state = want; fn(want); } };
    apply(applied[0], p.unlimitedStamina, Game::SetNeverTired);
    apply(applied[1], p.fastReload, Game::SetFastReload);
    apply(applied[2], p.noFallOffBike, Game::SetStayOnBike);
    apply(applied[3], p.neverBusted && Game::CanPreventBusted(), Game::SetNeverBusted);
    if (p.neverBusted) Game::ClearArrest();
    static bool driveByApplied = false;
    if (driveByApplied != p.freeDriveBy) { driveByApplied = p.freeDriveBy; Game::SetFreeDriveBy(driveByApplied); }
    if (p.infiniteBreath) Game::RefillBreath();
    static bool moveAimApplied = false;
    bool wantMoveAim = p.moveWhileAiming && !g_multiplayer;
    if (moveAimApplied != wantMoveAim) { moveAimApplied = wantMoveAim; Game::SetMoveWhileAiming(wantMoveAim); }

    float& hp = Game::PedHealth(ped);
    float& ar = Game::PedArmour(ped);

    static bool customHpOn = false;
    static float normalMaxHp = 100.0f;
    if (p.customHealth != customHpOn) {
        customHpOn = p.customHealth;
        if (customHpOn) normalMaxHp = Game::PedMaxHealth(ped);
        else {
            Game::PedMaxHealth(ped) = normalMaxHp;
            if (hp > normalMaxHp) hp = normalMaxHp;
        }
    }
    if (p.customHealth) Game::PedMaxHealth(ped) = (float)p.maxHealth;
    float maxHp = Game::PedMaxHealth(ped), maxAr = p.customArmour ? (float)p.maxArmour : 100.0f;
    static bool customArOn = false;
    if (customArOn && !p.customArmour && ar > 100.0f) ar = 100.0f;
    customArOn = p.customArmour;

    // Damage taken: compare against last frame and give some back (or take more).
    static float prevHp = -1, prevAr = -1;
    static float sinceHit = 0;
    bool hit = prevHp >= 0 && (hp < prevHp - 0.01f || ar < prevAr - 0.01f);
    if (hit) {
        sinceHit = 0;
        if (p.damageTaken != 100 && !g_toggles.godMode) {
            float scale = p.damageTaken / 100.0f;
            if (ar < prevAr) ar = std::clamp(prevAr - (prevAr - ar) * scale, 0.0f, 10000.0f);
            if (hp < prevHp) hp = std::max(hp <= 0.0f ? 0.0f : 1.0f, prevHp - (prevHp - hp) * scale);
        }
    } else {
        sinceHit += dt;
    }

    static float hpClock = 0, arClock = 0, ammoClock = 0;
    if (p.healthRegen && hp > 0 && hp < maxHp && sinceHit >= p.healthDelay && (hpClock += dt) >= p.healthEvery) {
        hpClock = 0;
        hp = std::min(maxHp, hp + p.healthAmount);
    }
    if (p.armourRegen && hp > 0 && ar < maxAr && sinceHit >= p.armourDelay && (arClock += dt) >= p.armourEvery) {
        arClock = 0;
        ar = std::min(maxAr, ar + p.armourAmount);
    }
    if (p.ammoRegen && (ammoClock += dt) >= p.ammoEvery) {
        ammoClock = 0;
        int slot = Game::ActiveSlot(), t, c, a;
        Game::WeaponSlot(slot, t, c, a);
        if (slot >= 2 && slot <= 8 && t) Game::SetSlotAmmo(slot, std::min(99999, a + p.ammoAmount));
    }
    if (p.customAmmo) {
        for (int s = 2; s <= 8; ++s) {
            int t, c, a;
            Game::WeaponSlot(s, t, c, a);
            if (t && a < p.reserveAmmo) Game::SetSlotAmmo(s, p.reserveAmmo);
        }
    }
    prevHp = hp; prevAr = ar;

    static float clipApplied = -1.0f;
    if (clipApplied != p.clipMult) { clipApplied = p.clipMult; Game::SetClipMultiplier(p.clipMult); }

    Combat::SpawnPendingExplosions();

    // Free-look drive-by: right mouse starts it, the mouse turns the camera, left mouse fires as usual.
    bool& driveBy = g_driveBy;
    static int weaponBefore = 0;
    int mouseX, mouseY;
    Hooks::TakeMouseDelta(mouseX, mouseY);
    int smg = 0;
    { int c, a; Game::WeaponSlot(4, smg, c, a); if (a <= 0) smg = 0; }
    bool wantDriveBy = p.freeDriveBy && Game::CanFreeDriveBy() && veh && Game::IsDriver(veh) && smg && g_gameFocused &&
                       (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    if (wantDriveBy && !driveBy) {
        driveBy = true;
        weaponBefore = Game::CurrentWeapon();
        Game::SetCurrentWeapon(smg);
        Game::StartDriveBy();
    } else if (!wantDriveBy && driveBy) {
        driveBy = false;
        if (veh) { Game::StopDriveBy(); Game::SetCurrentWeapon(weaponBefore); }
    }
    if (driveBy && Game::CameraMode() == 55) Game::TurnDriveByCamera(mouseX, mouseY, p.driveBySens);

    // Nitrous
    static void* nitroCar = nullptr;
    const NitroCfg& n = g_toggles.nitro;
    if (veh && (n.infinite || n.noCooldown || n.continuous)) {
        Game::NitroTick(veh, n.infinite, n.noCooldown, n.continuous);
        nitroCar = veh;
    } else if (veh && nitroCar == veh) {
        Game::NitroRestore(veh);
        nitroCar = nullptr;
    }
}

// Options that only need setting once: re-applied whenever their value changes, so a loaded
// config takes effect without any further action.
static void ApplyWorld()
{
    static float damage = 1.0f, gravity = 0.008f, speed = -1.0f;
    static bool weatherWasLocked = false;
    if (damage != g_toggles.damageMult) { damage = g_toggles.damageMult; Game::SetDamageMultiplier(damage); }
    if (gravity != g_toggles.gravity && Game::HasGravity()) { gravity = g_toggles.gravity; Game::SetGravity(gravity); }

    float want = g_toggles.gameSpeed;
    if (Wheel::IsOpen() && g_toggles.wheel.slowmo) want = 0.2f;
    else if (g_toggles.play.slowmoAiming && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) && g_gameFocused) want = g_toggles.play.slowmoScale;
    if (speed != want) { speed = want; Game::SetTimeScale(want); }

    if (weatherWasLocked && !g_toggles.lockWeather) Game::ReleaseWeather();
    weatherWasLocked = g_toggles.lockWeather;
}

void Tick()
{
    std::vector<std::function<void()>> pending;
    AcquireSRWLockExclusive(&g_queueLock);
    pending.swap(g_queue);
    ReleaseSRWLockExclusive(&g_queueLock);

    void* ped = Game::PlayerPed();
    if (!ped) return;   // drop queued actions while no game is loaded
    for (auto& fn : pending) fn();
    RiskTick();

    // Proofs live on the ped/car; re-assert them about once a second (cheap, survives
    // respawns and switching cars) and clear them once when a toggle goes off.
    static DWORD lastProofs = 0;
    static bool pedProofs = false, carProofs = false;
    bool refresh = GetTickCount() - lastProofs > 1000;
    if (refresh) lastProofs = GetTickCount();

    if (g_toggles.godMode) {
        if (refresh || !pedProofs) { Game::SetPlayerProofs(true); pedProofs = true; }
        if (Game::PedHealth(ped) < Game::PedMaxHealth(ped)) Game::PedHealth(ped) = Game::PedMaxHealth(ped);
    } else if (pedProofs) {
        Game::SetPlayerProofs(false);
        pedProofs = false;
    }

    if (g_toggles.infiniteArmour && Game::PedArmour(ped) < 100.0f) Game::PedArmour(ped) = 100.0f;
    if (g_toggles.infiniteAmmo) Game::RefillAmmo(ped);

    static bool neverWantedApplied = false;
    if (g_toggles.neverWanted != neverWantedApplied) {
        Game::SetMaxWantedLevel(g_toggles.neverWanted ? 0 : 6);
        if (g_toggles.neverWanted) Game::SetWantedLevel(0);
        neverWantedApplied = g_toggles.neverWanted;
    }

    void* veh = Game::PlayerVehicle();
    if (veh) {
        if (g_toggles.vehicleGod) {
            if (refresh || !carProofs) { Game::SetVehicleProofs(true); carProofs = true; }
            if (Game::VehicleHealth(veh) < 1000.0f) Game::VehicleHealth(veh) = 1000.0f;
        } else if (carProofs) {
            Game::SetVehicleProofs(false);
            carProofs = false;
        }
        if (g_toggles.autoRepair) Game::AutoRepair(veh);
    } else {
        carProofs = false;
    }

    // Frame time from QPC so speeds are fps-independent.
    static LARGE_INTEGER freq{}, last{};
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); last = now; }
    float dt = std::min(0.1f, float(now.QuadPart - last.QuadPart) / float(freq.QuadPart));
    last = now;
    static bool noclipWas = false;
    bool noclipDown = (GetAsyncKeyState(g_toggles.noclipKey) & 0x8000) != 0;
    if (noclipDown && !noclipWas && g_gameFocused) g_toggles.noclip = !g_toggles.noclip;
    noclipWas = noclipDown;
    Game::Noclip(g_toggles.noclip, g_gameFocused ? dt : 0.0f, g_toggles.noclipSpeed);   // dt 0 = hover in place

    Game::PreviewCamera(g_preview.active, g_preview.onVehicle, g_preview.yaw, g_preview.dist, g_preview.height);

    if (g_toggles.freezeTime) Game::SetTime(g_toggles.frozenHour, g_toggles.frozenMinute);
    if (g_toggles.lockWeather && refresh) Game::ForceWeather(g_toggles.weather);

    ApplyWorld();
    Combat::Tick();
    Gameplay(ped, veh, dt);
    GPS::Tick();
    Wheel::Tick();
    Game::MissionTick();
    if (g_bodyDirty && Game::CanChangeClothes()) { g_bodyDirty = false; Game::RebuildBody(); }

    if (g_toggles.mission.protectAllies && refresh) {
        std::vector<Blip> blips;
        Game::GetBlips(blips);
        for (const Blip& b : blips)
            if (b.type == 2 && (b.colour == 1 || b.colour == 2 || b.colour == 6))
                if (void* p = Game::PedFromHandle(b.handle)) Game::ProtectPed(p);
    }

    SelfTest(ped);
}

// Runs when RiffedMenu.selftest exists in the game dir: exercises engine calls and logs results.
// The stress phases try every skin and clothing item; progress survives a crash, so a restart
// carries on after the item that took the game down.
static std::string ProgressFile() { return Config::Dir() + "selftest-progress.txt"; }
static void SaveProgress(int phase, int index) { std::ofstream(ProgressFile(), std::ios::trunc) << phase << ' ' << index << '\n'; }

static bool StressStep(int& phase, int& index)
{
    // Phase 0: every skin, returning to CJ after each. Phase 1: every clothing item on CJ.
    if (phase == 0) {
        const auto& skins = GameData::Skins();
        if (index >= (int)skins.size()) { phase = 1; index = 0; Game::SetSkin(0); Log("STRESS skins done"); return true; }
        SaveProgress(phase, index);
        if (Game::PlayerVehicle()) { Log("STRESS waiting: on foot needed"); return true; }
        Log("STRESS skin %d/%d id=%d %s", index + 1, (int)skins.size(), skins[index].id, skins[index].name.c_str());
        Game::SetSkin(skins[index].id);
        ++index;
        return true;
    }
    if (phase == 1) {
        const auto& items = GameData::Clothes();
        if (index >= (int)items.size()) { phase = 2; index = 0; Log("STRESS clothes done"); return true; }
        SaveProgress(phase, index);
        const ClothingItem& it = items[index];
        Log("STRESS clothes %d/%d part=%d tex=%s model=%s", index + 1, (int)items.size(), it.part, it.texture.c_str(), it.model.c_str());
        Game::GiveClothes(it.texture.c_str(), it.model.c_str(), it.part);
        ++index;
        return true;
    }
    return false;
}

void SelfTest(void* ped)
{
    static int stage = -1, phase = 0, index = 0;
    static DWORD inGameSince = 0, nextStep = 0;
    if (stage == -1) {
        stage = GetFileAttributesA((GameDir() + "RiffedMenu.selftest").c_str()) != INVALID_FILE_ATTRIBUTES ? 0 : 99;
        if (stage == 0) {
            Log("SELFTEST armed - waiting 5 s in game");
            std::ifstream f(ProgressFile());
            int p, i;
            if (f >> p >> i) {
                Log("SELFTEST previous run stopped at phase %d index %d - that item crashed or hung the game", p, i);
                std::ofstream((Config::Dir() + "selftest-crashes.txt"), std::ios::app) << p << ' ' << i << '\n';
                phase = p; index = i + 1; stage = 20;
            }
        }
        inGameSince = GetTickCount();
    }
    if (stage >= 99 || GetTickCount() - inGameSince < 5000 || GetTickCount() < nextStep) return;
    nextStep = GetTickCount() + (stage == 20 ? 500 : 900);

    switch (stage++) {
    case 0: {
        Log("SELFTEST ped=%p health=%.1f/%.1f armour=%.1f money=%d peds=%d", ped, Game::PedHealth(ped), Game::PedMaxHealth(ped),
            Game::PedArmour(ped), Game::Money(), Game::PedPoolSize());
        CVector p = Game::PlayerPosition();
        Log("SELFTEST pos=%.1f %.1f %.1f interior=%d", p.x, p.y, p.z, Game::ActiveInterior());
        break;
    }
    case 1:
        GameData::Load();
        Log("SELFTEST %d clothing items, %d skins, gps nodes %d", (int)GameData::Clothes().size(), (int)GameData::Skins().size(), GPS::NodeCount());
        break;
    case 2:
        if (Game::ActiveInterior() != 0) { Log("SELFTEST leaving interior"); Game::Teleport({ 2495.0f, -1687.0f, 13.5f }, true); }
        break;
    case 3: {
        CVector pos = Game::EntityPos(ped), head, screen;
        bool bone = Game::PedBone(ped, 8, head);
        float sx = 0, sy = 0, fx = 0, fy = 0, rx = 9, ry = 9, ox = 0, oy = 0;
        bool onScreen = Game::WorldToScreen(pos, 2560, 1440, sx, sy);
        Game::ProjectionScale(fx, fy);
        Game::WorldToRadar(pos.x, pos.y, rx, ry);
        Game::RadarToScreen(0, 0, ox, oy);
        Log("SELFTEST bone=%d head=%.2f %.2f %.2f (root z %.2f); cam mode %d alpha %.2f beta %.2f; player on screen %d at %.0f,%.0f; scale %.2f %.2f",
            bone, head.x, head.y, head.z, pos.z, Game::CameraMode(), Game::CameraAlpha(), Game::CameraBeta(), onScreen, sx, sy, fx, fy);
        Log("SELFTEST radar: player at %.2f,%.2f, centre on screen %.0f,%.0f; on mission %d; can stop arrest %d", rx, ry, ox, oy, Game::OnMission(), Game::CanPreventBusted());
        int cops = 0, peds = 0;
        for (int i = 0; i < Game::PedPoolSize(); ++i) if (void* p = Game::PedAt(i)) { ++peds; cops += Game::PedType(p) == 6; }
        std::vector<Blip> blips; Game::GetBlips(blips);
        Log("SELFTEST peds alive in pool %d, cops %d, blips %d, wheel weapon txd for pistol: %s", peds, cops, (int)blips.size(), GameData::WeaponTxd(22).c_str());
        (void)screen;
        break;
    }
    case 4: g_toggles.gps.enabled = true; { CVector p = Game::PlayerPosition(); (void)p; } Log("SELFTEST gps on, nodes %d, route points %d", GPS::NodeCount(), GPS::RoutePoints()); break;
    case 5: Game::SpawnVehicle(560, true, "RIFFED"); break;
    case 6: {
        void* veh = Game::PlayerVehicle();
        Log("SELFTEST in vehicle: %s", veh ? "yes" : "NO");
        if (veh) { Game::AddPart(1010, -1); }
        break;
    }
    case 7: { void* veh = Game::PlayerVehicle(); if (veh) { g_toggles.nitro.continuous = true; Log("SELFTEST continuous nitrous on"); } break; }
    case 8: g_toggles.nitro.continuous = false; if (void* veh = Game::PlayerVehicle()) Game::NitroRestore(veh); break;
    case 9:
        g_toggles.play.unlimitedStamina = true; g_toggles.play.noFallOffBike = true; g_toggles.play.neverBusted = true;
        Log("SELFTEST stamina/bike/busted toggled");
        break;
    case 10:
        g_toggles.play.unlimitedStamina = false; g_toggles.play.noFallOffBike = false; g_toggles.play.neverBusted = false;
        Game::AddExplosion(Game::PlayerPosition(), 12);
        Game::SetClipMultiplier(1.0f);
        Log("SELFTEST explosion and clip table ok");
        break;
    case 11: if (Game::PlayerVehicle()) Game::Teleport(Game::PlayerPosition(), true); break;
    case 12: { int ch = 0; (void)ch; Features::Queue([] { Game::SetSkin(0); }); stage = 20; break; }
    case 20:
        --stage;
        stage = 20;
        if (!StressStep(phase, index)) {
            Log("SELFTEST finished - delete RiffedMenu.selftest to disable");
            DeleteFileA(ProgressFile().c_str());
            stage = 99;
        }
        break;
    default:
        break;
    }
}

} // namespace Features
