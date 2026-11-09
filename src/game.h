#pragma once
#include <cstdint>
#include <string>
#include <vector>

// GTA:SA engine access. Almost everything goes through the game's own SCM script
// interpreter (CRunningScript::ProcessOneCommand), the same trick CLEO uses: opcode
// semantics are identical across executables, so each supported exe only needs a
// handful of addresses (see kVersions in game.cpp). Struct field offsets (CPed,
// CVehicle, CPlayerInfo) are the same in every PC build.
//
// All functions must be called from the game's main thread (Features::Tick does that).

struct CVector { float x, y, z; };
struct Blip { uint32_t colour; int handle; CVector pos; int sprite; int type; int display; bool shortRange; };   // type: 1 car, 2 ped, 3 object, 4 coord

enum class GameVersion { Unknown, V10US, V10EU, V11US, V11EU, Steam, SteamR2, Steam2014 };

namespace Game {

GameVersion  DetectVersion();          // also resolves (and ASLR-rebases) addresses
GameVersion  Version();
const char*  VersionName(GameVersion v);
bool         Supported();              // exe has an address table
bool         HasGravity();             // gravity slider available on this exe
void*        D3DDevice();              // the game's IDirect3DDevice9*, null until created / unknown exe

// --- player (direct struct reads) ----------------------------------------
void*  PlayerPed();                    // CPlayerPed* or null (not in game yet)
bool   InGame();
float& PedHealth(void* ped);
float& PedMaxHealth(void* ped);
float& PedArmour(void* ped);
int&   Money();
void   RefillAmmo(void* ped);

// --- player (opcodes) -----------------------------------------------------
int    WantedLevel();
void   SetWantedLevel(int level);
void   SetMaxWantedLevel(int level);
void   SetPlayerProofs(bool on);
void   SetHealth(float hp);
void   AddArmour(int amount);

// --- position ---------------------------------------------------------------
CVector PlayerPosition();
int     ActiveInterior();              // 0 = outside world
void    Teleport(const CVector& pos, bool snapToGround);
bool    WaypointPosition(CVector& out);

// --- vehicles ---------------------------------------------------------------
void*    PlayerVehicle();              // CVehicle* if the player is in one
float&   VehicleHealth(void* veh);
int      VehicleModel(void* veh);
uint8_t  VehicleColor(void* veh, int which);
void     SetVehicleColors(int c1, int c2);
void     SetVehicleProofs(bool on);
void     RepairVehicle();
void     AutoRepair(void* veh);                 // call every frame: fixes health loss and visible damage
void     FlipVehicle();
void     BoostVehicle(float factor);
void     SpawnVehicle(int modelId, bool warpInside, const char* plate);   // no-op indoors; doors unlocked

// --- vehicle customizer (current car) -----------------------------------------
int      PaintjobCount(int model);
void     SetPaintjob(int index);                // -1 removes
void     AddPart(int part, int linked);         // linked: other half of a pair or -1
void     RemovePart(int part, int linked);

// --- outfit / skin -------------------------------------------------------------
void     GiveClothes(const char* texture, const char* model, int part);
void     RemoveClothes(int part);
void     SaveOutfit();                          // one slot, shared with the game's own shops
void     RestoreOutfit();
void     SetSkin(int model);                    // 0 = CJ; on foot only

// --- preview camera ---------------------------------------------------------------
// Call every frame while previewing; on=false restores the normal camera.
void     PreviewCamera(bool on, bool onVehicle, float yawDeg, float dist, float height);

// --- weapons ------------------------------------------------------------------------
bool     SetDamageMultiplier(float mult);       // all weapons, every ped; false if unsupported

// --- movement -----------------------------------------------------------------
// Camera-relative flight: WASD, Space up, Ctrl down, Shift faster. Call every frame.
void   Noclip(bool on, float dt, float speed);
void   GiveJetpack();                  // on foot only

// --- weapons ----------------------------------------------------------------
void   GiveWeapon(int weaponType, int ammo);
int    WeaponModelId(int weaponType);

// --- world ------------------------------------------------------------------
void   GetTime(int& h, int& m);
void   SetTime(int h, int m);
void   SetTimeScale(float s);
void   SetGravity(float g);
void   ForceWeather(int id);
void   ReleaseWeather();

// --- stats ------------------------------------------------------------------
float  GetStat(uint16_t id);
void   SetStat(uint16_t id, float value);

// --- entity pools -----------------------------------------------------------
int     PedPoolSize();
void*   PedAt(int index);              // null for free slots
int     PedHandle(void* ped);
void*   PedFromHandle(int handle);
void*   VehicleFromHandle(int handle);
int     PedType(void* ped);            // 6 cop, 7..16 gangs (8 is Grove Street)
int     PedState(void* ped);
bool    PedDead(void* ped);
void*   PedLastDamager(void* ped);
void*   PedCar(void* ped);
CVector EntityPos(void* entity);
CVector EntitySpeed(void* entity);
bool    PedBone(void* ped, int bone, CVector& out);   // 8 head, 3 chest, 2 pelvis
void    GetBlips(std::vector<Blip>& out);

// --- camera -----------------------------------------------------------------
bool    HasCamera();
int     CameraMode();
CVector CameraPos();
float&  CameraAlpha();                 // vertical angle, radians
float&  CameraBeta();                  // horizontal angle, radians
void    SetRenderCamera(const float* view, const float* proj);   // what Direct3D really draws with (4x4 each)
bool    WidescreenOn();                // cutscene bars
bool    PauseMenuOpen();
bool    WorldToScreen(const CVector& p, float w, float h, float& sx, float& sy);
void    ProjectionScale(float& sx, float& sy);
bool    LineOfSight(const CVector& a, const CVector& b, bool cars);

// --- radar ------------------------------------------------------------------
bool    HasRadar();
void    WorldToRadar(float x, float y, float& rx, float& ry);   // unit circle = radar edge
void    RadarToScreen(float rx, float ry, float& sx, float& sy);

// --- combat -----------------------------------------------------------------
void*   BulletImpactAddress();
void*   ArrestStartAddress();          // CTaskSimpleArrestPed::StartAnim
void    UndoArrest(void* ped, int previousState);
int     CurrentWeapon();
void    SetCurrentWeapon(int type);
void    WeaponSlot(int slot, int& type, int& clip, int& total);
void    SetSlotAmmo(int slot, int total);
int     ActiveSlot();
void    AddExplosion(const CVector& p, int type);
bool    SetClipMultiplier(float mult);
void    KillPed(void* ped);
void    ExplodeVehicle(void* veh);
void    ProtectPed(void* ped);

// --- player extras ----------------------------------------------------------
void    SetNeverTired(bool on);
void    SetFastReload(bool on);
void    SetStayOnBike(bool on);
void    RefillBreath();
bool    CanFreeDriveBy();
void    SetFreeDriveBy(bool on);
bool    IsDriver(void* veh);
void    StartDriveBy();
void    StopDriveBy();
void    TurnDriveByCamera(int mouseX, int mouseY, float userScale);
bool    CanPreventBusted();
void    SetNeverBusted(bool on);
void    ClearArrest();
bool    CanChangeClothes();            // CJ's own model, on foot
void    RebuildBody();                 // applies the current fat and muscle stats to CJ's model

// --- nitrous ----------------------------------------------------------------
void    NitroTick(void* veh, bool infinite, bool noCooldown, bool continuous);
void    NitroRestore(void* veh);

// --- missions ---------------------------------------------------------------
bool    HasMissions();
bool    OnMission();
std::string MissionName();             // the running mission's script name
bool    MissionPass();                 // false if the mission has no pass routine
bool    MissionFail();
bool    StartMission(int index, bool preserveStory);
void    MissionTick();                 // restores story variables after a replay
bool    ReplayActive();
void    DiscardReplay();
void    RecoverScreen();               // fade in, camera and controls back

} // namespace Game
