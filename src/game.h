#pragma once
#include <cstdint>

// GTA:SA engine access. Almost everything goes through the game's own SCM script
// interpreter (CRunningScript::ProcessOneCommand), the same trick CLEO uses: opcode
// semantics are identical across executables, so each supported exe only needs a
// handful of addresses (see kVersions in game.cpp). Struct field offsets (CPed,
// CVehicle, CPlayerInfo) are the same in every PC build.
//
// All functions must be called from the game's main thread (Features::Tick does that).

struct CVector { float x, y, z; };

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

} // namespace Game
