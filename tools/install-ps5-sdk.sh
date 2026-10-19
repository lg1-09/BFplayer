#!/bin/bash
# Unpack the PS5 Payload SDK and PacBrew into $HOME/ps5-sdk.
set -euo pipefail

dest="${PS5_SDK_ROOT:-$HOME/ps5-sdk}"
sdk_zip="${1:-$HOME/Downloads/ps5-payload-sdk.zip}"
pacbrew_tar="${2:-$HOME/Downloads/ps5-payload-dev.tar.gz}"

mkdir -p "$dest"

if [[ -f "$sdk_zip" ]]; then
    rm -rf "$dest/ps5-payload-sdk"
    unzip -q "$sdk_zip" -d "$dest"
    chmod +x "$dest/ps5-payload-sdk/bin/"*
    echo "SDK ready: $dest/ps5-payload-sdk"
else
    echo "Waiting for SDK zip: $sdk_zip"
fi

if [[ -f "$pacbrew_tar" ]]; then
    rm -rf "$dest/pacbrew-extract"
    mkdir -p "$dest/pacbrew-extract"
    tar -xzf "$pacbrew_tar" -C "$dest/pacbrew-extract"
    archive=$(find "$dest/pacbrew-extract" -type f -path '*/lib/libavformat.a' | head -n 1)
    if [[ -z "$archive" ]]; then
        echo "PacBrew archive has no lib/libavformat.a" >&2
        exit 1
    fi
    homebrew=$(cd "$(dirname "$(dirname "$archive")")" && pwd)
    mkdir -p "$dest/pacbrew-v0.37"
    ln -sfn "$homebrew" "$dest/pacbrew-v0.37/homebrew"
    echo "PacBrew ready: $dest/pacbrew-v0.37/homebrew"
else
    echo "Waiting for PacBrew archive: $pacbrew_tar"
fi

if llvm_prefix=$(brew --prefix llvm@18 2>/dev/null) && [[ -d "$llvm_prefix/bin" ]]; then
    llvm_bin="$llvm_prefix/bin"
else
    llvm_bin="/opt/homebrew/opt/llvm@18/bin"
fi
cat > "$dest/env.sh" <<EOF
export PS5_PAYLOAD_SDK="$dest/ps5-payload-sdk"
export PS5_PACBREW_HOME="$dest/pacbrew-v0.37/homebrew"
export LLVM_CONFIG="$llvm_bin/llvm-config"
export PATH="$llvm_bin:\$PS5_PAYLOAD_SDK/bin:\$PATH"
EOF

echo "Environment file: $dest/env.sh"
