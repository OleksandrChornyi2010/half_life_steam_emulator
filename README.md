# half_file_steam_emulator

This is a fork of [Goldberg steam emulator](https://gitlab.com/Mr_Goldberg/goldberg_emulator/) with the aim to make half-life based games' server browser work like the original one. For a readme on how to use it see: [The Release Readme](Readme_release.txt)

## How to use

The usage instruction doesn't differ from the original emulator

Replace the steam_api(64).dll (libsteam_api.so on linux) from the game with mine. For linux make sure that if the original api is 32 bit you use a 32 bit build and if it's 64 bit you use a 64 bit build.

Put a steam_appid.txt file that contains the appid of the game right beside it if there isn't one already.

If your game has an original steam_api(64).dll or libsteam_api.so older than may 2016 (On windows: Properties->Digital Signatures->Timestamp) you might have to add a steam_interfaces.txt beside my emulator library if the game isn't working. There is a linux script to generate it in the scripts folder of this repo.

For more information see: [The Release Readme](Readme_release.txt)

## Download Binaries

You can download stable builds from the [release section](https://github.com/OleksandrChornyi2010/half_life_steam_emulator/releases) of this repository.

## Features

- Supports both Linux and Windows
- Emulator can now work with favorite and history servers saved locally in `<steam_emulator_data_dir>`/platform/serverbrowser_hist.vdf.
- Can get internet and spectator servers from multiple master servers written in <steam_emulator_data_dir>`/platform/MasterServers.vdf.

## Setting up

Run the game once to create serverbrowser*hist.vdf and MasterServers.vdf files. Favorites and history lists will work out of the box, but you need to add a master server ip/domain into the `<steam_emulator_data_dir>`/platform/MasterServers.vdf file. Simply replace \_master server ip/domain goes here* text with master server ip or domain. If you want to have multiple master servers, you can copy a block that starts with `"0"` (or any other number) and ends with `}` and paste in right below the block that you copied, and change the address in the block that you pasted.

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

These are instructions for the steam_api build target. Other targets are not required.

### Preparation:

Notice that you need 32-bit protobuf built as a static library so the resulting emulator won't depend on it being installed. Notice that protoc package is also required and has to be the same version as protobuf lib. Currently any version higher than 3.1.0 is supported. Read Preparation topic for each OS for more info.

### Linux

#### Building .so:

##### Preparation:

Protobuf and protoc built into _/opt/protobuf-32_ are required.

##### Building:

Use preset to build:

```Bash
cmake --preset linux-x86-release
cmake --build --preset linux-x86-release -j$(nproc)
```

#### Building .dll:

You can use Linux cross platform compiler MinGW for the windows build. DLL built this way may crash because of CRT mismatch.

##### Preparation:

32 bit version of MinGW Compiler is required.

Protobuf built into _/opt/protobuf-mingw32_ **using MinGW**, and protoc built into _/opt/protobuf-32_ **using any linux compiler** are required.

##### Building:

Use preset to build:

```Bash
cmake --preset mingw-win-x86-release
cmake --build --preset mingw-win-x86-release -j$(nproc)
```

### Windows

#### Preparation

Protobuf and protoc built into _C:\\Libraries\\protobuf-32_ are required.

##### Building .dll:

Use preset to build:

```Bash
cmake --preset win-x86-release
cmake --build build/win/win-x86-release --config Release --target steam_api --parallel
```

## Design Choices / FAQ

##### Is this illegal?

It's as illegal as Wine or any HLE console emulator. All this does is remove the steam dependency from your steam games.

##### But it breaks Steam DRM ?

It doesn't break any DRM. If the game has a protection that doesn't let you use a custom steam api dll it needs to be cracked before you use this emulator. Steam is a DRM as much as any API is a DRM. Steam has actual DRM called steamstub which can easily be cracked but this won't crack it for you.

## Credits

- **[SaNNa](https://github.com/OleksandrChornyi2010)** — Modifications and maintenance for Half-Life based games.

- **[Mr Goldberg](https://gitlab.com/Mr_Goldberg/goldberg_emulator)** — Huge thanks to the original author for incredible work on the emulator!
