#include "game.h"
#include "log.h"
#include "features.h"
#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Per-exe address table. Values are VAs at the preferred image base 0x400000;
// they are rebased at runtime because the 2014 Steam exe is ASLR-enabled.
// ---------------------------------------------------------------------------
struct Addresses {
    uintptr_t processOneCommand;  // CRunningScript::ProcessOneCommand (thiscall)
    uintptr_t players;            // CWorld::Players[] (CPlayerInfo, 0x190 each; m_pPed at +0, money at +0xB8)
    uintptr_t radarTraces;        // CRadar::ms_RadarTrace[175] (0x28 each)
    uintptr_t gravity;            // float, 0 if not a fixed constant on this exe
    uintptr_t d3dDevice;          // RenderWare's IDirect3DDevice9* global
    uintptr_t modelInfoPtrs;      // CModelInfo::ms_modelInfoPtrs[]
    uintptr_t weaponInfo;         // aWeaponInfo[] (CWeaponInfo, 0x70 each)
    uintptr_t pedPool;            // CPools::ms_pPedPool
    uintptr_t vehPool;            // CPools::ms_pVehiclePool
    uintptr_t camera;             // TheCamera
    uintptr_t scriptSpace;        // CTheScripts::ScriptSpace
    uintptr_t activeScripts;      // CTheScripts::pActiveScripts
    uintptr_t onMissionVar;       // int: offset of $ONMISSION inside ScriptSpace
    uintptr_t worldToRadar;       // CRadar::TransformRealWorldPointToRadarSpace (cdecl out, in)
    uintptr_t radarToScreen;      // CRadar::TransformRadarPointToScreenSpace (cdecl out, in)
    uintptr_t boneFn;             // CPed::GetBonePosition (thiscall out, bone, update)
    uintptr_t bulletImpact;       // CWeapon::DoBulletImpact (thiscall, 6 args)
    uintptr_t addExplosion;       // CExplosion::AddExplosion (cdecl)
    uintptr_t bustedJump;         // CGameLogic::Update: `jne` past the "player is arrested" block
    uintptr_t frontEnd;           // FrontEndMenuManager
    uintptr_t arrestStart;        // CTaskSimpleArrestPed::StartAnim (thiscall ped)
    uintptr_t mouseSens;          // float: CCamera::m_fMouseAccelHorzntl, the game's mouse sensitivity option
    uintptr_t driveByRecentre;    // two ints: how long the driver's drive-by camera waits before swinging back to the side
    uintptr_t fireInstantHit;     // CWeapon::FireInstantHit (thiscall, 8 args)
    uintptr_t cameraAimLine;      // CCamera::Find3rdPersonCamTargetVector (thiscall range, source, &camera, &point)
    uintptr_t weaponInfoFor;      // CWeaponInfo::GetWeaponInfo (cdecl type, skill)
    uintptr_t pedWeaponSkill;     // CPed::GetWeaponSkill (thiscall type)
    uintptr_t animGroups;         // CPlayerPed::ProcessAnimGroups (thiscall): picks the walk/run set each frame
    uintptr_t pedPad;             // CPed::GetPadFromPlayer (thiscall)
    uintptr_t padSprint;          // CPad::GetSprint (thiscall), so the player's own key binding applies
    uintptr_t useGunTask;         // CPedIntelligence::GetTaskUseGun (thiscall)
    uintptr_t padWalkX;           // CPad::GetPedWalkLeftRight (thiscall)
    uintptr_t padWalkY;           // CPad::GetPedWalkUpDown (thiscall)
    uintptr_t gunMove;            // CTaskSimpleUseGun::ControlGunMove (thiscall, CVector2D*)
    uintptr_t driveByGate;        // CTaskSimpleGangDriveBy::ProcessPed: `cmp [info], 1; jne end`, bullet weapons only
    uintptr_t removeWeaponModel;  // CPed::RemoveWeaponModel (thiscall model, -1 = any)
    uintptr_t zonesVisited;       // CTheZones::ZonesVisited: 10x10 bytes, the pause map's fog
    uintptr_t zonesRevealed;      // CTheZones::ZonesRevealed: how many of those are set
    uintptr_t clockMs;            // CTimer::m_snTimeInMilliseconds
    uintptr_t weaveAngle;         // CCarCtrl::FindAngleToWeaveThroughTraffic (cdecl car, other, angle, angle, distance)
    uintptr_t weaveLoopTest;      // the two "have the angles settled?" tests in that function
    uintptr_t lineClear;          // CWorld::GetIsLineOfSightClear (cdecl from, to, 7 flags)
    uintptr_t lineHit;            // CWorld::ProcessLineOfSight (cdecl from, to, colpoint, entity, 8 flags)
};

static const Addresses kV10US     = { 0x469EB0, 0xB7CD98, 0xBA86F0, 0x863984, 0xC97C28, 0xA9B0C8, 0xC8AAB8,
                                      0xB74490, 0xB74494, 0xB6F028, 0xA49960, 0xA8B42C, 0, 0x583530, 0x583480, 0x5E4280,
                                      0x73B550, 0x736A50, 0, 0xBA6748, 0x68B7E0, 0xB6EC1C, 0 };
// Found by disassembly of gta-sa.exe build 459558 (PE timestamp 0x547CD3A3):
//   0x46EDD0 reads opcode at [ecx+0x14], sets NOT flag [ecx+0xD2], dispatches via table 0x914BB0
//   0xC100D0 is the base used by 139 sites of `imul r, r, 0x190`
//   0xC36858 = 0xC3687D - 0x25 from CRadar's free-slot scan (bInUse bit, 0x28 stride, 0x1B58 end)
//   0xD23768 is loaded 371 times and called through vtable slots 2 (Release) / 40 (GetDepthStencilSurface)
//   0xB12818 is indexed by model id in the GIVE_VEHICLE_PAINTJOB / GET_AVAILABLE_VEHICLE_MOD handlers
//   0xD164E8 is where the weapon.dat loader (0x5DBD10) writes entries: imul esi,esi,0x70; [esi+0xD164E8]
//   Pools: operands of GetAt in SET_CHAR_HEALTH / CHANGE_CAR_COLOUR. Camera: 0xBFC4A1 - 0x59 from GET_ACTIVE_CAMERA_COORDINATES.
//   ScriptSpace from CollectParameters, active list from StartNewScript, $ONMISSION from DECLARE_MISSION_FLAG.
//   0x5FBE20 uses bCalledPreRender + hierarchy matrices; 0x76E0D0 is the only `ret 0x18` calling GetWeaponInfo first;
//   0x769840 is the call in ADD_EXPLOSION; 0x445BA3 is the jne before the only call of ArrestPlayer (0x561200).
static const Addresses kSteam2014 = { 0x46EDD0, 0xC100D0, 0xC36858, 0, 0xD23768, 0xB12818, 0xD164E8,
                                      0xC018A8, 0xC018AC, 0xBFC448, 0xAC12D0, 0xB02D98, 0xABF01C, 0x5A0600, 0x5A0560, 0x5FBE20,
                                      0x76E0D0, 0x769840, 0x445BA3, 0xC33938, 0x6B8890, 0xBFC05C, 0x93EBBC,
                                      0x772D30, 0x5217D0, 0x7770D0, 0x5FF7C0, 0x6263C0, 0x626000, 0x551110, 0x61D230, 0x550590, 0x550640, 0x63DAC0, 0x64EBBD, 0x5FF5D0, 0xC30938, 0xC2EFF4, 0xC0FD74, 0x4347F0, 0x4349BF, 0x583750, 0x5849C0 };

static GameVersion g_version = GameVersion::Unknown;
static Addresses   g_addr{};
static uintptr_t   g_delta = 0;

template <typename T> static T& At(void* base, int offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <typename T> static T& Mem(uintptr_t a) { return *reinterpret_cast<T*>(a); }
static uintptr_t Rebase(uintptr_t va) { return va ? va + g_delta : 0; }

static bool Readable(uintptr_t a, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return a + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// Byte signature check with wildcards (-1) for relocated operands.
static bool Matches(uintptr_t a, std::initializer_list<int> sig)
{
    if (!Readable(a, sig.size())) return false;
    const uint8_t* p = (const uint8_t*)a;
    for (int b : sig) { if (b >= 0 && *p != b) return false; ++p; }
    return true;
}

// ---------------------------------------------------------------------------
// Script opcode caller
// ---------------------------------------------------------------------------
namespace {

constexpr int kScriptSize   = 0x200;   // CRunningScript is 0xE0; padded
constexpr int offCurrentIP  = 0x14;
constexpr int offLocalVars  = 0x3C;
constexpr int offCondResult = 0xC5;
constexpr int offNotFlag    = 0xD2;
constexpr int offLogicalOp  = 0xD0;

alignas(16) uint8_t g_script[kScriptSize];

struct Arg {
    enum Kind : uint8_t { Int, Float, Var, Str8, Str16 } kind;
    union { int32_t i; float f; const char* s; };
};
Arg I(int32_t v) { Arg a; a.kind = Arg::Int;   a.i = v; return a; }
Arg F(float v)   { Arg a; a.kind = Arg::Float; a.f = v; return a; }
Arg V(int idx)   { Arg a; a.kind = Arg::Var;   a.i = idx; return a; }
Arg S8(const char* s)  { Arg a; a.kind = Arg::Str8;  a.s = s; return a; }   // fixed 8-byte text label
Arg S16(const char* s) { Arg a; a.kind = Arg::Str16; a.s = s; return a; }   // fixed 16-byte text label

int32_t& VarI(int idx) { return At<int32_t>(g_script, offLocalVars + idx * 4); }
float&   VarF(int idx) { return At<float>(g_script, offLocalVars + idx * 4); }

// Executes one opcode. Returns the condition result (meaningful for IS_/HAS_ opcodes).
bool Op(uint16_t opcode, std::initializer_list<Arg> args)
{
    uint8_t code[128];
    size_t n = 0;
    memcpy(code + n, &opcode, 2); n += 2;
    for (const Arg& a : args) {
        switch (a.kind) {
        case Arg::Int:   code[n++] = 0x01; memcpy(code + n, &a.i, 4); n += 4; break;  // int32 literal
        case Arg::Float: code[n++] = 0x06; memcpy(code + n, &a.f, 4); n += 4; break;  // float literal
        case Arg::Var:   { code[n++] = 0x03; uint16_t v = (uint16_t)a.i; memcpy(code + n, &v, 2); n += 2; break; } // local var
        case Arg::Str8:
        case Arg::Str16: {
            size_t len = a.kind == Arg::Str8 ? 8 : 16;
            code[n++] = a.kind == Arg::Str8 ? 0x09 : 0x0F;
            memset(code + n, 0, len);
            strncpy((char*)code + n, a.s, len - 1);
            n += len;
            break;
        }
        }
    }
    At<uint8_t*>(g_script, offCurrentIP) = code;
    At<uint8_t>(g_script, offCondResult) = 0;
    At<uint8_t>(g_script, offNotFlag) = 0;
    At<uint16_t>(g_script, offLogicalOp) = 0;

    ((char(__thiscall*)(void*))g_addr.processOneCommand)(g_script);

    uint8_t* ip = At<uint8_t*>(g_script, offCurrentIP);
    if (ip != code + n) {
        static int complaints = 0;
        if (complaints++ < 20)
            Log("WARNING opcode %04X consumed %d bytes, expected %d - wrong param list?", opcode, (int)(ip - code), (int)n);
    }
    return At<uint8_t>(g_script, offCondResult) != 0;
}

int PlayerChar()
{
    Op(0x01F5, { I(0), V(0) });              // GET_PLAYER_CHAR 0 -> var0
    return VarI(0);
}

// Returns a car handle or -1 if the player is on foot.
int PlayerCar()
{
    int ch = PlayerChar();
    if (!Op(0x00DF, { I(ch) })) return -1;  // IS_CHAR_IN_ANY_CAR
    Op(0x03C0, { I(ch), V(1) });             // STORE_CAR_CHAR_IS_IN_NO_SAVE -> var1
    return VarI(1);
}

void LoadModels(std::initializer_list<int> models)
{
    for (int m : models) if (m > 0) Op(0x0247, { I(m) });   // REQUEST_MODEL
    Op(0x038B, {});                                           // LOAD_ALL_MODELS_NOW
}

void ReleaseModels(std::initializer_list<int> models)
{
    for (int m : models) if (m > 0) Op(0x0249, { I(m) });   // MARK_MODEL_AS_NO_LONGER_NEEDED
}

} // namespace

// ---------------------------------------------------------------------------
namespace off {
constexpr int PedHealth    = 0x540;
constexpr int PedMaxHealth = 0x544;
constexpr int PedArmour    = 0x548;
constexpr int PedVehicle   = 0x58C;
constexpr int PedWeapons   = 0x5A0;   // CWeapon[13], 0x1C each
constexpr int ModelIndex   = 0x22;
constexpr int VehColor1    = 0x434;
constexpr int VehColor2    = 0x435;
constexpr int VehHealth    = 0x4C0;
}

namespace Game {

GameVersion DetectVersion()
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    g_delta = base - 0x400000;
    g_version = GameVersion::Unknown;

    // 2014 Steam re-release: ProcessOneCommand prologue (inc word [CommandsExecuted]; mov eax,[ecx+14h]; ...)
    if (Matches(base + (0x46EDD0 - 0x400000),
                { 0x66, 0xFF, 0x05, -1, -1, -1, -1, 0x8B, 0x41, 0x14, 0x0F, 0xB7, 0x10, 0x83, 0xC0, 0x02,
                  0x89, 0x41, 0x14, 0x8B, 0xC2, 0xC1, 0xE8, 0x0F, 0x24, 0x01, 0x88, 0x81, 0xD2, 0x00, 0x00, 0x00 })) {
        g_version = GameVersion::Steam2014;
        g_addr = kSteam2014;
    } else if (g_delta == 0) {
        // Classic executables are not relocatable; same probes as plugin-sdk / SilentPatch.
        struct Probe { uintptr_t a; uint32_t v; GameVersion ver; } probes[] = {
            { 0x82457C, 0x94BF,     GameVersion::V10US },
            { 0x8245BC, 0x94BF,     GameVersion::V10EU },
            { 0x8252FC, 0x94BF,     GameVersion::V11US },
            { 0x82533C, 0x94BF,     GameVersion::V11EU },
            { 0x85EC4A, 0x94BF,     GameVersion::Steam },
            { 0x858D21, 0x3539F633, GameVersion::SteamR2 },
        };
        for (auto& p : probes)
            if (Readable(p.a, 4) && Mem<uint32_t>(p.a) == p.v) { g_version = p.ver; break; }
        if (g_version == GameVersion::V10US) g_addr = kV10US;
    }

    if (Supported()) {
        g_addr.processOneCommand = Rebase(g_addr.processOneCommand);
        g_addr.players           = Rebase(g_addr.players);
        g_addr.radarTraces       = Rebase(g_addr.radarTraces);
        g_addr.gravity           = Rebase(g_addr.gravity);
        g_addr.d3dDevice         = Rebase(g_addr.d3dDevice);
        g_addr.modelInfoPtrs     = Rebase(g_addr.modelInfoPtrs);
        g_addr.weaponInfo        = Rebase(g_addr.weaponInfo);
        for (uintptr_t* a : { &g_addr.pedPool, &g_addr.vehPool, &g_addr.camera, &g_addr.scriptSpace, &g_addr.activeScripts,
                              &g_addr.onMissionVar, &g_addr.worldToRadar, &g_addr.radarToScreen, &g_addr.boneFn,
                              &g_addr.bulletImpact, &g_addr.addExplosion, &g_addr.bustedJump, &g_addr.frontEnd, &g_addr.arrestStart, &g_addr.mouseSens, &g_addr.driveByRecentre,
                              &g_addr.fireInstantHit, &g_addr.cameraAimLine, &g_addr.weaponInfoFor, &g_addr.pedWeaponSkill, &g_addr.animGroups,
                              &g_addr.pedPad, &g_addr.padSprint, &g_addr.useGunTask, &g_addr.padWalkX, &g_addr.padWalkY, &g_addr.gunMove, &g_addr.driveByGate, &g_addr.removeWeaponModel, &g_addr.zonesVisited, &g_addr.zonesRevealed, &g_addr.clockMs, &g_addr.weaveAngle, &g_addr.weaveLoopTest, &g_addr.lineClear, &g_addr.lineHit })
            *a = Rebase(*a);
        memset(g_script, 0, sizeof(g_script));
        memcpy(g_script + 0x08, "riffed", 7);   // m_szName, shows up in game debug output
    }
    Log("Image base 0x%08lX (delta %+ld), version %s", (unsigned long)base, (long)g_delta, VersionName(g_version));
    return g_version;
}

GameVersion Version() { return g_version; }

const char* VersionName(GameVersion v)
{
    switch (v) {
    case GameVersion::V10US:     return "v1.0 US";
    case GameVersion::V10EU:     return "v1.0 EU";
    case GameVersion::V11US:     return "v1.01 US";
    case GameVersion::V11EU:     return "v1.01 EU";
    case GameVersion::Steam:     return "Steam v3.0";
    case GameVersion::SteamR2:   return "Steam (LV)";
    case GameVersion::Steam2014: return "Steam 2014 re-release";
    default:                     return "unknown";
    }
}

bool Supported()  { return g_addr.processOneCommand != 0; }
void* D3DDevice() { return g_addr.d3dDevice ? Mem<void*>(g_addr.d3dDevice) : nullptr; }
bool HasGravity() { return g_addr.gravity != 0; }

// --- player ------------------------------------------------------------------

void*  PlayerPed()             { return Supported() ? Mem<void*>(g_addr.players) : nullptr; }
bool   InGame()                { return PlayerPed() != nullptr; }
float& PedHealth(void* ped)    { return At<float>(ped, off::PedHealth); }
float& PedMaxHealth(void* ped) { return At<float>(ped, off::PedMaxHealth); }
float& PedArmour(void* ped)    { return At<float>(ped, off::PedArmour); }
int&   Money()                 { return Mem<int>(g_addr.players + 0xB8); }

void RefillAmmo(void* ped)
{
    // Remember the biggest clip seen per slot so the magazine never empties.
    static int maxClip[13] = {};
    static int lastType[13] = {};
    for (int s = 0; s < 13; ++s) {
        uint8_t* w = static_cast<uint8_t*>(ped) + off::PedWeapons + s * 0x1C;
        int& type = *reinterpret_cast<int*>(w + 0x0);
        int& clip = *reinterpret_cast<int*>(w + 0x8);
        int& total = *reinterpret_cast<int*>(w + 0xC);
        if (type == 0) continue;
        if (type != lastType[s]) { lastType[s] = type; maxClip[s] = clip; }
        if (clip > maxClip[s]) maxClip[s] = clip;
        if (maxClip[s] > 0 && clip < maxClip[s]) clip = maxClip[s];
        if (total < 9999) total = 9999;
    }
}

int WantedLevel()
{
    if (!InGame()) return 0;
    Op(0x01C0, { I(0), V(2) });              // STORE_WANTED_LEVEL player -> var2
    return VarI(2);
}

void SetWantedLevel(int level)    { if (InGame()) Op(0x010D, { I(0), I(level) }); }   // ALTER_WANTED_LEVEL
void SetMaxWantedLevel(int level) { if (InGame()) Op(0x01F0, { I(level) }); }         // SET_MAX_WANTED_LEVEL

void SetPlayerProofs(bool on)
{
    if (!InGame()) return;
    int b = on ? 1 : 0;
    Op(0x02AB, { I(PlayerChar()), I(b), I(b), I(b), I(b), I(b) });   // SET_CHAR_PROOFS bullet fire explosion collision melee
}

void SetHealth(float hp)   { if (InGame()) Op(0x0223, { I(PlayerChar()), I((int)hp) }); }   // SET_CHAR_HEALTH
void AddArmour(int amount) { if (InGame()) Op(0x035F, { I(PlayerChar()), I(amount) }); }    // ADD_ARMOUR_TO_CHAR

// --- position ------------------------------------------------------------------

CVector PlayerPosition()
{
    if (!InGame()) return { 0, 0, 0 };
    Op(0x00A0, { I(PlayerChar()), V(2), V(3), V(4) });   // GET_CHAR_COORDINATES
    return { VarF(2), VarF(3), VarF(4) };
}

int ActiveInterior()
{
    if (!InGame()) return 0;
    Op(0x077E, { V(2) });                    // GET_ACTIVE_INTERIOR
    return VarI(2);
}

void Teleport(const CVector& p, bool snapToGround)
{
    if (!InGame()) return;
    // Destinations are in the outside world: leave any interior first, otherwise the
    // game keeps streaming/rendering the interior and the player ends up "out of bounds".
    int ch = PlayerChar();
    Op(0x04BB, { I(0) });                                                         // SELECT_INTERIOR 0
    Op(0x0860, { I(ch), I(0) });                                                  // SET_CHAR_AREA_VISIBLE
    int car = PlayerCar();
    if (car >= 0) Op(0x0840, { I(car), I(0) });                                   // SET_CAR_AREA_VISIBLE
    Op(0x03CB, { F(p.x), F(p.y), F(p.z) });                                       // LOAD_SCENE
    Op(0x04E4, { F(p.x), F(p.y) });                                               // REQUEST_COLLISION
    Op(0x00A1, { I(ch), F(p.x), F(p.y), F(snapToGround ? -100.0f : p.z) });     // SET_CHAR_COORDINATES (moves car too)
    CVector now = PlayerPosition();
    Log("Teleport -> %.1f %.1f %.1f (landed at %.1f %.1f %.1f)", p.x, p.y, p.z, now.x, now.y, now.z);
}

bool WaypointPosition(CVector& out)
{
    if (!Supported() || !g_addr.radarTraces) return false;
    for (int i = 0; i < 175; ++i) {
        uintptr_t t = g_addr.radarTraces + i * 0x28;
        bool inUse = (Mem<uint8_t>(t + 0x25) & 0x02) != 0;
        if (inUse && Mem<uint8_t>(t + 0x24) == 41 /*RADAR_SPRITE_WAYPOINT*/) {
            out = Mem<CVector>(t + 0x08);
            return true;
        }
    }
    return false;
}

// --- vehicles --------------------------------------------------------------------

void* PlayerVehicle()
{
    void* ped = PlayerPed();
    if (!ped || !Op(0x00DF, { I(PlayerChar()) })) return nullptr;   // IS_CHAR_IN_ANY_CAR
    return At<void*>(ped, off::PedVehicle);
}

float&  VehicleHealth(void* veh)            { return At<float>(veh, off::VehHealth); }
int     VehicleModel(void* veh)             { return At<uint16_t>(veh, off::ModelIndex); }
uint8_t VehicleColor(void* veh, int which)  { return At<uint8_t>(veh, which ? off::VehColor2 : off::VehColor1); }

void SetVehicleColors(int c1, int c2) { int car = PlayerCar(); if (car >= 0) Op(0x0229, { I(car), I(c1), I(c2) }); } // CHANGE_CAR_COLOUR
void RepairVehicle()                  { int car = PlayerCar(); if (car >= 0) Op(0x0A30, { I(car) }); }             // FIX_CAR

// Dents, broken lights, lost doors and burst tyres don't always lower health (and god mode holds
// it at 1000), so compare the car's damage record too. Some models legally have "missing" doors,
// so the comparison is against how the car looks right after a repair, not against "no damage".
static void DamageSnapshot(void* veh, uint8_t out[20])
{
    memcpy(out, (uint8_t*)veh + 0x5A0 + 4, 20);        // CDamageManager: engine, wheels, doors, lights, panels
    for (int i = 5; i <= 10; ++i) {                    // an open door isn't damage
        if (out[i] == 1) out[i] = 0;
        else if (out[i] == 3) out[i] = 2;
    }
}

void AutoRepair(void* veh)
{
    static void* last = nullptr;
    static uint8_t baseline[20];
    static bool needBaseline = false;
    if (veh != last) { last = veh; RepairVehicle(); needBaseline = true; return; }

    int cls = At<int>(veh, 0x590);
    bool hasDamageRecord = cls >= 0 && cls <= 4;       // cars, monster trucks, quads, helicopters, planes
    uint8_t now[20] = {};
    if (hasDamageRecord) DamageSnapshot(veh, now);
    if (needBaseline) { memcpy(baseline, now, 20); needBaseline = false; return; }

    if (VehicleHealth(veh) < 1000.0f || memcmp(now, baseline, 20) != 0) { RepairVehicle(); needBaseline = true; }
}

void SetVehicleProofs(bool on)
{
    int car = PlayerCar();
    if (car < 0) return;
    int b = on ? 1 : 0;
    Op(0x02AC, { I(car), I(b), I(b), I(b), I(b), I(b) });   // SET_CAR_PROOFS
}

void FlipVehicle()
{
    int car = PlayerCar();
    if (car < 0) return;
    Op(0x0174, { I(car), V(2) });                            // GET_CAR_HEADING
    float heading = VarF(2);
    Op(0x00AA, { I(car), V(3), V(4), V(5) });                // GET_CAR_COORDINATES
    Op(0x00AB, { I(car), F(VarF(3)), F(VarF(4)), F(VarF(5) + 1.0f) }); // SET_CAR_COORDINATES
    Op(0x0175, { I(car), F(heading) });                      // SET_CAR_HEADING (rebuilds an upright matrix)
}

// On its roof and nearly stopped. Bikes, aircraft and boats are left alone, since a plane can fly inverted.
bool VehicleStuckUpsideDown(void* veh)
{
    if (At<int>(veh, 0x594) > 2) return false;              // 0 car, 1 monster truck, 2 quad
    void* m = At<void*>(veh, 0x14);
    if (!m || At<float>(m, 0x28) > -0.15f) return false;    // z of the car's up direction
    CVector v = EntitySpeed(veh);
    return v.x * v.x + v.y * v.y + v.z * v.z < 0.05f * 0.05f;
}

void BoostVehicle(float factor)
{
    int car = PlayerCar();
    if (car < 0) return;
    Op(0x02E3, { I(car), V(2) });                            // GET_CAR_SPEED
    float speed = VarF(2) < 10.0f ? 25.0f : VarF(2) * factor;
    Op(0x04BA, { I(car), F(speed) });                        // SET_CAR_FORWARD_SPEED
}

static float SpawnDistance(int model)
{
    switch (model) {
    case 577: case 592:                                       return 30.0f;  // AT-400, Andromada
    case 553: case 519: case 520: case 548: case 417: case 425:
    case 511: case 476: case 512: case 513: case 460: case 593:
    case 487: case 488: case 497: case 563: case 469: case 447: return 12.0f; // aircraft
    case 403: case 514: case 515: case 431: case 437: case 524: case 578: case 455: return 9.0f;
    default:                                                  return 6.0f;
    }
}

static void WarpIntoSpawned(int car, int model)
{
    if (InGame() && Op(0x056E, { I(car) })) {                        // DOES_VEHICLE_EXIST
        Op(0x036A, { I(PlayerChar()), I(car) });                     // WARP_CHAR_INTO_CAR
        Op(0x01C3, { I(car) });                                      // MARK_CAR_AS_NO_LONGER_NEEDED
    }
    ReleaseModels({ model });
}

void SpawnVehicle(int model, bool warpInside, const char* plate)
{
    if (!InGame() || ActiveInterior() != 0) return;
    int ch = PlayerChar();
    LoadModels({ model });
    if (plate && *plate) Op(0x0674, { I(model), S8(plate) });      // CUSTOM_PLATE_FOR_NEXT_CAR
    Op(0x04C4, { I(ch), F(0.0f), F(SpawnDistance(model)), F(0.5f), V(2), V(3), V(4) }); // GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS
    float x = VarF(2), y = VarF(3), z = VarF(4);
    Op(0x0172, { I(ch), V(5) });                                     // GET_CHAR_HEADING
    float heading = VarF(5);
    bool leaveFirst = warpInside && PlayerCar() >= 0;
    if (leaveFirst) Op(0x0362, { I(ch), F(x), F(y), F(z + 3.0f) });  // WARP_CHAR_FROM_CAR_TO_COORD
    Op(0x00A5, { I(model), F(x), F(y), F(z), V(6) });                // CREATE_CAR -> var6
    int car = VarI(6);
    Op(0x0175, { I(car), F(heading) });                              // SET_CAR_HEADING
    Op(0x020A, { I(car), I(1) });                                    // LOCK_CAR_DOORS 1 = unlocked (tank, some planes spawn locked)
    if (leaveFirst) {
        // Warping straight between cars leaves the camera on the old one. Once CJ has been out for a
        // frame the camera follows him again, and getting in behaves normally.
        Features::Queue([car, model] { Features::Queue([car, model] { WarpIntoSpawned(car, model); }); });
        Log("Spawned model %d handle %d at %.1f %.1f %.1f (leaving the old car first)", model, car, x, y, z);
        return;
    }
    if (warpInside) Op(0x036A, { I(ch), I(car) });                   // WARP_CHAR_INTO_CAR
    Op(0x01C3, { I(car) });                                          // MARK_CAR_AS_NO_LONGER_NEEDED
    ReleaseModels({ model });
    Log("Spawned model %d handle %d at %.1f %.1f %.1f%s", model, car, x, y, z, warpInside ? " (warped in)" : "");
}

// --- vehicle customizer ----------------------------------------------------------

int PaintjobCount(int model)
{
    if (!g_addr.modelInfoPtrs || model < 0 || model >= 20000) return 0;
    void* mi = Mem<void*>(g_addr.modelInfoPtrs + model * 4);
    if (!mi) return 0;
    int n = 0;
    while (n < 4 && At<int16_t>(mi, 0x2FA + n * 2) != -1) ++n;    // CVehicleModelInfo::m_anRemapTxds[4]
    return n;
}

void SetPaintjob(int index)   // -1 removes
{
    int car = PlayerCar();
    if (car >= 0) Op(0x06ED, { I(car), I(index) });                 // GIVE_VEHICLE_PAINTJOB
}

void AddPart(int part, int linked)
{
    int car = PlayerCar();
    if (car < 0) return;
    LoadModels({ part, linked });
    Op(0x06E7, { I(car), I(part), V(2) });                          // ADD_VEHICLE_MOD
    if (linked > 0) Op(0x06E7, { I(car), I(linked), V(2) });
    ReleaseModels({ part, linked });
}

void RemovePart(int part, int linked)
{
    int car = PlayerCar();
    if (car < 0) return;
    Op(0x06E8, { I(car), I(part) });                                // REMOVE_VEHICLE_MOD
    if (linked > 0) Op(0x06E8, { I(car), I(linked) });
}

// --- outfit / skin ---------------------------------------------------------------

static void BuildPlayer() { Op(0x070D, { I(0) }); }                  // BUILD_PLAYER_MODEL
void RebuildBody() { BuildPlayer(); }

// Texture part -> model slot, decoded from the game's CClothes::GetTextureDependency table.
static const int kClothesModelSlot[18] = { 0, 1, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, 6, 7, 8, 9 };

// CKeyGen::GetUppercaseKey: CRC-32 of the upper-cased name, without the final inversion.
static uint32_t ClothesKey(const char* name)
{
    uint32_t crc = 0xFFFFFFFF;
    for (; *name; ++name) {
        crc ^= (uint8_t)toupper((unsigned char)*name);
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
    }
    return crc;
}

bool CanChangeClothes()
{
    void* ped = PlayerPed();
    return ped && At<uint16_t>(ped, off::ModelIndex) == 0 && !PlayerVehicle();   // rebuilding any other model crashes
}

void GiveClothes(const char* texture, const char* model, int part)
{
    if (!CanChangeClothes()) return;
    if (strlen(texture) < 16 && strlen(model) < 16) {
        Op(0x087B, { I(0), S16(texture), S16(model), I(part) });    // GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP
    } else {
        // Script text is limited to 15 letters. A cut-off name (cutoffchinosblue is 16) points at a
        // texture that doesn't exist and the game hangs waiting for it, so write the keys directly.
        void* desc = Mem<void*>(g_addr.players + 0x08);             // CPedClothesDesc
        if (!desc || part < 0 || part >= 18) return;
        At<uint32_t>(desc, 0x28 + part * 4) = ClothesKey(texture);
        if (kClothesModelSlot[part] >= 0) At<uint32_t>(desc, kClothesModelSlot[part] * 4) = ClothesKey(model);
    }
    BuildPlayer();
}

void RemoveClothes(int part)
{
    // An empty name hashes to 0xFFFFFFFF, so clearing a slot means zeroing CPedClothesDesc keys directly.
    if (!CanChangeClothes() || part < 0 || part >= 18) return;
    void* desc = Mem<void*>(g_addr.players + 0x08);                 // CPlayerInfo::m_PlayerData.m_pPedClothesDesc
    if (!desc) return;
    At<uint32_t>(desc, 0x28 + part * 4) = 0;
    if (kClothesModelSlot[part] >= 0) At<uint32_t>(desc, kClothesModelSlot[part] * 4) = 0;
    BuildPlayer();
}

void SaveOutfit()    { Op(0x0793, {}); }                              // STORE_CLOTHES_STATE
void RestoreOutfit() { if (CanChangeClothes()) { Op(0x0794, {}); BuildPlayer(); } }               // RESTORE_CLOTHES_STATE

void SetSkin(int model)   // 0 = CJ
{
    if (!InGame() || PlayerCar() >= 0) return;
    if (model > 0) LoadModels({ model });
    Op(0x09C7, { I(0), I(model) });                                 // SET_PLAYER_MODEL
    if (model > 0) ReleaseModels({ model });
    else BuildPlayer();                                             // CJ needs his clothes rebuilt
    Log("Skin -> %d", model);
}

// --- preview camera -----------------------------------------------------------------

void PreviewCamera(bool on, bool onVehicle, float yawDeg, float dist, float height)
{
    static bool active = false;
    if (!on || !InGame()) {
        if (active) Op(0x015A, {});                                  // RESTORE_CAMERA_JUMPCUT
        active = false;
        return;
    }
    float x, y, z, heading;
    int car = onVehicle ? PlayerCar() : -1;
    if (car >= 0) {
        Op(0x00AA, { I(car), V(2), V(3), V(4) });                    // GET_CAR_COORDINATES
        Op(0x0174, { I(car), V(5) });                                // GET_CAR_HEADING
    } else {
        int ch = PlayerChar();
        Op(0x00A0, { I(ch), V(2), V(3), V(4) });                     // GET_CHAR_COORDINATES
        Op(0x0172, { I(ch), V(5) });                                 // GET_CHAR_HEADING
    }
    x = VarF(2); y = VarF(3); z = VarF(4); heading = VarF(5);
    float a = (heading + yawDeg) * 0.0174533f;                       // yaw 0 = in front, looking back at it
    float cx = x - sinf(a) * dist, cy = y + cosf(a) * dist;
    Op(0x015F, { F(cx), F(cy), F(z + height), F(0), F(0), F(0) });  // SET_FIXED_CAMERA_POSITION
    Op(0x0160, { F(x), F(y), F(z + (car >= 0 ? 0.3f : 0.5f)), I(2) }); // POINT_CAMERA_AT_POINT, jump cut
    active = true;
}

// --- weapon damage ------------------------------------------------------------------

// The pistol entry (type 22) must use model 346 in slot 2 with deagle (348) and shotgun (349)
// two and three entries later - a cheap check that the table really is CWeaponInfo[].
static bool LooksLikeWeaponTable(uint8_t* t)
{
    uint8_t* e = t + 22 * 0x70;
    return *(int*)(e + 0x0C) == 346 && *(int*)(e + 0x14) == 2 &&
           *(int*)(e + 2 * 0x70 + 0x0C) == 348 && *(int*)(e + 3 * 0x70 + 0x0C) == 349;
}

static uint8_t* FindWeaponInfo()
{
    static uint8_t* cached = nullptr;
    static bool searched = false;
    if (searched) return cached;
    searched = true;
    if (g_addr.weaponInfo && Readable(g_addr.weaponInfo, 80 * 0x70) && LooksLikeWeaponTable((uint8_t*)g_addr.weaponInfo)) {
        cached = (uint8_t*)g_addr.weaponInfo;
    } else {
        // Unknown layout: scan the exe's writable memory for the same signature.
        auto base = (uint8_t*)GetModuleHandleA(nullptr);
        auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        uint8_t* end = base + nt->OptionalHeader.SizeOfImage;
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        for (uint8_t* p = base; p < end && !cached;) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            uint8_t* rEnd = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && (mbi.Protect & writable))
                for (uint8_t* e = p + 22 * 0x70; e + 4 * 0x70 < rEnd; e += 4)
                    if (LooksLikeWeaponTable(e - 22 * 0x70)) { cached = e - 22 * 0x70; break; }
            p = rEnd;
        }
    }
    if (cached) Log("Weapon info table at %p (pistol clip %u, damage %u)", cached,
                    *(uint16_t*)(cached + 22 * 0x70 + 0x20), *(uint16_t*)(cached + 22 * 0x70 + 0x22));
    else        Log("Weapon info table not found - damage multiplier disabled");
    return cached;
}

bool SetDamageMultiplier(float mult)
{
    constexpr int kEntries = 80;    // 47 weapon types + poor/pro/special skill variants
    static uint16_t original[kEntries];
    static bool saved = false;
    uint8_t* table = FindWeaponInfo();
    if (!table) return false;
    for (int i = 0; i < kEntries; ++i) {
        uint8_t* e = table + i * 0x70;
        int model = *(int*)(e + 0x0C);
        if (model != -1 && model != 0 && (model < 321 || model > 372)) break;   // ran past the table
        uint16_t& dmg = *(uint16_t*)(e + 0x22);
        if (!saved) original[i] = dmg;
        if (original[i] == 0) continue;
        float v = original[i] * mult;
        dmg = (uint16_t)(v < 1.0f ? 1.0f : v > 30000.0f ? 30000.0f : v);
    }
    saved = true;
    return true;
}

void GiveJetpack()
{
    if (!InGame() || PlayerCar() >= 0) return;
    LoadModels({ 370 });
    Op(0x07A7, { I(PlayerChar()) });                         // TASK_JETPACK
    ReleaseModels({ 370 });
    Log("Jetpack given");
}

// --- noclip ----------------------------------------------------------------------

void Noclip(bool on, float dt, float speed)
{
    static bool active = false;
    static int frozenCar = -1;
    if (!InGame()) { active = false; frozenCar = -1; return; }
    int ch = PlayerChar();
    int car = PlayerCar();

    if (!on) {
        if (active) {
            Op(0x04D7, { I(ch), I(0) });                     // FREEZE_CHAR_POSITION off
            if (frozenCar >= 0) Op(0x0519, { I(frozenCar), I(0) });   // FREEZE_CAR_POSITION off
            active = false; frozenCar = -1;
        }
        return;
    }
    // (Re)freeze whatever we're moving so physics doesn't fight the position writes.
    if (!active || car != frozenCar) {
        if (frozenCar >= 0 && car != frozenCar) Op(0x0519, { I(frozenCar), I(0) });
        Op(0x04D7, { I(ch), I(1) });
        if (car >= 0) Op(0x0519, { I(car), I(1) });
        frozenCar = car;
        active = true;
    }

    Op(0x068D, { V(2), V(3), V(4) });                        // GET_ACTIVE_CAMERA_COORDINATES
    Op(0x068E, { V(5), V(6), V(7) });                        // GET_ACTIVE_CAMERA_POINT_AT
    float fx = VarF(5) - VarF(2), fy = VarF(6) - VarF(3), fz = VarF(7) - VarF(4);
    float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len < 0.001f) return;
    fx /= len; fy /= len; fz /= len;
    float hl = sqrtf(fx * fx + fy * fy);
    float rx = hl > 0.001f ? fy / hl : 1.0f, ry = hl > 0.001f ? -fx / hl : 0.0f;   // camera right, flat

    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    float mx = 0, my = 0, mz = 0;
    if (down('W')) { mx += fx; my += fy; mz += fz; }
    if (down('S')) { mx -= fx; my -= fy; mz -= fz; }
    if (down('D')) { mx += rx; my += ry; }
    if (down('A')) { mx -= rx; my -= ry; }
    if (down(VK_SPACE))   mz += 1.0f;
    if (down(VK_CONTROL)) mz -= 1.0f;
    float boost = down(VK_SHIFT) ? 4.0f : 1.0f;

    void* ent = car >= 0 ? At<void*>(PlayerPed(), off::PedVehicle) : PlayerPed();
    void* matrix = ent ? At<void*>(ent, 0x14) : nullptr;
    if (!matrix) return;
    CVector& pos = At<CVector>(matrix, 0x30);
    float step = speed * boost * dt;
    pos.x += mx * step; pos.y += my * step; pos.z += mz * step;
    At<CVector>(ent, 0x44) = { 0, 0, 0 };                    // move speed
    At<CVector>(ent, 0x50) = { 0, 0, 0 };                    // turn speed

    // Face where the camera looks.
    if (hl > 0.001f) {
        float deg = atan2f(-fx, fy) * 57.29578f;
        if (car >= 0) Op(0x0175, { I(car), F(deg) });        // SET_CAR_HEADING
        else          Op(0x0173, { I(ch), F(deg) });         // SET_CHAR_HEADING
    }
}

// --- weapons ---------------------------------------------------------------------

static int WeaponModel(int type)
{
    static const short models[47] = {
        0, 331, 333, 334, 335, 336, 337, 338, 339, 341, 321, 322, 323, 324, 325, 326,  // 0-15 melee/gifts
        342, 343, 344, 0, 0, 0,                                                         // 16-21 thrown
        346, 347, 348, 349, 350, 351, 352, 353, 355, 356, 372, 357, 358,                // 22-34 guns
        359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 371,                     // 35-46 heavy/misc
    };
    return type >= 0 && type < 47 ? models[type] : 0;
}

int WeaponModelId(int type) { return WeaponModel(type); }

void GiveWeapon(int type, int ammo)
{
    if (!InGame()) return;
    int m1 = WeaponModel(type);
    int m2 = type == 39 ? 364 : 0;   // satchel charges come with the detonator
    LoadModels({ m1, m2 });
    int ch = PlayerChar();
    Op(0x01B2, { I(ch), I(type), I(ammo) });   // GIVE_WEAPON_TO_CHAR
    Op(0x01B9, { I(ch), I(type) });            // SET_CURRENT_CHAR_WEAPON
    ReleaseModels({ m1, m2 });
    Log("GiveWeapon(%d, %d) model %d", type, ammo, m1);
}

// --- world -----------------------------------------------------------------------

void GetTime(int& h, int& m)
{
    Op(0x00BF, { V(2), V(3) });                // GET_TIME_OF_DAY
    h = VarI(2); m = VarI(3);
}

void SetTime(int h, int m)    { Op(0x00C0, { I(h), I(m) }); }    // SET_TIME_OF_DAY
void SetTimeScale(float s)    { Op(0x015D, { F(s) }); }          // SET_TIME_SCALE
void ForceWeather(int id)     { Op(0x01B6, { I(id) }); }         // FORCE_WEATHER_NOW (01B5 only schedules it)
void ReleaseWeather()         { Op(0x01B7, {}); }                // RELEASE_WEATHER

void SetGravity(float g)
{
    if (!g_addr.gravity) return;
    DWORD old;
    VirtualProtect((void*)g_addr.gravity, 4, PAGE_EXECUTE_READWRITE, &old);
    Mem<float>(g_addr.gravity) = g;
    VirtualProtect((void*)g_addr.gravity, 4, old, &old);
}

// --- stats -------------------------------------------------------------------------
// IDs below 82 are float stats, 120+ are int stats.

float GetStat(uint16_t id)
{
    if (id < 82) { Op(0x0653, { I(id), V(2) }); return VarF(2); }   // GET_FLOAT_STAT
    Op(0x0652, { I(id), V(2) }); return (float)VarI(2);              // GET_INT_STAT
}

void SetStat(uint16_t id, float v)
{
    if (id < 82) Op(0x062A, { I(id), F(v) });                       // SET_FLOAT_STAT
    else         Op(0x0629, { I(id), I((int)v) });                  // SET_INT_STAT
}

// --- entity pools ---------------------------------------------------------------------

static const int kPedSize = 0x7C4, kVehSize = 0xA18;

static void* PoolGet(uintptr_t poolVar, int size, int ref)
{
    if (!poolVar) return nullptr;
    uint8_t* pool = Mem<uint8_t*>(poolVar);
    if (!pool) return nullptr;
    int i = ref >> 8;
    if (i < 0 || i >= At<int>(pool, 8) || At<uint8_t*>(pool, 4)[i] != (uint8_t)ref) return nullptr;
    return At<uint8_t*>(pool, 0) + i * size;
}

int PedPoolSize()
{
    uint8_t* pool = g_addr.pedPool ? Mem<uint8_t*>(g_addr.pedPool) : nullptr;
    return pool ? At<int>(pool, 8) : 0;
}

void* PedAt(int i)
{
    uint8_t* pool = Mem<uint8_t*>(g_addr.pedPool);
    if (!pool || (int8_t)At<uint8_t*>(pool, 4)[i] < 0) return nullptr;
    return At<uint8_t*>(pool, 0) + i * kPedSize;
}

int PedHandle(void* ped)
{
    uint8_t* pool = Mem<uint8_t*>(g_addr.pedPool);
    int i = (int)(((uint8_t*)ped - At<uint8_t*>(pool, 0)) / kPedSize);
    return (i << 8) | At<uint8_t*>(pool, 4)[i];
}

void* PedFromHandle(int h)     { return PoolGet(g_addr.pedPool, kPedSize, h); }
void* VehicleFromHandle(int h) { return PoolGet(g_addr.vehPool, kVehSize, h); }

int     PedType(void* ped)        { return At<int>(ped, 0x598); }
int     PedState(void* ped)       { return At<int>(ped, 0x530); }
bool    PedDead(void* ped)        { int s = PedState(ped); return s == 54 || s == 55 || PedHealth(ped) <= 0.0f; }
void*   PedLastDamager(void* ped) { return At<void*>(ped, 0x764); }
void*   PedCar(void* ped)         { return (At<uint32_t>(ped, 0x46C) & 0x100) ? At<void*>(ped, off::PedVehicle) : nullptr; }
CVector EntityPos(void* e)        { void* m = At<void*>(e, 0x14); return m ? At<CVector>(m, 0x30) : At<CVector>(e, 4); }
CVector EntitySpeed(void* e)      { return At<CVector>(e, 0x44); }
CVector EntityForward(void* e)    { void* m = At<void*>(e, 0x14); return m ? At<CVector>(m, 0x10) : CVector{ 0.0f, 1.0f, 0.0f }; }

bool PedBone(void* ped, int bone, CVector& out)
{
    if (!g_addr.boneFn || !At<void*>(ped, 0x18)) return false;     // no clump = not streamed in
    out = { 0, 0, 0 };                                             // the game transforms this point: zero = the bone itself
    ((void(__thiscall*)(void*, CVector*, int, bool))g_addr.boneFn)(ped, &out, bone, false);
    return true;
}

// --- radar blips ------------------------------------------------------------------------

void GetBlips(std::vector<Blip>& out)
{
    out.clear();
    if (!g_addr.radarTraces) return;
    for (int i = 0; i < 175; ++i) {
        uintptr_t t = g_addr.radarTraces + i * 0x28;
        if (!(Mem<uint8_t>(t + 0x25) & 0x02)) continue;
        Blip b;
        b.colour = Mem<uint32_t>(t);
        b.handle = Mem<int>(t + 4);
        b.pos    = Mem<CVector>(t + 8);
        b.sprite = Mem<uint8_t>(t + 0x24);
        b.type   = (Mem<uint8_t>(t + 0x26) >> 2) & 0x0F;
        b.display = Mem<uint8_t>(t + 0x26) & 0x03;
        b.shortRange = (Mem<uint8_t>(t + 0x25) & 0x04) != 0;
        out.push_back(b);
    }
}

// --- camera / projection --------------------------------------------------------------

static uint8_t* ActiveCam() { return (uint8_t*)g_addr.camera + 0x174 + Mem<uint8_t>(g_addr.camera + 0x59) * 0x238; }

bool    HasCamera()     { return g_addr.camera != 0; }
int     CameraMode()    { return At<uint16_t>(ActiveCam(), 0x0C); }
CVector CameraPos()     { return At<CVector>(ActiveCam(), 0x19C); }
float&  CameraAlpha()   { return At<float>(ActiveCam(), 0xAC); }
float&  CameraBeta()    { return At<float>(ActiveCam(), 0xBC); }
bool    WidescreenOn()  { return g_addr.camera && Mem<uint8_t>(g_addr.camera + 0x3D) != 0; }
bool    PauseMenuOpen() { return g_addr.frontEnd && Mem<uint8_t>(g_addr.frontEnd + 0x5C) != 0; }

// The camera Direct3D really renders with, captured in the SetTransform hook. CCamera's own
// matrix is a poor fit on this exe (points landed well above where peds are drawn).
static float g_rView[16], g_rProj[16];
static bool  g_haveRender = false;

static void GameMatrixScale(float& sx, float& sy)
{
    // The view matrix rows hold scale * axis + 0.5 * forward, because screen centre is (0.5, 0.5).
    const float* m = (const float*)(g_addr.camera + 0xA04);
    float a = m[0] - 0.5f * m[2], b = m[4] - 0.5f * m[6], c = m[8] - 0.5f * m[10];
    sx = sqrtf(a * a + b * b + c * c);
    a = m[1] - 0.5f * m[2]; b = m[5] - 0.5f * m[6]; c = m[9] - 0.5f * m[10];
    sy = sqrtf(a * a + b * b + c * c);
}

// Direct3D also sees other perspective cameras each frame, so only accept the one with the
// game camera's lens and position.
void SetRenderCamera(const float* view, const float* proj)
{
    if (!g_addr.camera || !PlayerPed()) return;
    static int logs = 0;
    float fx, fy;
    GameMatrixScale(fx, fy);
    float expected = fx * 2.0f;
    CVector cam = CameraPos();
    const float* t = view + 12;
    float ex = -(t[0] * view[0] + t[1] * view[1] + t[2] * view[2]);
    float ey = -(t[0] * view[4] + t[1] * view[5] + t[2] * view[6]);
    float ez = -(t[0] * view[8] + t[1] * view[9] + t[2] * view[10]);
    float off = sqrtf((ex - cam.x) * (ex - cam.x) + (ey - cam.y) * (ey - cam.y) + (ez - cam.z) * (ez - cam.z));
    bool lens = expected > 0.01f && fabsf(proj[0] - expected) / expected < 0.08f;
    if (!lens || off > 6.0f) {
        if (logs < 8) { ++logs; Log("Render camera skipped: lens %.3f (want %.3f), %.1f m from game camera", proj[0], expected, off); }
        return;
    }
    memcpy(g_rView, view, sizeof(g_rView));
    memcpy(g_rProj, proj, sizeof(g_rProj));
    if (!g_haveRender) Log("Render camera locked: lens %.3f %.3f, centre %.3f %.3f, %.2f m from game camera", proj[0], proj[5], proj[8], proj[9], off);
    g_haveRender = true;
}

bool WorldToScreen(const CVector& p, float w, float h, float& sx, float& sy)
{
    if (g_haveRender) {
        const float* v = g_rView;   // Direct3D row vectors: point * view * projection
        float x = p.x * v[0] + p.y * v[4] + p.z * v[8]  + v[12];
        float y = p.x * v[1] + p.y * v[5] + p.z * v[9]  + v[13];
        float z = p.x * v[2] + p.y * v[6] + p.z * v[10] + v[14];
        const float* m = g_rProj;
        float cx = x * m[0] + y * m[4] + z * m[8]  + m[12];
        float cy = x * m[1] + y * m[5] + z * m[9]  + m[13];
        float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
        if (cw < 0.5f) return false;
        sx = (cx / cw + 1.0f) * 0.5f * w;
        sy = (1.0f - cy / cw) * 0.5f * h;
        return true;
    }
    const float* m = (const float*)(g_addr.camera + 0xA04);
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    if (z < 0.5f) return false;
    sx = w * (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / z;
    sy = h * (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / z;
    return true;
}

void ProjectionScale(float& sx, float& sy)
{
    if (g_haveRender) { sx = fabsf(g_rProj[0]) * 0.5f; sy = fabsf(g_rProj[5]) * 0.5f; return; }
    GameMatrixScale(sx, sy);
}

bool LineOfSight(const CVector& a, const CVector& b, bool cars)
{
    return Op(0x06BD, { F(a.x), F(a.y), F(a.z), F(b.x), F(b.y), F(b.z), I(1), I(cars ? 1 : 0), I(0), I(1), I(0) });
}

// --- radar projection -------------------------------------------------------------------

bool HasRadar() { return g_addr.worldToRadar && g_addr.radarToScreen; }

void WorldToRadar(float x, float y, float& rx, float& ry)
{
    float in[2] = { x, y }, out[2];
    ((void*(__cdecl*)(float*, float*))g_addr.worldToRadar)(out, in);
    rx = out[0]; ry = out[1];
}

void RadarToScreen(float rx, float ry, float& sx, float& sy)
{
    float in[2] = { rx, ry }, out[2];
    ((void*(__cdecl*)(float*, float*))g_addr.radarToScreen)(out, in);
    sx = out[0]; sy = out[1];
}

// --- weapons / combat ---------------------------------------------------------------------

void* BulletImpactAddress() { return (void*)g_addr.bulletImpact; }

int CurrentWeapon()
{
    if (!InGame()) return 0;
    Op(0x0470, { I(PlayerChar()), V(2) });           // GET_CURRENT_CHAR_WEAPON
    return VarI(2);
}

void SetCurrentWeapon(int type) { if (InGame()) Op(0x01B9, { I(PlayerChar()), I(type) }); }

void WeaponSlot(int slot, int& type, int& clip, int& total)
{
    uint8_t* w = (uint8_t*)PlayerPed() + off::PedWeapons + slot * 0x1C;
    type = At<int>(w, 0); clip = At<int>(w, 8); total = At<int>(w, 0xC);
}

void SetSlotAmmo(int slot, int total) { At<int>((uint8_t*)PlayerPed() + off::PedWeapons + slot * 0x1C, 0xC) = total; }

int ActiveSlot()
{
    int cur = CurrentWeapon();
    for (int s = 0; s < 13; ++s) { int t, c, a; WeaponSlot(s, t, c, a); if (t == cur) return s; }
    return 0;
}

void AddExplosion(const CVector& p, int type)
{
    if (!g_addr.addExplosion) return;
    ((bool(__cdecl*)(void*, void*, int, CVector, unsigned, bool, float, bool))g_addr.addExplosion)(nullptr, PlayerPed(), type, p, 0, true, -1.0f, false);
}

bool SetClipMultiplier(float mult)
{
    constexpr int kEntries = 80;
    static uint16_t original[kEntries];
    static bool saved = false;
    uint8_t* table = FindWeaponInfo();
    if (!table) return false;
    for (int i = 0; i < kEntries; ++i) {
        uint8_t* e = table + i * 0x70;
        int model = *(int*)(e + 0x0C);
        if (model != -1 && model != 0 && (model < 321 || model > 372)) break;
        uint16_t& clip = *(uint16_t*)(e + 0x20);
        if (!saved) original[i] = clip;
        if (original[i] <= 1) continue;      // melee and single-shot weapons keep their clip
        float v = original[i] * mult;
        clip = (uint16_t)(v < 1.0f ? 1.0f : v > 9999.0f ? 9999.0f : v);
    }
    saved = true;
    return true;
}

// Lets every aimable firearm be walked with while aiming and firing, as it can at Hitman skill.
// First-person, heavy and thrown weapons keep their own rules; so do melee weapons, which share the flag bits.
void SetMoveWhileAiming(bool on)
{
    constexpr int kEntries = 80;
    static uint32_t original[kEntries];
    static bool saved = false;
    uint8_t* table = FindWeaponInfo();
    if (!table) return;
    for (int i = 0; i < kEntries; ++i) {
        uint8_t* e = table + i * 0x70;
        int model = *(int*)(e + 0x0C);
        if (model != -1 && model != 0 && (model < 321 || model > 372)) break;
        uint32_t& flags = *(uint32_t*)(e + 0x18);
        if (!saved) original[i] = flags;
        bool firearm = *(int*)e != 0 && (original[i] & 1) && !(original[i] & (0x4 | 0x8 | 0x100 | 0x200));
        flags = on && firearm ? original[i] | 0x30 : original[i];
    }
    saved = true;
}

// --- player extras ------------------------------------------------------------------------

void SetNeverTired(bool on)   { if (InGame()) Op(0x0330, { I(0), I(on ? 1 : 0) }); }
void SetFastReload(bool on)   { if (InGame()) Op(0x0331, { I(0), I(on ? 1 : 0) }); }
void SetStayOnBike(bool on)   { if (InGame()) Op(0x08C6, { I(PlayerChar()), I(on ? 1 : 0) }); }
void ClearHunger() { Mem<uint16_t>(g_addr.players + 0x144) = 0; }   // CPlayerInfo::m_nNumHoursDidntEat

void RefillBreath()
{
    // Stays topped up at the highest value seen, which is the full bar for CJ's current lung stat.
    void* ped = PlayerPed();
    void* data = ped ? At<void*>(ped, 0x480) : nullptr;
    if (!data) return;
    float& breath = At<float>(data, 0x44);
    static float full = 0.0f;
    if (breath > full) full = breath;
    breath = full;
}

bool CanFreeDriveBy() { return g_addr.driveByRecentre != 0; }

bool IsDriver(void* veh) { return veh && At<void*>(veh, 0x460) == PlayerPed(); }

void StartDriveBy()
{
    // No target and no aiming point: as with the missions' own drive-bys, the player's mouse aims.
    Op(0x0713, { I(PlayerChar()), I(-1), I(-1), F(0.0f), F(0.0f), F(0.0f), F(300.0f), I(4), I(1), I(100) });   // TASK_DRIVE_BY
}

void TurnDriveByCamera(int mouseX, int mouseY, float userScale)
{
    // Same maths as the game's own mouse camera, so its sensitivity option applies here too.
    float sens = g_addr.mouseSens ? Mem<float>(g_addr.mouseSens) : 0.0025f;
    if (!(sens > 0.0f && sens < 0.1f)) sens = 0.0025f;
    float fov = At<float>(ActiveCam(), 0xB4) / 80.0f;
    if (!std::isfinite(CameraBeta())) CameraBeta() = 0.0f;     // one bad value here would stick and hang the game's line checks
    if (!std::isfinite(CameraAlpha())) CameraAlpha() = 0.0f;
    if (!std::isfinite(fov)) return;
    CameraBeta() -= 3.0f * mouseX * fov * sens * userScale;
    float a = CameraAlpha() - 4.0f * mouseY * fov * sens * 0.6f * userScale;
    CameraAlpha() = a < -1.2f ? -1.2f : a > 1.0f ? 1.0f : a;
}

// Lets the drive-by task keep going with anything that fires, not just bullet weapons. Melee still ends it.
void SetDriveByAnyWeapon(bool on)
{
    uintptr_t a = g_addr.driveByGate;
    if (!a || !Matches(a, { 0x83, 0x38, -1, 0x0F, -1 })) return;
    DWORD old;
    VirtualProtect((void*)a, 5, PAGE_EXECUTE_READWRITE, &old);
    Mem<uint8_t>(a + 2) = on ? 0x00 : 0x01;
    Mem<uint8_t>(a + 4) = on ? 0x84 : 0x85;                 // cmp type, 0; je end   /   cmp type, 1; jne end
    VirtualProtect((void*)a, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)a, 5);
}

bool WeaponFiresFromCar(int type)
{
    if (type <= 0 || type == 35 || type == 36 || !g_addr.weaponInfoFor) return false;   // rocket launchers neither show nor fire here
    void* info = ((void*(__cdecl*)(int, int))g_addr.weaponInfoFor)(type, 1);
    int fireType = info ? At<int>(info, 0) : 0;
    return fireType >= 1 && fireType <= 3;                  // bullets, projectiles, flames and sprays
}

bool HasAmmoFor(int type)
{
    for (int s = 0; s < 13; ++s) { int t, c, a; WeaponSlot(s, t, c, a); if (t == type) return a > 0; }
    return false;
}

// What CJ walked up to the car with, when the game swapped it for an SMG on the way in.
int WeaponBeforeCar()
{
    void* ped = PlayerPed();
    int saved = ped ? At<int>(ped, 0x70C) : 55;
    return saved == 55 ? 0 : saved;
}

void HideHeldWeapon()
{
    if (void* ped = PlayerPed()) if (g_addr.removeWeaponModel) ((void(__thiscall*)(void*, int))g_addr.removeWeaponModel)(ped, -1);
}

// Selects a weapon without putting it in CJ's hands while he's driving, and keeps it when he gets out.
void SetVehicleWeapon(int type)
{
    void* ped = PlayerPed();
    void* data = ped ? At<void*>(ped, 0x480) : nullptr;
    if (!data) return;
    for (int s = 0; s < 13; ++s) {
        int t, c, a;
        WeaponSlot(s, t, c, a);
        if (type == 0 ? s != 0 : t != type) continue;
        At<int8_t>(ped, 0x718) = (int8_t)s;
        At<uint8_t>(data, 0x20) = (uint8_t)s;
        if (At<int>(ped, 0x70C) != 55) At<int>(ped, 0x70C) = type;
        return;
    }
}

// The traffic links the intro closes and "Yay Ka-Boom-Boom" reopens.
static const float kBridgeRoads[][6] = {
    { -100.7515f, -927.8298f, 18.0000f, -68.3752f, -891.9871f, 14.0000f },
    { 609.7595f, 327.3437f, 15.8783f, 429.8884f, 616.0168f, 20.2890f },
    { 317.1688f, 707.7672f, 7.0000f, 437.5726f, 709.0657f, 20.5578f },
    { 391.1194f, 640.0150f, 7.0000f, 402.2627f, 664.7980f, 18.5098f },
    { 289.2904f, 636.3991f, 7.8675f, 409.4943f, 702.3849f, 20.0345f },
    { 300.3153f, 718.7909f, 7.7846f, 316.7906f, 781.0926f, 14.0795f },
    { 254.9982f, 837.0290f, 10.1731f, 270.7453f, 929.2404f, 30.2553f },
    { 210.7811f, 939.2068f, 10.9062f, 249.4799f, 959.1111f, 30.2141f },
    { 230.4545f, 946.0961f, 20.6674f, 255.9772f, 969.2755f, 30.4776f },
    { 249.4279f, 899.7975f, 10.5871f, 268.6826f, 933.5995f, 30.3975f },
    { 312.1081f, 694.1089f, 6.0000f, 324.0811f, 733.0005f, 10.0000f },
    { 324.4526f, 804.9198f, 9.6186f, 332.8747f, 814.3560f, 14.3925f },
    { 1690.8192f, 376.5103f, 28.1103f, 1730.2230f, 445.2955f, 30.8414f },
    { 1643.5355f, 227.3723f, 27.4457f, 1673.0623f, 295.5788f, 30.0815f },
    { 1673.7654f, 388.1013f, 40.2331f, 1815.8619f, 804.9291f, 10.0000f },
    { 1705.1558f, 308.3448f, 20.0000f, 1710.9475f, 316.4094f, 23.5612f },
    { -12.7067f, -1522.4554f, 1.0000f, 80.8463f, -1517.1113f, 5.0000f },
    { -16.3392f, -1532.8817f, 0.0394f, 69.3401f, -1523.7710f, 5.9220f },
    { 618.7253f, -1189.6063f, 18.0000f, 623.5441f, -1161.9812f, 22.0000f },
    { -33.4208f, -1341.8403f, 9.0000f, 35.3764f, -1303.9479f, 13.0000f },
    { -41.2393f, -1385.8701f, 8.0000f, -3.5883f, -1368.8558f, 10.5000f },
    { -1690.7048f, 539.6102f, 30.3278f, -1100.5674f, 1140.5695f, 50.7350f },
    { -1799.5405f, 379.7155f, 16.0000f, -1780.1991f, 392.2779f, 18.0000f },
    { -1092.4293f, 1286.5054f, 30.0000f, -1077.0385f, 1319.4948f, 35.0000f },
    { -1860.1334f, 314.7891f, 38.0000f, -1638.5630f, 557.4354f, 40.0000f },
    { -1737.3331f, 455.9431f, 30.3573f, -1710.3633f, 500.6261f, 40.4891f },
    { -1689.2291f, 513.0995f, 30.2597f, -1679.1241f, 524.8383f, 40.2500f },
    { -1742.9060f, 500.7302f, 30.4679f, -1650.3119f, 551.8201f, 40.7455f }
};

// Does what the story does when it opens an area: level 1 is San Fierro, level 2 is Las Venturas too.
void OpenMapTo(int level)
{
    if (level >= 1) Op(0x0777, { S16("BARRIERS1") });      // REMOVE_IPL
    if (level >= 2) {
        Op(0x0777, { S16("BARRIERS2") });
        for (auto& b : kBridgeRoads) Op(0x01E7, { F(b[0]), F(b[1]), F(b[2]), F(b[3]), F(b[4]), F(b[5]) });   // SWITCH_ROADS_ON
    }
}

void CloseMapTo(int level)
{
    if (level < 1) Op(0x0776, { S16("BARRIERS1") });       // REQUEST_IPL
    if (level < 2) {
        Op(0x0776, { S16("BARRIERS2") });
        for (auto& b : kBridgeRoads) Op(0x01E8, { F(b[0]), F(b[1]), F(b[2]), F(b[3]), F(b[4]), F(b[5]) });   // SWITCH_ROADS_OFF
    }
}

bool CanRevealMap() { return g_addr.zonesVisited != 0; }

// area: 0 everything, 1 Los Santos and Red County, 2 San Fierro and its countryside, 3 Las Venturas and the desert.
void RevealMap(int area)
{
    if (!g_addr.zonesVisited) return;
    uint8_t* cells = (uint8_t*)g_addr.zonesVisited;
    int revealed = 0;
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) {                      // each cell is 600 m, y counted from the south edge
            int region = x >= 5 && y <= 5 ? 1 : x <= 4 && y <= 7 ? 2 : 3;
            uint8_t& cell = cells[10 * x + 9 - y];
            if (area == 0 || area == region) cell = 1;
            revealed += cell != 0;
        }
    }
    Mem<int>(g_addr.zonesRevealed) = revealed;
}

void StopDriveBy() { Op(0x0687, { I(PlayerChar()) }); }                 // CLEAR_CHAR_TASKS

// CCam::Process_AimWeapon counts how long the mouse has been idle and then swings a driver's camera
// back to the fixed side view. With limits nothing can reach, it stays on the mouse-aimed path.
void SetFreeDriveBy(bool on)
{
    if (!g_addr.driveByRecentre) return;
    static int original[2];
    static bool saved = false;
    int* limits = (int*)g_addr.driveByRecentre;
    if (!saved) { original[0] = limits[0]; original[1] = limits[1]; saved = true; }
    limits[0] = on ? 0x7FFFFFFF : original[0];
    limits[1] = on ? 0x7FFFFFFF : original[1];
}

void* FireInstantHitAddress() { return (void*)g_addr.fireInstantHit; }
void* AnimGroupsAddress()     { return (void*)g_addr.animGroups; }

// The game lowers the player's accuracy to 95 every frame, and that is what scatters the shots.
void SteadyAim(void* ped) { At<uint8_t>(ped, 0x71A) = 100; }

float* WeaponAccuracy(void* ped, int weapon)
{
    int skill = ((uint8_t(__thiscall*)(void*, int))g_addr.pedWeaponSkill)(ped, weapon);
    void* info = ((void*(__cdecl*)(int, int))g_addr.weaponInfoFor)(weapon, skill);
    return info ? &At<float>(info, 0x38) : nullptr;
}

// Moves a shot's starting point onto the line through the crosshair, level with the gun, so the
// bullet follows that line instead of crossing it from the muzzle.
bool OnCrosshairLine(const CVector& from, CVector& out)
{
    if (!HasCamera()) return false;
    int mode = CameraMode();
    if (mode != 53 && mode != 55 && mode != 65) return false;   // the third-person aiming cameras
    CVector cam, point;
    ((void(__thiscall*)(void*, float, CVector, CVector*, CVector*))g_addr.cameraAimLine)((void*)g_addr.camera, 100.0f, from, &cam, &point);
    float x = point.x - cam.x, y = point.y - cam.y, z = point.z - cam.z;
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 0.01f) return false;
    x /= len; y /= len; z /= len;
    float along = (from.x - cam.x) * x + (from.y - cam.y) * y + (from.z - cam.z) * z;
    out = { cam.x + x * along, cam.y + y * along, cam.z + z * along };
    return true;
}

void* LineClearAddress() { return (void*)g_addr.lineClear; }
void* LineHitAddress()   { return (void*)g_addr.lineHit; }
void* WeaveAngleAddress() { return (void*)g_addr.weaveAngle; }

// The traffic code repeats until two angles stop changing, but tests "not equal" in a way that is
// also true for a NaN, so one bad number hangs the game. These tests now treat a NaN as settled.
bool FixWeaveLoop()
{
    uintptr_t a = g_addr.weaveLoopTest;
    if (!a || !Matches(a, { 0xF6, 0xC4, 0x44, 0x7A, -1 }) || !Matches(a + 0x0E, { 0xF6, 0xC4, 0x44, 0x0F, 0x8B })) return false;
    DWORD old;
    VirtualProtect((void*)a, 0x14, PAGE_EXECUTE_READWRITE, &old);
    Mem<uint8_t>(a + 0x02) = 0x40;                          // test ah, 0x40 (equal, or a NaN)
    Mem<uint8_t>(a + 0x03) = 0x74;                          // je: go round again only when really different
    Mem<uint8_t>(a + 0x10) = 0x40;
    Mem<uint8_t>(a + 0x12) = 0x85;                          // jne: leave when settled
    VirtualProtect((void*)a, 0x14, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)a, 0x14);
    return true;
}

unsigned ClockMs() { return g_addr.clockMs ? Mem<unsigned>(g_addr.clockMs) : 0; }

void* GunMoveAddress() { return (void*)g_addr.gunMove; }

// The gun task builds its movement up by 0.07 per 30 fps frame but cancels anything below 0.1 on the
// same frame, so above 30 fps it never gets going. Starting it past that point removes the dead end.
void StartGunMove(void* task, const float* wanted)
{
    void* ped = PlayerPed();
    if (!ped || task != ((void*(__thiscall*)(void*))g_addr.useGunTask)(At<void*>(ped, 0x47C))) return;
    float& x = At<float>(task, 0x14);
    float& y = At<float>(task, 0x18);
    float have = fabsf(x) + fabsf(y), want = fabsf(wanted[0]) + fabsf(wanted[1]);
    if (have > 0.12f || want < 0.01f) return;
    x = wanted[0] * 0.15f / want;
    y = wanted[1] * 0.15f / want;
}

void*& PedWeaponObject(void* ped) { return At<void*>(ped, 0x4F4); }

bool WantsArmedSprint(void* ped)
{
    if (At<uint32_t>(ped, 0x46C) & (0x100 | 0x4000000)) return false;   // in a vehicle or crouching
    if (!PedWeaponObject(ped)) return false;                            // nothing in hand
    if (void* intelligence = At<void*>(ped, 0x47C)) {
        void* gun = ((void*(__thiscall*)(void*))g_addr.useGunTask)(intelligence);
        if (gun && At<void*>(gun, 0x30)) return false;                   // aiming or firing
    }
    void* pad = ((void*(__thiscall*)(void*))g_addr.pedPad)(ped);
    if (!pad) return false;
    // Go by the movement keys: the game reports a running state even while CJ stands with sprint held.
    if (!((short(__thiscall*)(void*))g_addr.padWalkX)(pad) && !((short(__thiscall*)(void*))g_addr.padWalkY)(pad)) return false;
    return ((uint8_t(__thiscall*)(void*))g_addr.padSprint)(pad) != 0;
}

bool CanPreventBusted() { return g_addr.bustedJump != 0 && Mem<uint8_t>(g_addr.bustedJump) >= 0x75; }

void SetNeverBusted(bool on)
{
    if (!g_addr.bustedJump) return;
    uint8_t want = on ? 0xEB : 0x75;                 // jne -> jmp: the game never sees the arrest
    if (Mem<uint8_t>(g_addr.bustedJump) == want) return;
    DWORD old;
    VirtualProtect((void*)g_addr.bustedJump, 1, PAGE_EXECUTE_READWRITE, &old);
    Mem<uint8_t>(g_addr.bustedJump) = want;
    VirtualProtect((void*)g_addr.bustedJump, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)g_addr.bustedJump, 1);
}

void* ArrestStartAddress() { return (void*)g_addr.arrestStart; }

static void ClearArrestMarks(void* ped)
{
    At<uint32_t>(ped, 0x474) &= ~0x40u;              // bIsBeingArrested: a set flag stops CJ getting up
    if (void* data = At<void*>(ped, 0x480)) At<void*>(data, 8) = nullptr;   // arresting cop
}

void UndoArrest(void* ped, int previousState)
{
    if (PedState(ped) == 63) At<int>(ped, 0x530) = previousState == 63 ? 1 : previousState;
    ClearArrestMarks(ped);
}

void ClearArrest()
{
    void* ped = PlayerPed();
    if (!ped) return;
    // A cop arresting a driver forces the car to a stop, and only the Busted sequence would undo it.
    void* car = PedCar(ped);
    if (car && At<void*>(car, 0x460) == ped && !PedDead(ped) && (At<uint8_t>(car, 0x36) >> 3) == 9)   // STATUS_FORCED_STOP
        At<uint8_t>(car, 0x36) &= 0x07;                                                                // STATUS_PLAYER
    if (PedState(ped) == 63) {                       // PEDSTATE_ARRESTED, reached some other way
        At<int>(ped, 0x530) = 1;                     // PEDSTATE_IDLE
        ClearArrestMarks(ped);
        Op(0x0687, { I(PlayerChar()) });             // CLEAR_CHAR_TASKS
    }
}

void KillPed(void* ped)
{
    PedHealth(ped) = 0.0f;
    Op(0x05BE, { I(PedHandle(ped)) });               // TASK_DIE
}

void ExplodeVehicle(void* veh)
{
    uint8_t* pool = Mem<uint8_t*>(g_addr.vehPool);
    int i = (int)(((uint8_t*)veh - At<uint8_t*>(pool, 0)) / kVehSize);
    Op(0x020B, { I((i << 8) | At<uint8_t*>(pool, 4)[i]) });   // EXPLODE_CAR
}

void ProtectPed(void* ped)
{
    Op(0x02AB, { I(PedHandle(ped)), I(1), I(1), I(1), I(1), I(1) });   // SET_CHAR_PROOFS
    if (PedHealth(ped) < PedMaxHealth(ped)) PedHealth(ped) = PedMaxHealth(ped);
}

// --- nitrous ------------------------------------------------------------------------------

void NitroTick(void* veh, bool infinite, bool noCooldown, bool continuous)
{
    int cls = At<int>(veh, 0x590);                   // m_nVehicleClass: automobile, monster truck, quad
    if (cls < 0 || cls > 2) return;
    uint8_t& boosts = At<uint8_t>(veh, 0x48A);
    float& state = At<float>(veh, 0x8A4);            // 1 ready, <0 burning, 0..1 recharging

    if (continuous && boosts == 0) {                 // needs a bottle fitted before the game will burn anything
        AddPart(1010, -1);
        return;
    }
    if (boosts == 0) return;
    if (infinite || continuous) boosts = 101;        // 101+ means "don't count down"
    if (continuous) {
        if (At<void*>(veh, 0x460) == PlayerPed() && (state >= 0.0f || state < -0.9f)) state = -0.1f;
    } else if (noCooldown && state >= 0.0f && state < 1.0f) {
        state = 1.0f;
    }
}

void NitroRestore(void* veh)
{
    if (At<uint8_t>(veh, 0x48A) >= 101) At<uint8_t>(veh, 0x48A) = 10;
}

// --- missions -----------------------------------------------------------------------------
// A mission lives in its own block of script space. Each one starts with a gosub into its body and
// returns into "if wasted or busted, run the fail routine, then clean up". Its pass routine shows
// the M_PASS text, so pointing the script there runs the game's own rewards and cleanup.

static constexpr int kMainSize = 200000, kMissionSize = 69000;
static uint8_t* MissionBlock() { return (uint8_t*)g_addr.scriptSpace + kMainSize; }

static uint8_t* FindMissionScript()
{
    if (!g_addr.activeScripts) return nullptr;
    uint8_t* block = MissionBlock();
    for (uint8_t* s = Mem<uint8_t*>(g_addr.activeScripts); s; s = At<uint8_t*>(s, 0))
        if (At<uint8_t*>(s, 0x10) == block) return s;
    return nullptr;
}

bool HasMissions() { return g_addr.scriptSpace && g_addr.onMissionVar && g_addr.activeScripts; }

static int& OnMissionFlag() { return Mem<int>(g_addr.scriptSpace + Mem<int>(g_addr.onMissionVar)); }

bool OnMission() { return HasMissions() && OnMissionFlag() != 0; }

std::string MissionName()
{
    uint8_t* s = FindMissionScript();
    return s ? std::string((char*)s + 8, strnlen((char*)s + 8, 8)) : std::string();
}

static int FindRoutine(const char* key)
{
    uint8_t* b = MissionBlock();
    size_t klen = strlen(key);
    for (int q = 0; q + (int)klen < kMissionSize; ++q) {
        if (memcmp(b + q, key, klen) != 0) continue;
        for (int p = q - 1; p > 0 && p > q - 900; --p) {
            int start = -1;
            if (b[p] == 0x51 && b[p + 1] == 0x00) start = p + 2;                      // previous RETURN
            if (b[p] == 0x02 && b[p + 1] == 0x00 && b[p + 2] == 0x01) start = p + 7;  // previous GOTO
            if (start < 0) continue;
            // Some missions only reach their reward if a "you really finished" check passes, and
            // skipping leaves that check false. Start after any check that would jump past the text.
            for (int g = start; g + 7 <= q; ++g) {
                if (b[g] != 0x4D || b[g + 1] != 0x00 || b[g + 2] != 0x01) continue;   // GOTO_IF_FALSE
                int target;
                memcpy(&target, b + g + 3, 4);
                if (-target > q) start = g + 7;
            }
            return start;
        }
        return -1;
    }
    return -1;
}

void RecoverScreen();

static bool JumpMission(const char* key)
{
    uint8_t* s = FindMissionScript();
    if (!s) return false;
    int target = FindRoutine(key);
    uint8_t* b = MissionBlock();
    int ret = -1;
    for (int i = 0; i < 48; ++i)
        if (b[i] == 0x50 && b[i + 1] == 0x00 && b[i + 2] == 0x01) { ret = i + 7; break; }
    if (target < 0 || ret < 0) return false;

    At<uint8_t*>(s, 0x18) = b + ret;      // gosub stack: return straight into the mission's cleanup path
    At<uint16_t>(s, 0x38) = 1;
    At<uint8_t*>(s, 0x14) = b + target;
    At<uint32_t>(s, 0xCC) = 0;            // cancel any WAIT

    RecoverScreen();
    return true;
}

bool MissionPass() { return JumpMission("M_PASS"); }
bool MissionFail() { return JumpMission("M_FAIL"); }

static std::vector<uint8_t> g_savedGlobals;
static DWORD g_replayStart = 0;
static bool g_replaying = false;

bool StartMission(int index, bool preserveStory)
{
    if (!HasMissions() || !InGame() || OnMission()) return false;
    if (preserveStory && g_savedGlobals.empty()) {
        int size = Mem<int>(g_addr.scriptSpace + 3);     // first segment jump = end of global variables
        if (size > 0 && size < kMainSize) g_savedGlobals.assign((uint8_t*)g_addr.scriptSpace, (uint8_t*)g_addr.scriptSpace + size);
    }
    g_replayStart = GetTickCount();
    g_replaying = true;
    OnMissionFlag() = 1;
    Op(0x0417, { I(index) });                            // LOAD_AND_LAUNCH_MISSION_INTERNAL
    Log("Mission %d started (story preserved: %s)", index, preserveStory ? "yes" : "no");
    return true;
}

void RecoverScreen()
{
    Op(0x02EA, {});                       // CLEAR_CUTSCENE (does nothing if none is loaded)
    Op(0x02A3, { I(0) });                 // SWITCH_WIDESCREEN off
    Op(0x016A, { I(500), I(1) });         // DO_FADE in
    Op(0x02EB, {});                       // RESTORE_CAMERA_JUMPCUT
    Op(0x0373, {});                       // SET_CAMERA_BEHIND_PLAYER
    Op(0x01B4, { I(0), I(1) });           // SET_PLAYER_CONTROL on
}

void MissionTick()
{
    if (!g_replaying || GetTickCount() - g_replayStart < 3000) return;
    if (OnMission() || FindMissionScript()) return;
    g_replaying = false;
    if (!g_savedGlobals.empty()) {
        memcpy((void*)g_addr.scriptSpace, g_savedGlobals.data(), g_savedGlobals.size());
        g_savedGlobals.clear();
        Log("Replay finished: story variables restored");
    }
    // Some missions end faded to black because the story script normally carries on from there
    // (Wu Zi Mu is one). A replay has no such script, so hand the screen back here.
    RecoverScreen();
}

bool ReplayActive() { return !g_savedGlobals.empty(); }

void DiscardReplay() { g_savedGlobals.clear(); g_replaying = false; }

} // namespace Game
