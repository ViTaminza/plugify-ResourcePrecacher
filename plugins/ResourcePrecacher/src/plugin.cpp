// ResourcePrecacher — Plugify C++ port of the Pisex Metamod:Source plugin.
//
// Behaviour kept from the original:
//   * the resource list of resource_precacher.ini ("Models" block, values only) is read at plugin
//     start and registered from the manifest callback on every map load, just like the original
//     registered its own precache list from the game-session-manifest hook.
//   * the second list, precacher.ini ("Precacher" block), is read the same way. In the Pisex
//     installation that file belonged to the utils/menus plugin (its AddPrecache call); pisexcompat
//     deliberately does not cover AddPrecache (README §6), so this plugin - the one whose job is
//     precaching - owns both files now.
//
// Porting notes:
//   * s2sdk clears g_Precached at map end (Hook_DisconnectGameNow). Its manifest hook invokes
//     OnBuildGameSessionManifest listeners BEFORE copying that set into the engine manifest.
//     Keep our own list and populate s2sdk::Precache() from that callback on EVERY map load,
//     including the automatic map reload after MultiAddonManager mounts the workshop addons.
//     Startup-only registration is lost during that reload and produces ERROR models.
//   * The original's exported IResourcePrecacher interface (AddPrecache, queried through
//     MetaFactory by other plugins) is not exported: a Plugify plugin that wants to precache calls
//     s2sdk::Precache() directly, which is the same list.
//   * Config loading happens once at plugin start (the original loaded its config in Load()); a
//     plugin loaded into a running map takes effect from the next map, like the original.
//
// Fail soft: without s2sdk bindings or without the config files the plugin logs and keeps running;
// OnPluginStart never returns Failed (that stops every Plugify plugin).

#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>

#include <pisex/pisex.hpp>

#include <s2sdk/s2sdk.hpp>

#include "precache_core.hpp"

namespace {

// The plugin's own file (the original's LoadConfig path, "addons/configs/resource_precacher.ini").
constexpr const char* kResourceFile = "resource_precacher.ini";
constexpr const char* kResourceRoot = "Models";
// The utils plugin's list, shipped as addons/configs/precacher.ini.
constexpr const char* kUtilsFile = "precacher.ini";
constexpr const char* kUtilsRoot = "Precacher";

std::vector<std::string> g_resources;
bool g_manifestRegistered = false;

void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

void LogInfo(std::string_view message) {
	LogMessage(message, plg::Severity::Info);
}

void LogWarn(std::string_view message) {
	LogMessage(message, plg::Severity::Warning);
}

void LogError(std::string_view message) {
	LogMessage(message, plg::Severity::Error);
}

// Keep the configured resources independently of s2sdk's per-map set.
void LoadPrecacheFile(const char* file, const char* rootName) {
	pisex::KeyValues kv(rootName);
	if (!kv.LoadFromFile(file)) {
		LogWarn(std::string("ResourcePrecacher: failed to load ") + file);
		return;
	}
	const auto resources = precacher::CollectPrecacheList(kv);
	g_resources.insert(g_resources.end(), resources.begin(), resources.end());
	LogInfo("ResourcePrecacher: loaded " + std::to_string(resources.size()) + " resource(s) from " + file);
}

void OnBuildManifest() {
	if (!__s2sdk_Precache) return;
	int count = 0;
	for (const std::string& resource : g_resources) {
		try {
			s2sdk::Precache(plg::string(resource));
			++count;
		} catch (const std::exception& ex) {
			LogError(std::string("ResourcePrecacher: Precache(\"") + resource + "\") failed: " + ex.what());
		} catch (...) {
			LogError(std::string("ResourcePrecacher: Precache(\"") + resource + "\") failed");
		}
	}
	LogInfo("ResourcePrecacher: registered " + std::to_string(count) + " resource(s) for game session manifest");
}

class ResourcePrecacherPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override {
		try {
			pisex::Init("ResourcePrecacher");
			g_resources.clear();
			LoadPrecacheFile(kResourceFile, kResourceRoot);
			LoadPrecacheFile(kUtilsFile, kUtilsRoot);
			if (!__s2sdk_Precache || !__s2sdk_OnBuildGameSessionManifest_Register ||
				!__s2sdk_OnBuildGameSessionManifest_Unregister) {
				LogError("ResourcePrecacher: required s2sdk precache/manifest bindings are missing - precaching disabled");
				return {};
			}
			s2sdk::OnBuildGameSessionManifest_Register(&OnBuildManifest);
			g_manifestRegistered = true;
			LogInfo("ResourcePrecacher: ready (" + std::to_string(g_resources.size()) +
					" resource(s), registered manifest listener)");
		} catch (const std::exception& ex) {
			LogError(std::string("ResourcePrecacher: OnPluginStart failed: ") + ex.what());
		} catch (...) {
			LogError("ResourcePrecacher: OnPluginStart failed with an unknown exception");
		}
		return {}; // never Failed
	}

	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds) override {
		try {
			pisex::Poll();
		} catch (...) {
			LogError("ResourcePrecacher: Poll() threw");
		}
		return {};
	}

	plg::PluginResult OnPluginEnd() override {
		try {
			if (g_manifestRegistered) {
				s2sdk::OnBuildGameSessionManifest_Unregister(&OnBuildManifest);
				g_manifestRegistered = false;
			}
			g_resources.clear();
			pisex::Shutdown();
		} catch (const std::exception& ex) {
			LogError(std::string("ResourcePrecacher: OnPluginEnd failed: ") + ex.what());
		} catch (...) {
			LogError("ResourcePrecacher: OnPluginEnd failed with an unknown exception");
		}
		return {};
	}
};

ResourcePrecacherPlugin g_resourcePrecacherPlugin;

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_resourcePrecacherPlugin)
