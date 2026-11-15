#pragma once
#include "imgui.h"

// Look of the menu: one palette derived from the accent colour, the fonts, and the UI scale.
// Every size in the UI code goes through S() so the menu looks the same at any resolution.
namespace Theme {

struct Palette {
    ImVec4 accent, accentHi, accentLo, accentSoft, onAccent;
    ImVec4 text, textMuted, textDim;
    ImVec4 panel, sidebar, card, cardBorder, hairline, field, fieldHover;
    ImVec4 track, knob, shadow, warning, danger;
};

struct Preset { const char* name; ImVec4 colour; };

extern Palette P;
extern ImFont* Body;      // Inter, everything by default
extern ImFont* Small;     // Inter, hints and secondary text
extern ImFont* Heading;   // Barlow Condensed, section titles (upper case)
extern ImFont* Title;     // Barlow Condensed, menu and page titles

int PresetCount();
const Preset& PresetAt(int i);

float Scale();                            // current UI scale (resolution x text size)
inline float S(float v) { return v * Scale(); }

void BuildFonts(float clientHeight);      // (re)build the font atlas and style for this window height
bool FontsOutdated(float clientHeight);   // the window or text size changed and has settled
void Refresh();                           // re-derive palette and style colours from the settings

ImU32 Col(const ImVec4& c, float alpha = 1.0f);   // honours ImGui's current style alpha (disabled, fades)
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
ImVec4 Alpha(const ImVec4& c, float a);

// Simple upper-casing that also covers Polish letters (UTF-8, Latin-1 and Latin Extended-A).
const char* Upper(const char* text);

}
