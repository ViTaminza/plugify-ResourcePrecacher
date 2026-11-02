// ResourcePrecacher — Plugify C++ plugin: port of the Pisex Metamod:Source plugin.
//
// Behaviour kept from the original:
//   * the resource list of resource_precacher.ini ("Models" block, values only) is read at plugin
//     start and registered from the manifest callback on every map load, just like the original
//     registered its own precache list from the game-session-manifest hook.
//   * the second list, precacher.ini ("Precacher" block), is read the same way. In the Metamod
//     installation that file belonged to the utils/menus plugin (its AddPrecache call), so this
//     plugin - the one whose job is precaching - owns both files now.
//   * the original's exported IResourcePrecacher interface (AddPrecache, queried through MetaFactory
//     by other plugins) is not exported: a Plugify plugin that wants to precache calls
//     s2sdk::Precache() directly, which is the same list.
//
// This plugin calls s2sdk directly - no pisexcompat, no helper library (PLUGIN_STANDARD §7). The
// engine-free parts - the KV1 reader, the list extraction and the config path resolution - live in
// src/precache_core.hpp, and resource_precacher_tests runs exactly that code.
//
// Porting notes:
//   * s2sdk clears g_Precached at map end (Hook_DisconnectGameNow). Its manifest hook invokes
//     OnBuildGameSessionManifest listeners BEFORE copying that set into the engine manifest. Keep
//     our own list and populate s2sdk::Precache() from that callback on EVERY map load, including
//     the automatic map reload after MultiAddonManager mounts the workshop addons. Startup-only
//     registration is lost during that reload and produces ERROR models.
//   * Config loading happens once at plugin start (the original loaded its config in Load()); a
//     plugin loaded into a running map takes effect from the next map, like the original. The files
//     are looked up under <plg::GetConfigsDir()> (see precache_core.hpp).
//   * Nothing is polled per frame: there is no timer, no map listener and no OnPluginUpdate - the
//     only s2sdk calls are the register/unregister pair and Precache() inside the manifest callback,
//     which s2sdk invokes while the engine is building the manifest.
//
// Fail soft: without s2sdk bindings or without the config files the plugin logs and keeps running;
// OnPluginStart never returns Failed (that stops every Plugify plugin).

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>

#include <s2sdk/s2sdk.hpp>

#include "precache_core.hpp"

namespace {

// Keep the configured resources independently of s2sdk's per-map set.
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

// <plg::GetConfigsDir()>; "" when the plugin runs without the Plugify host.
std::string ConfigsDir() {
	if (plg::GetConfigsDir == nullptr) {
		return {};
	}
	return std::string(std::string_view(plg::GetConfigsDir()));
}

// Reads one list into g_resources. A missing or malformed file only warns: the other list and the
// plugin itself keep working.
void LoadPrecacheFile(std::string_view configsDir, std::string_view file) {
	if (configsDir.empty()) {
		return; // already reported once by OnPluginStart
	}
	const std::string path = precacher::ResolveConfigFile(configsDir, file);
	if (path.empty()) {
		LogWarn("ResourcePrecacher: no " + std::string(file) + " in " + std::string(configsDir) +
				" (checked the configs root and " + std::string(precacher::kPluginDir) + "/) - list skipped");
		return;
	}
	const std::optional<precacher::KvNode> root = precacher::LoadKv1File(path);
	if (!root) {
		LogWarn("ResourcePrecacher: failed to parse " + path + " - list skipped");
		return;
	}
	const std::vector<std::string> resources = precacher::CollectPrecacheList(*root);
	g_resources.insert(g_resources.end(), resources.begin(), resources.end());
	LogInfo("ResourcePrecacher: loaded " + std::to_string(resources.size()) + " resource(s) from " + path);
}

void OnBuildManifest() {
	if (__s2sdk_Precache == nullptr) {
		return;
	}
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
			g_resources.clear();
			const std::string configsDir = ConfigsDir();
			if (configsDir.empty()) {
				LogWarn("ResourcePrecacher: no Plugify configs directory - no resource can be configured");
			}
			LoadPrecacheFile(configsDir, precacher::kResourceFile);
			LoadPrecacheFile(configsDir, precacher::kUtilsFile);
			if (__s2sdk_Precache == nullptr || __s2sdk_OnBuildGameSessionManifest_Register == nullptr ||
				__s2sdk_OnBuildGameSessionManifest_Unregister == nullptr) {
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

	plg::PluginResult OnPluginEnd() override {
		try {
			// Nothing may point into this library after `plg reload` (PITFALLS A7): the manifest
			// listener goes first, then the list it fed.
			if (g_manifestRegistered) {
				if (__s2sdk_OnBuildGameSessionManifest_Unregister != nullptr) {
					s2sdk::OnBuildGameSessionManifest_Unregister(&OnBuildManifest);
				}
				g_manifestRegistered = false;
			}
			g_resources.clear();
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
