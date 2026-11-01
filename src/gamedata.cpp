#include "gamedata.h"
#include "log.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace {

bool g_loaded = false;
std::vector<RGB> g_colours;
std::map<std::string, int> g_vehicleIds;          // lower-case model name -> id
std::map<std::string, int> g_partIds;             // lower-case part name -> id
std::map<int, std::string> g_partNames;
std::map<int, std::vector<int>> g_partsByVehicle; // vehicle id -> part ids
std::map<int, int> g_links;
std::vector<int> g_wheels;
std::vector<ClothingItem> g_clothes;
std::vector<PedSkin> g_skins;

std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Splits a data line into tokens: '#' starts a comment, commas/tabs/spaces separate.
std::vector<std::string> Tokens(std::string line)
{
    if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
    for (char& c : line) if (c == ',' || c == '\t' || c == '\r') c = ' ';
    std::vector<std::string> out;
    std::istringstream ss(line);
    for (std::string t; ss >> t;) out.push_back(t);
    return out;
}

bool IsNumber(const std::string& s) { return !s.empty() && (isdigit((unsigned char)s[0]) || s[0] == '-'); }

// Calls fn(tokens) for every non-empty line, tracking "section"/"end" style blocks via `block`.
template <typename Fn>
bool ReadFile(const char* rel, Fn fn)
{
    std::ifstream f(GameDir() + rel);
    if (!f) { Log("GameData: cannot open %s", rel); return false; }
    for (std::string line; std::getline(f, line);) {
        auto t = Tokens(line);
        if (!t.empty()) fn(t);
    }
    return true;
}

std::string PartCategory(const std::string& n)
{
    struct { const char* prefix; const char* cat; } map[] = {
        { "spl_", "Spoiler" }, { "exh_", "Exhaust" }, { "fbmp_", "Front bumper" }, { "rbmp_", "Rear bumper" },
        { "rf_", "Roof" }, { "wg_", "Side skirts" }, { "bntl_", "Hood vents" }, { "bntr_", "Hood vents" },
        { "bnt_", "Hood" }, { "lgt_", "Lamps" }, { "bbb_", "Bull bar" }, { "fbb_", "Bull bar" },
        { "nto_", "Nitro" }, { "wheel_", "Wheels" }, { "hydralics", "Hydraulics" }, { "stereo", "Stereo" },
        { "misc_", "Misc" },
    };
    for (auto& m : map) if (n.rfind(m.prefix, 0) == 0) return m.cat;
    return "Other";
}

void LoadColours()
{
    bool in = false;
    ReadFile("data\\carcols.dat", [&](const std::vector<std::string>& t) {
        if (t[0] == "col") { in = true; return; }
        if (t[0] == "end") { in = false; return; }
        if (in && t.size() >= 3 && IsNumber(t[0]))
            g_colours.push_back({ (uint8_t)atoi(t[0].c_str()), (uint8_t)atoi(t[1].c_str()), (uint8_t)atoi(t[2].c_str()) });
    });
}

void LoadIds()
{
    ReadFile("data\\vehicles.ide", [&](const std::vector<std::string>& t) {
        if (t.size() >= 2 && IsNumber(t[0])) g_vehicleIds[Lower(t[1])] = atoi(t[0].c_str());
    });
    ReadFile("data\\maps\\veh_mods\\veh_mods.ide", [&](const std::vector<std::string>& t) {
        if (t.size() >= 2 && IsNumber(t[0])) {
            int id = atoi(t[0].c_str());
            g_partIds[Lower(t[1])] = id;
            g_partNames[id] = Lower(t[1]);
        }
    });
}

void LoadCarMods()
{
    std::string section;
    ReadFile("data\\carmods.dat", [&](const std::vector<std::string>& t) {
        if (t.size() == 1 && (t[0] == "link" || t[0] == "mods" || t[0] == "wheel")) { section = t[0]; return; }
        if (t[0] == "end") { section.clear(); return; }
        auto part = [&](const std::string& n) { auto it = g_partIds.find(Lower(n)); return it == g_partIds.end() ? -1 : it->second; };
        if (section == "link" && t.size() >= 2) {
            int a = part(t[0]), b = part(t[1]);
            if (a >= 0 && b >= 0) { g_links[a] = b; g_links[b] = a; }
        } else if (section == "mods" && t.size() >= 2) {
            auto v = g_vehicleIds.find(Lower(t[0]));
            if (v == g_vehicleIds.end()) return;
            for (size_t i = 1; i < t.size(); ++i) if (int p = part(t[i]); p >= 0) g_partsByVehicle[v->second].push_back(p);
        } else if (section == "wheel") {
            for (size_t i = 1; i < t.size(); ++i)
                if (int p = part(t[i]); p >= 0 && std::find(g_wheels.begin(), g_wheels.end(), p) == g_wheels.end()) g_wheels.push_back(p);
        }
    });
    std::sort(g_wheels.begin(), g_wheels.end());
}

void LoadClothes()
{
    // Only the "prices" block has the item rows; the shop blocks just list names again.
    std::string sub;
    bool prices = false;
    ReadFile("data\\shopping.dat", [&](const std::vector<std::string>& t) {
        if (t[0] == "section") {
            if (t.size() > 1 && t[1] == "prices") prices = true;
            else if (t.size() > 1 && t[1] == "shops") prices = false;
            else if (prices && t.size() > 1) sub = t[1];
            return;
        }
        if (t[0] == "end") { sub.clear(); return; }
        if (!prices) return;
        if ((sub == "Clothes" || sub == "Haircuts") && t.size() >= 4 && IsNumber(t[3])) {
            g_clothes.push_back({ t[0], t[2], atoi(t[3].c_str()) });
        } else if (sub == "Tattoos" && t.size() >= 3 && IsNumber(t[2])) {
            g_clothes.push_back({ t[0], "", atoi(t[2].c_str()) });
        }
    });
}

void LoadSkins()
{
    bool in = false;
    ReadFile("data\\peds.ide", [&](const std::vector<std::string>& t) {
        if (t[0] == "peds") { in = true; return; }
        if (t[0] == "end") { in = false; return; }
        if (in && t.size() >= 2 && IsNumber(t[0]) && atoi(t[0].c_str()) > 0) g_skins.push_back({ atoi(t[0].c_str()), t[1] });
    });
}

} // namespace

namespace GameData {

bool Load()
{
    if (g_loaded) return true;
    g_loaded = true;
    LoadColours();
    LoadIds();
    LoadCarMods();
    LoadClothes();
    LoadSkins();
    Log("GameData: %d colours, %d vehicles, %d parts (%d tunable cars, %d wheels, %d links), %d clothing items, %d skins",
        (int)g_colours.size(), (int)g_vehicleIds.size(), (int)g_partIds.size(), (int)g_partsByVehicle.size(),
        (int)g_wheels.size(), (int)g_links.size() / 2, (int)g_clothes.size(), (int)g_skins.size());
    return true;
}

const std::vector<RGB>& CarColours()            { Load(); return g_colours; }
const std::vector<ClothingItem>& Clothes()      { Load(); return g_clothes; }
const std::vector<PedSkin>& Skins()             { Load(); return g_skins; }
int LinkedPart(int id)                          { Load(); auto it = g_links.find(id); return it == g_links.end() ? -1 : it->second; }

std::vector<TuningPart> PartsFor(int vehicleModel)
{
    Load();
    std::vector<TuningPart> out;
    auto it = g_partsByVehicle.find(vehicleModel);
    if (it == g_partsByVehicle.end()) return out;     // not a tunable car (bikes, boats, aircraft, ...)

    std::set<int> ids(it->second.begin(), it->second.end());
    for (int w : g_wheels) ids.insert(w);             // any tunable car takes any wheel
    for (const char* n : { "hydralics", "stereo" }) if (auto p = g_partIds.find(n); p != g_partIds.end()) ids.insert(p->second);
    for (int id : ids) {
        // Right-hand side skirts are fitted automatically with the left ones.
        const std::string& n = g_partNames[id];
        if (n.rfind("wg_r_", 0) == 0 && g_links.count(id)) continue;
        out.push_back({ id, n, PartCategory(n) });
    }
    std::stable_sort(out.begin(), out.end(), [](const TuningPart& a, const TuningPart& b) { return a.category < b.category; });
    return out;
}

const char* BodyPartName(int part)
{
    switch (part) {
    case 0:  return "Torso";
    case 1:  return "Hair";
    case 2:  return "Legs";
    case 3:  return "Shoes";
    case 4:  return "Tattoo: left upper arm";
    case 5:  return "Tattoo: left lower arm";
    case 6:  return "Tattoo: right upper arm";
    case 7:  return "Tattoo: right lower arm";
    case 8:  return "Tattoo: back";
    case 9:  return "Tattoo: left chest";
    case 10: return "Tattoo: right chest";
    case 11: return "Tattoo: stomach";
    case 12: return "Tattoo: lower back";
    case 13: return "Necklace";
    case 14: return "Watch";
    case 15: return "Glasses";
    case 16: return "Hat";
    case 17: return "Special";
    default: return "Other";
    }
}

} // namespace GameData
