#!/bin/bash
set -e

echo "Preparing the build environment..."
CORES=$(nproc)

# Toolchain setup
CLANG_TAR="clang-4053586.tar.gz"
CLANG_URL="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/4053586/clang-4053586.tar.gz"
CLANG_DIR=$PWD/toolchain/clang-4053586

if [[ ! -d "$CLANG_DIR" ]]; then
    echo "Downloading Clang toolchain..."
    mkdir -p toolchain
    curl -L "$CLANG_URL" -o "$CLANG_TAR"
    mkdir -p "$CLANG_DIR"
    tar -xzf "$CLANG_TAR" -C "$CLANG_DIR"
    rm "$CLANG_TAR"
fi

export PATH=$CLANG_DIR/bin:$PATH

MAKE_ARGS="
LLVM=1 \
LLVM_IAS=1 \
ARCH=arm64 \
READELF=$CLANG_DIR/bin/llvm-readelf \
O=out
"

# Pick defconfig
DEFCONFIG=$(basename arch/arm64/configs/*_defconfig 2>/dev/null)
if [[ -z "$DEFCONFIG" ]]; then
    echo "Error: No defconfig found in arch/arm64/configs/"
    exit 1
fi

echo "-----------------------------------------------"
echo "Building kernel using $DEFCONFIG"

# Step 1: generate defconfig
make ${MAKE_ARGS} -j$CORES "$DEFCONFIG"

# Step 2: build kernel
make ${MAKE_ARGS} -j$CORES 2>&1 | tee build.log

# Step 3: prepare flashable structure
FLASH_DIR=$PWD/out/flashable
FILES_DIR=$FLASH_DIR/files
META_DIR=$FLASH_DIR/META-INF/com/google/android

rm -rf $FLASH_DIR
mkdir -p $FILES_DIR
mkdir -p $META_DIR

# Copy kernel image
if [[ -f out/arch/arm64/boot/Image.gz-dtb ]]; then
    cp out/arch/arm64/boot/Image.gz-dtb $FILES_DIR/boot.img
elif [[ -f out/arch/arm64/boot/Image.gz ]]; then
    cp out/arch/arm64/boot/Image.gz $FILES_DIR/boot.img
elif [[ -f out/arch/arm64/boot/Image ]]; then
    cp out/arch/arm64/boot/Image $FILES_DIR/boot.img
else
    echo "Error: Kernel image not found!"
    exit 1
fi

# Copy dtbo if present
[[ -f out/arch/arm64/boot/dtbo.img ]] && cp out/arch/arm64/boot/dtbo.img $FILES_DIR/dtbo.img

# Copy updater-script & update-binary from build/
cp build/updater-script $META_DIR/updater-script
cp build/update-binary $META_DIR/update-binary

# Step 4: make flashable zip
ZIP_NAME="ArtisanKRNL-7420-$(date +%Y%m%d-%H%M).zip"
cd $FLASH_DIR
zip -r9 "../../$ZIP_NAME" . -x "*.git*" -x "README.md"
cd ../..

echo "-----------------------------------------------"
echo "Build finished successfully!"
echo "Flashable zip created: $ZIP_NAME"
