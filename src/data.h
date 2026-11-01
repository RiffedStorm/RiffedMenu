#pragma once
#include "game.h"

// Static tables for the menu. Model / weapon / stat IDs are the stock v1.0 ones.

struct VehicleEntry { int id; const char* name; const char* category; };
struct WeaponEntry  { int id; const char* name; const char* group; int ammo; };
struct TeleportEntry{ const char* name; const char* region; CVector pos; };
struct StatEntry    { uint16_t id; const char* name; float max; };

// Trains (449, 537, 538, 569, 570, 590) and RC Cam (594) are left out on purpose:
// CCheat::VehicleCheat can't place them and the game crashes.
inline const VehicleEntry kVehicles[] = {
    {411,"Infernus","Sports"},{429,"Banshee","Sports"},{415,"Cheetah","Sports"},{451,"Turismo","Sports"},
    {541,"Bullet","Sports"},{506,"Super GT","Sports"},{480,"Comet","Sports"},{477,"ZR-350","Sports"},
    {494,"Hotring Racer","Sports"},{502,"Hotring Racer A","Sports"},{503,"Hotring Racer B","Sports"},
    {402,"Buffalo","Sports"},{603,"Phoenix","Sports"},{602,"Alpha","Sports"},{559,"Jester","Sports"},
    {560,"Sultan","Sports"},{562,"Elegy","Sports"},{565,"Flash","Sports"},{587,"Euros","Sports"},
    {558,"Uranus","Sports"},{589,"Club","Sports"},{496,"Blista Compact","Sports"},{533,"Feltzer","Sports"},
    {555,"Windsor","Sports"},{434,"Hotknife","Sports"},

    {400,"Landstalker","Sedan/SUV"},{401,"Bravura","Sedan/SUV"},{404,"Perennial","Sedan/SUV"},{405,"Sentinel","Sedan/SUV"},
    {409,"Stretch","Sedan/SUV"},{410,"Manana","Sedan/SUV"},{421,"Washington","Sedan/SUV"},{426,"Premier","Sedan/SUV"},
    {436,"Previon","Sedan/SUV"},{439,"Stallion","Sedan/SUV"},{445,"Admiral","Sedan/SUV"},{458,"Solair","Sedan/SUV"},
    {466,"Glendale","Sedan/SUV"},{467,"Oceanic","Sedan/SUV"},{474,"Hermes","Sedan/SUV"},{475,"Sabre","Sedan/SUV"},
    {479,"Regina","Sedan/SUV"},{489,"Rancher","Sedan/SUV"},{491,"Virgo","Sedan/SUV"},{492,"Greenwood","Sedan/SUV"},
    {495,"Sandking","Sedan/SUV"},{500,"Mesa","Sedan/SUV"},{507,"Elegant","Sedan/SUV"},{516,"Nebula","Sedan/SUV"},
    {517,"Majestic","Sedan/SUV"},{518,"Buccaneer","Sedan/SUV"},{526,"Fortune","Sedan/SUV"},{527,"Cadrona","Sedan/SUV"},
    {529,"Willard","Sedan/SUV"},{534,"Remington","Sedan/SUV"},{535,"Slamvan","Sedan/SUV"},{536,"Blade","Sedan/SUV"},
    {540,"Vincent","Sedan/SUV"},{542,"Clover","Sedan/SUV"},{546,"Intruder","Sedan/SUV"},{547,"Primo","Sedan/SUV"},
    {549,"Tampa","Sedan/SUV"},{550,"Sunrise","Sedan/SUV"},{551,"Merit","Sedan/SUV"},{554,"Yosemite","Sedan/SUV"},
    {561,"Stratum","Sedan/SUV"},{566,"Tahoma","Sedan/SUV"},{567,"Savanna","Sedan/SUV"},{575,"Broadway","Sedan/SUV"},
    {576,"Tornado","Sedan/SUV"},{579,"Huntley","Sedan/SUV"},{580,"Stafford","Sedan/SUV"},{585,"Emperor","Sedan/SUV"},
    {600,"Picador","Sedan/SUV"},{412,"Voodoo","Sedan/SUV"},{419,"Esperanto","Sedan/SUV"},{422,"Bobcat","Sedan/SUV"},
    {543,"Sadler","Sedan/SUV"},{478,"Walton","Sedan/SUV"},{418,"Moonbeam","Sedan/SUV"},

    {416,"Ambulance","Emergency"},{407,"Fire Truck","Emergency"},{544,"Fire Truck (ladder)","Emergency"},
    {596,"Police LS","Emergency"},{597,"Police SF","Emergency"},{598,"Police LV","Emergency"},{599,"Police Ranger","Emergency"},
    {523,"HPV1000","Emergency"},{427,"Enforcer","Emergency"},{490,"FBI Rancher","Emergency"},{528,"FBI Truck","Emergency"},
    {601,"S.W.A.T.","Emergency"},{432,"Rhino","Emergency"},{433,"Barracks","Emergency"},{470,"Patriot","Emergency"},

    {403,"Linerunner","Industrial"},{406,"Dumper","Industrial"},{408,"Trashmaster","Industrial"},{413,"Pony","Industrial"},
    {414,"Mule","Industrial"},{423,"Mr Whoopee","Industrial"},{428,"Securicar","Industrial"},{431,"Bus","Industrial"},
    {437,"Coach","Industrial"},{440,"Rumpo","Industrial"},{443,"Packer","Industrial"},{455,"Flatbed","Industrial"},
    {456,"Yankee","Industrial"},{459,"RC Van","Industrial"},{482,"Burrito","Industrial"},{483,"Camper","Industrial"},
    {486,"Dozer","Industrial"},{498,"Boxville","Industrial"},{499,"Benson","Industrial"},{508,"Journey","Industrial"},
    {514,"Tanker","Industrial"},{515,"Roadtrain","Industrial"},{524,"Cement Truck","Industrial"},{525,"Tow Truck","Industrial"},
    {530,"Forklift","Industrial"},{531,"Tractor","Industrial"},{532,"Combine Harvester","Industrial"},{552,"Utility Van","Industrial"},
    {573,"Dune","Industrial"},{574,"Sweeper","Industrial"},{578,"DFT-30","Industrial"},{582,"News Van","Industrial"},
    {583,"Tug","Industrial"},{588,"Hotdog","Industrial"},{609,"Boxburg","Industrial"},{420,"Taxi","Industrial"},
    {438,"Cabbie","Industrial"},{442,"Romero","Industrial"},{457,"Caddy","Industrial"},{485,"Baggage","Industrial"},
    {572,"Mower","Industrial"},{571,"Kart","Industrial"},{444,"Monster","Industrial"},{556,"Monster A","Industrial"},
    {557,"Monster B","Industrial"},{504,"Bloodring Banger","Industrial"},{568,"Bandito","Industrial"},{424,"BF Injection","Industrial"},
    {471,"Quad","Industrial"},{539,"Vortex","Industrial"},

    {461,"PCJ-600","Bikes"},{462,"Faggio","Bikes"},{463,"Freeway","Bikes"},{468,"Sanchez","Bikes"},{521,"FCR-900","Bikes"},
    {522,"NRG-500","Bikes"},{581,"BF-400","Bikes"},{586,"Wayfarer","Bikes"},{448,"Pizzaboy","Bikes"},
    {481,"BMX","Bikes"},{509,"Bike","Bikes"},{510,"Mountain Bike","Bikes"},

    {425,"Hunter","Air"},{520,"Hydra","Air"},{417,"Leviathan","Air"},{447,"Seasparrow","Air"},{469,"Sparrow","Air"},
    {487,"Maverick","Air"},{488,"News Chopper","Air"},{497,"Police Maverick","Air"},{548,"Cargobob","Air"},
    {563,"Raindance","Air"},{460,"Skimmer","Air"},{476,"Rustler","Air"},{511,"Beagle","Air"},{512,"Cropduster","Air"},
    {513,"Stuntplane","Air"},{519,"Shamal","Air"},{553,"Nevada","Air"},{577,"AT-400","Air"},{592,"Andromada","Air"},
    {593,"Dodo","Air"},{441,"RC Bandit","Air"},{464,"RC Baron","Air"},{465,"RC Raider","Air"},{501,"RC Goblin","Air"},
    {564,"RC Tiger","Air"},

    {430,"Predator","Boats"},{446,"Squalo","Boats"},{452,"Speeder","Boats"},{453,"Reefer","Boats"},{454,"Tropic","Boats"},
    {472,"Coastguard","Boats"},{473,"Dinghy","Boats"},{484,"Marquis","Boats"},{493,"Jetmax","Boats"},{595,"Launch","Boats"},
};

inline const char* kVehicleCategories[] = { "All", "Sports", "Sedan/SUV", "Emergency", "Industrial", "Bikes", "Air", "Boats" };

inline const WeaponEntry kWeapons[] = {
    {1,"Brass Knuckles","Melee",1},{2,"Golf Club","Melee",1},{3,"Nightstick","Melee",1},{4,"Knife","Melee",1},
    {5,"Baseball Bat","Melee",1},{6,"Shovel","Melee",1},{7,"Pool Cue","Melee",1},{8,"Katana","Melee",1},
    {9,"Chainsaw","Melee",1},{15,"Cane","Melee",1},{14,"Flowers","Melee",1},{10,"Dildo","Melee",1},
    {22,"Pistol","Pistols",500},{23,"Silenced Pistol","Pistols",500},{24,"Desert Eagle","Pistols",500},
    {25,"Shotgun","Shotguns",200},{26,"Sawn-off","Shotguns",200},{27,"Combat Shotgun","Shotguns",200},
    {28,"Micro Uzi","SMGs",1000},{29,"MP5","SMGs",1000},{32,"Tec-9","SMGs",1000},
    {30,"AK-47","Rifles",1000},{31,"M4","Rifles",1000},{33,"Country Rifle","Rifles",200},{34,"Sniper Rifle","Rifles",200},
    {35,"Rocket Launcher","Heavy",50},{36,"Heat-Seeker","Heavy",50},{37,"Flamethrower","Heavy",2000},{38,"Minigun","Heavy",5000},
    {16,"Grenade","Thrown",50},{17,"Tear Gas","Thrown",50},{18,"Molotov","Thrown",50},{39,"Satchel Charge","Thrown",50},
    {41,"Spray Can","Misc",5000},{42,"Fire Extinguisher","Misc",5000},{43,"Camera","Misc",100},
    {44,"Night Vision","Misc",1},{45,"Thermal Goggles","Misc",1},{46,"Parachute","Misc",1},
};

inline const char* kWeaponGroups[] = { "Melee", "Pistols", "Shotguns", "SMGs", "Rifles", "Heavy", "Thrown", "Misc" };

// Z is a hint only: Game::Teleport always streams the area and snaps to ground.
inline const TeleportEntry kTeleports[] = {
    {"Grove Street (CJ's house)",  "Los Santos",    {2495.0f, -1687.0f, 13.5f}},
    {"LS International Airport",   "Los Santos",    {1682.0f, -2244.0f, 13.5f}},
    {"Santa Maria Beach",          "Los Santos",    {370.0f, -1850.0f, 7.7f}},
    {"Vinewood Sign",              "Los Santos",    {1415.0f, -805.0f, 85.0f}},
    {"Verona Beach Gym",           "Los Santos",    {664.0f, -1868.0f, 5.5f}},
    {"Doherty Garage",             "San Fierro",    {-2026.5f, 156.0f, 29.0f}},
    {"SF International Airport",   "San Fierro",    {-1420.0f, -287.0f, 14.1f}},
    {"Gant Bridge",                "San Fierro",    {-2670.0f, 1600.0f, 64.0f}},
    {"Mount Chiliad summit",       "Countryside",   {-2324.0f, -1638.0f, 483.7f}},
    {"Angel Pine",                 "Countryside",   {-2150.0f, -2440.0f, 30.6f}},
    {"Sherman Dam",                "Desert",        {-900.0f, 2000.0f, 60.0f}},
    {"Big Ear",                    "Desert",        {-360.0f, 1580.0f, 76.0f}},
    {"Area 69 (gate)",             "Desert",        {214.0f, 1860.0f, 13.0f}},
    {"Abandoned Airstrip",         "Desert",        {390.0f, 2500.0f, 16.5f}},
    {"Fort Carson",                "Desert",        {-110.0f, 1130.0f, 19.7f}},
    {"The Strip",                  "Las Venturas",  {2027.0f, 1008.0f, 10.8f}},
    {"LV Airport",                 "Las Venturas",  {1477.0f, 1300.0f, 10.8f}},
};

inline const char* kWeatherNames[] = {
    "Extra sunny LS", "Sunny LS", "Extra sunny smog LS", "Sunny smog LS", "Cloudy LS",
    "Sunny SF", "Extra sunny SF", "Cloudy SF", "Rainy SF", "Foggy SF",
    "Sunny LV", "Extra sunny LV", "Cloudy LV",
    "Extra sunny countryside", "Sunny countryside", "Cloudy countryside", "Rainy countryside",
    "Extra sunny desert", "Sunny desert", "Sandstorm desert", "Underwater",
    "Extra colours 1", "Extra colours 2",
};

inline const StatEntry kBodyStats[] = {
    {21, "Fat", 1000}, {22, "Stamina", 1000}, {23, "Muscle", 1000}, {24, "Max health", 1000},
    {25, "Sex appeal", 1000}, {68, "Respect", 1000}, {225, "Lung capacity", 1000}, {81, "Gambling", 1000},
};

inline const StatEntry kDrivingStats[] = {
    {160, "Driving skill", 1000}, {223, "Flying skill", 1000}, {229, "Bike skill", 1000}, {230, "Cycling skill", 1000},
};

inline const StatEntry kWeaponSkills[] = {
    {69, "Pistol", 1000}, {70, "Silenced pistol", 1000}, {71, "Desert Eagle", 1000}, {72, "Shotgun", 1000},
    {73, "Sawn-off", 1000}, {74, "Combat shotgun", 1000}, {75, "Micro Uzi", 1000}, {76, "MP5", 1000},
    {77, "AK-47", 1000}, {78, "M4", 1000}, {79, "Sniper rifle", 1000},
};
