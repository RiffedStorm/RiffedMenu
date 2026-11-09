#pragma once
struct CVector;
namespace GPS {
void Tick();    // choose a destination and ask for a route when needed (game tick)
void Render();  // draw the route over the minimap
bool Objective(CVector& out);   // current mission marker or target
const char* Status();           // plain English, for the menu
int  NodeCount();
int  RoutePoints();
}
