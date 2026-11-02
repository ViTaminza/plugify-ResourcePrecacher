// Tests for plugins/ResourcePrecacher: everything in the port that does not need an engine.
//
//   1. The self-contained KV1 reader of src/precache_core.hpp: the grammar the original's files use
//      (quoted and unquoted tokens, // and /* */ comments, escapes, nested blocks, a key that
//      carries both a value and a block, #base lines, a UTF-8 BOM) and the rejection of malformed
//      input - a half-read list must never reach the manifest.
//   2. CollectPrecacheList() against the two real config files (RP_TEST_CONFIG_DIR) and against
//      hand-written edge cases: values of the root block in file order, key names irrelevant, empty
//      values and nested blocks skipped.
//   3. ResolveConfigFile(): <configs>/<file> first, then <configs>/ResourcePrecacher/<file>, and ""
//      for an empty configs directory or a file that exists nowhere.
//   4. The real plugin lifecycle: this executable compiles src/plugin.cpp in, the __s2sdk_* globals
//      below replace the plugify bindings, and <plg::GetConfigsDir()> points at the test data. The
//      stubs model s2sdk clearing its precache set at map end and calling manifest listeners before
//      copying that set into the next game session manifest.
//
// No pisexcompat, no Plugify host, no engine: only the plugin's own code is exercised.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <plg/plugin.hpp>

#include <s2sdk/s2sdk.hpp>

#include <unistd.h>

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

void CheckEqStr(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n", what.c_str(),
					std::string(expected).c_str(), std::string(actual).c_str());
	}
}

std::string DataPath(std::string_view file) {
	return std::string(RP_TEST_CONFIG_DIR) + "/" + std::string(file);
}

// Parses a buffer that must be valid; returns an empty root when it is not (so the caller's checks
// fail loudly instead of crashing).
precacher::KvNode ParseOrEmpty(const std::string& text, const std::string& what) {
	std::optional<precacher::KvNode> root = precacher::ParseKv1(text);
	Check(root.has_value(), what + " parses");
	return root ? std::move(*root) : precacher::KvNode{};
}

// --- 1. the KV1 reader -------------------------------------------------------------------------

void TestKvReader() {
	// Quoted and unquoted tokens, both comment styles, an escape sequence, a nested block, a key
	// that carries both a value and a block, and a #base line that is not followed.
	const precacher::KvNode root = ParseOrEmpty(R"(
#base "should/not/be/read.kv"
"Models"
{
	"quoted"    "models/quoted.vmdl"   // line comment
	unquoted    models/unquoted.vmdl
	/* block comment with "quoted" "examples" */
	"escaped"   "a\"b\\c\nd\te\r"
	"both"      "a value"  { "inner" "models/inner.vmdl" }
	"nested"    { "deep" { "deeper" "x" } }
}
)",
									"the full grammar sample");
	CheckEqStr(root.name, "Models", "the single top-level block is the file's root");
	CheckEq(static_cast<int>(root.children.size()), 5, "every entry is kept (and #base dropped)");
	if (root.children.size() == 5) {
		CheckEqStr(root.children[0].name, "quoted", "quoted key");
		CheckEqStr(root.children[0].value, "models/quoted.vmdl", "quoted value");
		Check(root.children[0].hasValue, "quoted pair has a value");
		CheckEqStr(root.children[1].name, "unquoted", "unquoted key");
		CheckEqStr(root.children[1].value, "models/unquoted.vmdl", "unquoted value");
		CheckEqStr(root.children[2].name, "escaped", "a key after a block comment");
		CheckEqStr(root.children[2].value, "a\"b\\c\nd\te\r",
				   "\\\" \\\\ \\n \\t \\r escapes are decoded, other escapes stay verbatim");
		CheckEqStr(root.children[3].name, "both", "a key may carry a value and a block");
		Check(root.children[3].hasValue, "such a key keeps its value");
		CheckEqStr(root.children[3].value, "a value", "and the value is the root's entry");
		CheckEq(static_cast<int>(root.children[3].children.size()), 1, "and it keeps its block too");
		CheckEqStr(root.children[4].name, "nested", "nested block key");
		Check(!root.children[4].hasValue, "a pure block has no value");
		CheckEq(static_cast<int>(root.children[4].children.size()), 1, "the nested block has one child");
	}

	// Find() is a case-insensitive direct-child lookup, like Valve's FindKey.
	Check(root.Find("QUOTED") != nullptr, "Find is case-insensitive");
	Check(root.Find("missing") == nullptr, "Find answers nullptr for a missing key");
	if (const precacher::KvNode* nested = root.Find("nested"); nested != nullptr) {
		Check(!nested->hasValue, "a block has no value of its own");
		const precacher::KvNode* deep = nested->Find("deep");
		Check(deep != nullptr, "the nested block keeps its child");
		if (deep != nullptr) {
			const precacher::KvNode* deeper = deep->Find("deeper");
			Check(deeper != nullptr, "and that child keeps its own");
			if (deeper != nullptr) {
				CheckEqStr(deeper->value, "x", "the innermost value is parsed");
			}
		}
	}

	// A file with no block at all is a plain list of top-level nodes, not an error.
	const precacher::KvNode flat = ParseOrEmpty("\"1\" \"models/a.vmdl\"\n\"2\" \"models/b.vmdl\"\n",
												"a block-less file");
	CheckEq(static_cast<int>(flat.children.size()), 2, "a block-less file keeps its entries");

	// An empty file is valid and empty (the plugin then registers nothing for it).
	const precacher::KvNode empty = ParseOrEmpty("   \n// nothing here\n/* nor here */\n", "an empty file");
	Check(empty.children.empty(), "an empty file has no entries");

	// A UTF-8 BOM must not swallow the root block ("Models" would otherwise become an extra key).
	const precacher::KvNode bom = ParseOrEmpty("\xEF\xBB\xBF\"Models\" { \"1\" \"models/bom.vmdl\" }",
											   "a BOM-prefixed file");
	CheckEqStr(bom.name, "Models", "a UTF-8 BOM is skipped");
	CheckEq(static_cast<int>(bom.children.size()), 1, "and the block is still unwrapped");

	// Malformed input is rejected as a whole: an unterminated string, a missing '}' and a stray '}'.
	Check(!precacher::ParseKv1("\"Models\" { \"1\" \"models/a.vmdl\" ").has_value(),
		  "a missing '}' is rejected");
	Check(!precacher::ParseKv1("\"Models\" { \"1\" \"unterminated.vmdl }").has_value(),
		  "an unterminated string is rejected");
	Check(!precacher::ParseKv1("\"Models\" { }\n}\n").has_value(), "a stray top-level '}' is rejected");
}

// --- 2. the list --------------------------------------------------------------------------------

void TestResourceFile() {
	const std::string path = DataPath(precacher::kResourceFile);
	const std::optional<precacher::KvNode> root = precacher::LoadKv1File(path);
	Check(root.has_value(), "the live resource_precacher.ini is readable: " + path);
	if (!root) {
		return;
	}
	CheckEqStr(root->name, "Models", "the live file keeps its Models root");
	const std::vector<std::string> list = precacher::CollectPrecacheList(*root);
	CheckEq(static_cast<int>(list.size()), 2, "two resources in the live file");
	if (list.size() == 2) {
		CheckEqStr(list[0], "models/props/de_dust/hr_dust/dust_windows/dust_rollupdoor_96x128_surface_lod.vmdl",
				   "first resource (value of the first key, the key name does not matter)");
		CheckEqStr(list[1],
				   "models/props/de_nuke/hr_nuke/chainlink_fence_001/chainlink_fence_001_256_capped.vmdl",
				   "second resource");
	}
}

void TestUtilsFile() {
	const std::string path = DataPath(precacher::kUtilsFile);
	const std::optional<precacher::KvNode> root = precacher::LoadKv1File(path);
	Check(root.has_value(), "the live precacher.ini is readable: " + path);
	if (!root) {
		return;
	}
	const std::vector<std::string> list = precacher::CollectPrecacheList(*root);
	// The real file has two entries ("menu_ui" and "menu_ui_css") plus a comment block; comments and
	// the examples inside them must not produce entries.
	CheckEq(static_cast<int>(list.size()), 2, "two resources in the live utils file");
	if (list.size() == 2) {
		CheckEqStr(list[0], "panorama/layout/custom_game/menu_ui.vxml", "menu_ui");
		CheckEqStr(list[1], "panorama/styles/custom_game/ui.vcss", "menu_ui_css");
	}
}

void TestCollectEdges() {
	const precacher::KvNode root = ParseOrEmpty(R"(
"Models"
{
	// a comment with a "quoted" "example" that must not become an entry
	"1"  "models/a.vmdl"     // trailing comment
	"2"  "particles/b.vpcf"
	"empty" ""
	"nested" { "3" "soundevents/c.vsndevts" }
	"valued_block" "models/kept.vmdl" { "inner" "models/skipped.vmdl" }
}
)",
											  "the edge-case file");
	const std::vector<std::string> list = precacher::CollectPrecacheList(root);
	// The empty value, the nested block and the block's inner value contribute nothing; a key that
	// carries a value *and* a block does contribute its own value (the original's FOR_EACH_VALUE).
	CheckEq(static_cast<int>(list.size()), 3, "only the root values are collected");
	if (list.size() == 3) {
		CheckEqStr(list[0], "models/a.vmdl", "first value, in file order");
		CheckEqStr(list[1], "particles/b.vpcf", "second value");
		CheckEqStr(list[2], "models/kept.vmdl", "a value next to a block is kept");
	}

	// Nothing to collect: a file whose root block only holds blocks.
	const precacher::KvNode blocksOnly = ParseOrEmpty("\"Models\" { \"a\" { \"b\" \"c\" } }",
													  "a block-only file");
	Check(precacher::CollectPrecacheList(blocksOnly).empty(), "a block-only file yields no resource");
	Check(!precacher::LoadKv1File(DataPath("does_not_exist.ini")).has_value(),
		  "a missing file is reported instead of parsed");
}

// --- 3. config file resolution ------------------------------------------------------------------

void TestResolution() {
	const std::string dataDir(RP_TEST_CONFIG_DIR);

	// The live files sit in the flat layout, which is the first candidate.
	CheckEqStr(precacher::ResolveConfigFile(dataDir, precacher::kResourceFile),
			   dataDir + "/" + std::string(precacher::kResourceFile), "the flat resource_precacher.ini is found");
	CheckEqStr(precacher::ResolveConfigFile(dataDir, precacher::kUtilsFile),
			   dataDir + "/" + std::string(precacher::kUtilsFile), "the flat precacher.ini is found");
	CheckEqStr(precacher::ResolveConfigFile(dataDir + "/", precacher::kUtilsFile),
			   dataDir + "/" + std::string(precacher::kUtilsFile), "a trailing slash does not double up");

	CheckEq(static_cast<int>(precacher::ConfigCandidates(dataDir, precacher::kUtilsFile).size()), 2,
			"two candidates per file");
	CheckEqStr(precacher::ResolveConfigFile(dataDir, "does_not_exist.ini"), "",
			   "a file that exists nowhere resolves to nothing");
	Check(precacher::ConfigCandidates("", precacher::kUtilsFile).empty(),
		  "no configs directory means no candidates");
	CheckEqStr(precacher::ResolveConfigFile("", precacher::kUtilsFile), "",
			   "no configs directory resolves to nothing");

	// The plugin's own directory is the second candidate (PLUGIN_STANDARD §3 / package.sh), and the
	// flat spelling still wins when both exist.
	std::error_code err;
	const std::filesystem::path tempRoot =
			std::filesystem::temp_directory_path(err) /
			("resource_precacher_tests_" + std::to_string(::getpid()));
	if (err) {
		Check(false, "the temporary directory is available: " + err.message());
		return;
	}
	std::filesystem::remove_all(tempRoot, err);
	std::filesystem::create_directories(tempRoot / std::string(precacher::kPluginDir), err);
	if (!std::filesystem::is_directory(tempRoot / std::string(precacher::kPluginDir))) {
		Check(false, "the throwaway configs directory was created");
		return;
	}

	const std::filesystem::path nested = tempRoot / std::string(precacher::kPluginDir) / "nested.ini";
	{
		std::ofstream out(nested, std::ios::binary | std::ios::trunc);
		out << "\"Models\" { \"1\" \"models/nested.vmdl\" }\n";
	}
	const std::string nestedResolved = precacher::ResolveConfigFile(tempRoot.string(), "nested.ini");
	CheckEqStr(nestedResolved, nested.string(), "the ResourcePrecacher/ layout is found");
	if (!nestedResolved.empty()) {
		const std::optional<precacher::KvNode> root = precacher::LoadKv1File(nestedResolved);
		Check(root.has_value(), "the file in the plugin directory is readable");
		if (root) {
			const std::vector<std::string> list = precacher::CollectPrecacheList(*root);
			CheckEq(static_cast<int>(list.size()), 1, "and yields its resource");
			if (list.size() == 1) {
				CheckEqStr(list[0], "models/nested.vmdl", "nested.vmdl path");
			}
		}
	}

	const std::filesystem::path flat = tempRoot / "nested.ini";
	{
		std::ofstream out(flat, std::ios::binary | std::ios::trunc);
		out << "\"Models\" { \"1\" \"models/flat.vmdl\" }\n";
	}
	CheckEqStr(precacher::ResolveConfigFile(tempRoot.string(), "nested.ini"), flat.string(),
			   "the flat file wins over the plugin-directory one");
	std::filesystem::remove_all(tempRoot, err);
}

// --- 4. the plugin lifecycle --------------------------------------------------------------------

std::vector<s2sdk::OnBuildGameSessionManifestCallback> g_manifestListeners;
std::set<std::string> g_sdkPrecached;
std::string g_rejectedResource;
std::string g_configsDir;

plg::string TestConfigsDir() {
	return plg::string(g_configsDir.c_str());
}

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

// Every resource both config files provide, read through the same code the plugin runs.
std::set<std::string> ExpectedResources(const std::string& configsDir) {
	std::set<std::string> expected;
	for (const std::string_view file : {precacher::kResourceFile, precacher::kUtilsFile}) {
		const std::string path = precacher::ResolveConfigFile(configsDir, file);
		Check(!path.empty(), "lifecycle config is readable: " + std::string(file));
		const std::optional<precacher::KvNode> root = precacher::LoadKv1File(path);
		Check(root.has_value(), "lifecycle config parses: " + std::string(file));
		if (!root) {
			continue;
		}
		for (const std::string& resource : precacher::CollectPrecacheList(*root)) {
			expected.insert(resource);
		}
	}
	return expected;
}

void TestManifestLifecycle() {
	g_configsDir.assign(RP_TEST_CONFIG_DIR);
	plg::GetConfigsDir = &TestConfigsDir;
	__s2sdk_Precache = &Precache;
	__s2sdk_OnBuildGameSessionManifest_Register = &RegisterManifest;
	__s2sdk_OnBuildGameSessionManifest_Unregister = &UnregisterManifest;

	const std::set<std::string> expected = ExpectedResources(g_configsDir);
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
	std::set<std::string> remaining = expected;
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
	CheckEq(static_cast<int>(g_manifestListeners.size()), 0, "the second unload removes the listener too");

	// Without a teardown binding the plugin must not leave a listener it cannot remove.
	__s2sdk_OnBuildGameSessionManifest_Unregister = nullptr;
	(void)plugin->OnPluginStart();
	Check(g_manifestListeners.empty(), "missing teardown binding does not leave an unsafe listener");
	(void)plugin->OnPluginEnd();
	Check(g_manifestListeners.empty(), "and unloading it is a no-op");
	__s2sdk_OnBuildGameSessionManifest_Unregister = &UnregisterManifest;

	// Without a configs directory the plugin still starts and registers its listener; it just has
	// nothing to precache (fail soft - a missing mount must not disable the plugin).
	plg::GetConfigsDir = nullptr;
	(void)plugin->OnPluginStart();
	CheckEq(static_cast<int>(g_manifestListeners.size()), 1, "the listener is registered without a configs dir");
	BuildNextManifest();
	Check(g_sdkPrecached.empty(), "and nothing is precached from a configs dir that does not exist");
	(void)plugin->OnPluginEnd();

	// Without the s2sdk bindings the plugin logs and stays unloaded-clean instead of crashing.
	plg::GetConfigsDir = &TestConfigsDir;
	__s2sdk_OnBuildGameSessionManifest_Register = nullptr;
	__s2sdk_Precache = nullptr;
	(void)plugin->OnPluginStart();
	Check(g_manifestListeners.empty(), "missing s2sdk bindings register nothing");
	(void)plugin->OnPluginEnd();
	Check(g_manifestListeners.empty(), "and unload stays clean");
	g_configsDir.clear();
}

} // namespace

int main() {
	TestKvReader();
	TestResourceFile();
	TestUtilsFile();
	TestCollectEdges();
	TestResolution();
	TestManifestLifecycle();
	std::printf("resource_precacher_tests: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
