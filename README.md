# ResourcePrecacher (Plugify)

Adds the resources listed in resource_precacher.ini (and in precacher.ini, the utils plugin's list of the Metamod server) to the game session manifest of every map load, so clients download them early. A port of the Metamod:Source plugin 'ResourcePrecacher' by Pisex && komashchenko; the resource files keep their original layout.

Plugify port (fork) of **ResourcePrecacher** by Pisex && komashchenko (Metamod (C++)).
Original: https://github.com/Pisex/ResourcePrecacher
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, hudmenu (optional)

## Installation
Download `ResourcePrecacher-1.0.1.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/ResourcePrecacher`.

## License
GPL-3.0, see LICENSE.

