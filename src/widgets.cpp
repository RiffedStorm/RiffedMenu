#include "widgets.h"
#include "theme.h"
#include "features.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <windows.h>

using namespace Theme;

namespace UI {

namespace {

// ---------------------------------------------------------------------------------------------
// Search index and jump-to highlight
std::vector<Entry> g_index;
bool        g_indexing = false;
int         g_page = -1;
std::string g_section;
int         g_focusPage = -1;
std::string g_focusLabel;
double      g_focusTime = 0;
bool        g_focusScrolled = false;

// Visible part of a label: drops "##id" and printf formats ("Opacity %.2f" -> "Opacity").
std::string CleanLabel(const char* label)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    std::string out;
    for (const char* p = label; p < end; ++p) {
        if (*p == '%' && p + 1 < end) {
            if (p[1] == '%') { out += '%'; ++p; continue; }
            ++p;
            while (p < end && strchr("0123456789.+-# lh", *p)) ++p;
            continue;                                   // the conversion letter is skipped too
        }
        out += *p;
    }
    while (!out.empty() && strchr(" :x-", out.back())) out.pop_back();
    size_t s = out.find_first_not_of(' ');
    return s == std::string::npos ? std::string() : out.substr(s);
}

// ---------------------------------------------------------------------------------------------
// Cards
struct Card {
    ImGuiWindow* window;
    ImDrawListSplitter splitter;
    ImVec2 start;
    float right, pad;
};
std::vector<std::unique_ptr<Card>> g_cards;

struct Sub { float x, y; bool on; };
std::vector<Sub> g_subs;

void CloseCard(Card& c)
{
    ImGuiWindow* w = c.window;
    ImGui::Dummy(ImVec2(0, c.pad * 0.35f));
    ImGui::Unindent(c.pad);
    w->WorkRect.Max.x += c.pad;
    w->ContentRegionRect.Max.x += c.pad;
    float y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    ImDrawList* dl = w->DrawList;
    c.splitter.SetCurrentChannel(dl, 0);
    float r = S(12);
    ImVec2 a(c.start.x, c.start.y), b(c.right, y1);
    dl->AddRectFilled(ImVec2(a.x, a.y + S(2)), ImVec2(b.x, b.y + S(3)), Col(P.shadow, 0.18f), r);    // soft drop shadow
    dl->AddRectFilled(a, b, Col(P.card), r);
    dl->AddRect(a, b, Col(P.cardBorder), r, 0, 1.0f);
    dl->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f), Col(ImVec4(1, 1, 1, 0.05f)), 1.0f);   // top light
    c.splitter.Merge(dl);
    ImGui::Dummy(ImVec2(0, S(4)));
}

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) { return Mix(a, b, t); }

float FrameH() { return ImGui::GetFrameHeight(); }

// Width for widgets that honour SetNextItemWidth; full width when none was set.
float ItemWidth()
{
    ImGuiContext& g = *GImGui;
    if (g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasWidth) return ImGui::CalcItemWidth();
    return ImGui::GetContentRegionAvail().x;
}

void FocusHighlight(const char* label)
{
    if (g_focusLabel.empty() || g_page != g_focusPage || g_indexing) return;
    if (CleanLabel(label) != g_focusLabel) return;
    if (!g_focusScrolled) { ImGui::ScrollToItem(ImGuiScrollFlags_AlwaysCenterY); g_focusScrolled = true; g_focusTime = ImGui::GetTime(); }
    float age = (float)(ImGui::GetTime() - g_focusTime);
    if (age > 2.0f) { g_focusLabel.clear(); return; }
    float a = age < 0.2f ? age / 0.2f : 1.0f - (age - 0.2f) / 1.8f;
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax(), e(S(4), S(3));
    ImGui::GetWindowDrawList()->AddRect(ImVec2(mn.x - e.x, mn.y - e.y), ImVec2(mx.x + e.x, mx.y + e.y), Col(P.accent, a), S(8), 0, S(2));
}

template <typename T>
bool SliderImpl(const char* label, ImGuiDataType type, T* v, T lo, T hi, const char* fmt, ImGuiSliderFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const float w = ItemWidth(), h = FrameH();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_Inputable)) { if (g_indexing) Register(fmt); return false; }

    bool temp = ImGui::TempInputIsActive(id);
    // ButtonBehavior below does the real hover test; a second ItemHoverable() per frame trips ImGui's duplicate-ID check.
    const bool hovered = g.HoveredWindow == window && ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    if (!temp && hovered && (g.IO.MouseClickedCount[0] == 2 || (ImGui::IsMouseClicked(0) && g.IO.KeyCtrl))) temp = true;
    if (temp) {
        T mn = lo, mx = hi;
        bool changed = ImGui::TempInputScalar(bb, id, label, type, v, fmt, &mn, &mx);
        return changed;
    }

    bool hov = false, held = false;
    ImGui::ButtonBehavior(bb, id, &hov, &held, ImGuiButtonFlags_PressedOnClick);
    const bool log = (flags & ImGuiSliderFlags_Logarithmic) && lo > 0;
    const float inset = h * 0.5f;
    const float x0 = bb.Min.x + inset, x1 = bb.Max.x - inset;
    bool changed = false;
    if (held && x1 > x0) {
        float t = std::clamp((g.IO.MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        double nv = log ? lo * pow((double)hi / lo, t) : lo + (hi - lo) * (double)t;
        T newV = std::is_integral<T>::value ? (T)llround(nv) : (T)nv;
        if (newV != *v) { *v = newV; changed = true; ImGui::MarkItemEdited(id); }
    }
    float t = (float)(log ? std::log((double)std::max(*v, lo) / lo) / std::log((double)hi / lo)
                          : (hi > lo ? ((double)*v - lo) / ((double)hi - lo) : 0.0));
    t = std::clamp(t, 0.0f, 1.0f);
    float shown = Anim(id, t, 22.0f);
    float hv = Anim(id + 1, (hov || held) ? 1.0f : 0.0f);

    ImDrawList* dl = window->DrawList;
    const float r = h * 0.5f;
    dl->AddRectFilled(bb.Min, bb.Max, Col(Lerp(P.field, P.fieldHover, hv)), r);
    float kx = x0 + (x1 - x0) * shown;
    if (kx > bb.Min.x + 1) dl->AddRectFilled(bb.Min, ImVec2(kx + r * 0.2f, bb.Max.y), Col(Lerp(P.accentSoft, P.accentLo, 0.35f + hv * 0.4f)), r);
    dl->AddCircleFilled(ImVec2(kx, bb.GetCenter().y), r * 0.62f + hv * S(1), Col(P.accent, 0.25f * hv), 20);
    dl->AddCircleFilled(ImVec2(kx, bb.GetCenter().y), r * 0.5f, Col(P.knob), 20);

    char buf[96];
    if (std::is_integral<T>::value) snprintf(buf, sizeof(buf), fmt, (int)*v);
    else snprintf(buf, sizeof(buf), fmt, (double)*v);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    float maxW = w - h * 1.2f;
    ImVec2 tp(bb.GetCenter().x - std::min(ts.x, maxW) * 0.5f, bb.GetCenter().y - ts.y * 0.5f);
    if (TextFit(dl, tp, Col(P.text), buf, nullptr, maxW) && hovered) ImGui::SetTooltip("%s", buf);
    Register(fmt);
    return changed;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Animation

bool AnimationsOn() { return g_toggles.animations; }

void Ease(float& value, float target, float speed)
{
    if (!AnimationsOn()) { value = target; return; }
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    value += (target - value) * (1.0f - expf(-speed * dt));
    if (fabsf(target - value) < 0.0015f) value = target;
}

float Anim(ImGuiID id, float target, float speed)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    float v = st->GetFloat(id, target);
    Ease(v, target, speed);
    st->SetFloat(id, v);
    return v;
}

// ---------------------------------------------------------------------------------------------
// Text

bool TextFit(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text, const char* end, float maxWidth, ImFont* font)
{
    if (!font) font = ImGui::GetFont();
    float size = font == ImGui::GetFont() ? ImGui::GetFontSize() : font->FontSize;
    if (!end) end = text + strlen(text);
    float w = font->CalcTextSizeA(size, FLT_MAX, 0, text, end).x;
    if (w <= maxWidth + 0.5f) { dl->AddText(font, size, pos, col, text, end); return false; }
    const char* dots = "...";
    float dw = font->CalcTextSizeA(size, FLT_MAX, 0, dots).x;
    const char* cut = text;
    while (cut < end) {
        const char* next = cut + 1;
        while (next < end && (*next & 0xC0) == 0x80) ++next;       // keep UTF-8 letters whole
        if (font->CalcTextSizeA(size, FLT_MAX, 0, text, next).x + dw > maxWidth) break;
        cut = next;
    }
    while (cut > text && cut[-1] == ' ') --cut;
    dl->AddText(font, size, pos, col, text, cut);
    float cw = font->CalcTextSizeA(size, FLT_MAX, 0, text, cut).x;
    dl->AddText(font, size, ImVec2(pos.x + cw, pos.y), col, dots);
    return true;
}

void Hint(const char* text)
{
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

// ---------------------------------------------------------------------------------------------
// Controls

bool Checkbox(const char* label, bool* v, const char* hint)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiContext& g = *GImGui;
    if (window->SkipItems) { if (g_indexing) Register(label); return false; }
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float h = FrameH();
    const float th = h * 0.64f, tw = th * 1.8f;
    const float natural = tw + S(10) + ImGui::CalcTextSize(label, end).x + S(8);
    float limit = (g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasWidth) ? ImGui::CalcItemWidth() : ImGui::GetContentRegionAvail().x;
    const float w = std::max(std::min(natural, limit), tw + S(24));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    if (pressed) *v = !*v;
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    float on = Anim(id, *v ? 1.0f : 0.0f, 16.0f);
    float hv = Anim(id + 1, hovered ? 1.0f : 0.0f);

    ImDrawList* dl = window->DrawList;
    if (hv > 0.01f) dl->AddRectFilled(ImVec2(p.x - S(4), p.y), ImVec2(p.x + w, p.y + h), Col(ImVec4(1, 1, 1, 0.04f * hv)), S(7));
    const ImVec2 t0(p.x, p.y + (h - th) * 0.5f), t1(t0.x + tw, t0.y + th);
    if (on > 0.01f) dl->AddRectFilled(ImVec2(t0.x - S(2), t0.y - S(2)), ImVec2(t1.x + S(2), t1.y + S(2)), Col(P.accent, 0.18f * on), th);   // glow
    dl->AddRectFilled(t0, t1, Col(Lerp(Lerp(P.track, ImVec4(1, 1, 1, 0.2f), hv * 0.5f), P.accent, on)), th * 0.5f);
    const float kr = th * 0.5f - S(2.5f);
    const float kx = t0.x + th * 0.5f + (tw - th) * on;
    dl->AddCircleFilled(ImVec2(kx, t0.y + th * 0.5f + S(1)), kr, Col(P.shadow, 0.25f), 16);
    dl->AddCircleFilled(ImVec2(kx, t0.y + th * 0.5f), kr, Col(P.knob), 16);

    const float tx = t1.x + S(10);
    ImVec2 tp(tx, p.y + (h - ImGui::GetFontSize()) * 0.5f);
    bool cut = TextFit(dl, tp, Col(P.text), label, end, p.x + w - tx);
    if (hovered && (cut || (hint && *hint)) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (cut && hint && *hint) ImGui::SetTooltip("%.*s\n\n%s", (int)(end - label), label, hint);
        else if (cut) ImGui::SetTooltip("%.*s", (int)(end - label), label);
        else ImGui::SetTooltip("%s", hint);
    }
    Register(label);
    return pressed;
}

bool Button(const char* label, const ImVec2& sizeArg, int kind)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    ImVec2 size = ImGui::CalcItemSize(sizeArg, ts.x + S(28), FrameH());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    const ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    float hv = Anim(id, hovered ? 1.0f : 0.0f), dn = Anim(id + 1, held ? 1.0f : 0.0f, 24.0f);

    ImDrawList* dl = window->DrawList;
    ImVec4 bg, border, text = P.text;
    if (kind == Primary) {
        bg = Lerp(P.accent, P.accentHi, hv * 0.6f); border = Alpha(P.accentHi, 0.6f); text = P.onAccent;
    } else if (kind == Danger) {
        bg = Lerp(Alpha(P.danger, 0.16f), Alpha(P.danger, 0.32f), hv); border = Alpha(P.danger, 0.35f + 0.3f * hv);
    } else {
        bg = Lerp(P.field, Alpha(P.accent, 0.20f), hv); border = Lerp(P.cardBorder, Alpha(P.accent, 0.55f), hv);
    }
    float shrink = dn * S(1.0f);
    ImVec2 a(p.x + shrink, p.y + shrink), b(p.x + size.x - shrink, p.y + size.y - shrink);
    dl->AddRectFilled(a, b, Col(bg), S(8));
    dl->AddRect(a, b, Col(border), S(8), 0, 1.0f);
    float maxW = size.x - S(16);
    ImVec2 tp(p.x + (size.x - std::min(ts.x, maxW)) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    if (TextFit(dl, tp, Col(text), label, end, maxW) && hovered) ImGui::SetTooltip("%.*s", (int)(end - label), label);
    Register(label);
    return pressed;
}

bool SmallButton(const char* label)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(2)));
    ImGui::PushFont(Theme::Small);
    ImVec2 ts = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label));
    bool r = Button(label, ImVec2(ts.x + S(18), ImGui::GetFontSize() + S(8)));
    ImGui::PopFont();
    ImGui::PopStyleVar();
    return r;
}

bool IconButton(const char* id, Icons::Id icon, float size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    ImGuiID iid = ImGui::GetItemID();
    float hv = Anim(iid, ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    dl->AddCircleFilled(c, size * 0.5f, Col(ImVec4(1, 1, 1, 0.08f * hv)), 24);
    Icons::Draw(dl, icon, c, size * 0.42f, Col(Lerp(P.textMuted, P.text, hv)));
    return pressed;
}

bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt, ImGuiSliderFlags flags)
{
    return SliderImpl<float>(label, ImGuiDataType_Float, v, lo, hi, fmt, flags);
}

bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt, ImGuiSliderFlags flags)
{
    return SliderImpl<int>(label, ImGuiDataType_S32, v, lo, hi, fmt, flags);
}

bool Segmented(const char* idStr, int* v, const char* const items[], int count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0) return false;
    ImGui::PushID(idStr);
    const float w = ItemWidth(), h = FrameH();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(ImVec4(0, 0, 0, 0.28f)), S(9));
    const float pad = S(3), segW = (w - pad * 2) / count;
    const ImGuiID animId = ImGui::GetID("##ind");
    float x = Anim(animId, (float)std::clamp(*v, 0, count - 1), 18.0f);
    ImVec2 a(p.x + pad + segW * x, p.y + pad);
    dl->AddRectFilled(a, ImVec2(a.x + segW, p.y + h - pad), Col(P.accent), S(7));

    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad + segW * i, p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(segW, h)) && *v != i) { *v = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        float sel = std::clamp(1.0f - fabsf(x - i), 0.0f, 1.0f);
        ImVec4 tc = Lerp(hov ? P.text : P.textMuted, P.onAccent, sel);
        const char* end = ImGui::FindRenderedTextEnd(items[i]);
        ImVec2 ts = ImGui::CalcTextSize(items[i], end);
        float maxW = segW - S(10);
        ImVec2 tp(p.x + pad + segW * i + (segW - std::min(ts.x, maxW)) * 0.5f, p.y + (h - ts.y) * 0.5f);
        if (TextFit(dl, tp, Col(tc), items[i], end, maxW) && hov) ImGui::SetTooltip("%s", items[i]);
        if (g_indexing) Register(items[i]);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed;
}

bool Combo(const char* label, int* current, const char* const items[], int count, int popupMax)
{
    // A few short choices read better as a segmented row; long or many choices stay a dropdown.
    float need = 0;
    for (int i = 0; i < count; ++i) need += ImGui::CalcTextSize(items[i]).x + S(26);
    if (count <= 4 && need <= ItemWidth()) return Segmented(label, current, items, count);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), ImGui::GetStyle().FramePadding.y));
    bool r = ImGui::Combo(label, current, items, count, popupMax);
    ImGui::PopStyleVar();
    if (g_indexing) for (int i = 0; i < count; ++i) Register(items[i]);
    return r;
}

bool Combo(const char* label, int* current, const char* (*getter)(void*, int), void* data, int count, int popupMax)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), ImGui::GetStyle().FramePadding.y));
    bool r = ImGui::Combo(label, current, getter, data, count, popupMax);
    ImGui::PopStyleVar();
    return r;
}

bool RadioButton(const char* label, bool active)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float h = FrameH();
    ImVec2 size(ts.x + S(26), h), p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    ImGuiID id = ImGui::GetItemID();
    float on = Anim(id, active ? 1.0f : 0.0f), hv = Anim(id + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + h), Col(Lerp(Lerp(P.field, P.fieldHover, hv), P.accent, on)), h * 0.5f);
    dl->AddText(ImVec2(p.x + S(13), p.y + (h - ts.y) * 0.5f), Col(Lerp(P.text, P.onAccent, on)), label, end);
    Register(label);
    return pressed;
}

bool RadioButton(const char* label, int* v, int value)
{
    bool pressed = RadioButton(label, *v == value);
    if (pressed) *v = value;
    return pressed;
}

static bool InputImpl(const char* label, const char* hint, char* buf, size_t size, ImGuiInputTextFlags flags,
                      ImGuiInputTextCallback cb, void* data, bool icon)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const float w = ItemWidth(), h = FrameH();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID id = window->GetID(label);
    ImGuiStorage* st = ImGui::GetStateStorage();
    float focus = st->GetFloat(id + 7, 0.0f);       // eased from last frame's active state
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(Lerp(P.field, P.fieldHover, focus)), S(9));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Col(Lerp(P.cardBorder, P.accent, focus * 0.8f)), S(9), 0, 1.0f);
    float iconW = icon ? h * 0.9f : S(4);
    if (icon) Icons::Draw(dl, Icons::Search, ImVec2(p.x + iconW * 0.55f, p.y + h * 0.5f), h * 0.42f, Col(Lerp(P.textDim, P.accent, focus)));

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, P.textDim);
    ImGui::SetCursorScreenPos(ImVec2(p.x + iconW, p.y));
    ImGui::SetNextItemWidth(w - iconW - S(4));
    bool r = ImGui::InputTextWithHint(label, hint, buf, size, flags, cb, data);
    bool active = ImGui::IsItemActive();
    ImGui::PopStyleColor(4);
    float f = focus;
    Ease(f, active ? 1.0f : 0.0f, 14.0f);
    st->SetFloat(id + 7, f);
    if (hint && g_indexing) Register(hint);
    return r;
}

bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t size, ImGuiInputTextFlags flags,
                       ImGuiInputTextCallback cb, void* data)
{
    return InputImpl(label, hint, buf, size, flags, cb, data, false);
}

bool SearchBox(const char* label, const char* hint, char* buf, size_t size)
{
    return InputImpl(label, hint, buf, size, 0, nullptr, nullptr, true);
}

bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(label);
    ImGuiStorage* st = ImGui::GetStateStorage();
    float hv = st->GetFloat(id + 3, 0.0f), sel = st->GetFloat(id + 4, selected ? 1.0f : 0.0f);
    Ease(sel, selected ? 1.0f : 0.0f, 16.0f);
    const ImGuiStyle& s = ImGui::GetStyle();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x;
    float h = size.y > 0 ? size.y : ImGui::GetTextLineHeight();
    ImVec2 a(p.x - s.ItemSpacing.x * 0.25f, p.y - s.ItemSpacing.y * 0.5f), b(p.x + w + s.ItemSpacing.x * 0.25f, p.y + h + s.ItemSpacing.y * 0.5f);
    ImDrawList* dl = window->DrawList;
    if (hv > 0.01f) dl->AddRectFilled(a, b, Col(ImVec4(1, 1, 1, 0.055f * hv)), S(6));
    if (sel > 0.01f) {
        dl->AddRectFilled(a, b, Col(P.accent, 0.18f * sel), S(6));
        dl->AddRectFilled(ImVec2(a.x, a.y + S(3)), ImVec2(a.x + S(3), b.y - S(3)), Col(P.accent, sel), S(2));
    }
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    if (selected) ImGui::PushStyleColor(ImGuiCol_Text, P.accentHi);
    bool r = ImGui::Selectable(label, selected, flags, size);
    if (selected) ImGui::PopStyleColor();
    ImGui::PopStyleColor(3);
    Ease(hv, ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
    st->SetFloat(id + 3, hv);
    st->SetFloat(id + 4, sel);
    return r;
}

void ProgressBar(float fraction, const ImVec2& sizeArg, const char* overlay)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    ImVec2 size = ImGui::CalcItemSize(sizeArg, ImGui::CalcItemWidth(), FrameH());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    float f = std::clamp(fraction, 0.0f, 1.0f);
    ImDrawList* dl = window->DrawList;
    float r = size.y * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(P.field), r);
    const ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);   // callers tint health red, armour blue
    if (f > 0.001f) dl->AddRectFilled(p, ImVec2(p.x + std::max(size.x * f, r * 2), p.y + size.y), Col(Alpha(fill, 0.85f)), r);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), Col(P.cardBorder), r, 0, 1.0f);
    if (overlay) {
        ImVec2 ts = ImGui::CalcTextSize(overlay);
        float maxW = size.x - S(12);
        TextFit(dl, ImVec2(p.x + (size.x - std::min(ts.x, maxW)) * 0.5f, p.y + (size.y - ts.y) * 0.5f), Col(P.text), overlay, nullptr, maxW);
    }
}

bool KeyChip(const char* label, int* vk, const char* waitingText, const char* keyName)
{
    static int* waiting = nullptr;
    bool changed = false;
    if (waiting == vk) {
        for (int k = 1; k < 255; ++k) {
            if (k == 0x01 || k == 0x02) continue;                 // left / right mouse
            if (GetAsyncKeyState(k) & 0x8000) {
                if (k != 0x1B) { *vk = k; changed = true; }       // Escape cancels
                waiting = nullptr;
                break;
            }
        }
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return changed;
    ImGui::PushID(vk);
    const float h = FrameH(), w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const char* chipText = waiting == vk ? waitingText : keyName;
    float chipW = std::min(std::max(ImGui::CalcTextSize(chipText).x + S(30), w * 0.3f), w * 0.5f);
    ImDrawList* dl = window->DrawList;
    float textW = w - chipW - S(16);
    bool cut = TextFit(dl, ImVec2(p.x + S(6), p.y + (h - ImGui::GetFontSize()) * 0.5f), Col(P.text), label, nullptr, textW);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - chipW, p.y));
    if (ImGui::InvisibleButton("##chip", ImVec2(chipW, h))) waiting = waiting == vk ? nullptr : vk;
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered();
    float hv = Anim(id, hov ? 1.0f : 0.0f);
    bool wait = waiting == vk;
    float pulse = wait ? 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 6.0f) : 0.0f;
    ImVec2 a(p.x + w - chipW, p.y), b(p.x + w, p.y + h);
    dl->AddRectFilled(a, b, Col(wait ? Alpha(P.accent, 0.22f) : Lerp(P.field, P.fieldHover, hv)), S(8));
    dl->AddRect(a, b, Col(wait ? Alpha(P.accent, 0.5f + 0.5f * pulse) : Lerp(P.cardBorder, P.accentLo, hv)), S(8), 0, wait ? S(1.5f) : 1.0f);
    Icons::Draw(dl, Icons::Keyboard, ImVec2(a.x + S(14), a.y + h * 0.5f), h * 0.42f, Col(wait ? P.accent : P.textMuted));
    float maxW = chipW - S(32);
    ImVec2 ts = ImGui::CalcTextSize(chipText);
    TextFit(dl, ImVec2(a.x + S(26) + std::max(0.0f, (maxW - ts.x) * 0.5f), a.y + (h - ts.y) * 0.5f), Col(wait ? P.accentHi : P.text), chipText, nullptr, maxW);
    if (cut && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + textW, p.y + h))) ImGui::SetTooltip("%s", label);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    Register(label);
    return changed;
}

// ---------------------------------------------------------------------------------------------
// Layout

void Section(const char* title)
{
    CloseCards();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    auto card = std::make_unique<Card>();
    card->window = w;
    card->pad = S(14);
    card->start = ImGui::GetCursorScreenPos();
    card->right = w->WorkRect.Max.x;
    card->splitter.Split(w->DrawList, 2);
    card->splitter.SetCurrentChannel(w->DrawList, 1);
    ImGui::Dummy(ImVec2(0, card->pad * 0.25f));
    ImGui::Indent(card->pad);
    w->WorkRect.Max.x -= card->pad;
    w->ContentRegionRect.Max.x -= card->pad;

    ImGui::PushFont(Theme::Heading);
    ImVec2 p = ImGui::GetCursorScreenPos();
    const char* up = Theme::Upper(title);
    float maxW = w->WorkRect.Max.x - p.x;
    ImGui::Dummy(ImVec2(std::min(ImGui::CalcTextSize(up).x, maxW), ImGui::GetFontSize()));
    if (TextFit(w->DrawList, p, Col(P.accent), up, nullptr, maxW) && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", title);
    ImGui::PopFont();
    g_section = title;
    Register(title);
    g_cards.push_back(std::move(card));
}

void CloseCards()
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    while (!g_cards.empty() && g_cards.back()->window == w) {
        CloseCard(*g_cards.back());
        g_cards.pop_back();
    }
}

bool BeginChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags)
{
    if (childFlags & ImGuiChildFlags_Borders) {
        // Bordered lists get the card look instead of ImGui's frame.
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 sz = ImGui::CalcItemSize(size, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
        w->DrawList->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), Col(ImVec4(0, 0, 0, 0.22f)), S(10));
        w->DrawList->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), Col(P.cardBorder), S(10));
        childFlags &= ~ImGuiChildFlags_Borders;
        childFlags |= ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(6)));
        bool r = ImGui::BeginChild(id, size, childFlags, windowFlags);
        ImGui::PopStyleVar();
        return r;
    }
    return ImGui::BeginChild(id, size, childFlags, windowFlags);
}

void EndChild()
{
    CloseCards();
    ImGui::EndChild();
}

void BeginSub(bool parentOn)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    g_subs.push_back({ p.x + S(7), p.y, parentOn });
    ImGui::Indent(S(18));
    ImGui::BeginDisabled(!parentOn);
}

void EndSub()
{
    ImGui::EndDisabled();
    ImGui::Unindent(S(18));
    if (g_subs.empty()) return;
    Sub s = g_subs.back();
    g_subs.pop_back();
    float y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(s.x, s.y + S(4)), ImVec2(s.x, y1 - S(4)), Col(s.on ? P.accentLo : P.hairline), S(2));
}

// ---------------------------------------------------------------------------------------------
// Search

void IndexBegin(int page) { g_indexing = true; g_page = page; g_section.clear(); }
void IndexEnd() { g_indexing = false; }
bool Indexing() { return g_indexing; }
const std::vector<Entry>& Index() { return g_index; }
void ClearIndex() { g_index.clear(); }
void SetPage(int page) { g_page = page; g_section.clear(); }

void FocusOn(int page, const std::string& label)
{
    g_focusPage = page;
    g_focusLabel = label;
    g_focusScrolled = false;
}

void Register(const char* label)
{
    if (!label || !*label) return;
    if (g_indexing) {
        std::string clean = CleanLabel(label);
        if (clean.size() < 2) return;
        for (const Entry& e : g_index) if (e.page == g_page && e.label == clean) return;
        g_index.push_back({ g_page, clean == g_section ? std::string() : g_section, clean });
        return;
    }
    FocusHighlight(label);
}

} // namespace UI
