#include "config.h"
#include "features.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace {

enum class K { Bool, Int, Float, Str, Color };
struct Entry { const char* name; K kind; void* ptr; int size; };

std::vector<Entry>& Table()
{
    static std::vector<Entry> t;
    if (!t.empty()) return t;
    Toggles& g = g_toggles;
    auto B = [&](const char* n, bool& v)  { t.push_back({ n, K::Bool, &v, 0 }); };
    auto I = [&](const char* n, int& v)   { t.push_back({ n, K::Int, &v, 0 }); };
    auto F = [&](const char* n, float& v) { t.push_back({ n, K::Float, &v, 0 }); };
    auto C = [&](const char* n, float* v) { t.push_back({ n, K::Color, v, 0 }); };

    B("godMode", g.godMode); B("infiniteArmour", g.infiniteArmour); B("infiniteAmmo", g.infiniteAmmo);
    B("neverWanted", g.neverWanted); B("noclip", g.noclip); F("noclipSpeed", g.noclipSpeed);
    B("vehicleGod", g.vehicleGod); B("autoRepair", g.autoRepair); B("freezeTime", g.freezeTime);
    I("frozenHour", g.frozenHour); I("frozenMinute", g.frozenMinute); B("lockWeather", g.lockWeather);
    I("weather", g.weather); B("spawnInside", g.spawnInside); t.push_back({ "plate", K::Str, g.plate, sizeof(g.plate) });
    F("damageMult", g.damageMult); F("gameSpeed", g.gameSpeed); F("gravity", g.gravity); I("ammoMul", g.ammoMul);
    I("menuKey", g.menuKey); I("noclipKey", g.noclipKey); F("textScale", g.textScale); F("opacity", g.opacity);
    I("language", g.language); B("updateNotify", g.updateNotify);

    AimbotCfg& a = g.aim;
    B("aim.enabled", a.enabled); B("aim.advanced", a.advanced); I("aim.mode", a.mode); I("aim.key", a.key);
    F("aim.fov", a.fov); F("aim.smooth", a.smooth); F("aim.maxDist", a.maxDist); I("aim.bone", a.bone);
    I("aim.priority", a.priority); B("aim.cops", a.cops); B("aim.enemies", a.enemies); B("aim.attackers", a.attackers);
    B("aim.missionTargets", a.missionTargets); B("aim.civilians", a.civilians); B("aim.visibleOnly", a.visibleOnly);
    B("aim.sticky", a.sticky); B("aim.prediction", a.prediction); B("aim.ignoreInVehicles", a.ignoreInVehicles);
    B("aim.drawFov", a.drawFov); B("aim.driveBy", a.driveBy);

    EspCfg& e = g.esp;
    B("esp.enabled", e.enabled); B("esp.advanced", e.advanced); B("esp.onlyAimbot", e.onlyAimbot); B("esp.box", e.box);
    B("esp.health", e.health); B("esp.armour", e.armour); B("esp.distance", e.distance); B("esp.label", e.label);
    B("esp.snapline", e.snapline); B("esp.cops", e.cops); B("esp.enemies", e.enemies); B("esp.civilians", e.civilians);
    B("esp.friends", e.friends); F("esp.maxDist", e.maxDist); F("esp.thickness", e.thickness);
    C("esp.colCop", e.colCop); C("esp.colEnemy", e.colEnemy); C("esp.colCiv", e.colCiv); C("esp.colFriend", e.colFriend);

    GameplayCfg& p = g.play;
    B("play.customHealth", p.customHealth); I("play.maxHealth", p.maxHealth); B("play.customArmour", p.customArmour);
    I("play.maxArmour", p.maxArmour); B("play.customAmmo", p.customAmmo); I("play.reserveAmmo", p.reserveAmmo);
    F("play.clipMult", p.clipMult); B("play.healthRegen", p.healthRegen); F("play.healthAmount", p.healthAmount);
    F("play.healthEvery", p.healthEvery); F("play.healthDelay", p.healthDelay); B("play.armourRegen", p.armourRegen);
    F("play.armourAmount", p.armourAmount); F("play.armourEvery", p.armourEvery); F("play.armourDelay", p.armourDelay);
    B("play.ammoRegen", p.ammoRegen); I("play.ammoAmount", p.ammoAmount); F("play.ammoEvery", p.ammoEvery);
    B("play.lifesteal", p.lifesteal); F("play.healthPerKill", p.healthPerKill); F("play.armourPerKill", p.armourPerKill);
    I("play.moneyPerKill", p.moneyPerKill); I("play.damageTaken", p.damageTaken); B("play.explodingBullets", p.explodingBullets);
    I("play.explosionType", p.explosionType); B("play.noSpread", p.noSpread); B("play.moveWhileAiming", p.moveWhileAiming); B("play.armedSprint", p.armedSprint); B("play.freeDriveBy", p.freeDriveBy); B("play.anyVehicleWeapon", p.anyVehicleWeapon); F("play.driveBySens", p.driveBySens); B("play.neverBusted", p.neverBusted); B("play.noFallOffBike", p.noFallOffBike);
    B("play.unlimitedStamina", p.unlimitedStamina); B("play.infiniteBreath", p.infiniteBreath); B("play.fastReload", p.fastReload);
    B("play.slowmoAiming", p.slowmoAiming); F("play.slowmoScale", p.slowmoScale);

    B("nitro.infinite", g.nitro.infinite); B("nitro.noCooldown", g.nitro.noCooldown); B("nitro.continuous", g.nitro.continuous);
    B("mission.preserveStory", g.mission.preserveStory); B("mission.protectAllies", g.mission.protectAllies);
    B("gps.enabled", g.gps.enabled); B("gps.toObjective", g.gps.toObjective); B("gps.onFoot", g.gps.onFoot); C("gps.colWaypoint", g.gps.colWaypoint); C("gps.colMission", g.gps.colMission); F("gps.width", g.gps.width);
    B("wheel.enabled", g.wheel.enabled); B("wheel.slowmo", g.wheel.slowmo); B("wheel.inVehicles", g.wheel.inVehicles); I("wheel.key", g.wheel.key); F("wheel.scale", g.wheel.scale);
    return t;
}

bool g_multi = false;
bool g_autoSave = false;
std::string g_autoload, g_current;

std::string ConfigsDir() { return Config::Dir() + "configs\\"; }
std::string PathOf(const std::string& name) { return ConfigsDir() + name + ".ini"; }

bool WriteFile(const std::string& path)
{
    std::ofstream f(path, std::ios::trunc);
    if (!f) { Log("Config: cannot write %s", path.c_str()); return false; }
    f << "# RiffedMenu settings\n";
    for (const Entry& e : Table()) {
        f << e.name << '=';
        switch (e.kind) {
        case K::Bool:  f << (*(bool*)e.ptr ? 1 : 0); break;
        case K::Int:   f << *(int*)e.ptr; break;
        case K::Float: f << *(float*)e.ptr; break;
        case K::Str:   f << (char*)e.ptr; break;
        case K::Color: { float* c = (float*)e.ptr; f << c[0] << ' ' << c[1] << ' ' << c[2] << ' ' << c[3]; break; }
        }
        f << '\n';
    }
    f << "favorites=";
    for (size_t i = 0; i < g_toggles.favorites.size(); ++i) f << (i ? "," : "") << g_toggles.favorites[i];
    f << '\n';
    return true;
}

bool ReadFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::map<std::string, std::string> kv;
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    for (const Entry& e : Table()) {
        auto it = kv.find(e.name);
        if (it == kv.end()) continue;           // keep the current value for options added later
        const char* v = it->second.c_str();
        switch (e.kind) {
        case K::Bool:  *(bool*)e.ptr = atoi(v) != 0; break;
        case K::Int:   *(int*)e.ptr = atoi(v); break;
        case K::Float: *(float*)e.ptr = (float)atof(v); break;
        case K::Str:   strncpy((char*)e.ptr, v, e.size - 1); ((char*)e.ptr)[e.size - 1] = 0; break;
        case K::Color: { float* c = (float*)e.ptr; sscanf(v, "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]); break; }
        }
    }
    if (auto it = kv.find("favorites"); it != kv.end()) {
        g_toggles.favorites.clear();
        std::stringstream ss(it->second);
        for (std::string id; std::getline(ss, id, ',');) if (!id.empty()) g_toggles.favorites.push_back(atoi(id.c_str()));
    }
    return true;
}

void WriteMeta()
{
    std::ofstream f(Config::Dir() + "riffedmenu.meta", std::ios::trunc);
    f << "multi=" << (g_multi ? 1 : 0) << "\nautosave=" << (g_autoSave ? 1 : 0) << "\nautoload=" << g_autoload << "\n";
}

void LoadMeta()
{
    std::ifstream f(Config::Dir() + "riffedmenu.meta");
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("multi=", 0) == 0) g_multi = line[6] == '1';
        else if (line.rfind("autosave=", 0) == 0) g_autoSave = line[9] == '1';
        else if (line.rfind("autoload=", 0) == 0) g_autoload = line.substr(9);
    }
}

} // namespace

namespace Config {

std::string Dir()
{
    static std::string d;
    if (d.empty()) {
        d = GameDir() + "RiffedMenu\\";
        CreateDirectoryA(d.c_str(), nullptr);
        CreateDirectoryA((d + "configs\\").c_str(), nullptr);
    }
    return d;
}

std::string Sanitize(const std::string& name)
{
    std::string out;
    for (char c : name) if (!strchr("\\/:*?\"<>|.", c) && (unsigned char)c >= 32) out += c;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.substr(0, 40);
}

void Init()
{
    LoadMeta();
    ReadFile(Dir() + "settings.ini");           // the base settings always load first
    if (g_multi && !g_autoload.empty()) {
        if (Load(g_autoload)) Log("Config: autoloaded '%s'", g_autoload.c_str());
        else { Log("Config: autoload '%s' is missing", g_autoload.c_str()); g_autoload.clear(); WriteMeta(); }
    }
}

void SaveDefault() { WriteFile(Dir() + "settings.ini"); WriteMeta(); }
void SaveMeta() { WriteMeta(); }

void AutoSave()
{
    if (!g_autoSave) return;
    if (g_multi && !g_current.empty()) Save(g_current);     // keep editing the config that's loaded
    else SaveDefault();
}

bool& AutoSaveEnabled() { return g_autoSave; }

void ResetOptions()
{
    Toggles fresh;
    fresh.language = g_toggles.language;
    fresh.favorites = g_toggles.favorites;
    g_toggles = fresh;
    g_current.clear();      // no longer that config, so autosave can't write these defaults into it
}

void FactoryReset()
{
    for (const std::string& n : List()) DeleteFileA(PathOf(n).c_str());
    DeleteFileA((Dir() + "settings.ini").c_str());
    DeleteFileA((Dir() + "riffedmenu.meta").c_str());
    g_multi = g_autoSave = false;
    g_autoload.clear();
    g_current.clear();
    g_toggles = Toggles();
}

bool& MultiEnabled() { return g_multi; }
const std::string& Autoload() { return g_autoload; }
const std::string& Current() { return g_current; }
void SetAutoload(const std::string& name) { g_autoload = name; WriteMeta(); }

std::vector<std::string> List()
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((ConfigsDir() + "*.ini").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string n = fd.cFileName;
        out.push_back(n.substr(0, n.size() - 4));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

bool Save(const std::string& name)
{
    if (name.empty() || !WriteFile(PathOf(name))) return false;
    g_current = name;
    return true;
}

bool Load(const std::string& name)
{
    if (name.empty() || !ReadFile(PathOf(name))) return false;
    g_current = name;
    return true;
}

bool Delete(const std::string& name)
{
    if (!DeleteFileA(PathOf(name).c_str())) return false;
    if (g_autoload == name) SetAutoload("");
    if (g_current == name) g_current.clear();
    return true;
}

} // namespace Config
