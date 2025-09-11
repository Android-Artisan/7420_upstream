#!/bin/bash

unset_flags() {
    cat << EOF
Usage: $(basename "$0") [options]
Options:
    -m, --model [value]    Specify the model code of the phone
    -k, --ksu [y/N]        Include KernelSU
    -r, --recovery [y/N]   Compile kernel for an Android Recovery
    -c, --ccache [y/N]     Use ccache to cache compilations
    -f, --freq [value]     Set CPU frequency (underclocked, overclocked, original "if want to add yourself its in Freq dir")
    -e, --extra-configs    Enable extra configuration selection
EOF
    exit 1
}

# If no arguments are passed, show help and exit
if [[ $# -eq 0 ]]; then
    unset_flags
fi

EXTRA_CONFIGS_ENABLED="n"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --model|-m)
            MODEL="$2"
            shift 2
            ;;
        --ksu|-k)
            KSU_OPTION="$2"
            shift 2
            ;;
        --recovery|-r)
            RECOVERY_OPTION="$2"
            shift 2
            ;;
        --ccache|-c)
            CCACHE_OPTION="$2"
            shift 2
            ;;
        --freq|-f)
            FREQ_OPTION="$2"
            shift 2
            ;;
        --extra-configs|-e)
            EXTRA_CONFIGS_ENABLED="y"
            shift
            ;;
        *)
            unset_flags
            ;;
    esac
done

echo "Preparing the build environment..."

pushd $(dirname "$0") > /dev/null
CORES=$(grep -c processor /proc/cpuinfo)

# Define toolchain variables
CLANG_DIR=$PWD/toolchain/neutron_18
PATH=$CLANG_DIR/bin:$PATH

if [[ "$CCACHE_OPTION" == "y" ]]; then
    CCACHE=ccache
fi

MAKE_ARGS="
LLVM=1 \
LLVM_IAS=1 \
ARCH=arm64 \
CCACHE=$CCACHE \
READELF=$CLANG_DIR/bin/llvm-readelf \
O=out
"

KERNEL_DEFCONFIG=eyeless_"$MODEL"_defconfig

if [[ "$RECOVERY_OPTION" == "y" ]]; then
    RECOVERY=recovery.config
    KSU_OPTION=n
fi

if [ -z "$KSU_OPTION" ]; then
    read -p "Include KernelSU (y/N): " KSU_OPTION
fi

if [[ "$KSU_OPTION" == "y" ]]; then
    KSU=ksu.config
fi

# Function to select extra configs interactively
select_extra_configs() {
    echo "-----------------------------------------------"
    echo "Select Extra Configurations to Merge:"
    echo "-----------------------------------------------"

    EXTRA_DIR="Extra"
    mapfile -t EXTRA_FILES < <(ls "$EXTRA_DIR"/*.config 2>/dev/null)

    if [[ ${#EXTRA_FILES[@]} -eq 0 ]]; then
        echo "No extra configurations found in $EXTRA_DIR"
        return
    fi

    SELECTED_CONFIGS=()

    while true; do
        echo "Available Extra Configs:"
        for i in "${!EXTRA_FILES[@]}"; do
            printf "[%2d] %s\n" "$((i+1))" "$(basename "${EXTRA_FILES[$i]}")"
        done
        echo "[ A ] Add all"
        echo "[ R ] Remove selected"
        echo "[ D ] Done selecting"
        echo "-----------------------------------------------"
        read -p "Enter number(s) of config(s) to add/remove, 'A' to add all, 'R' to remove, or 'D' to finish: " CHOICE

        case "$CHOICE" in
            [0-9]*)
                for num in $CHOICE; do
                    INDEX=$((num-1))
                    if [[ $INDEX -ge 0 && $INDEX -lt ${#EXTRA_FILES[@]} ]]; then
                        if [[ " ${SELECTED_CONFIGS[*]} " =~ " ${EXTRA_FILES[$INDEX]} " ]]; then
                            echo "Already added: $(basename "${EXTRA_FILES[$INDEX]}")"
                        else
                            SELECTED_CONFIGS+=("${EXTRA_FILES[$INDEX]}")
                            echo "Added: $(basename "${EXTRA_FILES[$INDEX]}")"
                        fi
                    else
                        echo "Invalid selection: $num"
                    fi
                done
                ;;
            A|a)
                SELECTED_CONFIGS=("${EXTRA_FILES[@]}")
                echo "Added all configurations!"
                break
                ;;
            R|r)
                if [[ ${#SELECTED_CONFIGS[@]} -eq 0 ]]; then
                    echo "No configs selected yet."
                else
                    echo "Currently Selected Configs:"
                    for i in "${!SELECTED_CONFIGS[@]}"; do
                        printf "[%2d] %s\n" "$((i+1))" "$(basename "${SELECTED_CONFIGS[$i]}")"
                    done
                    read -p "Enter the number(s) to remove: " REMOVE_CHOICE
                    for num in $REMOVE_CHOICE; do
                        INDEX=$((num-1))
                        if [[ $INDEX -ge 0 && $INDEX -lt ${#SELECTED_CONFIGS[@]} ]]; then
                            echo "Removed: $(basename "${SELECTED_CONFIGS[$INDEX]}")"
                            unset "SELECTED_CONFIGS[$INDEX]"
                            SELECTED_CONFIGS=("${SELECTED_CONFIGS[@]}")
                        else
                            echo "Invalid selection: $num"
                        fi
                    done
                fi
                ;;
            D|d)
                break
                ;;
            *)
                echo "Invalid option! Please select again."
                ;;
        esac
    done
}

# Call function only if -e flag is set
if [[ "$EXTRA_CONFIGS_ENABLED" == "y" ]]; then
    select_extra_configs
fi

echo "-----------------------------------------------"
echo "Building kernel using "$KERNEL_DEFCONFIG""
if [[ "$EXTRA_CONFIGS_ENABLED" == "y" && ${#SELECTED_CONFIGS[@]} -gt 0 ]]; then
    echo "Applying extra configs:"
    for cfg in "${SELECTED_CONFIGS[@]}"; do
        echo "- $(basename "$cfg")"
    done
fi

make ${MAKE_ARGS} -j$CORES $KERNEL_DEFCONFIG "${SELECTED_CONFIGS[@]}" || exit 1

echo "Building kernel..."
make ${MAKE_ARGS} -j$CORES 2>&1 | tee build.log || exit 1

echo "Build finished successfully!"
