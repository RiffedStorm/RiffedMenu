#include "theme.h"
#include "features.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>

// Inter and Barlow Condensed (SIL Open Font License 1.1, see assets/fonts) are built into the .asi,
// so Polish letters work on every PC with no font files to install.
__asm__(".section .rdata,\"dr\"\n"
        "_riffed_inter_start:\n.incbin \"Inter.ttf\"\n_riffed_inter_end:\n"
        "_riffed_barlow_m_start:\n.incbin \"BarlowCondensed-Medium.ttf\"\n_riffed_barlow_m_end:\n"
        "_riffed_barlow_sb_start:\n.incbin \"BarlowCondensed-SemiBold.ttf\"\n_riffed_barlow_sb_end:\n"
        ".text\n");
extern "C" const unsigned char riffed_inter_start[]     __asm__("_riffed_inter_start");
extern "C" const unsigned char riffed_inter_end[]       __asm__("_riffed_inter_end");
extern "C" const unsigned char riffed_barlow_m_start[]  __asm__("_riffed_barlow_m_start");
extern "C" const unsigned char riffed_barlow_m_end[]    __asm__("_riffed_barlow_m_end");
extern "C" const unsigned char riffed_barlow_sb_start[] __asm__("_riffed_barlow_sb_start");
extern "C" const unsigned char riffed_barlow_sb_end[]   __asm__("_riffed_barlow_sb_end");

namespace Theme {

Palette P;
ImFont* Body = nullptr;
ImFont* Small = nullptr;
ImFont* Heading = nullptr;
ImFont* Title = nullptr;

namespace {

const Preset kPresets[] = {
    { "Sunset orange",   ImVec4(0.949f, 0.639f, 0.227f, 1) },
    { "Grove green",     ImVec4(0.247f, 0.682f, 0.353f, 1) },
    { "Ballas purple",   ImVec4(0.639f, 0.357f, 0.839f, 1) },
    { "San Fierro teal", ImVec4(0.184f, 0.702f, 0.722f, 1) },
    { "Venturas gold",   ImVec4(0.878f, 0.722f, 0.290f, 1) },
};

float g_scale = 1.0f;          // what the atlas was built for
float g_pending = 0.0f;        // a new scale waiting to settle
DWORD g_pendingSince = 0;
int   g_builtAccent = -1;

float WantedScale(float clientHeight)
{
    float res = std::max(1.0f, clientHeight / 1080.0f);
    return res * std::clamp(g_toggles.textScale, 0.7f, 2.0f);
}

ImFont* AddFont(const unsigned char* a, const unsigned char* b, float px)
{
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x203A, 0x2190, 0x2193, 0 };
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)a, (int)(b - a), px, &cfg, ranges);
}

ImVec4 Lighten(const ImVec4& c, float t) { return Mix(c, ImVec4(1, 1, 1, c.w), t); }

void ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding = 16; s.ChildRounding = 10; s.FrameRounding = 8; s.PopupRounding = 10;
    s.GrabRounding = 8; s.TabRounding = 8; s.ScrollbarRounding = 8;
    s.WindowPadding = ImVec2(18, 16); s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6); s.CellPadding = ImVec2(6, 2);
    s.ScrollbarSize = 10; s.GrabMinSize = 24;
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = 1; s.FrameBorderSize = 0;
    s.DisabledAlpha = 0.38f;
    s.SeparatorTextBorderSize = 1;
    s.ScaleAllSizes(g_scale);
}

} // namespace

int PresetCount() { return (int)(sizeof(kPresets) / sizeof(kPresets[0])); }
const Preset& PresetAt(int i) { return kPresets[std::clamp(i, 0, PresetCount() - 1)]; }
float Scale() { return g_scale; }

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImVec4 Alpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, c.w * a); }
ImU32 Col(const ImVec4& c, float alpha) { return ImGui::GetColorU32(Alpha(c, alpha)); }

void Refresh()
{
    const ImVec4 a = PresetAt(g_toggles.accent).colour;
    float lum = 0.299f * a.x + 0.587f * a.y + 0.114f * a.z;
    P.accent     = a;
    P.accentHi   = Lighten(a, 0.25f);
    P.accentLo   = Alpha(a, 0.45f);
    P.accentSoft = Alpha(a, 0.16f);
    P.onAccent   = lum > 0.55f ? ImVec4(0.12f, 0.08f, 0.03f, 1) : ImVec4(1, 1, 1, 1);
    P.text       = ImVec4(0.93f, 0.91f, 0.87f, 1);
    P.textMuted  = ImVec4(0.66f, 0.63f, 0.58f, 1);
    P.textDim    = ImVec4(0.47f, 0.45f, 0.42f, 1);
    P.panel      = ImVec4(0.075f, 0.066f, 0.058f, 1);   // warm smoked glass; alpha comes from Opacity
    P.sidebar    = ImVec4(0.0f, 0.0f, 0.0f, 0.22f);
    P.card       = ImVec4(1, 1, 1, 0.035f);
    P.cardBorder = ImVec4(1, 1, 1, 0.065f);
    P.hairline   = ImVec4(1, 1, 1, 0.08f);
    P.field      = ImVec4(1, 1, 1, 0.055f);
    P.fieldHover = ImVec4(1, 1, 1, 0.09f);
    P.track      = ImVec4(1, 1, 1, 0.13f);
    P.knob       = ImVec4(1, 1, 1, 1);
    P.shadow     = ImVec4(0, 0, 0, 1);
    P.warning    = ImVec4(0.95f, 0.78f, 0.32f, 1);
    P.danger     = ImVec4(0.92f, 0.36f, 0.33f, 1);

    // Colours for the stock ImGui widgets the menu still uses (lists, colour pickers, text fields).
    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                 = P.text;
    c[ImGuiCol_TextDisabled]         = P.textMuted;
    c[ImGuiCol_WindowBg]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = ImVec4(0.09f, 0.08f, 0.07f, 0.97f);
    c[ImGuiCol_Border]               = P.hairline;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = P.field;
    c[ImGuiCol_FrameBgHovered]       = P.fieldHover;
    c[ImGuiCol_FrameBgActive]        = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_TitleBg]              = P.panel;
    c[ImGuiCol_TitleBgActive]        = P.panel;
    c[ImGuiCol_CheckMark]            = a;
    c[ImGuiCol_SliderGrab]           = a;
    c[ImGuiCol_SliderGrabActive]     = P.accentHi;
    c[ImGuiCol_Button]               = P.field;
    c[ImGuiCol_ButtonHovered]        = Alpha(a, 0.28f);
    c[ImGuiCol_ButtonActive]         = Alpha(a, 0.45f);
    c[ImGuiCol_Header]               = Alpha(a, 0.22f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_HeaderActive]         = Alpha(a, 0.32f);
    c[ImGuiCol_Separator]            = P.hairline;
    c[ImGuiCol_SeparatorHovered]     = P.accentLo;
    c[ImGuiCol_SeparatorActive]      = a;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_ScrollbarGrabActive]  = P.accentLo;
    c[ImGuiCol_PlotHistogram]        = a;
    c[ImGuiCol_TableHeaderBg]        = P.field;
    c[ImGuiCol_TableBorderLight]     = P.hairline;
    c[ImGuiCol_TableBorderStrong]    = P.hairline;
    c[ImGuiCol_TextSelectedBg]       = Alpha(a, 0.35f);
    c[ImGuiCol_NavCursor]            = a;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.45f);
    g_builtAccent = g_toggles.accent;
}

void BuildFonts(float clientHeight)
{
    g_scale = WantedScale(clientHeight);
    g_pending = 0.0f;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    Body    = AddFont(riffed_inter_start, riffed_inter_end, 16.0f * g_scale);
    Small   = AddFont(riffed_inter_start, riffed_inter_end, 13.5f * g_scale);
    Heading = AddFont(riffed_barlow_m_start, riffed_barlow_m_end, 16.0f * g_scale);
    Title   = AddFont(riffed_barlow_sb_start, riffed_barlow_sb_end, 27.0f * g_scale);
    io.FontDefault = Body;
    io.FontGlobalScale = 1.0f;
    ApplyStyle();
    Refresh();
    Log("Fonts: Inter + Barlow Condensed at UI scale %.2f", g_scale);
}

bool FontsOutdated(float clientHeight)
{
    if (g_builtAccent != g_toggles.accent) Refresh();
    float want = WantedScale(clientHeight);
    if (fabsf(want - g_scale) < 0.01f) { g_pending = 0.0f; return false; }
    if (fabsf(want - g_pending) > 0.001f) { g_pending = want; g_pendingSince = GetTickCount(); return false; }
    return GetTickCount() - g_pendingSince > 300 && !ImGui::GetIO().MouseDown[0];   // not while a slider is dragged
}

const char* Upper(const char* text)
{
    static std::string out;
    out.clear();
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        if (*p < 0x80) { out += (char)toupper(*p); ++p; continue; }
        if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            unsigned cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 0x20;                 // Latin-1 lower case
            else if (cp >= 0x100 && cp <= 0x137 && (cp & 1)) cp -= 1;                 // ą ć ę ...
            else if (cp >= 0x139 && cp <= 0x148 && !(cp & 1)) cp -= 1;                // ł ń
            else if (cp >= 0x14A && cp <= 0x177 && (cp & 1)) cp -= 1;                 // ś ...
            else if (cp >= 0x179 && cp <= 0x17E && !(cp & 1)) cp -= 1;                // ź ż
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
            p += 2;
            continue;
        }
        out += (char)*p++;
    }
    return out.c_str();
}

} // namespace Theme
