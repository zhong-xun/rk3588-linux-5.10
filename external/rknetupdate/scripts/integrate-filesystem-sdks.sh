#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE_DIR="$PROJECT_DIR/packaging/filesystem"
LINUX_ROOT="/home/topeet/Linux"
MODE=check
INCLUDE_EXTBOOT=1
declare -a REQUESTED_SDKS=()

usage()
{
  cat <<'EOF'
Usage: integrate-filesystem-sdks.sh [--check|--apply] [OPTIONS]

Install RKNetUpdate into all standard Rockchip Ubuntu, Debian and Buildroot
rootfs build stages. *_extboot_6.1 trees are included for their Ubuntu rootfs;
they receive the service in dry-run mode because they do not provide the
single-slot Recovery contract used by rootfs/update.img writes.

Options:
  --check                 Verify integration without changing files (default)
  --apply                 Apply/update managed hooks and Buildroot options
  --linux-root PATH       Rockchip SDK collection (default: /home/topeet/Linux)
  --sdk PATH              Process one SDK root; may be supplied more than once
  --no-extboot            Skip *_extboot_6.1 trees
  -h, --help              Show this help
EOF
}

while (($#)); do
  case "$1" in
    --check) MODE=check; shift ;;
    --apply) MODE=apply; shift ;;
    --linux-root)
      (($# >= 2)) || { echo "missing value for --linux-root" >&2; exit 2; }
      LINUX_ROOT="$2"; shift 2 ;;
    --sdk)
      (($# >= 2)) || { echo "missing value for --sdk" >&2; exit 2; }
      REQUESTED_SDKS+=("$2"); shift 2 ;;
    --no-extboot) INCLUDE_EXTBOOT=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for file in \
  "$TEMPLATE_DIR/rknetupdate.conf" \
  "$TEMPLATE_DIR/rknetupdate.safe.conf" \
  "$TEMPLATE_DIR/rknetupdate.standard-emmc.conf" \
  "$TEMPLATE_DIR/systemd/rknetupdate.service" \
  "$TEMPLATE_DIR/sysv/S90rknetupdate" \
  "$TEMPLATE_DIR/post-rknetupdate-rootfs.sh"; do
  [ -r "$file" ] || { echo "missing filesystem template: $file" >&2; exit 1; }
done
sh -n "$TEMPLATE_DIR/sysv/S90rknetupdate"
bash -n "$TEMPLATE_DIR/post-rknetupdate-rootfs.sh"

declare -a SDKS=()
if ((${#REQUESTED_SDKS[@]})); then
  SDKS=("${REQUESTED_SDKS[@]}")
else
  while IFS= read -r sdk; do SDKS+=("$sdk"); done < <(
    find "$LINUX_ROOT" -mindepth 2 -maxdepth 2 -type d \
      \( -name 'rk*-linux-5.10' -o -name 'rk*-linux-6.1' \) | sort
  )
  if ((INCLUDE_EXTBOOT)); then
    while IFS= read -r sdk; do SDKS+=("$sdk"); done < <(
      find "$LINUX_ROOT" -mindepth 2 -maxdepth 2 -type d \
        -name '*_extboot_6.1' | sort
    )
  fi
fi
((${#SDKS[@]})) || { echo "no SDKs found" >&2; exit 1; }

declare -a COMPONENT_ARGS=("--$MODE")
for sdk in "${SDKS[@]}"; do
  COMPONENT_ARGS+=(--sdk "$sdk")
done
"$PROJECT_DIR/scripts/install-sdk-rknetupdate.sh" "${COMPONENT_ARGS[@]}"

same_file()
{
  [ -f "$1" ] && [ -f "$2" ] && cmp -s "$1" "$2"
}

managed_text()
{
  [ -f "$1" ] && {
    grep -q '^# RKNETUPDATE-MANAGED-FILESYSTEM-HOOK-V1$' "$1" ||
      grep -q '^# RKNETUPDATE-MANAGED-FILESYSTEM-V1$' "$1" ||
      grep -q '^# RKNETUPDATE-MANAGED-FILESYSTEM-CONFIG-V1$' "$1" ||
      grep -q '^ExecStart=/usr/sbin/rknetupdate ' "$1" ||
      grep -q '^DAEMON=/usr/sbin/rknetupdate$' "$1"
  }
}

install_text()
{
  local src="$1" dst="$2" mode="$3"
  if same_file "$src" "$dst"; then return 0; fi
  if [ -e "$dst" ] && ! managed_text "$dst"; then
    echo "refusing to replace unmanaged file: $dst" >&2
    return 1
  fi
  if [ "$MODE" = apply ]; then
    install -D -m "$mode" "$src" "$dst"
  else
    echo "missing or stale managed file: $dst" >&2
    return 1
  fi
}

ensure_config_line()
{
  local file="$1" line="$2"
  grep -Fxq "$line" "$file" && return 0
  if grep -Eq "^${line%%=*}=" "$file"; then
    echo "conflicting ${line%%=*} in $file" >&2
    return 1
  fi
  if [ "$MODE" = apply ]; then
    printf '\n%s\n' "$line" >> "$file"
  else
    echo "missing $line in $file" >&2
    return 1
  fi
}

ensure_buildroot_tools()
{
  local sdk="$1" common="$sdk/buildroot/configs/rockchip/base/common.config"
  [ -r "$common" ] || return 0
  grep -Fq '#include "fs/e2fs.config"' "$common" || {
    echo "missing e2fs config include: $common" >&2; return 1;
  }
  ensure_config_line "$common" 'BR2_PACKAGE_GPTFDISK=y' || return 1
  ensure_config_line "$common" 'BR2_PACKAGE_GPTFDISK_SGDISK=y' || return 1
}

ensure_external_rootfs_hook()
{
  local file="$1"
  local old_marker='# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V1'
  local init_marker='# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-INIT'
  local call_marker='# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-CALL'
  [ -f "$file" ] || return 0

  if grep -Fxq "$init_marker" "$file" || grep -Fxq "$call_marker" "$file"; then
    if [ "$(grep -Fxc "$init_marker" "$file")" -ne 1 ] ||
       [ "$(grep -Fxc "$call_marker" "$file")" -ne 1 ] ||
       grep -Fxq "$old_marker" "$file"; then
      echo "incomplete or conflicting RKNetUpdate V2 rootfs hook in $file" >&2
      return 1
    fi
    for expected in \
      'RKNETUPDATE_POST_BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"' \
      'RKNETUPDATE_SDK_DIR="$(cd "$RKNETUPDATE_POST_BUILD_DIR/.." && pwd)"' \
      'RKNETUPDATE_TARGET_DIR="$(realpath "${1:?target rootfs is required}")"' \
      'RKNETUPDATE_ROOTFS_HOOK="$RKNETUPDATE_SDK_DIR/device/rockchip/common/scripts/post-rknetupdate-rootfs.sh"' \
      'RKNETUPDATE_STANDALONE=1 "$RKNETUPDATE_ROOTFS_HOOK" "$RKNETUPDATE_TARGET_DIR" unknown'; do
      grep -Fqx "$expected" "$file" || {
        echo "stale RKNetUpdate V2 rootfs hook in $file: $expected" >&2
        return 1
      }
    done
    bash -n "$file" || return 1
    return 0
  fi

  if ! grep -Fxq "$old_marker" "$file" && grep -q 'post-rknetupdate-rootfs.sh' "$file"; then
    echo "conflicting RKNetUpdate rootfs hook in $file" >&2
    return 1
  fi
  if [ "$MODE" != apply ]; then
    if grep -Fxq "$old_marker" "$file"; then
      echo "stale V1 external rootfs hook: $file" >&2
    else
      echo "missing external rootfs hook: $file" >&2
    fi
    return 1
  fi

  local line stripped with_init completed
  stripped="$(mktemp "${file}.rknetupdate-stripped.XXXXXX")" || return 1
  with_init="$(mktemp "${file}.rknetupdate-init.XXXXXX")" || {
    rm -f "$stripped"
    return 1
  }
  completed="$(mktemp "${file}.rknetupdate-complete.XXXXXX")" || {
    rm -f "$stripped" "$with_init"
    return 1
  }

  awk -v marker="$old_marker" '
    $0 == marker { skipping = 1; found = 1; next }
    skipping && $0 == "fi" { skipping = 0; next }
    !skipping { print }
    END { if (skipping) exit 42 }
  ' "$file" > "$stripped" || {
    rm -f "$stripped" "$with_init" "$completed"
    return 1
  }

  head -n 1 "$stripped" > "$with_init"
  cat >> "$with_init" <<'EOF'

# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-INIT
# Capture both paths before the vendor script changes its working directory.
RKNETUPDATE_POST_BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RKNETUPDATE_SDK_DIR="$(cd "$RKNETUPDATE_POST_BUILD_DIR/.." && pwd)"
RKNETUPDATE_TARGET_DIR="$(realpath "${1:?target rootfs is required}")"
RKNETUPDATE_ROOTFS_HOOK="$RKNETUPDATE_SDK_DIR/device/rockchip/common/scripts/post-rknetupdate-rootfs.sh"
EOF
  tail -n +2 "$stripped" >> "$with_init"

  line="$(grep -n '^exit 0$' "$with_init" | tail -1 | cut -d: -f1 || true)"
  if [ -n "$line" ]; then
    head -n "$((line - 1))" "$with_init" > "$completed"
  else
    cp "$with_init" "$completed"
  fi
  cat >> "$completed" <<'EOF'

# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-CALL
if [ ! -x "$RKNETUPDATE_ROOTFS_HOOK" ]; then
  echo "RKNetUpdate: missing rootfs hook: $RKNETUPDATE_ROOTFS_HOOK" >&2
  exit 1
fi
RKNETUPDATE_STANDALONE=1 "$RKNETUPDATE_ROOTFS_HOOK" "$RKNETUPDATE_TARGET_DIR" unknown
EOF
  if [ -n "$line" ]; then
    tail -n +"$line" "$with_init" >> "$completed"
  fi
  bash -n "$completed" || {
    rm -f "$stripped" "$with_init" "$completed"
    return 1
  }
  chmod --reference="$file" "$completed" || {
    rm -f "$stripped" "$with_init" "$completed"
    return 1
  }
  mv "$completed" "$file" || {
    rm -f "$stripped" "$with_init" "$completed"
    return 1
  }
  rm -f "$stripped" "$with_init"
}

apply_sdk()
{
  local sdk="$1" common hook link profile profile_name
  sdk="$(realpath "$sdk")"
  common="$sdk/device/rockchip/common"
  for required in "$sdk/build.sh" "$common/scripts/post-helper" "$common/post-hooks"; do
    [ -e "$required" ] || {
      echo "not a supported Rockchip filesystem SDK ($required missing): $sdk" >&2
      return 1
    }
  done

  hook="$common/scripts/post-rknetupdate-rootfs.sh"
  install_text "$TEMPLATE_DIR/post-rknetupdate-rootfs.sh" "$hook" 0755 || return 1

  case "$(basename "$sdk")" in
    *_extboot_6.1)
      profile="$TEMPLATE_DIR/rknetupdate.safe.conf"
      profile_name=safe-extboot
      ;;
    *)
      profile="$TEMPLATE_DIR/rknetupdate.standard-emmc.conf"
      profile_name=standard-emmc
      ;;
  esac
  install_text "$profile" "$common/scripts/rknetupdate-filesystem.conf" 0644 || return 1

  link="$common/post-hooks/91-rknetupdate-rootfs.sh"
  if [ -L "$link" ] && [ "$(readlink "$link")" = '../scripts/post-rknetupdate-rootfs.sh' ]; then
    :
  elif [ -e "$link" ] || [ -L "$link" ]; then
    echo "refusing unexpected post-hook entry: $link" >&2
    return 1
  elif [ "$MODE" = apply ]; then
    ln -s ../scripts/post-rknetupdate-rootfs.sh "$link" || return 1
  else
    echo "missing post-hook link: $link" >&2
    return 1
  fi

  ensure_external_rootfs_hook "$sdk/ubuntu/post-build.sh" || return 1
  ensure_external_rootfs_hook "$sdk/debian/post-build.sh" || return 1
  if [ "$profile_name" = standard-emmc ]; then
    ensure_buildroot_tools "$sdk" || return 1
  fi
  printf 'OK\t%s\t%s\n' "$profile_name" "$sdk"
}

failures=0
for sdk in "${SDKS[@]}"; do
  if ! apply_sdk "$sdk"; then failures=$((failures + 1)); fi
done

if ((failures)); then
  echo "$failures filesystem SDK integration(s) failed" >&2
  exit 1
fi
echo "RKNetUpdate filesystem $MODE completed for ${#SDKS[@]} SDK(s)."
