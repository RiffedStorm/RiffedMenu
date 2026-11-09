#include "gps.h"
#include "features.h"
#include "game.h"
#include "gamedata.h"
#include "log.h"
#include "imgui.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <vector>

// Own route finder over the game's road network (nodes0..63.dat inside gta3.img). Nothing here
// depends on the exe build, and searching on a worker thread keeps the game smooth.
namespace {

struct Node { float x, y, z; uint32_t firstLink; uint8_t linkCount; bool usable; };
std::vector<Node> g_nodes;
std::vector<uint32_t> g_links;
std::vector<std::vector<uint32_t>> g_grid;      // 100 m cells over the 6 km map
const int kGrid = 60;
volatile LONG g_loaded = 0;

int Cell(float v) { return std::clamp((int)((v + 3000.0f) / 100.0f), 0, kGrid - 1); }

template <typename T> T Rd(const std::string& b, size_t o) { T v; memcpy(&v, b.data() + o, sizeof(T)); return v; }

void LoadGraph()
{
    uint32_t base[64] = {}, count[64] = {};
    std::vector<std::string> files(64);
    uint32_t total = 0;
    for (int a = 0; a < 64; ++a) {
        files[a] = GameData::ImgRead("nodes" + std::to_string(a) + ".dat");
        base[a] = total;
        if (files[a].size() >= 20) { count[a] = Rd<uint32_t>(files[a], 0); total += count[a]; }
    }
    g_nodes.resize(total);
    g_grid.assign(kGrid * kGrid, {});
    for (int a = 0; a < 64; ++a) {
        const std::string& f = files[a];
        if (f.size() < 20) continue;
        uint32_t nodes = Rd<uint32_t>(f, 0), vehicle = Rd<uint32_t>(f, 4), navi = Rd<uint32_t>(f, 12);
        size_t linkTable = 20 + nodes * 28 + navi * 14;
        for (uint32_t i = 0; i < nodes; ++i) {
            size_t o = 20 + i * 28;
            uint16_t linkId = Rd<uint16_t>(f, o + 16);
            uint32_t flags = Rd<uint32_t>(f, o + 24);
            Node& n = g_nodes[base[a] + i];
            n.x = Rd<int16_t>(f, o + 8) / 8.0f;
            n.y = Rd<int16_t>(f, o + 10) / 8.0f;
            n.z = Rd<int16_t>(f, o + 12) / 8.0f;
            n.linkCount = flags & 0x0F;
            n.firstLink = (uint32_t)g_links.size();
            n.usable = i < vehicle && !(flags & 0x80) && !(flags & 0x100);   // roads only: no boats, no emergency lanes
            for (int l = 0; l < n.linkCount; ++l) {
                size_t lo = linkTable + (size_t)(linkId + l) * 4;
                if (lo + 4 > f.size()) { g_links.push_back(0xFFFFFFFF); continue; }
                uint16_t ta = Rd<uint16_t>(f, lo), tn = Rd<uint16_t>(f, lo + 2);
                g_links.push_back(ta < 64 && tn < count[ta] ? base[ta] + tn : 0xFFFFFFFF);
            }
        }
    }

    // The road data has small islands no road connects to the rest. A marker that snapped to one
    // could never be routed to, so only the largest connected network can be picked.
    std::vector<uint32_t> parent(g_nodes.size());
    for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto find = [&](uint32_t x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    for (uint32_t i = 0; i < g_nodes.size(); ++i) {
        const Node& n = g_nodes[i];
        if (!n.usable) continue;
        for (int l = 0; l < n.linkCount; ++l) {
            uint32_t v = g_links[n.firstLink + l];
            if (v != 0xFFFFFFFF && g_nodes[v].usable) parent[find(i)] = find(v);
        }
    }
    std::vector<uint32_t> size(g_nodes.size(), 0);
    uint32_t biggest = 0, best = 0;
    for (uint32_t i = 0; i < g_nodes.size(); ++i)
        if (g_nodes[i].usable) { uint32_t r = find(i); if (++size[r] > best) { best = size[r]; biggest = r; } }
    for (uint32_t i = 0; i < g_nodes.size(); ++i)
        if (g_nodes[i].usable && find(i) == biggest) g_grid[Cell(g_nodes[i].y) * kGrid + Cell(g_nodes[i].x)].push_back(i);
    Log("GPS: %d road nodes, %d links, main network %d", (int)g_nodes.size(), (int)g_links.size(), (int)best);
    InterlockedExchange(&g_loaded, 1);
}

// useZ picks the road on the right level, so a bridge overhead doesn't count as "closest".
int Nearest(float x, float y, float z, bool useZ)
{
    int cx = Cell(x), cy = Cell(y), best = -1;
    float bestD = 1e12f;
    for (int r = 0; r < kGrid; ++r) {
        for (int j = std::max(0, cy - r); j <= std::min(kGrid - 1, cy + r); ++j)
            for (int i = std::max(0, cx - r); i <= std::min(kGrid - 1, cx + r); ++i) {
                if (std::max(abs(i - cx), abs(j - cy)) != r) continue;
                for (uint32_t id : g_grid[j * kGrid + i]) {
                    const Node& n = g_nodes[id];
                    float dz = useZ ? (n.z - z) * 3.0f : 0.0f;
                    float d = (n.x - x) * (n.x - x) + (n.y - y) * (n.y - y) + dz * dz;
                    if (d < bestD) { bestD = d; best = (int)id; }
                }
            }
        if (best >= 0 && r >= 2) break;
    }
    return best;
}

struct Pt { float x, y, z; };

bool FindRoute(float sx, float sy, float sz, float dx, float dy, float dz, bool destHasZ, std::vector<Pt>& out)
{
    int s = Nearest(sx, sy, sz, true), d = Nearest(dx, dy, dz, destHasZ);
    if (s < 0 || d < 0) return false;
    static std::vector<float> cost;
    static std::vector<int> prev;
    static std::vector<uint32_t> stamp;
    static uint32_t run = 0;
    if (cost.size() != g_nodes.size()) { cost.resize(g_nodes.size()); prev.resize(g_nodes.size()); stamp.assign(g_nodes.size(), 0); }
    ++run;
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    const Node& goal = g_nodes[d];
    auto h = [&](const Node& n) { return hypotf(n.x - goal.x, n.y - goal.y); };
    cost[s] = 0; prev[s] = -1; stamp[s] = run;
    open.push({ h(g_nodes[s]), s });
    bool found = false;
    while (!open.empty()) {
        auto [f, u] = open.top(); open.pop();
        if (u == d) { found = true; break; }
        const Node& n = g_nodes[u];
        if (f > cost[u] + h(n) + 0.01f) continue;
        for (int l = 0; l < n.linkCount; ++l) {
            uint32_t v = g_links[n.firstLink + l];
            if (v == 0xFFFFFFFF || !g_nodes[v].usable) continue;
            const Node& m = g_nodes[v];
            float c = cost[u] + sqrtf((m.x - n.x) * (m.x - n.x) + (m.y - n.y) * (m.y - n.y) + (m.z - n.z) * (m.z - n.z));
            if (stamp[v] != run || c < cost[v]) {
                stamp[v] = run; cost[v] = c; prev[v] = u;
                open.push({ c + h(m), (int)v });
            }
        }
    }
    if (!found) return false;
    std::vector<Pt> pts;
    pts.push_back({ dx, dy, destHasZ ? dz : g_nodes[d].z });
    for (int at = d; at >= 0; at = prev[at]) pts.push_back({ g_nodes[at].x, g_nodes[at].y, g_nodes[at].z });
    std::reverse(pts.begin(), pts.end());
    out.swap(pts);
    return true;
}

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE  g_wake = nullptr;
struct Req { float sx, sy, sz, dx, dy, dz; bool destHasZ; };
Req  g_req;
bool g_pending = false;
std::vector<Pt> g_route;
bool g_have = false;

DWORD WINAPI Worker(LPVOID)
{
    LoadGraph();
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        AcquireSRWLockExclusive(&g_lock);
        Req r = g_req;
        bool go = g_pending;
        g_pending = false;
        ReleaseSRWLockExclusive(&g_lock);
        if (!go) continue;
        std::vector<Pt> pts;
        bool ok = FindRoute(r.sx, r.sy, r.sz, r.dx, r.dy, r.dz, r.destHasZ, pts);
        AcquireSRWLockExclusive(&g_lock);
        if (ok) { g_route.swap(pts); g_have = true; } else { g_route.clear(); g_have = false; }
        ReleaseSRWLockExclusive(&g_lock);
    }
}

void Start()
{
    static bool started = false;
    if (started) return;
    started = true;
    g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
}

// The marker the mission wants you at: yellow ones first, nearest wins. Blips hidden from the
// radar and short-range map icons don't count.
bool ObjectivePos(CVector& out)
{
    std::vector<Blip> blips;
    Game::GetBlips(blips);
    CVector me = Game::PlayerPosition();
    int bestRank = 99;
    float bestDist = 1e12f;
    bool found = false;
    for (const Blip& b : blips) {
        if (b.sprite != 0 || b.display == 0 || b.shortRange) continue;
        CVector p = b.pos;
        if (b.type == 2) { void* e = Game::PedFromHandle(b.handle); if (!e) continue; p = Game::EntityPos(e); }
        else if (b.type == 1) { void* e = Game::VehicleFromHandle(b.handle); if (!e) continue; p = Game::EntityPos(e); }
        else if (b.type != 4 && b.type != 5) continue;
        int rank = b.colour == 4 ? 0 : 1;
        float d = hypotf(p.x - me.x, p.y - me.y);
        if (rank < bestRank || (rank == bestRank && d < bestDist)) { bestRank = rank; bestDist = d; out = p; found = true; }
    }
    return found;
}

bool g_toMission = false;

bool Destination(CVector& out)
{
    g_toMission = false;
    if (Game::WaypointPosition(out)) return true;
    if (!g_toggles.gps.toObjective || !Game::OnMission() || !ObjectivePos(out)) return false;
    g_toMission = true;
    return true;
}

bool g_active = false;
const char* g_status = "Off";

void SetStatus(const char* s)
{
    if (s != g_status) { g_status = s; Log("GPS status: %s", s); }
}

float DistToRoute(const CVector& me)
{
    float best = 1e9f;
    AcquireSRWLockShared(&g_lock);
    for (const Pt& p : g_route) best = std::min(best, hypotf(hypotf(p.x - me.x, p.y - me.y), (p.z - me.z) * 2.0f));
    ReleaseSRWLockShared(&g_lock);
    return best;
}

// Clips a segment to the unit circle in radar space. False if nothing is left.
bool ClipCircle(float& ax, float& ay, float& bx, float& by)
{
    float dx = bx - ax, dy = by - ay;
    float A = dx * dx + dy * dy, B = 2 * (ax * dx + ay * dy), C = ax * ax + ay * ay - 1.0f;
    if (A < 1e-9f) return C <= 0;
    float disc = B * B - 4 * A * C;
    if (disc <= 0) return false;
    float s = sqrtf(disc), t0 = (-B - s) / (2 * A), t1 = (-B + s) / (2 * A);
    t0 = std::max(t0, 0.0f); t1 = std::min(t1, 1.0f);
    if (t0 >= t1) return false;
    float ox = ax, oy = ay;
    ax = ox + dx * t0; ay = oy + dy * t0;
    bx = ox + dx * t1; by = oy + dy * t1;
    return true;
}

} // namespace

namespace GPS {

bool Objective(CVector& out) { return ObjectivePos(out); }
int NodeCount() { return g_loaded ? (int)g_nodes.size() : 0; }
int RoutePoints() { AcquireSRWLockShared(&g_lock); int n = (int)g_route.size(); ReleaseSRWLockShared(&g_lock); return n; }

const char* Status() { return g_status; }

void Tick()
{
    g_active = false;
    if (!g_toggles.gps.enabled || !Game::HasRadar()) { SetStatus("Off"); return; }
    Start();
    if (!g_loaded) { SetStatus("Loading road data"); return; }
    if (Game::ActiveInterior() != 0) { SetStatus("Not available indoors"); return; }
    if (!g_toggles.gps.onFoot && !Game::PlayerVehicle()) { SetStatus("Get in a vehicle"); return; }

    static DWORD last = 0;
    if (GetTickCount() - last < 500) { g_active = g_have; return; }
    last = GetTickCount();

    CVector dest;
    if (!Destination(dest)) {
        AcquireSRWLockExclusive(&g_lock); g_route.clear(); g_have = false; ReleaseSRWLockExclusive(&g_lock);
        SetStatus("No marker on the map");
        return;
    }
    g_active = true;
    CVector me = Game::PlayerPosition();
    static float lastDx = 1e9f, lastDy = 1e9f;
    bool moved = hypotf(dest.x - lastDx, dest.y - lastDy) > 8.0f;
    bool lost = !g_have || DistToRoute(me) > 45.0f;
    SetStatus(g_have ? "Route shown" : "Finding a route");
    if (moved || lost) {
        lastDx = dest.x; lastDy = dest.y;
        AcquireSRWLockExclusive(&g_lock);
        g_req = { me.x, me.y, me.z, dest.x, dest.y, dest.z, g_toMission && dest.z > -50.0f && fabsf(dest.z) > 0.01f };
        g_pending = true;
        ReleaseSRWLockExclusive(&g_lock);
        SetEvent(g_wake);
    }
}

void Render()
{
    if (!g_active || !Game::InGame() || Game::PauseMenuOpen() || Game::WidescreenOn() || !Game::HasRadar()) return;
    std::vector<Pt> route;
    AcquireSRWLockShared(&g_lock);
    route = g_route;
    ReleaseSRWLockShared(&g_lock);
    if (route.size() < 2) return;

    CVector me = Game::PlayerPosition();
    size_t start = 0;
    float best = 1e9f;
    for (size_t i = 0; i < route.size(); ++i) {
        float d = hypotf(hypotf(route[i].x - me.x, route[i].y - me.y), (route[i].z - me.z) * 2.0f);
        if (d < best) { best = d; start = i; }
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float scale = ImGui::GetFontSize() / 17.0f;
    const float* c = g_toMission ? g_toggles.gps.colMission : g_toggles.gps.colWaypoint;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    float rx0, ry0;
    Game::WorldToRadar(me.x, me.y, rx0, ry0);
    for (size_t i = start; i < route.size(); ++i) {
        float rx1, ry1;
        Game::WorldToRadar(route[i].x, route[i].y, rx1, ry1);
        float ax = rx0, ay = ry0, bx = rx1, by = ry1;
        if (ClipCircle(ax, ay, bx, by)) {
            float s0x, s0y, s1x, s1y;
            Game::RadarToScreen(ax, ay, s0x, s0y);
            Game::RadarToScreen(bx, by, s1x, s1y);
            dl->AddLine(ImVec2(s0x, s0y), ImVec2(s1x, s1y), col, g_toggles.gps.width * scale);
        }
        rx0 = rx1; ry0 = ry1;
    }
}

} // namespace GPS
