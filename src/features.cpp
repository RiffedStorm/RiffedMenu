#include "features.h"
#include "game.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <vector>

Toggles g_toggles;
bool    g_gameFocused = false;

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

void Tick()
{
    std::vector<std::function<void()>> pending;
    AcquireSRWLockExclusive(&g_queueLock);
    pending.swap(g_queue);
    ReleaseSRWLockExclusive(&g_queueLock);

    void* ped = Game::PlayerPed();
    if (!ped) return;   // drop queued actions while no game is loaded
    for (auto& fn : pending) fn();

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
        if (g_toggles.autoRepair && Game::VehicleHealth(veh) < 999.0f) Game::RepairVehicle();
    } else {
        carProofs = false;
    }

    // Noclip: F7 toggles it in game too. Frame time from QPC so speed is fps-independent.
    static LARGE_INTEGER freq{}, last{};
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); last = now; }
    float dt = std::min(0.1f, float(now.QuadPart - last.QuadPart) / float(freq.QuadPart));
    last = now;
    static bool f7Was = false;
    bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !f7Was && g_gameFocused) g_toggles.noclip = !g_toggles.noclip;
    f7Was = f7;
    Game::Noclip(g_toggles.noclip, g_gameFocused ? dt : 0.0f, g_toggles.noclipSpeed);   // dt 0 = hover in place

    if (g_toggles.freezeTime) Game::SetTime(g_toggles.frozenHour, g_toggles.frozenMinute);
    if (g_toggles.lockWeather && refresh) Game::ForceWeather(g_toggles.weather);

    SelfTest(ped);
}

// Runs once per session when RiffedMenu.selftest exists in the game dir: exercises every
// engine call with harmless values and logs the results (used to validate a new exe port).
void SelfTest(void* ped)
{
    static int stage = -1;
    static DWORD inGameSince = 0;
    if (stage == -1) {
        stage = GetFileAttributesA((GameDir() + "RiffedMenu.selftest").c_str()) != INVALID_FILE_ATTRIBUTES ? 0 : 99;
        if (stage == 0) Log("SELFTEST armed - waiting 5 s in game");
        inGameSince = GetTickCount();
    }
    if (stage >= 99 || GetTickCount() - inGameSince < 5000) return;

    switch (stage++) {
    case 0: {
        Log("SELFTEST ped=%p health=%.1f/%.1f armour=%.1f money=%d", ped, Game::PedHealth(ped), Game::PedMaxHealth(ped),
            Game::PedArmour(ped), Game::Money());
        CVector p = Game::PlayerPosition();
        int h, m; Game::GetTime(h, m);
        Log("SELFTEST pos=%.1f %.1f %.1f time=%02d:%02d wanted=%d", p.x, p.y, p.z, h, m, Game::WantedLevel());
        Log("SELFTEST stats fat=%.0f stamina=%.0f muscle=%.0f driving=%.0f pistol=%.0f", Game::GetStat(21), Game::GetStat(22),
            Game::GetStat(23), Game::GetStat(160), Game::GetStat(69));
        CVector w;
        if (Game::WaypointPosition(w)) Log("SELFTEST waypoint at %.1f %.1f", w.x, w.y);
        else Log("SELFTEST no waypoint set");
        break;
    }
    case 1: Game::SetWantedLevel(1); Log("SELFTEST wanted after set(1) = %d", Game::WantedLevel()); break;
    case 2: Game::SetWantedLevel(0); Log("SELFTEST wanted after set(0) = %d", Game::WantedLevel()); break;
    case 3: Game::SetPlayerProofs(true); Game::SetPlayerProofs(false); Log("SELFTEST proofs on/off ok"); break;
    case 4: Game::GiveWeapon(22, 50); Log("SELFTEST weapon slot2 type=%d", *(int*)((char*)ped + 0x5A0 + 2 * 0x1C)); break;
    case 5: Game::SpawnVehicle(411); break;
    case 6: Log("SELFTEST active interior = %d", Game::ActiveInterior()); break;
    default:
        Log("SELFTEST finished - delete RiffedMenu.selftest to disable");
        stage = 99;
    }
}

} // namespace Features
