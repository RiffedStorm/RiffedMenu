#include "features.h"
#include "game.h"
#include "log.h"
#include "gamedata.h"
#include <windows.h>
#include <algorithm>
#include <vector>

Toggles g_toggles;
PreviewCam g_preview;
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

    Game::PreviewCamera(g_preview.active, g_preview.onVehicle, g_preview.yaw, g_preview.dist, g_preview.height);

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
    static DWORD nextStep = 0;
    if (stage >= 99 || GetTickCount() - inGameSince < 5000 || GetTickCount() < nextStep) return;
    nextStep = GetTickCount() + 600;   // let the game settle between steps (warps, streaming)

    switch (stage++) {
    case 0: {
        Log("SELFTEST ped=%p health=%.1f/%.1f armour=%.1f money=%d", ped, Game::PedHealth(ped), Game::PedMaxHealth(ped),
            Game::PedArmour(ped), Game::Money());
        CVector p = Game::PlayerPosition();
        int h, m; Game::GetTime(h, m);
        Log("SELFTEST pos=%.1f %.1f %.1f time=%02d:%02d wanted=%d interior=%d", p.x, p.y, p.z, h, m, Game::WantedLevel(), Game::ActiveInterior());
        break;
    }
    case 1:
        GameData::Load();
        Log("SELFTEST sultan: %d parts, %d paintjobs; %d clothing items, %d skins, %d colours",
            (int)GameData::PartsFor(560).size(), Game::PaintjobCount(560), (int)GameData::Clothes().size(),
            (int)GameData::Skins().size(), (int)GameData::CarColours().size());
        break;
    case 2:
        if (Game::ActiveInterior() != 0) { Log("SELFTEST leaving interior"); Game::Teleport({ 2495.0f, -1687.0f, 13.5f }, true); }
        break;
    case 3: Game::SpawnVehicle(560, true, "RIFFED"); break;
    case 4: {
        void* veh = Game::PlayerVehicle();
        Log("SELFTEST in vehicle after spawn-inside: %s (model %d)", veh ? "yes" : "NO", veh ? Game::VehicleModel(veh) : -1);
        if (!veh) { stage = 7; break; }
        auto parts = GameData::PartsFor(560);
        for (auto& pt : parts)
            if (pt.category == "Spoiler" || pt.category == "Side skirts" || pt.category == "Wheels") {
                Game::AddPart(pt.id, GameData::LinkedPart(pt.id));
                Log("SELFTEST fitted %s %d (%s), linked %d", pt.category.c_str(), pt.id, pt.name.c_str(), GameData::LinkedPart(pt.id));
                if (pt.category == "Wheels") break;
            }
        Game::SetPaintjob(0);
        Game::SetVehicleColors(3, 1);
        Log("SELFTEST paintjob 0 + colours 3/1 applied, now %d/%d", Game::VehicleColor(veh, 0), Game::VehicleColor(veh, 1));
        break;
    }
    case 5: g_preview = { true, true, 35.0f, 6.5f, 1.8f }; Game::PreviewCamera(true, true, 35.0f, 6.5f, 1.8f); Log("SELFTEST showroom camera on"); break;
    case 6: g_preview.active = false; Game::PreviewCamera(false, true, 0, 0, 0); Log("SELFTEST showroom camera off"); break;
    case 7: Game::SaveOutfit(); Game::GiveClothes("glasses01", "glasses01", 15); Log("SELFTEST gave glasses01"); break;
    case 8: Game::RestoreOutfit(); Log("SELFTEST outfit restored"); break;
    case 9: Log("SELFTEST damage multiplier supported: %s", Game::SetDamageMultiplier(1.0f) ? "yes" : "NO"); break;
    default:
        Log("SELFTEST finished - delete RiffedMenu.selftest to disable");
        stage = 99;
    }
}

} // namespace Features
