#!/bin/bash

set -e
set -o pipefail
SECONDS=0

while getopts ":dpa:snt:xbc:i:j:Tuh" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja Multi-Config"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    i )
        export CMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH:+$CMAKE_IGNORE_PREFIX_PATH;}$OPTARG"
        ;;
    j )
        export CMAKE_BUILD_PARALLEL_LEVEL="$OPTARG"
        ;;
    T )
        export BUILD_TESTS="1"
        ;;
    u )
        export BUILD_TARGET="universal"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -u: Build universal app only (requires existing arm64 and x86_64 app bundles)"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 12.0"
        echo "   -x: Use Ninja Multi-Config CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -i: Add a prefix to ignore during CMake dependency discovery (repeatable), defaults to /opt/local:/usr/local:/opt/homebrew"
        echo "   -j: Set the number of parallel build jobs (CMAKE_BUILD_PARALLEL_LEVEL)"
        echo "   -T: Build and run tests (set ORCA_TESTS_BUILD_ONLY=1 to build without running)"
        exit 0
        ;;
    * )
        ;;
  esac
done

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="12.0"
fi

if [ -z "$CMAKE_IGNORE_PREFIX_PATH" ]; then
  export CMAKE_IGNORE_PREFIX_PATH="/opt/local:/usr/local:/opt/homebrew"
fi

CMAKE_VERSION=$(cmake --version | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/')
if [ "$CMAKE_VERSION" -ge 4 ] 2>/dev/null; then
  export CMAKE_POLICY_VERSION_MINIMUM=3.10
  export CMAKE_POLICY_COMPAT="-DCMAKE_POLICY_VERSION_MINIMUM=3.10"
  echo "Detected CMake 4.x, adding compatibility flag (env + cmake arg)"
else
  export CMAKE_POLICY_COMPAT=""
fi

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - CMAKE_IGNORE_PREFIX_PATH: $CMAKE_IGNORE_PREFIX_PATH"
echo

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"

export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"

function build_deps() {
    for _ARCH in x86_64 arm64; do
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                        -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                        ${CMAKE_POLICY_COMPAT}
                fi
                cmake --build . --config "$BUILD_CONFIG" --target deps
            )
        fi
    done
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "OrcaSlicer_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "build"
    )
}

function relocate_python_runtime() {
    local app="$1"
    local pydir="$app/Contents/MacOS/python"
    if [ -d "$pydir" ] && [ ! -L "$pydir" ]; then
        rm -rf "$app/Contents/Resources/python"
        mv "$pydir" "$app/Contents/Resources/python"
        ln -s ../Resources/python "$pydir"
    fi
}

function verify_python_runtime() {
    local app="$1"
    local pydir="$app/Contents/MacOS/python"
    [ -d "$pydir" ] || return 0
    if [ ! -L "$pydir" ]; then
        echo "ERROR: Contents/MacOS/python must be a symlink into Contents/Resources" >&2
        echo "       (see relocate_python_runtime in this script)" >&2
        exit 1
    fi
    local pybin="$pydir/bin/python3"
    if [ ! -x "$pybin" ]; then
        echo "ERROR: bundled python/ present but no interpreter at $pybin" >&2
        exit 1
    fi
    echo "  Verifying bundled Python runtime in $(basename "$app")..."
    local bad
    bad=$(otool -arch all -L "$pybin" "$app/Contents/MacOS/OrcaSlicer" | grep "libpython" | grep -v "@rpath/" || true)
    if [ -n "$bad" ]; then
        echo "ERROR: a bundled binary references libpython by absolute path (relocation regression):" >&2
        echo "$bad" >&2
        exit 1
    fi
    if ! otool -arch all -l "$app/Contents/MacOS/OrcaSlicer" | grep -q "path @executable_path/python/lib "; then
        echo "ERROR: OrcaSlicer lacks the @executable_path/python/lib rpath (relocation regression)" >&2
        exit 1
    fi
    if ! "$pybin" -c "import ssl"; then
        echo "ERROR: bundled Python failed to start (libpython relocation broken," >&2
        echo "       or missing Rosetta 2 for the x86_64 leg?)" >&2
        exit 1
    fi
}

function build_slicer() {
    for _ARCH in x86_64 arm64; do
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DORCA_TOOLS=ON \
                    ${ORCA_UPDATER_SIG_KEY:+-DORCA_UPDATER_SIG_KEY="$ORCA_UPDATER_SIG_KEY"} \
                    ${BUILD_TESTS:+-DBUILD_TESTS=ON} \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                    -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                    ${CMAKE_POLICY_COMPAT}
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
        )

        if [ "1." == "$BUILD_TESTS". ] && [ "1." != "$ORCA_TESTS_BUILD_ONLY". ]; then
            "$PROJECT_DIR/scripts/run_unit_tests.sh" "build/$_ARCH/tests" "$BUILD_CONFIG"
        fi

        echo "Verify localization with gettext..."
        (
            cd "$PROJECT_DIR"
            ./scripts/run_gettext.sh
        )

        echo "Fix macOS app package..."
        (
            cd "$PROJECT_BUILD_DIR"
            mkdir -p OrcaSlicer
            cd OrcaSlicer
            rm -rf ./OrcaSlicer.app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer.app" ./OrcaSlicer.app
            resources_path=$(readlink ./OrcaSlicer.app/Contents/Resources)
            rm ./OrcaSlicer.app/Contents/Resources
            cp -R "$resources_path" ./OrcaSlicer.app/Contents/Resources
            relocate_python_runtime ./OrcaSlicer.app
            find ./OrcaSlicer.app/ -name '.DS_Store' -delete

            verify_python_runtime ./OrcaSlicer.app
            
            if [ -f "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer_profile_validator.app/Contents/MacOS/OrcaSlicer_profile_validator" ]; then
                echo "Copying OrcaSlicer_profile_validator.app..."
                rm -rf ./OrcaSlicer_profile_validator.app
                cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer_profile_validator.app" ./OrcaSlicer_profile_validator.app
                find ./OrcaSlicer_profile_validator.app/ -name '.DS_Store' -delete
                verify_python_runtime ./OrcaSlicer_profile_validator.app
            fi
        )

    fi
    done
}

function lipo_dir() {
    local universal_dir="$1"
    local x86_64_dir="$2"

    while IFS= read -r -d '' f; do
        local rel="${f#"$universal_dir"/}"
        local x86="$x86_64_dir/$rel"
        if [ -f "$x86" ]; then
            echo "  lipo: $rel"
            lipo -create "$f" "$x86" -output "$f.tmp"
            mv "$f.tmp" "$f"
        else
            echo "  warning: no x86_64 counterpart for $rel, keeping arm64 only"
        fi
    done < <(find "$universal_dir" -type f -print0 | while IFS= read -r -d '' candidate; do
        if file "$candidate" | grep -q "Mach-O"; then
            printf '%s\0' "$candidate"
        fi
    done)
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    ARM64_APP="$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer.app"
    X86_64_APP="$PROJECT_DIR/build/x86_64/OrcaSlicer/OrcaSlicer.app"

    mkdir -p "$PROJECT_BUILD_DIR/OrcaSlicer"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/OrcaSlicer/OrcaSlicer.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$ARM64_APP" "$UNIVERSAL_APP"

    echo "Creating universal binaries for OrcaSlicer.app..."
    lipo_dir "$UNIVERSAL_APP" "$X86_64_APP"
    echo "Universal OrcaSlicer.app created at $UNIVERSAL_APP"
    verify_python_runtime "$UNIVERSAL_APP"

    ARM64_VALIDATOR="$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer_profile_validator.app"
    X86_64_VALIDATOR="$PROJECT_DIR/build/x86_64/OrcaSlicer/OrcaSlicer_profile_validator.app"
    if [ -d "$ARM64_VALIDATOR" ] && [ -d "$X86_64_VALIDATOR" ]; then
        echo "Creating universal binaries for OrcaSlicer_profile_validator.app..."
        UNIVERSAL_VALIDATOR_APP="$PROJECT_BUILD_DIR/OrcaSlicer/OrcaSlicer_profile_validator.app"
        rm -rf "$UNIVERSAL_VALIDATOR_APP"
        cp -R "$ARM64_VALIDATOR" "$UNIVERSAL_VALIDATOR_APP"
        lipo_dir "$UNIVERSAL_VALIDATOR_APP" "$X86_64_VALIDATOR"
        echo "Universal OrcaSlicer_profile_validator.app created at $UNIVERSAL_VALIDATOR_APP"
    fi
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    universal)
        build_universal
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, universal, all."
        exit 1
        ;;
esac

if [ "$ARCH" = "universal" ] && { [ "$BUILD_TARGET" = "all" ] || [ "$BUILD_TARGET" = "slicer" ]; }; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi

elapsed=$SECONDS
printf "\nBuild completed in %dh %dm %ds\n" $((elapsed/3600)) $((elapsed%3600/60)) $((elapsed%60))
