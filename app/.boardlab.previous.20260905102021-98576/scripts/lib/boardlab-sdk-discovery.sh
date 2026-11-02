#!/usr/bin/env bash

# Print NUL-delimited SDK roots whose Buildroot tree integrates BoardLab.
# Callers own validation and must not infer support from directory names.
boardlab_discover_sdk_roots()
{
    local linux_root="$1"

    boardlab_scan_sdk_parent "$linux_root" | sort -zu
}

boardlab_scan_sdk_parent()
{
    local directory="$1"
    local child
    local name

    if [ -f "$directory/buildroot/package/rockchip/boardlab/boardlab.mk" ] &&
       [ -f "$directory/buildroot/package/rockchip/Config.in" ]; then
        printf '%s\0' "$directory"
        return
    fi

    for child in "$directory"/* "$directory"/.[!.]* "$directory"/..?*; do
        [ -d "$child" ] && [ ! -L "$child" ] || continue
        name=${child##*/}
        case "$name" in
            .git|.repo|app|build|buildroot|debian|device|dl|external|IMAGE|images|kernel|output|rkbin|rockdev|rootfs|tmp|u-boot|ubuntu|yocto)
                continue
                ;;
        esac
        boardlab_scan_sdk_parent "$child"
    done
}
