# half_file_steam_emulator

This is a fork of [Goldberg steam emulator](https://gitlab.com/Mr_Goldberg/goldberg_emulator/) with the aim to make half-life based games server browser work like the original one. Currently it works only on Linux, but I plan to add windows support soon. For a readme on how to use it see: [The Release Readme](Readme_release.txt)

## How to use
The usage instruction doesn't differ from the original emulator

Replace the steam_api(64).dll (libsteam_api.so on linux) from the game with mine. For linux make sure that if the original api is 32 bit you use a 32 bit build and if it's 64 bit you use a 64 bit build.

Put a steam_appid.txt file that contains the appid of the game right beside it if there isn't one already.

If your game has an original steam_api(64).dll or libsteam_api.so older than may 2016 (On windows: Properties->Digital Signatures->Timestamp) you might have to add a steam_interfaces.txt beside my emulator library if the game isn't working. There is a linux script to generate it in the scripts folder of this repo.

I also recommend adding local_save.txt to the game directory and writing a folder name (e.g. "steam_data") to save your steam data locally for each game.

For more information see: [The Release Readme](Readme_release.txt)

## Download Binaries
You can download stable builds from the [release section](https://github.com/OleksandrChornyi2010/half_life_steam_emulator/releases) of this repository.

## Features

- Emulator can now work with favorite and history servers saved locally.
- Getting internet servers will be implemented soon.
- Windows support will also be added.

## Contributions

All contributions are welcome. Try to make your code work on both Linux and Windows.

If you want a particular game to be supported by the emulator, create an issue in [the issues tab](https://github.com/OleksandrChornyi2010/half_life_steam_emulator/issues) with an <span style="
    color: #f06292; 
    border: 1px solid #f06292; 
    background-color: transparent; 
    padding: 2px 12px; 
    border-radius: 20px; 
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif;
    font-size: 12px; 
    font-weight: 600; 
    display: inline-block;
    line-height: 1.5;
">
  add game support
</span> label

## Building
These are instructions for the steam_api build target. Other targets are not required for half-life.

### Linux
#### 1. Requiriments:
You need protobuf-lite (dev package) and protoc.

##### Critical: For Half-Life based games, you must have the 32-bit versions of these libraries
- Currently, version 5.28.1 of protobuf is used, but you can try using higher versions.
- Protobuf and it's dependencies must be installed into _/usr/lib32/_ as a static library (.a) so the resulting emulator won't depend on system libs.

#### 2. Building using CMake Presets
Since we have a CMakePresets.json, building is easy:
```Bash
#32-bit Debug
cmake --preset linux-x86-debug 
cmake --build build/linux/linux-x86-debug -j8
```

### Windows
Windows builds are not currently made and tested, but you're welcome to try it out youself.

## Design Choices / FAQ

##### Is this illegal?

It's as illegal as Wine or any HLE console emulator. All this does is remove the steam dependency from your steam games.

##### But it breaks Steam DRM ?

It doesn't break any DRM. If the game has a protection that doesn't let you use a custom steam api dll it needs to be cracked before you use my emulator. Steam is a DRM as much as any API is a DRM. Steam has actual DRM called steamstub which can easily be cracked but this won't crack it for you.

## Credits
* **[Mr Goldberg](https://gitlab.com/Mr_Goldberg/goldberg_emulator)** — Huge thanks to the original author for the incredible work on the core emulator!
* **[SaNNa](https://github.com/OleksandrChornyi2010)** — Modifications and maintenance for Half-Life based games.
