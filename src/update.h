#pragma once
#include <string>
namespace Update {
void Start();                 // background check of the latest GitHub release
bool Available();
std::string Latest();         // tag of the newest release once known
void OpenReleasePage();
}
