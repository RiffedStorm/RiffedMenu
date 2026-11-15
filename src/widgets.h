#pragma once
#include "imgui.h"
#include "icons.h"
#include <cfloat>
#include <string>
#include <vector>

// Custom widgets: ImGui only does hit-testing and input, everything visible is drawn with the
// draw list. Signatures match the ImGui calls they replace, so the menu code reads the same.
// Labels never overlap: text that doesn't fit is cut with "..." and shown in full on hover.
namespace UI {

// Animation: a per-widget float eased towards target, frame-rate independent.
float Anim(ImGuiID id, float target, float speed = 14.0f);
void  Ease(float& value, float target, float speed);
bool  AnimationsOn();

// Draws text cut to maxWidth; returns true when it had to be shortened.
bool  TextFit(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text, const char* end, float maxWidth, ImFont* font = nullptr);
void  Hint(const char* text);   // tooltip for the last item

enum ButtonKind { Secondary, Primary, Danger };

bool Checkbox(const char* label, bool* v, const char* hint = nullptr);   // pill switch with a sliding knob; hint = tooltip
bool Button(const char* label, const ImVec2& size = ImVec2(0, 0), int kind = Secondary);
bool SmallButton(const char* label);
bool IconButton(const char* id, Icons::Id icon, float size);
bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d", ImGuiSliderFlags flags = 0);
bool Segmented(const char* id, int* v, const char* const items[], int count);
bool Combo(const char* label, int* current, const char* const items[], int count, int popupMax = -1);
bool Combo(const char* label, int* current, const char* (*getter)(void*, int), void* data, int count, int popupMax = -1);
bool RadioButton(const char* label, bool active);
bool RadioButton(const char* label, int* v, int value);
bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t size, ImGuiInputTextFlags flags = 0,
                       ImGuiInputTextCallback cb = nullptr, void* data = nullptr);
bool SearchBox(const char* label, const char* hint, char* buf, size_t size);   // InputTextWithHint with a magnifier
bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
void ProgressBar(float fraction, const ImVec2& size = ImVec2(-FLT_MIN, 0), const char* overlay = nullptr);
bool KeyChip(const char* label, int* vk, const char* waitingText, const char* keyName);

// Layout
void Section(const char* title);   // starts a rounded card that lasts until the next section or the end of the window
void CloseCards();                 // closes cards opened in the current window
bool BeginChild(const char* id, const ImVec2& size = ImVec2(0, 0), ImGuiChildFlags childFlags = 0, ImGuiWindowFlags windowFlags = 0);
void EndChild();
void BeginSub(bool parentOn);      // indented, greyed while the parent toggle is off
void EndSub();

// Global search
struct Entry { int page; std::string section, label; };
void IndexBegin(int page);
void IndexEnd();
bool Indexing();
const std::vector<Entry>& Index();
void ClearIndex();
void SetPage(int page);                     // page being drawn, for jump-to highlights
void FocusOn(int page, const std::string& label);
void Register(const char* label);           // after an item: adds it to the index, highlights it after a search jump

}
