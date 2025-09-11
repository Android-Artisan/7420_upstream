#!/bin/bash
set -euo pipefail

echo "Preparing the build environment..."
CORES=$(nproc)

# Toolchain setup
CLANG_TAR="clang-4053586.tar.gz"
CLANG_URL="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/tags/android-8.1.0_r1/clang-4053586.tar.gz"
CLANG_DIR=$PWD/toolchain/clang-4053586

if [[ ! -d "$CLANG_DIR" ]]; then
    echo "Downloading Clang toolchain..."
    mkdir -p toolchain
    curl -L "$CLANG_URL" -o "$CLANG_TAR"
    # try extract as gz tar
    if tar -tzf "$CLANG_TAR" >/dev/null 2>&1; then
        mkdir -p "$CLANG_DIR"
        tar -xzf "$CLANG_TAR" -C "$CLANG_DIR" --strip-components=1
        rm "$CLANG_TAR"
    else
        # try unzip (some mirrors provide a zip)
        if unzip -t "$CLANG_TAR" >/dev/null 2>&1; then
            mkdir -p tmp_unzip
            unzip "$CLANG_TAR" -d tmp_unzip
            # move contents into CLANG_DIR
            mkdir -p "$CLANG_DIR"
            # move inner folder contents (if repo zip structure)
            mv tmp_unzip/*/* "$CLANG_DIR" 2>/dev/null || mv tmp_unzip/* "$CLANG_DIR"
            rm -rf tmp_unzip
            rm "$CLANG_TAR"
        else
            echo "Error: downloaded clang archive is not a tar.gz or zip. Inspect $CLANG_TAR"
            file "$CLANG_TAR"
            exit 1
        fi
    fi
fi

export PATH="$CLANG_DIR/bin:$PATH"

MAKE_ARGS="
LLVM=1 \
LLVM_IAS=1 \
ARCH=arm64 \
READELF=$CLANG_DIR/bin/llvm-readelf \
O=out
"

# Use plain defconfig
KERNEL_DEFCONFIG=defconfig

echo "-----------------------------------------------"
echo "Building kernel using $KERNEL_DEFCONFIG"

# Step 1: generate defconfig
make ${MAKE_ARGS} -j"$CORES" "$KERNEL_DEFCONFIG"

# Step 2: build kernel
make ${MAKE_ARGS} -j"$CORES" 2>&1 | tee build.log

# Step 3: prepare flashable structure
FLASH_DIR=$PWD/out/flashable
FILES_DIR=$FLASH_DIR/files
META_DIR=$FLASH_DIR/META-INF/com/google/android

rm -rf "$FLASH_DIR"
mkdir -p "$FILES_DIR"
mkdir -p "$META_DIR"

# Copy kernel image into files/boot.img (choose available variant)
if [[ -f out/arch/arm64/boot/Image.gz-dtb ]]; then
    cp out/arch/arm64/boot/Image.gz-dtb "$FILES_DIR/boot.img"
elif [[ -f out/arch/arm64/boot/Image.gz ]]; then
    cp out/arch/arm64/boot/Image.gz "$FILES_DIR/boot.img"
elif [[ -f out/arch/arm64/boot/Image ]]; then
    cp out/arch/arm64/boot/Image "$FILES_DIR/boot.img"
else
    echo "Error: Kernel image not found in out/arch/arm64/boot/"
    exit 1
fi

# Copy dtbo if present
[[ -f out/arch/arm64/boot/dtbo.img ]] && cp out/arch/arm64/boot/dtbo.img "$FILES_DIR/dtbo.img"

# Copy updater-script & update-binary from build/
if [[ -f build/updater-script ]]; then
    cp build/updater-script "$META_DIR/updater-script"
else
    echo "Warning: build/updater-script not found"
fi

if [[ -f build/update-binary ]]; then
    cp build/update-binary "$META_DIR/update-binary"
else
    echo "Warning: build/update-binary not found"
fi

# Step 4: make flashable zip with the required name
ZIP_NAME="ArtisanKRNL-7420-$(date +%Y%m%d-%H%M).zip"
cd "$FLASH_DIR"
zip -r9 "../../$ZIP_NAME" . -x "*.git*" -x "README.md"
cd - >/dev/null

echo "-----------------------------------------------"
echo "Build finished successfully!"
echo "Flashable zip created: $ZIP_NAME"
