#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Catalogues read from the game's own text data files at first use, so lists always match
// the installed game (including mods that edit these files).
//   data/carcols.dat               car colour palette
//   data/vehicles.ide              vehicle model id <-> name
//   data/maps/veh_mods/veh_mods.ide  tuning part id <-> name
//   data/carmods.dat               which parts fit which car, linked pairs, wheels
//   data/shopping.dat              clothes, haircuts, tattoos
//   data/peds.ide                  ped skins

struct RGB { uint8_t r, g, b; };

struct TuningPart {
    int id;
    std::string name;
    std::string category;   // "Spoiler", "Exhaust", ...
};

struct ClothingItem {
    std::string texture;    // e.g. "vestblack"
    std::string model;      // e.g. "vest"; empty for tattoos
    int part;               // texture body part (0 torso, 1 head, 2 legs, 3 shoes, 4-12 tattoos, 13-17 extras)
};

struct PedSkin { int id; std::string name; };

namespace GameData {

bool Load();                                   // idempotent; logs what it found
const std::vector<RGB>& CarColours();
std::vector<TuningPart> PartsFor(int vehicleModel);   // compatible parts + universal ones, grouped by category
int LinkedPart(int partId);                    // other half of a pair (side skirts), or -1
const std::vector<ClothingItem>& Clothes();
const std::vector<PedSkin>& Skins();
const char* BodyPartName(int part);

std::string ImgRead(const std::string& fileName);   // a file from models\gta3.img, "" if absent (any thread)
bool ImgHas(const std::string& fileName);
std::string WeaponTxd(int weaponType);              // texture dictionary that holds the weapon's icon
const char* WeaponDisplayName(int weaponType);

} // namespace GameData
