# ResourcePrecacher (Plugify)

Adds the resources listed in resource_precacher.ini (and in precacher.ini, the utils plugin's list of the Metamod server) to the game session manifest of every map load, so clients download them early. A port of the Metamod:Source plugin 'ResourcePrecacher' by Pisex && komashchenko; the resource files keep their original layout.

Plugify port (fork) of **ResourcePrecacher** by Pisex && komashchenko (Metamod (C++)).
Original: https://github.com/Pisex/ResourcePrecacher
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.1.0
- Plugify dependencies: s2sdk (>=2.18.2)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Installation
Download `ResourcePrecacher-1.1.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/ResourcePrecacher`.

## License
GPL-3.0, see LICENSE.

