// Tests for plugins/ResourcePrecacher: the engine-free list extraction (src/precache_core.hpp)
// against the two *live* config files of the production server
// (ref/prod-27016/addons/configs/resource_precacher.ini and precacher.ini), plus the parser edge
// cases (comments, key names that do not matter, empty values, nested blocks).
//
// The real plugin is linked below; stubs model s2sdk clearing its set at map end and calling
// manifest listeners before it copies that set into the next game session manifest.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <plg/plugin.hpp>

#include <pisex/keyvalues.hpp>
#include <pisex/paths.hpp>
#include <s2sdk/s2sdk.hpp>

#include "precache_core.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEq(int actual, int expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s\n  expected: %d\n  actual:   %d\n", what.c_str(), expected, actual);
	}
}

void CheckEqStr(const std::string& actual, const std::string& expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n", what.c_str(), expected.c_str(),
					actual.c_str());
	}
}

std::string ReadFileOrEmpty(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return {};
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool LoadText(pisex::KeyValues& kv, const std::string& text) {
	return kv.LoadFromBuffer(text);
}

void TestResourceFile() {
	const std::string path = std::string(RP_TEST_CONFIG_DIR) + "/resource_precacher.ini";
	const std::string text = ReadFileOrEmpty(path);
	Check(!text.empty(), "the live resource_precacher.ini is readable");
	pisex::KeyValues kv("Models");
	Check(LoadText(kv, text), "resource_precacher.ini parses");
	const std::vector<std::string> list = precacher::CollectPrecacheList(kv);
	CheckEq(static_cast<int>(list.size()), 2, "two resources in the live file");
	CheckEqStr(list[0], "models/props/de_dust/hr_dust/dust_windows/dust_rollupdoor_96x128_surface_lod.vmdl",
			   "first resource (value of the first key, the key name does not matter)");
	CheckEqStr(list[1],
			   "models/props/de_nuke/hr_nuke/chainlink_fence_001/chainlink_fence_001_256_capped.vmdl",
			   "second resource");
}

void TestUtilsFile() {
	const std::string path = std::string(RP_TEST_CONFIG_DIR) + "/precacher.ini";
	const std::string text = ReadFileOrEmpty(path);
	Check(!text.empty(), "the live precacher.ini is readable");
	pisex::KeyValues kv("Precacher");
	Check(LoadText(kv, text), "precacher.ini parses");
	const std::vector<std::string> list = precacher::CollectPrecacheList(kv);
	// The live file has two entries ("menu_ui" and "menu_ui_css") plus a comment block; comments
	// and the examples inside them must not produce entries.
	CheckEq(static_cast<int>(list.size()), 2, "two resources in the live utils file");
	CheckEqStr(list[0], "panorama/layout/custom_game/menu_ui.vxml", "menu_ui");
	CheckEqStr(list[1], "panorama/styles/custom_game/ui.vcss", "menu_ui_css");
}

void TestParserEdges() {
	pisex::KeyValues kv("Models");
	Check(LoadText(kv, R"(
"Models"
{
	// a comment with a "quoted" example that must not become an entry
	"1"  "models/a.vmdl"     // trailing comment
	"2"  "particles/b.vpcf"
	"empty" ""
	"nested" { "3" "soundevents/c.vsndevts" }
}
)"),
		  "edge-case file parses");
	const std::vector<std::string> list = precacher::CollectPrecacheList(kv);
	CheckEq(static_cast<int>(list.size()), 2, "values only: the empty value and the nested block are skipped");
	CheckEqStr(list[0], "models/a.vmdl", "first value");
	CheckEqStr(list[1], "particles/b.vpcf", "second value");

	// A file whose root block is missing entirely parses as empty (the plugin then warns about the
	// file only when it cannot be read at all).
	pisex::KeyValues broken("Models");
	Check(!broken.LoadFromBuffer("\"Models\" { \"1\" "), "a malformed file is rejected");
}

std::vector<s2sdk::OnBuildGameSessionManifestCallback> g_manifestListeners;
std::set<std::string> g_sdkPrecached;
std::string g_rejectedResource;

void RegisterManifest(s2sdk::OnBuildGameSessionManifestCallback callback) {
	g_manifestListeners.push_back(callback);
}

void UnregisterManifest(s2sdk::OnBuildGameSessionManifestCallback callback) {
	std::erase(g_manifestListeners, callback);
}

void Precache(const plg::string& resource) {
	const std::string path(resource.data(), resource.size());
	if (path == g_rejectedResource) {
		throw std::runtime_error("simulated precache failure");
	}
	g_sdkPrecached.insert(path);
}

void BuildNextManifest() {
	// Hook_DisconnectGameNow clears g_Precached. The build hook calls listeners FIRST.
	g_sdkPrecached.clear();
	for (const auto callback : g_manifestListeners) {
		callback();
	}
}

void TestManifestLifecycle() {
	pisex::SetConfigDir(RP_TEST_CONFIG_DIR);
	__s2sdk_Precache = &Precache;
	__s2sdk_OnBuildGameSessionManifest_Register = &RegisterManifest;
	__s2sdk_OnBuildGameSessionManifest_Unregister = &UnregisterManifest;

	std::set<std::string> expected;
	for (const auto* file : {"resource_precacher.ini", "precacher.ini"}) {
		pisex::KeyValues kv(file == std::string_view("resource_precacher.ini") ? "Models" : "Precacher");
		Check(kv.LoadFromFile(file), "lifecycle config is readable");
		for (const auto& path : precacher::CollectPrecacheList(kv)) {
			expected.insert(path);
		}
	}
	CheckEq(static_cast<int>(expected.size()), 4, "both config files supply the manifest");

	auto* plugin = plg::GetPlugin();
	(void)plugin->OnPluginStart();
	CheckEq(static_cast<int>(g_manifestListeners.size()), 1, "one manifest listener is registered");
	for (int map = 0; map < 3; ++map) {
		BuildNextManifest();
		Check(g_sdkPrecached == expected, "all resources survive map transition " + std::to_string(map));
	}

	// A bad resource must not prevent the remaining entries from reaching the manifest.
	g_rejectedResource = *expected.begin();
	BuildNextManifest();
	auto remaining = expected;
	remaining.erase(g_rejectedResource);
	Check(g_sdkPrecached == remaining, "a precache exception is isolated to one resource");
	g_rejectedResource.clear();
	BuildNextManifest();
	Check(g_sdkPrecached == expected, "failed resource is retried on the next manifest");

	(void)plugin->OnPluginEnd();
	Check(g_manifestListeners.empty(), "unload removes callbacks into plugin code");
	BuildNextManifest();
	Check(g_sdkPrecached.empty(), "unloaded plugin no longer populates manifests");
	(void)plugin->OnPluginStart();
	CheckEq(static_cast<int>(g_manifestListeners.size()), 1, "reload registers exactly one listener");
	BuildNextManifest();
	Check(g_sdkPrecached == expected, "reload restores resources from both config files");
	(void)plugin->OnPluginEnd();

	__s2sdk_OnBuildGameSessionManifest_Unregister = nullptr;
	(void)plugin->OnPluginStart();
	Check(g_manifestListeners.empty(), "missing teardown binding does not leave an unsafe listener");
	(void)plugin->OnPluginEnd();
	__s2sdk_OnBuildGameSessionManifest_Register = nullptr;
	__s2sdk_Precache = nullptr;
	pisex::SetConfigDir("");
}

} // namespace

int main() {
	TestResourceFile();
	TestUtilsFile();
	TestParserEdges();
	TestManifestLifecycle();
	std::printf("resource_precacher_tests: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
