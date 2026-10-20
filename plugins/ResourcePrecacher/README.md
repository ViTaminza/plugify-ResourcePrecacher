# ResourcePrecacher

Reads `configs/ResourcePrecacher/resource_precacher.ini` (`Models`) and
`configs/ResourcePrecacher/precacher.ini` (`Precacher`) once at plugin start.
Each root value is a resource path; empty values and nested blocks are ignored.

Resources must be submitted through `s2sdk::Precache` from an
`OnBuildGameSessionManifest` listener **on every map load**. In s2sdk 2.18.2,
`Hook_DisconnectGameNow` clears `g_Precached` at map end, and
`Hook_BuildGameSessionManifest` calls listeners before adding the set to the
engine manifest. Registering only at plugin start loses the list on the automatic
map reload after workshop mounting, and on subsequent map changes.

Version 1.0.1 keeps the configured paths in the plugin and restores the list from
the manifest listener. Unloading removes the listener. Loading the plugin into
an already running map takes effect on the next map load.

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

The tests link the real plugin and model s2sdk's map-end reset followed by manifest
construction. They cover both config files, repeated map loads, a failing resource,
unload/reload, and a missing listener teardown binding. The regression produced
eight failures before the fix; all 29 checks pass after it.

In game, ResourcePrecacher 1.0.1 registers all seven configured entries at initial
load, on addon reload, and after an explicit `changelevel de_dust2`; the fence's
missing-manifest/loading errors no longer appear.
