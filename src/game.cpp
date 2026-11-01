#include "game.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <initializer_list>

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
};

static const Addresses kV10US     = { 0x469EB0, 0xB7CD98, 0xBA86F0, 0x863984, 0xC97C28, 0xA9B0C8, 0xC8AAB8 };
// Found by disassembly of gta-sa.exe build 459558 (PE timestamp 0x547CD3A3):
//   0x46EDD0 reads opcode at [ecx+0x14], sets NOT flag [ecx+0xD2], dispatches via table 0x914BB0
//   0xC100D0 is the base used by 139 sites of `imul r, r, 0x190`
//   0xC36858 = 0xC3687D - 0x25 from CRadar's free-slot scan (bInUse bit, 0x28 stride, 0x1B58 end)
//   0xD23768 is loaded 371 times and called through vtable slots 2 (Release) / 40 (GetDepthStencilSurface)
//   0xB12818 is indexed by model id in the GIVE_VEHICLE_PAINTJOB / GET_AVAILABLE_VEHICLE_MOD handlers
//   0xD164E8 is where the weapon.dat loader (0x5DBD10) writes entries: imul esi,esi,0x70; [esi+0xD164E8]
static const Addresses kSteam2014 = { 0x46EDD0, 0xC100D0, 0xC36858, 0, 0xD23768, 0xB12818, 0xD164E8 };

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
    Op(0x00A5, { I(model), F(x), F(y), F(z), V(6) });                // CREATE_CAR -> var6
    int car = VarI(6);
    Op(0x0175, { I(car), F(heading) });                              // SET_CAR_HEADING
    Op(0x020A, { I(car), I(1) });                                    // LOCK_CAR_DOORS 1 = unlocked (tank, some planes spawn locked)
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

void GiveClothes(const char* texture, const char* model, int part)
{
    if (!InGame()) return;
    Op(0x087B, { I(0), S16(texture), S16(model), I(part) });        // GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP
    BuildPlayer();
}

void RemoveClothes(int part)
{
    // An empty name hashes to 0xFFFFFFFF, so clearing a slot means zeroing CPedClothesDesc keys directly.
    // Texture part -> model slot, decoded from the game's CClothes::GetTextureDependency table.
    static const int kModelSlot[18] = { 0, 1, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, 6, 7, 8, 9 };
    if (!InGame() || part < 0 || part >= 18) return;
    void* desc = Mem<void*>(g_addr.players + 0x08);                 // CPlayerInfo::m_PlayerData.m_pPedClothesDesc
    if (!desc) return;
    At<uint32_t>(desc, 0x28 + part * 4) = 0;
    if (kModelSlot[part] >= 0) At<uint32_t>(desc, kModelSlot[part] * 4) = 0;
    BuildPlayer();
}

void SaveOutfit()    { Op(0x0793, {}); }                              // STORE_CLOTHES_STATE
void RestoreOutfit() { Op(0x0794, {}); BuildPlayer(); }               // RESTORE_CLOTHES_STATE

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

} // namespace Game
