# Dethrace

[![Workflow](https://github.com/dethrace-labs/dethrace/actions/workflows/workflow.yaml/badge.svg)](https://github.com/dethrace-labs/dethrace/actions/workflows/workflow.yml)

Dethrace is an attempt to learn how the 1997 driving/mayhem game [Carmageddon](https://en.wikipedia.org/wiki/Carmageddon) works behind the scenes and rebuild it to run natively on modern systems.

## Status

<img width="50%" src="https://raw.githubusercontent.com/Link4Electronics/reccmp-report/refs/heads/main/progress.svg">

## Building

### Dependencies

Dethrace using CMake to build, and SDL2/3 at runtime. The easiest way to install them is via your favorite package manager.

The SDL3 GPU renderer (`Emulate3DFX = 2`) additionally needs the shader toolchain at build time. It compiles one set of GLSL sources into the shader formats SDL3 GPU expects per backend:

- **glslang** (`glslangValidator`, `glslang`, or `glslc`) — GLSL to SPIR-V (Vulkan).
- **SDL_shadercross** (`shadercross`) — SPIR-V to MSL (Metal) and DXIL (D3D12). This is the tool [SDL3 uses itself](https://github.com/libsdl-org/SDL_shadercross); its output follows the SDL3-GPU slot conventions by construction. Required on macOS; the build fails at configure time without it.
- **spirv-cross** and **dxc** — fallback translator for setups without SDL_shadercross. Windows only; without either SDL_shadercross or dxc the build silently drops the D3D12 backend and falls back to Vulkan.

OSX:

```sh
brew install SDL2 SDL3 glslang cmake
```

Linux:

```sh
apt-get install libsdl2-dev libsdl3-dev glslang-tools cmake
```

Windows (MSYS2):

```sh
pacman -S mingw-w64-x86_64-sdl2 mingw-w64-x86_64-sdl3 mingw-w64-x86_64-glslang cmake make
```

MSYS2 and vcpkg do not ship `shadercross`; build it from [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) (or install `spirv-cross` and `dxc` instead; `dxc` is downloaded from the [DirectXShaderCompiler releases](https://github.com/microsoft/DirectXShaderCompiler/releases) and must be on `PATH` before configuring).

### Clone

Dethrace uses [git submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules), so we must pull them after the inital clone:

```sh
git clone --recursive https://github.com/Link4Electronics/dethrace
```

### Build

Dethrace uses [cmake](https://cmake.org/)

To generate the build files and compbile:

```sh
mkdir build
cd build
cmake ..
make
```

## Running the game

Dethrace does not ship with any content. You'll need access to the data from the original game. If you don't have an original CD then you can [buy Carmageddon from GoG.com](https://www.gog.com/game/carmageddon_max_pack).

`dethrace` also supports the various freeware demos:

- [Original Carmageddon demo](https://archive.org/details/Carmageddon_1020)
- [Splat Pack demo](https://archive.org/details/CARMASPL)

Dethrace generally expects to be placed into the top level Carmageddon folder. You know you have the right folder when you see the original `CARMA.EXE` there. If you are on Windows, you must also place `SDL3.dll` in the same folder.

### Configuration INI file

Alternatively, you may configure a different Carmageddon directory and settings by providing a [dethrace.ini file](docs/CONFIGURATION.md).

### CD audio

Dethrace supports the GOG cd audio convention. If there is a `MUSIC` folder in the Carmageddon folder containing files `Track02.ogg`, `Track03.ogg` etc, then Dethrace will use those files in place of the original CD audio functions.

## Background

Watcom debug symbols for an earlier internal build [were discovered](http://1amstudios.com/2014/12/02/carma1-symbols-dumped) named `DETHRSC.SYM` on the [Carmageddon Splat Pack](http://carmageddon.wikia.com/wiki/Carmageddon_Splat_Pack) expansion CD release. The symbols unfortunately did not match any known released executable, meaning they were interesting but not immediately usable to reverse engineer the game.

This is what it looked like from the Watcom debugger - the names of all the methods were present but the code location they were pointing to was junk:

![watcom-debugger](http://1amstudios.com/img/watcom-debugger.jpg)

We are slowly replacing the original assembly code with equivalent C code, function by function.

### Is "dethrace" a typo?

No, well, I don't think so at least. The original files according to the symbol dump were stored in `c:\DETHRACE`, and the symbol file is called `DETHSRC.SYM`. Maybe they removed the "a" to be compatible with [8.3 filenames](https://en.wikipedia.org/wiki/8.3_filename)?

## Contributing
See [docs](./CONTRIBUTING.md)

## Changelog

[From the beginning until release](docs/CHANGELOG.md)

## Credits

- CrayzKirk (did the first manual matching up functions and data structures in the DOS executable to the debugging symbols and proved it was possible!)
- The developer at Stainless Software who left an old debugging .SYM file on the Splat Pack CD ;)
- https://github.com/isledecomp/reccmp tooling

## Legal

Dethrace is released to the Public Domain. The documentation and function provided by Dethrace may only be utilized with assets provided by ownership of Carmageddon.

The source code in this repository is for non-commerical use only. If you use the source code you may not charge others for access to it or any derivative work thereof.

Dethrace and any of its' maintainers are in no way associated with or endorsed by SCi, Stainless Software or THQ Nordic.
