#!/bin/bash

ROOT_DIR="$(dirname "$0")/.."

cd "${ROOT_DIR}" || exit 1

TEST_DIR="${1:-build/tests}"
BUILD_CONFIG="${2:-}"

NUMPY_VERSION="2.5.3"

numpy_unavailable() {
    if [ -n "${CI:-}" ]; then
        echo "error: $1" >&2
        exit 1
    fi
    echo "warning: $1; the numpy-backed binding tests will cover only the numpy-absent path."
}

has_pinned_numpy() {
    "${python_exe}" -c "import sys, numpy; sys.exit(numpy.__version__ != '${NUMPY_VERSION}')" >/dev/null 2>&1
}

find_args=("${TEST_DIR}" \( -path '*/python/bin/python3' -o -path '*/python/python.exe' \))
[ -n "${BUILD_CONFIG}" ] && find_args+=(-path "*/${BUILD_CONFIG}/*")
python_exe="$(find "${find_args[@]}" -print -quit 2>/dev/null)"

if [ -z "${python_exe}" ]; then
    numpy_unavailable "no bundled Python under ${TEST_DIR}"
elif ! has_pinned_numpy; then
    uv_exe="${python_exe%/python/*}/tools/uv/uv"
    echo "Installing numpy ${NUMPY_VERSION} into the embedded test interpreter (${python_exe})..."
    if [ ! -x "${uv_exe}" ]; then
        numpy_unavailable "no uv staged beside the tests"
    elif ! "${uv_exe}" pip install --python "${python_exe}" --only-binary :all: "numpy==${NUMPY_VERSION}" \
            || ! has_pinned_numpy; then
        numpy_unavailable "could not install numpy ${NUMPY_VERSION} into ${python_exe}"
    fi
fi

args=(--test-dir "${TEST_DIR}" -LE "NotWorking|RequiresApp" --no-tests=error --output-junit "$(pwd)/ctest_results.xml" --output-on-failure -j)
[ -n "${BUILD_CONFIG}" ] && args+=(--build-config "${BUILD_CONFIG}")
ctest "${args[@]}"
