# BoardLab product environment (login shells / weston profile)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
export QT_WAYLAND_DISABLE_WINDOWDECORATION="${QT_WAYLAND_DISABLE_WINDOWDECORATION:-1}"
export BOARDLAB_DATA_ROOT="${BOARDLAB_DATA_ROOT:-/var/lib/boardlab}"
export BOARDLAB_CACHE_DIR="${BOARDLAB_CACHE_DIR:-/var/cache/boardlab}"
export BOARDLAB_LOG_DIR="${BOARDLAB_LOG_DIR:-/var/log/boardlab}"
