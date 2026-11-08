#!/usr/bin/env bash
# shellcheck disable=SC2329

set -uo pipefail

VALIDATOR_RELEASE_URL="https://github.com/OrcaSlicer/OrcaSlicer/releases/download/nightly-builds"
FIXTURE_RELEASE_URL="https://github.com/OrcaSlicer/OrcaSlicer-profile-validator/releases/download/fixture-archive"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

HOST_ARCH="$(uname -m)"
HOST_OS="$(uname -s)"
case "${HOST_OS}" in
    Darwin*) HOST_OS=Darwin ;;
    MINGW*|MSYS*|CYGWIN*) HOST_OS=Windows ;;
    Linux*) HOST_OS=Linux ;;
esac

PROFILES_DIR="${REPO_ROOT}/resources/profiles"
case "${HOST_OS}" in
    Darwin) DEFAULT_WORK_DIR="${HOME}/Library/Caches/orca-profile-check" ;;
    Windows) DEFAULT_WORK_DIR="${LOCALAPPDATA:-${HOME}/AppData/Local}/orca-profile-check" ;;
    *) DEFAULT_WORK_DIR="${XDG_CACHE_HOME:-${HOME}/.cache}/orca-profile-check" ;;
esac
WORK_DIR="${DEFAULT_WORK_DIR}"
VALIDATOR="${ORCA_PROFILE_VALIDATOR:-}"
VENDOR=""
LOG_LEVEL=2
PREFER_DOWNLOAD=0
REFRESH=0

ALL_CHECKS=(profile_tool validate_system validate_slice validate_filament_subtypes validate_custom)
CHECKS=()
RESULTS=""

usage() {
    cat <<EOF
Run the profile checks from .github/workflows/check_profiles.yml locally.

Usage: scripts/check_profile.sh [OPTION]... [CHECK]...

Checks (default: all, in this order):
  profile_tool                  scripts/orca_profile_tool.py check
  validate_system               validator -p <profiles> -l <level>
  validate_slice                validator -p <profiles> -s -l <level>
  validate_filament_subtypes    validator -p <profiles> -l <level> -f
  validate_custom               validator against every released custom-preset fixture

Options:
  -p, --profiles DIR   profile tree to validate (default: resources/profiles)
  -v, --vendor NAME    check only this vendor, named after its <Vendor>.json (e.g. "Co Print")
      --validator BIN  OrcaSlicer_profile_validator to use; also \$ORCA_PROFILE_VALIDATOR.
                       Default: the local build*/ Release build (then RelWithDebInfo, then
                       Debug) for this architecture, else the nightly release build is
                       downloaded for this platform
      --download       ignore local builds and use the downloaded nightly validator
      --refresh        re-download the validator and fixtures instead of using the cache
      --work-dir DIR   downloads, logs and fixture trees (default: ${DEFAULT_WORK_DIR})
  -l, --log-level N    validator log level (default: ${LOG_LEVEL}, as in CI)
  -h, --help           show this help

Note: profile_tool is the only check that is not the validator binary; it makes the static
checks the validator cannot, because the validator loads the tree the way the slicer does
and so never sees a profile no <vendor>.json indexes, a preset name two files claim, or a
file normalize and update-index would still rewrite.

Note: --vendor narrows validate_custom too, by keeping only that vendor's presets in each
fixture tree. The one check it cannot narrow is validate_slice for a vendor that ships no
printers; the summary reports that one as skipped, and naming it explicitly still runs it.
profile_tool keeps its two cross-vendor checks (setting_id and filament_id) tree-wide, so a
scoped run can still fail on another vendor's files.
EOF
}

msg() { printf '%s\n' "$*" >&2; }
die() { printf 'check_profile.sh: %s\n' "$*" >&2; exit 2; }

if [ -t 1 ]; then
    C_RED=$'\033[91m'; C_GREEN=$'\033[92m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=''; C_GREEN=''; C_BOLD=''; C_RESET=''
fi

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        -p|--profiles) [ $# -ge 2 ] || die "$1 needs a directory"; PROFILES_DIR="$2"; shift 2 ;;
        -v|--vendor) [ $# -ge 2 ] || die "$1 needs a vendor name"; VENDOR="$2"; shift 2 ;;
        --validator) [ $# -ge 2 ] || die "$1 needs a path"; VALIDATOR="$2"; shift 2 ;;
        --work-dir) [ $# -ge 2 ] || die "$1 needs a directory"; WORK_DIR="$2"; shift 2 ;;
        -l|--log-level) [ $# -ge 2 ] || die "$1 needs a number"; LOG_LEVEL="$2"; shift 2 ;;
        --download) PREFER_DOWNLOAD=1; shift ;;
        --refresh) REFRESH=1; shift ;;
        -*) die "unknown option '$1' (try --help)" ;;
        *)
            known=0
            for check in "${ALL_CHECKS[@]}"; do
                [ "$1" = "${check}" ] && known=1
            done
            [ "${known}" -eq 1 ] || die "unknown check '$1' (try --help)"
            CHECKS[${#CHECKS[@]}]="$1"
            shift
            ;;
    esac
done

NAMED_CHECKS="${#CHECKS[@]}"
[ "${NAMED_CHECKS}" -gt 0 ] || CHECKS=("${ALL_CHECKS[@]}")

[ -d "${PROFILES_DIR}" ] || die "profile directory not found: ${PROFILES_DIR}"
PROFILES_DIR="$(cd -- "${PROFILES_DIR}" && pwd)"

if [ -n "${VENDOR}" ]; then
    wanted="$(printf '%s' "${VENDOR}" | tr '[:upper:]' '[:lower:]')"
    found=""
    suggestion=""
    for file in "${PROFILES_DIR}"/*.json; do
        name="${file##*/}"; name="${name%.json}"
        [ -d "${PROFILES_DIR}/${name}" ] || continue
        if [ "${name}" = "${VENDOR}" ]; then
            found=1
            break
        fi
        [ "$(printf '%s' "${name}" | tr '[:upper:]' '[:lower:]')" = "${wanted}" ] && suggestion="${name}"
    done
    if [ -z "${found}" ]; then
        [ -z "${suggestion}" ] || die "unknown vendor '${VENDOR}'; vendor names are case-sensitive, did you mean '${suggestion}'?"
        die "unknown vendor '${VENDOR}': no such vendor in ${PROFILES_DIR}"
    fi
fi

LOG_DIR="${WORK_DIR}/logs"
mkdir -p "${LOG_DIR}" || die "cannot create ${LOG_DIR}"
WORK_DIR="$(cd -- "${WORK_DIR}" && pwd)"
LOG_DIR="${WORK_DIR}/logs"

wants() {
    local check
    for check in "${CHECKS[@]}"; do
        [ "${check}" = "$1" ] && return 0
    done
    return 1
}

STASHED_USER_DIR=""

stash_user_presets() {
    [ -d "${PROFILES_DIR}/user" ] || return 0
    STASHED_USER_DIR="${WORK_DIR}/user-presets-$$"
    rm -rf "${STASHED_USER_DIR}"
    mv "${PROFILES_DIR}/user" "${STASHED_USER_DIR}" || { STASHED_USER_DIR=""; die "cannot move ${PROFILES_DIR}/user aside"; }
    msg "moved ${PROFILES_DIR}/user aside for the run (restored on exit)"
}

restore_user_presets() {
    find "${PROFILES_DIR}/user" -depth -type d -exec rmdir {} + 2>/dev/null
    [ -n "${STASHED_USER_DIR}" ] || return 0
    if [ -d "${PROFILES_DIR}/user" ]; then
        msg "${PROFILES_DIR}/user is not empty; your presets stay in ${STASHED_USER_DIR}"
        STASHED_USER_DIR=""
        return 0
    fi
    mv "${STASHED_USER_DIR}" "${PROFILES_DIR}/user"
    STASHED_USER_DIR=""
}

LOCK_DIR="${WORK_DIR}/.lock"
mkdir "${LOCK_DIR}" 2>/dev/null ||
    die "another run is using ${WORK_DIR} (pass --work-dir, or remove ${LOCK_DIR} if no run is active)"

cleanup() {
    restore_user_presets
    rmdir "${LOCK_DIR}" 2>/dev/null
}

trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

stash_user_presets

fetch() {
    local url="$1" dest="$2" force="${3:-}"
    if [ "${REFRESH}" -eq 0 ] && [ -z "${force}" ] && [ -s "${dest}" ]; then
        return 0
    fi
    mkdir -p "$(dirname -- "${dest}")" || return 1
    msg "downloading $(basename -- "${dest}") ..."
    if ! curl -fsSL --retry 3 -o "${dest}.part" "${url}"; then
        rm -f "${dest}.part"
        msg "download failed: ${url}"
        return 1
    fi
    mv -f "${dest}.part" "${dest}"
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        return 1
    fi
}

validator_search_dirs() {
    local config
    for config in Release RelWithDebInfo Debug; do
        printf '%s\n' "${REPO_ROOT}"/build*/src/"${config}" "${REPO_ROOT}"/build*/*/src/"${config}"
    done
    printf '%s\n' "${REPO_ROOT}"/build*/src "${REPO_ROOT}"/build*/*/src
}

is_foreign_arch_dir() {
    case "${HOST_ARCH}" in
        arm64|aarch64) case "$1" in *x86_64*|*x86-64*|*x64*|*amd64*) return 0 ;; esac ;;
        x86_64|amd64) case "$1" in *arm64*|*aarch64*) return 0 ;; esac ;;
    esac
    return 1
}

find_local_validator() {
    local dir candidate foreign=""
    while IFS= read -r dir; do
        [ -d "${dir}" ] || continue
        for candidate in \
            "${dir}/OrcaSlicer_profile_validator" \
            "${dir}/OrcaSlicer_profile_validator.exe" \
            "${dir}/OrcaSlicer_profile_validator.app/Contents/MacOS/OrcaSlicer_profile_validator"; do
            [ -f "${candidate}" ] && [ -x "${candidate}" ] || continue
            if ! is_foreign_arch_dir "${dir#"${REPO_ROOT}"/}"; then
                printf '%s\n' "${candidate}"
                return 0
            fi
            [ -n "${foreign}" ] || foreign="${candidate}"
        done
    done <<EOF
$(validator_search_dirs)
EOF
    [ -n "${foreign}" ] || return 1
    msg "no ${HOST_ARCH} build found, falling back to ${foreign}"
    printf '%s\n' "${foreign}"
}

download_validator() {
    local dest="${WORK_DIR}/validator" binary dmg app mounted app_src
    case "${HOST_OS}" in
        Linux)
            case "${HOST_ARCH}" in
                arm64|aarch64) msg "the nightly Linux validator is x86_64; build it locally for ${HOST_ARCH}" ;;
            esac
            binary="${dest}/OrcaSlicer_profile_validator"
            fetch "${VALIDATOR_RELEASE_URL}/OrcaSlicer_profile_validator_Linux_Ubuntu2404_nightly" "${binary}" || return 1
            chmod +x "${binary}" || return 1
            ;;
        Darwin)
            dmg="${dest}/OrcaSlicer_profile_validator.dmg"
            app="${dest}/OrcaSlicer_profile_validator.app"
            binary="${app}/Contents/MacOS/OrcaSlicer_profile_validator"
            fetch "${VALIDATOR_RELEASE_URL}/OrcaSlicer_profile_validator_Mac_universal_nightly.dmg" "${dmg}" || return 1
            if [ ! -x "${binary}" ] || [ "${REFRESH}" -eq 1 ]; then
                mounted="${dest}/mnt"
                rm -rf "${mounted}" "${app}"
                mkdir -p "${mounted}" || return 1
                hdiutil attach -nobrowse -readonly -mountpoint "${mounted}" "${dmg}" >/dev/null || return 1
                app_src="$(find "${mounted}" -maxdepth 1 -name '*.app' -print 2>/dev/null | head -n 1)"
                if [ -n "${app_src}" ]; then
                    cp -R "${app_src}" "${app}"
                fi
                hdiutil detach "${mounted}" >/dev/null 2>&1
                rmdir "${mounted}" 2>/dev/null
                [ -x "${binary}" ] || { msg "no validator app inside ${dmg}"; return 1; }
            fi
            ;;
        Windows)
            binary="${dest}/OrcaSlicer_profile_validator.exe"
            fetch "${VALIDATOR_RELEASE_URL}/OrcaSlicer_profile_validator_Windows_nightly.exe" "${binary}" || return 1
            chmod +x "${binary}" || return 1
            ;;
        *)
            msg "no nightly validator published for ${HOST_OS}; build it (-DORCA_TOOLS=ON) and pass --validator"
            return 1
            ;;
    esac
    printf '%s\n' "${binary}"
}

resolve_validator() {
    if [ -n "${VALIDATOR}" ]; then
        [ -x "${VALIDATOR}" ] || die "validator not executable: ${VALIDATOR}"
        return 0
    fi
    if [ "${PREFER_DOWNLOAD}" -eq 0 ]; then
        VALIDATOR="$(find_local_validator)"
        if [ -n "${VALIDATOR}" ]; then
            msg "using locally built validator: ${VALIDATOR}"
            return 0
        fi
    fi
    VALIDATOR="$(download_validator)" || die "could not obtain a profile validator"
    msg "using downloaded validator: ${VALIDATOR}"
}

check_profile_tool() {
    python3 "${REPO_ROOT}/scripts/orca_profile_tool.py" check --profiles "${PROFILES_DIR}" --vendor "${VENDOR}"
}

check_validate_system() {
    "${VALIDATOR}" -p "${PROFILES_DIR}" -v "${VENDOR}" -l "${LOG_LEVEL}"
}

check_validate_slice() {
    "${VALIDATOR}" -p "${PROFILES_DIR}" -v "${VENDOR}" -s -l "${LOG_LEVEL}"
}

check_validate_filament_subtypes() {
    "${VALIDATOR}" -p "${PROFILES_DIR}" -v "${VENDOR}" -l "${LOG_LEVEL}" -f
}

vendor_display_name() {
    local name
    name="$(VENDOR_JSON="${PROFILES_DIR}/${VENDOR}.json" python3 - <<'PY'
import json
import os

try:
    with open(os.environ["VENDOR_JSON"], encoding="utf-8") as fh:
        print(json.load(fh).get("name", ""))
except Exception:
    pass
PY
)"
    printf '%s\n' "${name:-${VENDOR}}"
}

check_validate_custom() {
    local fixtures_dir="${WORK_DIR}/profile-fixtures"
    local output_dir="${WORK_DIR}/custom-preset-validation"
    local summary="${output_dir}/summary.md"
    local status=0 failed_logs="" vendor_prefix="" total_kept=0
    local version asset expected_sha256 asset_url fixture_zip profile_tree log_path actual_sha256 result kept

    command -v unzip >/dev/null 2>&1 || { msg "unzip is required for validate_custom"; return 1; }
    [ -z "${VENDOR}" ] || vendor_prefix="$(vendor_display_name)_"
    mkdir -p "${fixtures_dir}" "${output_dir}" || return 1

    fetch "${FIXTURE_RELEASE_URL}/manifest.json" "${fixtures_dir}/manifest.json" force || return 1

    MANIFEST_PATH="${fixtures_dir}/manifest.json" python3 - > "${fixtures_dir}/fixtures.tsv" <<'PY'
import json
import os

with open(os.environ["MANIFEST_PATH"], encoding="utf-8") as fh:
    manifest = json.load(fh)

if isinstance(manifest, dict):
    entries = manifest.get("fixtures", [])
else:
    entries = manifest

for entry in entries:
    version = entry.get("version", "")
    asset = entry.get("asset", "")
    sha256 = entry.get("asset_sha256", "")
    if not version or not asset:
        continue
    print(f"{version}\t{asset}\t{sha256}")
PY

    if [ ! -s "${fixtures_dir}/fixtures.tsv" ]; then
        echo "No custom preset fixtures found in ${FIXTURE_RELEASE_URL}/manifest.json"
        return 1
    fi

    {
        echo "## Custom Preset Fixture Validation"
        echo ""
        echo "| Version | Status | Log |"
        echo "| --- | --- | --- |"
    } > "${summary}"

    while IFS=$'\t' read -r version asset expected_sha256; do
        [ -n "${version}" ] || continue
        fixture_zip="${fixtures_dir}/${asset}"
        profile_tree="${output_dir}/profiles-${version}"
        log_path="${output_dir}/${version}.log"

        asset_url="${FIXTURE_RELEASE_URL}/$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "${asset}")"
        fetch "${asset_url}" "${fixture_zip}" || return 1

        if [ -n "${expected_sha256}" ] && [ "${expected_sha256}" != "<sha256>" ]; then
            actual_sha256="$(sha256_of "${fixture_zip}")"
            if [ -z "${actual_sha256}" ]; then
                msg "no sha256 tool available, skipping checksum of ${asset}"
            elif [ "${actual_sha256}" != "${expected_sha256}" ]; then
                msg "checksum mismatch for ${asset}, re-downloading"
                rm -f "${fixture_zip}"
                fetch "${asset_url}" "${fixture_zip}" || return 1
                actual_sha256="$(sha256_of "${fixture_zip}")"
                [ "${actual_sha256}" = "${expected_sha256}" ] || { msg "${asset}: expected ${expected_sha256}, got ${actual_sha256}"; return 1; }
            fi
        fi

        msg "validating custom presets from ${version} ..."
        rm -rf "${profile_tree}"
        mkdir -p "${profile_tree}" || return 1
        if [ -n "${VENDOR}" ]; then
            cp -a "${PROFILES_DIR}"/*.json "${PROFILES_DIR}/${VENDOR}" \
                "${PROFILES_DIR}/OrcaFilamentLibrary" "${profile_tree}/" || return 1
            unzip -q "${fixture_zip}" "user/*/${vendor_prefix}*" -d "${profile_tree}"
            result=$?
            [ "${result}" -eq 0 ] || [ "${result}" -eq 11 ] || return 1
            kept="$(find "${profile_tree}/user" -type f 2>/dev/null | wc -l | tr -d ' ')"
            total_kept=$((total_kept + kept))
            msg "  ${kept} ${VENDOR} preset file(s)"
        else
            cp -a "${PROFILES_DIR}/." "${profile_tree}/" || return 1
            rm -rf "${profile_tree}/user"
            unzip -q "${fixture_zip}" -d "${profile_tree}" || return 1
        fi

        "${VALIDATOR}" -p "${profile_tree}" -v "${VENDOR}" -l "${LOG_LEVEL}" > "${log_path}" 2>&1
        result=$?

        if [ "${result}" -eq 0 ]; then
            echo "| ${version} | PASS | ${version}.log |" >> "${summary}"
            rm -rf "${profile_tree}"
        else
            echo "| ${version} | FAIL | ${version}.log |" >> "${summary}"
            failed_logs="${failed_logs}${log_path}"$'\n'
            status=1
        fi
    done < "${fixtures_dir}/fixtures.tsv"

    if [ -n "${VENDOR}" ] && [ "${total_kept}" -eq 0 ]; then
        msg "no ${VENDOR} presets in any fixture; validate_custom checked nothing"
    fi

    cat "${summary}"
    if [ -n "${failed_logs}" ]; then
        echo ""
        echo "## Failed Fixture Logs"
        while IFS= read -r log_path; do
            [ -n "${log_path}" ] || continue
            echo ""
            echo "### $(basename "${log_path}" .log)"
            echo '```'
            head -c 12000 "${log_path}" || echo "No output captured"
            echo '```'
        done <<EOF
${failed_logs}
EOF
    fi

    return "${status}"
}

comment_heading() {
    case "$1" in
        profile_tool) echo "### Profile Check Failed (orca_profile_tool.py)" ;;
        validate_system) echo "### System Profile Validation Failed" ;;
        validate_slice) echo "### Slice Validation Failed (custom g-code expansion)" ;;
        validate_filament_subtypes) echo "### Filament Subtype Validation Failed" ;;
        validate_custom) echo "### Custom Preset Validation Failed" ;;
    esac
}

run_check() {
    local name="$1"
    local log="${LOG_DIR}/${name}.log"
    local result
    printf '\n%s==> %s%s%s\n' "${C_BOLD}" "${name}" "${VENDOR:+ (${VENDOR})}" "${C_RESET}"
    "check_${name}" 2>&1 | tee "${log}"
    result="${PIPESTATUS[0]}"
    if [ "${result}" -eq 0 ]; then
        printf '%s    %s passed%s\n' "${C_GREEN}" "${name}" "${C_RESET}"
        RESULTS="${RESULTS}${name}"$'\t'"pass"$'\n'
    else
        printf '%s    %s failed (exit %s)%s\n' "${C_RED}" "${name}" "${result}" "${C_RESET}"
        RESULTS="${RESULTS}${name}"$'\t'"fail"$'\n'
    fi
}

if wants validate_system || wants validate_slice || wants validate_filament_subtypes || wants validate_custom; then
    resolve_validator
fi

for check in "${ALL_CHECKS[@]}"; do
    wants "${check}" || continue
    if [ "${check}" = validate_slice ] && [ "${NAMED_CHECKS}" -eq 0 ] && [ -n "${VENDOR}" ] &&
        [ ! -d "${PROFILES_DIR}/${VENDOR}/machine" ]; then
        RESULTS="${RESULTS}${check}"$'\t'"skip"$'\t'"${VENDOR} ships no printers"$'\n'
    else
        run_check "${check}"
    fi
done

failed=0
skipped=0
printf '\n%s==> summary%s\n' "${C_BOLD}" "${C_RESET}"
while IFS=$'\t' read -r name result why; do
    [ -n "${name}" ] || continue
    if [ "${result}" = "pass" ]; then
        printf '%s    PASS%s  %s\n' "${C_GREEN}" "${C_RESET}" "${name}"
    elif [ "${result}" = "skip" ]; then
        printf '%s    SKIP%s  %s  (%s)\n' "${C_BOLD}" "${C_RESET}" "${name}" "${why}"
        skipped=1
    else
        printf '%s    FAIL%s  %s  (%s)\n' "${C_RED}" "${C_RESET}" "${name}" "${LOG_DIR}/${name}.log"
        failed=1
    fi
done <<EOF
${RESULTS}
EOF

if [ "${failed}" -eq 0 ]; then
    rm -f "${WORK_DIR}/pr_comment.md"
    if [ "${skipped}" -eq 0 ]; then
        printf '\n%sAll checks passed.%s Logs: %s\n' "${C_GREEN}" "${C_RESET}" "${LOG_DIR}"
    else
        printf '\n%sEvery check that ran passed%s, but CI runs the skipped ones too. Logs: %s\n' \
            "${C_GREEN}" "${C_RESET}" "${LOG_DIR}"
    fi
    exit 0
fi

{
    echo "<!-- profile-validation-comment -->"
    echo "## :x: Profile Validation Errors"
    echo ""
    while IFS=$'\t' read -r name result; do
        [ "${result}" = "fail" ] || continue
        comment_heading "${name}"
        echo ""
        echo '```'
        head -c 30000 "${LOG_DIR}/${name}.log" || echo "No output captured"
        echo '```'
        echo ""
    done <<INNER
${RESULTS}
INNER
    echo "---"
    # shellcheck disable=SC2016
    echo '*Fix the errors above and push a new commit. To reproduce this run locally: `scripts/check_profile.sh`, or `scripts\check_profile.bat` on Windows.*'
} > "${WORK_DIR}/pr_comment.md"

printf '\n%sOne or more profile checks failed.%s Logs: %s\n' "${C_RED}" "${C_RESET}" "${LOG_DIR}"
printf 'The comment CI would post: %s\n' "${WORK_DIR}/pr_comment.md"
exit 1
