#pragma once
#include <string>
#include <vector>

namespace Config {
void Init();                              // load meta file, then the autoload (or default) config
void SaveDefault();                       // settings used while multiple configs are off
void ResetOptions();                      // options back to defaults; configs, favorites and language stay
void FactoryReset();                      // back to a fresh install: settings, configs and switches
std::string Dir();                        // game\RiffedMenu\ (created on demand)

void SaveMeta();                          // remembers the two switches below and the autoload name
void AutoSave();                          // called when the menu closes: saves only if autosave is on
bool& AutoSaveEnabled();
bool& MultiEnabled();
const std::string& Autoload();            // name of the config loaded at start, "" = none
void SetAutoload(const std::string& name);
const std::string& Current();

std::vector<std::string> List();
bool Save(const std::string& name);
bool Load(const std::string& name);
bool Delete(const std::string& name);
std::string Sanitize(const std::string& name);
}
