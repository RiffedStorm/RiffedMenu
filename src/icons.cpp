#include "icons.h"
#include <algorithm>
#include <cmath>

namespace Icons {

namespace {
const float kPi = 3.14159265f;
}

void Draw(ImDrawList* dl, Id icon, ImVec2 c, float size, ImU32 col)
{
    const float s = size * 0.5f;                   // half extent
    const float t = std::max(1.2f, size * 0.085f); // stroke
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    auto line = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), col, t); };

    switch (icon) {
    case Player:
        dl->AddCircle(P(0, -0.38f), s * 0.34f, col, 20, t);
        dl->PathArcTo(P(0, 0.95f), s * 0.72f, kPi * 1.08f, kPi * 1.92f, 20);
        dl->PathStroke(col, 0, t);
        break;
    case Vehicle: {
        dl->AddRect(P(-0.9f, -0.05f), P(0.9f, 0.45f), col, s * 0.18f, 0, t);
        ImVec2 roof[4] = { P(-0.55f, -0.05f), P(-0.32f, -0.5f), P(0.32f, -0.5f), P(0.55f, -0.05f) };
        dl->AddPolyline(roof, 4, col, 0, t);
        dl->AddCircleFilled(P(-0.5f, 0.55f), s * 0.17f, col, 12);
        dl->AddCircleFilled(P(0.5f, 0.55f), s * 0.17f, col, 12);
        break;
    }
    case Customize:   // spray can
        dl->AddRect(P(-0.42f, -0.35f), P(0.32f, 0.9f), col, s * 0.16f, 0, t);
        dl->AddRect(P(-0.22f, -0.62f), P(0.12f, -0.35f), col, 0, 0, t);
        dl->AddCircleFilled(P(0.52f, -0.62f), s * 0.07f, col);
        dl->AddCircleFilled(P(0.72f, -0.45f), s * 0.07f, col);
        dl->AddCircleFilled(P(0.72f, -0.8f), s * 0.07f, col);
        break;
    case Weapons: {   // pistol
        ImVec2 g[7] = { P(-0.9f, -0.45f), P(0.9f, -0.45f), P(0.9f, -0.08f), P(-0.05f, -0.08f),
                        P(-0.25f, 0.8f), P(-0.7f, 0.8f), P(-0.55f, -0.08f) };
        dl->AddPolyline(g, 7, col, ImDrawFlags_Closed, t);
        line(-0.05f, -0.08f, 0.05f, 0.25f);
        break;
    }
    case Gameplay:    // sliders
        for (int i = 0; i < 3; ++i) {
            float y = -0.6f + i * 0.6f, k = i == 1 ? 0.35f : -0.3f;
            line(-0.85f, y, 0.85f, y);
            dl->AddCircleFilled(P(k, y), s * 0.2f, col, 12);
        }
        break;
    case Combat:      // crosshair
        dl->AddCircle(c, s * 0.62f, col, 24, t);
        line(0, -0.95f, 0, -0.35f); line(0, 0.35f, 0, 0.95f);
        line(-0.95f, 0, -0.35f, 0); line(0.35f, 0, 0.95f, 0);
        dl->AddCircleFilled(c, s * 0.1f, col);
        break;
    case Missions: {  // flag
        line(-0.65f, -0.9f, -0.65f, 0.95f);
        ImVec2 f[5] = { P(-0.65f, -0.8f), P(0.0f, -0.65f), P(0.75f, -0.8f), P(0.75f, 0.05f), P(-0.65f, 0.05f) };
        dl->AddPolyline(f, 5, col, ImDrawFlags_Closed, t);
        break;
    }
    case Outfit: {    // t-shirt
        ImVec2 sh[10] = { P(-0.3f, -0.8f), P(-0.9f, -0.5f), P(-0.7f, -0.05f), P(-0.5f, -0.15f), P(-0.5f, 0.85f),
                          P(0.5f, 0.85f), P(0.5f, -0.15f), P(0.7f, -0.05f), P(0.9f, -0.5f), P(0.3f, -0.8f) };
        dl->AddPolyline(sh, 10, col, 0, t);
        dl->PathArcTo(P(0, -0.8f), s * 0.3f, 0, kPi, 10);
        dl->PathStroke(col, 0, t);
        break;
    }
    case Teleport:    // map pin
        dl->PathArcTo(P(0, -0.25f), s * 0.6f, kPi * 0.8f, kPi * 2.2f, 24);
        dl->PathLineTo(P(0, 0.95f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircle(P(0, -0.25f), s * 0.2f, col, 12, t);
        break;
    case World:       // globe
        dl->AddCircle(c, s * 0.85f, col, 28, t);
        dl->AddEllipse(c, ImVec2(s * 0.38f, s * 0.85f), col, 0, 24, t);
        line(-0.85f, 0, 0.85f, 0);
        break;
    case Stats:
        dl->AddRectFilled(P(-0.8f, 0.1f), P(-0.4f, 0.85f), col, s * 0.06f);
        dl->AddRectFilled(P(-0.2f, -0.35f), P(0.2f, 0.85f), col, s * 0.06f);
        dl->AddRectFilled(P(0.4f, -0.8f), P(0.8f, 0.85f), col, s * 0.06f);
        break;
    case Settings: {  // gear
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            float a = i * 2 * kPi / teeth;
            ImVec2 d(cosf(a), sinf(a));
            dl->AddLine(ImVec2(c.x + d.x * s * 0.55f, c.y + d.y * s * 0.55f), ImVec2(c.x + d.x * s * 0.92f, c.y + d.y * s * 0.92f), col, t * 1.9f);
        }
        dl->AddCircle(c, s * 0.6f, col, 24, t);
        dl->AddCircle(c, s * 0.24f, col, 16, t);
        break;
    }
    case Search:
        dl->AddCircle(P(-0.15f, -0.15f), s * 0.58f, col, 20, t);
        line(0.28f, 0.28f, 0.85f, 0.85f);
        break;
    case Close:
        line(-0.6f, -0.6f, 0.6f, 0.6f); line(0.6f, -0.6f, -0.6f, 0.6f);
        break;
    case Check: {
        ImVec2 k[3] = { P(-0.7f, 0.0f), P(-0.2f, 0.5f), P(0.75f, -0.5f) };
        dl->AddPolyline(k, 3, col, 0, t * 1.2f);
        break;
    }
    case ChevronDown: {
        ImVec2 k[3] = { P(-0.6f, -0.25f), P(0, 0.35f), P(0.6f, -0.25f) };
        dl->AddPolyline(k, 3, col, 0, t);
        break;
    }
    case Bell:
        dl->PathArcTo(P(0, -0.2f), s * 0.55f, kPi, 2 * kPi, 16);
        dl->PathLineTo(P(0.55f, 0.45f)); dl->PathLineTo(P(0.8f, 0.6f)); dl->PathLineTo(P(-0.8f, 0.6f)); dl->PathLineTo(P(-0.55f, 0.45f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(P(0, 0.85f), s * 0.14f, col);
        break;
    case Keyboard:
        dl->AddRect(P(-0.95f, -0.55f), P(0.95f, 0.55f), col, s * 0.15f, 0, t);
        for (int i = 0; i < 4; ++i) dl->AddRectFilled(P(-0.7f + i * 0.4f, -0.3f), P(-0.5f + i * 0.4f, -0.1f), col);
        line(-0.5f, 0.28f, 0.5f, 0.28f);
        break;
    }
}

}
