#!/bin/sh

set -eu

ARCH=$(uname -m)
export ARCH
export OUTPATH=./dist
export APPNAME=dethrace
export STARTUPWMCLASS=dethrace
export DEPLOY_OPENGL=1
export DEPLOY_VULKAN=1
export ADD_HOOKS="self-updater.hook"
export UPINFO="gh-releases-zsync|${GITHUB_REPOSITORY%/*}|${GITHUB_REPOSITORY#*/}|latest|*$ARCH.AppImage.zsync"

echo "Installing package dependencies..."
echo "---------------------------------------------------------------"
pacman -Syu --noconfirm \
    cmake        \
    libdecor     \
    sdl3         \
    shaderc      \
    vulkan-headers

echo "Installing debloated packages..."
echo "---------------------------------------------------------------"
get-debloated-pkgs --add-common --prefer-nano

echo "Building dethrace..."
echo "---------------------------------------------------------------"
# Version from the checked-out tree. A short commit hash keeps VERSION.txt
# clean so the release job can form the v<version> tag without a doubled "v".
VERSION="$(git rev-parse --short HEAD)"
export VERSION

mkdir -p ./dist
echo "${VERSION}" > ./dist/VERSION.txt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DDETHRACE_PLATFORM_SDL2=OFF \
    -DDETHRACE_PLATFORM_SDL3=ON
cmake --build build -j"$(nproc)"

mkdir -p ./AppDir/bin
mv -v ./build/dethrace ./AppDir/bin/

# Desktop entry + icon. Icon must match the file name (no extension) placed
# next to the desktop entry, which quick-sharun surfaces as the AppImage icon.
cat > ./AppDir/dethrace.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=dethrace
Comment=Reverse engineering the 1997 game "Carmageddon"
Exec=dethrace
Icon=dethrace
Terminal=false
Categories=Game;Action;Simulator
StartupWMClass=dethrace
EOF
cp -v ./packaging/icon_source.png ./AppDir/dethrace.png

# First-run hook: the AppImage AppRun sources every AppDir/bin/*.hook at
# startup. This one creates dethrace.ini in the user data dir and cd's there
# so the game finds its config and DATA files.
cp -v ./packaging/setup-config.hook ./AppDir/bin/

# Deploy dependencies
quick-sharun ./AppDir/bin/dethrace

# this app has problems with other locales breaking physics
echo 'LC_ALL=C.UTF-8' >> ./AppDir/.env

# Turn AppDir into AppImage
quick-sharun --make-appimage
