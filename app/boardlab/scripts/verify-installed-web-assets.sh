#!/usr/bin/env bash
# Verify the Web assets served from BoardLab's QRC, rather than files in a
# source checkout.  This catches the common "rsync only" update mistake.
set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE[0]}"
if command -v readlink >/dev/null 2>&1; then
  RESOLVED_SCRIPT="$(readlink -f -- "${SCRIPT_PATH}" 2>/dev/null || true)"
  [[ -n "${RESOLVED_SCRIPT}" ]] && SCRIPT_PATH="${RESOLVED_SCRIPT}"
fi
SCRIPT_DIR="$(cd -- "$(dirname -- "${SCRIPT_PATH}")" && pwd -P)"
DEFAULT_PREFIX="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"

PREFIX="${BOARDLAB_PREFIX:-${DEFAULT_PREFIX}}"
BINARY=""
MANIFEST=""
SOURCE_DIR=""
BASE_URL=""
PORT="${BOARDLAB_WEB_VERIFY_PORT:-}"
TMP_DIR=""
APP_PID=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--prefix PATH] [--binary PATH] [--manifest PATH] [--source PATH] [--url URL] [--port PORT]

Without --url, starts the installed BoardLab binary on a loopback-only,
temporary Web port and compares its QRC-served CSS/JavaScript with the
installed manifest.  It never starts or enables boardlab.service.

  --source PATH  Also require the installed manifest to match PATH/app.js and
                 PATH/styles.css. Use this immediately after rebuilding.
  --url URL      Verify an already running BoardLab Web bridge instead of
                 starting a temporary process, for example:
                 http://127.0.0.1:17999
EOF
}

fail() {
  printf 'BoardLab Web QRC verification failed: %s\n' "$*" >&2
  if [[ -n "${TMP_DIR}" && -f "${TMP_DIR}/boardlab.log" ]]; then
    printf '%s\n' '--- temporary BoardLab log ---' >&2
    sed -n '1,120p' "${TMP_DIR}/boardlab.log" >&2 || true
  fi
  exit 1
}

cleanup() {
  if [[ -n "${APP_PID}" ]] && kill -0 "${APP_PID}" 2>/dev/null; then
    kill "${APP_PID}" 2>/dev/null || true
    wait "${APP_PID}" 2>/dev/null || true
  fi
  [[ -n "${TMP_DIR}" ]] && rm -rf -- "${TMP_DIR}"
}
trap cleanup EXIT

require_value() {
  local option="$1"
  [[ "$#" -ge 2 ]] || fail "missing value for ${option}"
}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --prefix)
      require_value "$@"
      PREFIX="$2"
      shift 2
      ;;
    --binary)
      require_value "$@"
      BINARY="$2"
      shift 2
      ;;
    --manifest)
      require_value "$@"
      MANIFEST="$2"
      shift 2
      ;;
    --source)
      require_value "$@"
      SOURCE_DIR="$2"
      shift 2
      ;;
    --url)
      require_value "$@"
      BASE_URL="$2"
      shift 2
      ;;
    --port)
      require_value "$@"
      PORT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

[[ -n "${BINARY}" ]] || BINARY="${PREFIX}/bin/boardlab"
[[ -n "${MANIFEST}" ]] || MANIFEST="${PREFIX}/share/boardlab/web-assets.sha256"
[[ -x "${BINARY}" ]] || fail "installed binary is missing or not executable: ${BINARY}"
[[ -r "${MANIFEST}" ]] || fail "Web asset manifest is missing: ${MANIFEST}"
command -v curl >/dev/null 2>&1 || fail "curl is required to fetch the QRC-served assets"
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is required to validate the asset fingerprints"

if [[ -n "${SOURCE_DIR}" ]]; then
  [[ -d "${SOURCE_DIR}" ]] || fail "source Web directory is missing: ${SOURCE_DIR}"
fi

manifest_entries=0
seen_app_js=0
seen_styles_css=0
while read -r expected_hash asset_name ignored; do
  [[ -z "${expected_hash}" || "${expected_hash}" == \#* ]] && continue
  [[ -z "${ignored}" ]] || fail "invalid manifest line in ${MANIFEST}"
  [[ "${expected_hash}" =~ ^[0-9a-fA-F]{64}$ ]] || fail "invalid SHA-256 in ${MANIFEST}: ${expected_hash}"
  [[ "${asset_name}" =~ ^[A-Za-z0-9._-]+$ ]] || fail "unsafe asset name in ${MANIFEST}: ${asset_name}"
  case "${asset_name}" in
    app.js|styles.css)
      ;;
    *)
      fail "unsupported Web asset in ${MANIFEST}: ${asset_name}"
      ;;
  esac
  case "${asset_name}" in
    app.js)
      [[ "${seen_app_js}" -eq 0 ]] || fail "duplicate manifest entry: ${asset_name}"
      seen_app_js=1
      ;;
    styles.css)
      [[ "${seen_styles_css}" -eq 0 ]] || fail "duplicate manifest entry: ${asset_name}"
      seen_styles_css=1
      ;;
  esac

  if [[ -n "${SOURCE_DIR}" ]]; then
    [[ -r "${SOURCE_DIR}/${asset_name}" ]] || fail "source asset is missing: ${SOURCE_DIR}/${asset_name}"
    source_hash="$(sha256sum "${SOURCE_DIR}/${asset_name}" | awk '{print $1}')"
    [[ "${source_hash}" == "${expected_hash,,}" ]] || fail "source asset changed after installation: ${asset_name}; rebuild and reinstall before restarting BoardLab"
  fi
  manifest_entries=$((manifest_entries + 1))
done <"${MANIFEST}"
[[ "${manifest_entries}" -eq 2 && "${seen_app_js}" -eq 1 && "${seen_styles_css}" -eq 1 ]] || fail "manifest must contain one app.js and one styles.css entry"

if [[ -z "${TMP_DIR}" ]]; then
  TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/boardlab-web-qrc.XXXXXX")"
fi

wait_for_url() {
  local url="$1"
  local attempts="$2"
  local attempt
  for ((attempt = 1; attempt <= attempts; ++attempt)); do
    if curl -fsS --max-time 1 "${url}/healthz" >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "${APP_PID}" ]] && ! kill -0 "${APP_PID}" 2>/dev/null; then
      return 1
    fi
    sleep 0.1
  done
  return 1
}

start_temporary_bridge() {
  local candidate_port
  local attempt
  local start_port

  [[ -n "${TMP_DIR}" ]] || TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/boardlab-web-qrc.XXXXXX")"
  printf '%s\n' 'boardlab-web-qrc-verify' >"${TMP_DIR}/web.token"
  if [[ -n "${PORT}" ]]; then
    [[ "${PORT}" =~ ^[0-9]+$ ]] || fail "invalid port: ${PORT}"
    [[ "${PORT}" -ge 1 && "${PORT}" -le 65535 ]] || fail "port out of range: ${PORT}"
    start_port="${PORT}"
  else
    start_port="$((30000 + ($$ % 20000)))"
  fi

  for ((attempt = 0; attempt < 12; ++attempt)); do
    candidate_port="$((start_port + attempt))"
    [[ "${candidate_port}" -le 65535 ]] || candidate_port="$((20000 + attempt))"
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    QML_DISABLE_DISK_CACHE=1 \
    BOARDLAB_WEB_TOKEN= \
    BOARDLAB_WEB_TOKEN_FILE="${TMP_DIR}/web.token" \
      "${BINARY}" --framed --web --web-bind 127.0.0.1 --web-port "${candidate_port}" \
      >"${TMP_DIR}/boardlab.log" 2>&1 &
    APP_PID=$!
    BASE_URL="http://127.0.0.1:${candidate_port}"
    if wait_for_url "${BASE_URL}" 35; then
      return 0
    fi
    if kill -0 "${APP_PID}" 2>/dev/null; then
      kill "${APP_PID}" 2>/dev/null || true
      wait "${APP_PID}" 2>/dev/null || true
    fi
    APP_PID=""
  done
  fail "temporary BoardLab Web bridge did not become ready; see the log above"
}

if [[ -n "${BASE_URL}" ]]; then
  BASE_URL="${BASE_URL%/}"
  case "${BASE_URL}" in
    http://*|https://*)
      ;;
    *)
      fail "--url must begin with http:// or https://"
      ;;
  esac
  wait_for_url "${BASE_URL}" 5 || fail "Web bridge is not healthy at ${BASE_URL}"
else
  start_temporary_bridge
fi

verified_entries=0
while read -r expected_hash asset_name ignored; do
  [[ -z "${expected_hash}" || "${expected_hash}" == \#* ]] && continue
  actual_file="${TMP_DIR}/${asset_name}.actual"
  curl -fsS --max-time 5 "${BASE_URL}/assets/${asset_name}" -o "${actual_file}" || fail "cannot fetch ${BASE_URL}/assets/${asset_name}"
  actual_hash="$(sha256sum "${actual_file}" | awk '{print $1}')"
  [[ "${actual_hash}" == "${expected_hash,,}" ]] || fail "${asset_name} served by ${BASE_URL} does not match the installed QRC manifest; restart BoardLab from the updated desktop launcher"
  printf 'verified embedded Web asset: %s (%s)\n' "${asset_name}" "${actual_hash}"
  verified_entries=$((verified_entries + 1))
done <"${MANIFEST}"

[[ "${verified_entries}" -eq "${manifest_entries}" ]] || fail "manifest entries changed during verification"
printf 'BoardLab Web QRC assets are current: %s\n' "${BINARY}"
