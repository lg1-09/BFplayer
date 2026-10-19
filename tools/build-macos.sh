#!/bin/bash
# Build dist/bfplayer.elf and dist/bfplayer-standalone.elf on macOS.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

sdk_root="${PS5_SDK_ROOT:-$HOME/ps5-sdk}"
if [[ -f "$sdk_root/env.sh" ]]; then
    # shellcheck disable=SC1091
    source "$sdk_root/env.sh"
fi

sdk="${PS5_PAYLOAD_SDK:-$sdk_root/ps5-payload-sdk}"
pacbrew="${PS5_PACBREW_HOME:-$sdk_root/pacbrew-v0.37/homebrew}"
llvm_bin="${LLVM_BIN:-$(brew --prefix llvm@18)/bin}"
export PATH="$llvm_bin:$sdk/bin:$PATH"
export LLVM_CONFIG="${LLVM_CONFIG:-$llvm_bin/llvm-config}"

cxx="$sdk/bin/prospero-clang++"
cc="$sdk/bin/prospero-clang"
strip="$llvm_bin/llvm-strip"
build="build/ps5"

require() {
    if [[ ! -e "$1" ]]; then
        echo "Missing $1" >&2
        echo "Finish the SDK downloads, then run tools/install-ps5-sdk.sh" >&2
        exit 1
    fi
}

require "$cxx"
require "$cc"
require "$strip"
require "$pacbrew/include/SDL2/SDL.h"
require "$pacbrew/lib/libavformat.a"
require "$pacbrew/lib/libass.a"
require "vendor/SDL_ps5_backend/lib/SDL_ps5video.c.o"
require "assets/fonts/NotoSans-Regular.otf"

mkdir -p "$build" dist
version=$(tr -d '[:space:]' < VERSION)
cat > "$build/bfplayer_version.h" <<EOF
#pragma once
#define BFPLAYER_VERSION "$version"
EOF

common=(
    -std=c++20 -Wall -Wextra -Wpedantic -DBFPLAYER_PS5=1
    -Ivendor/SDL_kitchensink/include
    -I"$pacbrew/include" -I"$pacbrew/include/SDL2" -Iinclude
    -include "$build/bfplayer_version.h"
    -O2 -DNDEBUG
)
ccommon=(
    -std=c17 -Wall -Wextra -Wpedantic -DBFPLAYER_PS5=1
    -DKIT_VERSION_MAJOR=2 -DKIT_VERSION_MINOR=0 -DKIT_VERSION_PATCH=0
    -Ivendor/SDL_kitchensink/include
    -I"$pacbrew/include" -I"$pacbrew/include/SDL2" -Iinclude
    -include "$build/bfplayer_version.h"
    -O2 -DNDEBUG
)

objects=()
compile_cxx() {
    local object="$build/$(basename "${1%.*}").o"
    echo "c++ $1"
    "$cxx" "${common[@]}" -c "$1" -o "$object"
    objects+=("$object")
}
compile_c() {
    local object="$build/$(basename "${1%.*}").o"
    echo "cc  $1"
    "$cc" "${ccommon[@]}" -c "$1" -o "$object"
    objects+=("$object")
}

cxx_sources=(
    src/main.cpp
    src/core/artwork.cpp
    src/core/bulk_import.cpp
    src/core/diagnostics.cpp
    src/core/dlna_protocol.cpp
    src/core/hdr_tonemap.cpp
    src/core/library_scanner.cpp
    src/core/library_view.cpp
    src/core/list_navigation.cpp
    src/core/media_sources.cpp
    src/core/player_settings.cpp
    src/core/playlist.cpp
    src/core/remote_control.cpp
    src/core/safe_read_file.cpp
    src/core/source_uri.cpp
    src/core/subtitle_browser.cpp
    src/core/subtitle_provider.cpp
    src/core/video_layout.cpp
    src/core/video_thumbnail.cpp
    src/network/dlna_client.cpp
    src/core/library_database.cpp
    src/core/media_probe.cpp
    src/playback/external_subtitles.cpp
    src/ui/library_ui.cpp
    src/ui/playback_osd.cpp
)
c_sources=(
    src/core/text_encoding.c
    src/playback/kitsubimage_safe.c
    src/playback/kitchensink_audio_clock.c
    src/playback/kitchensink_subtitle_timing.c
    vendor/SDL_kitchensink/src/kiterror.c
    vendor/SDL_kitchensink/src/kitformat.c
    vendor/SDL_kitchensink/src/kitlib.c
    vendor/SDL_kitchensink/src/kitplayer.c
    vendor/SDL_kitchensink/src/kitsource.c
    vendor/SDL_kitchensink/src/kitutils.c
    vendor/SDL_kitchensink/src/internal/kitdecoder.c
    vendor/SDL_kitchensink/src/internal/kitdecoderthread.c
    vendor/SDL_kitchensink/src/internal/kitdemuxer.c
    vendor/SDL_kitchensink/src/internal/kitdemuxerthread.c
    vendor/SDL_kitchensink/src/internal/kitlibstate.c
    vendor/SDL_kitchensink/src/internal/kitpacketbuffer.c
    vendor/SDL_kitchensink/src/internal/kittimer.c
    vendor/SDL_kitchensink/src/internal/libass.c
    vendor/SDL_kitchensink/src/internal/audio/kitaudio.c
    vendor/SDL_kitchensink/src/internal/audio/kitaudioutils.c
    vendor/SDL_kitchensink/src/internal/subtitle/kitatlas.c
    vendor/SDL_kitchensink/src/internal/subtitle/kitsubtitle.c
    vendor/SDL_kitchensink/src/internal/subtitle/kitsubtitlepacket.c
    vendor/SDL_kitchensink/src/internal/subtitle/renderers/kitsubass.c
    vendor/SDL_kitchensink/src/internal/subtitle/renderers/kitsubrenderer.c
    vendor/SDL_kitchensink/src/internal/utils/kithelpers.c
    vendor/SDL_kitchensink/src/internal/video/kitvideo.c
    vendor/SDL_kitchensink/src/internal/video/kitvideoutils.c
)

for source in "${cxx_sources[@]}"; do
    compile_cxx "$source"
done
for source in "${c_sources[@]}"; do
    compile_c "$source"
done
cp vendor/SDL_ps5_backend/lib/SDL_ps5video.c.o "$build/SDL_ps5video.override.o"
objects+=("$build/SDL_ps5video.override.o")

echo "link bfplayer.elf"
"$cc" -o "$build/bfplayer.unstripped.elf" "${objects[@]}" \
    -L"$pacbrew/lib" \
    -Wl,--start-group \
    -lSDL2_image -lSDL2_ttf -lSDL2 \
    -lavfilter -lavformat -lavcodec -lswscale -lswresample -lavutil \
    -lass -lfontconfig -lharfbuzz -lfribidi -lfreetype \
    -lpng16 -lbz2 -lz -llzma -lssl -lcrypto -liconv \
    -lsamplerate -lexpat -ltinyxml2 -lsqlite3 \
    -lc++ -lc++abi -lunwind \
    -Wl,--end-group \
    -lSceSystemService -lSceUserService -lScePad -lSceAudioOut \
    -lSceVideoOut -lSceKeyboard -lSceImeDialog

"$strip" --strip-all -o dist/bfplayer.elf "$build/bfplayer.unstripped.elf"
player_size=$(stat -f %z dist/bfplayer.elf)
printf '\n#define BFPLAYER_PLAYER_UNCOMPRESSED_SIZE %sUL\n' "$player_size" >> "$build/bfplayer_version.h"
gzip -c -9 dist/bfplayer.elf > "$build/bfplayer.elf.gz"

standalone_common=(
    -std=c17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG
    -Iinclude -Isrc/launcher -Isrc/launcher/core -I"$pacbrew/include"
    -include "$build/bfplayer_version.h"
)
standalone_objects=()
for source in \
    src/launcher/standalone_launcher.c \
    src/launcher/standalone_route.c \
    src/launcher/core/pt.c \
    src/launcher/core/elfldr.c \
    src/launcher/core/hbldr.c
do
    object="$build/standalone_$(basename "${source%.*}").o"
    echo "cc  $source"
    "$cc" "${standalone_common[@]}" -c "$source" -o "$object"
    standalone_objects+=("$object")
done

echo "link bfplayer-standalone.elf"
"$cc" -o dist/bfplayer-standalone.elf "${standalone_objects[@]}" \
    -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil \
    -L"$pacbrew/lib" -lz

shasum -a 256 dist/bfplayer.elf | awk '{print $1 "  bfplayer.elf"}' > dist/bfplayer.sha256
shasum -a 256 dist/bfplayer-standalone.elf | awk '{print $1 "  bfplayer-standalone.elf"}' > dist/bfplayer-standalone.sha256
ls -lh dist/bfplayer.elf dist/bfplayer-standalone.elf
