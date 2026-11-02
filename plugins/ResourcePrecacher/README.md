# ResourcePrecacher

Version 1.1.0 — rewritten onto pure `s2sdk` (PLUGIN_STANDARD §7): no `pisexcompat`, no guard
library. The Valve KeyValues reader and the config path resolution are part of the plugin
(`src/precache_core.hpp`), and `s2sdk` is the only dependency left in the manifest (the optional
`hudmenu` entry and its `__hudmenu_*` export are gone).

Reads `resource_precacher.ini` (`Models`) and `precacher.ini` (`Precacher`) once at plugin start.
Each file is looked up as `<configs>/<file>` first and as `<configs>/ResourcePrecacher/<file>`
second, where `<configs>` is `plg::GetConfigsDir()`; the second spelling is the layout `package.sh`
installs and PLUGIN_STANDARD §3 mandates. Each root value is a resource path; empty values and
nested blocks are ignored.

Resources must be submitted through `s2sdk::Precache` from an
`OnBuildGameSessionManifest` listener **on every map load**. In s2sdk 2.18.2,
`Hook_DisconnectGameNow` clears `g_Precached` at map end, and
`Hook_BuildGameSessionManifest` calls listeners before adding the set to the
engine manifest. Registering only at plugin start loses the list on the automatic
map reload after workshop mounting, and on subsequent map changes.

The plugin keeps the configured paths in its own list and restores it from the manifest listener.
There is no timer, no map listener and no `OnPluginUpdate`: the only `s2sdk` calls are the
register/unregister pair and `Precache()` inside the callback. Unloading removes the listener.
Loading the plugin into an already running map takes effect on the next map load.

This fixes the built-in Nuke fence
`models/props/de_nuke/hr_nuke/chainlink_fence_001/chainlink_fence_001_256_capped.vmdl`
appearing as ERROR on Dust2 despite being present in game content. No extra model
files or changes to the configured path are needed.

Validation:

```bash
./build.sh plugify-ResourcePrecacher resource_precacher_tests
docker run --rm -v "$PWD":/src -w /src \
  --entrypoint /src/build/tests/resource_precacher_tests plugify-builder:1
```

The tests compile `src/plugin.cpp` in, so they run the plugin's own code: the KV1 grammar (quoted and
unquoted tokens, `//` and `/* */` comments, escapes, nested blocks, a `#base` line, a UTF-8 BOM) and
the rejection of malformed input, the two real config files, both config lookups, and the whole
lifecycle with stubbed `s2sdk` bindings — s2sdk's map-end reset followed by manifest construction,
repeated map loads, a failing resource, unload/reload, a missing teardown binding, a missing configs
directory and missing bindings. All 84 checks pass.
