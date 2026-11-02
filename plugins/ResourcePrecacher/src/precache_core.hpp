// precache_core — the engine-free part of the ResourcePrecacher port: turning a Metamod
// installation's resource_precacher.ini / precacher.ini into the list of resources to register in a
// game session manifest.
//
// Header-only and self-contained on purpose (PLUGIN_STANDARD §7: no pisexcompat, no helper
// library). The production files are the originals, unchanged - addons/configs/resource_precacher.ini
// (the plugin's own "Models" block) and addons/configs/precacher.ini (the utils/menus plugin's
// "Precacher" block) - so no conversion is needed and resource_precacher_tests parses the very same
// bytes the engine gets.
//
// 1. LoadKv1File() / ParseKv1(): a read-only Valve KeyValues (KV1) reader. The accepted grammar is
//    Valve's: quoted ("key" "value") and unquoted (key value) tokens, a key may carry a value and a
//    block, nested blocks, // and /* */ comments, \" \\ \n \r \t escapes inside quotes, and
//    #base / #include / #define lines are skipped instead of followed. A malformed file - an
//    unterminated string, a missing '}' or a stray top-level '}' - is rejected as a whole
//    (std::nullopt), so half a list never reaches the engine. A file that is one "Root" { ... }
//    block is returned as that block, like Valve's loader.
//
// 2. CollectPrecacheList(): the original's LoadConfig walked the root block's *values* - the key
//    names do not matter, only the value is a resource path:
//
//      "Models"
//      {
//          "dust_rollupdoor_96x128_surface_lod"  "models/props/de_dust/hr_dust/dust_windows/....vmdl"
//      }
//
//    The port registers the same list through s2sdk::Precache() (see plugin.cpp). The only
//    deliberate difference: an empty value is skipped. The original pushed it and handed "" to
//    IEntityResourceManifest::AddResource, which can only produce engine noise; the utils plugin's
//    list (precacher.ini) skipped empty values itself.
//
// 3. ResolveConfigFile(): where the two lists are read from, under <plg::GetConfigsDir()>. The
//    flat spelling (<configs>/<file>) is tried first, then the plugin's own directory
//    (<configs>/ResourcePrecacher/<file>) that PLUGIN_STANDARD §3 mandates and package.sh installs.

#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace precacher {

// --- 1. the KV1 reader ------------------------------------------------------------------------

struct KvNode {
	std::string name;
	std::string value;
	bool hasValue = false;
	std::vector<KvNode> children;

	// First child with that name (case-insensitive, like Valve's FindKey), or nullptr.
	const KvNode* Find(std::string_view key) const;
};

namespace detail {

inline char LowerCase(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool EqualNoCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (LowerCase(a[i]) != LowerCase(b[i])) {
			return false;
		}
	}
	return true;
}

//   file  := node* EOF
//   node  := token [ token ] [ '{' node* '}' ]
//   token := '"' escaped '"' | raw
class Parser {
public:
	explicit Parser(std::string_view text) : text_(text) {}

	bool Parse(KvNode& root) {
		ParseNodes(root, false);
		return !failed_;
	}

private:
	void SkipSpace() {
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c))) {
				++pos_;
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
				pos_ += 2;
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
				pos_ += 2;
				while (pos_ + 1 < text_.size() && !(text_[pos_] == '*' && text_[pos_ + 1] == '/')) {
					++pos_;
				}
				pos_ = (pos_ + 1 < text_.size()) ? pos_ + 2 : text_.size();
				continue;
			}
			break;
		}
	}

	// One token; false at the end of the input or at a '}'.
	bool ReadToken(std::string& out) {
		SkipSpace();
		if (pos_ >= text_.size() || text_[pos_] == '}') {
			return false;
		}
		out.clear();
		if (text_[pos_] == '"') {
			++pos_;
			while (pos_ < text_.size()) {
				const char c = text_[pos_++];
				if (c == '\\' && pos_ < text_.size()) {
					const char esc = text_[pos_++];
					switch (esc) {
					case 'n':
						out.push_back('\n');
						break;
					case 'r':
						out.push_back('\r');
						break;
					case 't':
						out.push_back('\t');
						break;
					case '"':
						out.push_back('"');
						break;
					case '\\':
						out.push_back('\\');
						break;
					default:
						out.push_back('\\');
						out.push_back(esc);
						break;
					}
					continue;
				}
				if (c == '"') {
					return true;
				}
				out.push_back(c);
			}
			failed_ = true; // unterminated string
			return true;
		}
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '"') {
				break;
			}
			if (c == '/' && pos_ + 1 < text_.size() && (text_[pos_ + 1] == '/' || text_[pos_ + 1] == '*')) {
				break;
			}
			out.push_back(c);
			++pos_;
		}
		return true;
	}

	bool PeekIs(char c) {
		SkipSpace();
		return pos_ < text_.size() && text_[pos_] == c;
	}

	// Children are appended as soon as they are complete, so the recursion below works on a local
	// node and never holds a reference into a vector that the recursion may reallocate.
	void ParseNodes(KvNode& parent, bool insideBlock) {
		while (true) {
			SkipSpace();
			if (pos_ >= text_.size()) {
				if (insideBlock) {
					failed_ = true; // missing '}'
				}
				return;
			}
			if (text_[pos_] == '}') {
				if (!insideBlock) {
					failed_ = true; // stray '}'
				}
				++pos_;
				return;
			}
			std::string key;
			if (!ReadToken(key)) {
				return;
			}
			if (!key.empty() && key[0] == '#') {
				// #base / #include / #define: no inclusion, skip the line.
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			KvNode node;
			node.name = std::move(key);
			if (!PeekIs('{')) {
				std::string value;
				if (ReadToken(value)) {
					node.value = std::move(value);
					node.hasValue = true;
				}
			}
			if (PeekIs('{')) {
				++pos_;
				ParseNodes(node, true);
			}
			parent.children.push_back(std::move(node));
		}
	}

	std::string_view text_;
	std::size_t pos_ = 0;
	bool failed_ = false;
};

} // namespace detail

inline const KvNode* KvNode::Find(std::string_view key) const {
	if (key.empty()) {
		return nullptr;
	}
	for (const KvNode& child : children) {
		if (detail::EqualNoCase(child.name, key)) {
			return &child;
		}
	}
	return nullptr;
}

inline std::optional<KvNode> ParseKv1(std::string_view text) {
	// A UTF-8 BOM would otherwise become part of the first key ("Models") and hide the file's single
	// top-level block from the unwrapping below, silently emptying the list.
	constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
	if (text.starts_with(kUtf8Bom)) {
		text.remove_prefix(kUtf8Bom.size());
	}
	KvNode root;
	detail::Parser parser(text);
	if (!parser.Parse(root)) {
		return std::nullopt;
	}
	// A single top-level block is the file's root ("Models" { ... }), as in Valve's loader.
	if (root.children.size() == 1 && !root.children.front().children.empty()) {
		KvNode top = std::move(root.children.front());
		return top;
	}
	return root;
}

inline std::optional<KvNode> LoadKv1File(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::ostringstream content;
	content << file.rdbuf();
	return ParseKv1(content.str());
}

// --- 2. the resource list ---------------------------------------------------------------------

// The values of the root block, in file order (FOR_EACH_VALUE of the original). Nested blocks and
// empty values contribute nothing.
inline std::vector<std::string> CollectPrecacheList(const KvNode& root) {
	std::vector<std::string> resources;
	resources.reserve(root.children.size());
	for (const KvNode& child : root.children) {
		if (child.hasValue && !child.value.empty()) {
			resources.push_back(child.value);
		}
	}
	return resources;
}

// --- 3. where the files are read from ---------------------------------------------------------

// The plugin's own config folder: PLUGIN_STANDARD §3 - the folder name matches the manifest name.
inline constexpr std::string_view kPluginDir = "ResourcePrecacher";

// The plugin's own list (the original's LoadConfig path, "addons/configs/resource_precacher.ini").
inline constexpr std::string_view kResourceFile = "resource_precacher.ini";
// The utils/menus plugin's list, shipped as addons/configs/precacher.ini.
inline constexpr std::string_view kUtilsFile = "precacher.ini";

inline std::string JoinPath(std::string_view base, std::string_view name) {
	if (base.empty()) {
		return std::string(name);
	}
	if (name.empty()) {
		return std::string(base);
	}
	std::string out(base);
	if (out.back() != '/') {
		out.push_back('/');
	}
	if (name.front() == '/') {
		name.remove_prefix(1);
	}
	out.append(name);
	return out;
}

inline bool FileExists(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	std::ifstream file(path, std::ios::binary);
	return file.good();
}

// <configsDir>/<fileName>, then <configsDir>/ResourcePrecacher/<fileName>. Empty when there is no
// configs directory at all (the plugin logs that once instead of warning per file).
inline std::vector<std::string> ConfigCandidates(std::string_view configsDir, std::string_view fileName) {
	std::vector<std::string> candidates;
	if (configsDir.empty() || fileName.empty()) {
		return candidates;
	}
	candidates.push_back(JoinPath(configsDir, fileName));
	candidates.push_back(JoinPath(JoinPath(configsDir, kPluginDir), fileName));
	return candidates;
}

// The first candidate that exists on disk, or "" when none does.
inline std::string ResolveConfigFile(std::string_view configsDir, std::string_view fileName) {
	for (const std::string& candidate : ConfigCandidates(configsDir, fileName)) {
		if (FileExists(candidate)) {
			return candidate;
		}
	}
	return {};
}

} // namespace precacher
