// precache_core — the engine-free part of the ResourcePrecacher port: turning a loaded
// resource_precacher.ini / precacher.ini into the list of resources to register.
//
// The original (its LoadConfig) walked the root
// block's *values* - the key names do not matter, only the value is a resource path:
//
//   "Models" { "dust_rollupdoor_96x128_surface_lod" "models/props/.../dust_rollupdoor ....vmdl" }
//
// and inside the plugin's own hook added each of them to the game session manifest. The port
// registers the same list through s2sdk::Precache() (see plugin.cpp).
//
// The only deliberate difference: an empty value is skipped. The original pushed it and handed ""
// to IEntityResourceManifest::AddResource, which can only produce engine noise; the utils plugin's
// list (precacher.ini) skipped empty values itself.

#pragma once

#include <string>
#include <vector>

#include <pisex/keyvalues.hpp>

namespace precacher {

// Collects the values of the root block, in file order (FOR_EACH_VALUE of the original).
inline std::vector<std::string> CollectPrecacheList(pisex::KeyValues& root) {
	std::vector<std::string> resources;
	FOR_EACH_VALUE((&root), value) {
		const char* resource = value->GetString(nullptr, "");
		if (resource != nullptr && resource[0] != '\0') {
			resources.emplace_back(resource);
		}
	}
	return resources;
}

} // namespace precacher
