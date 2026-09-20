#!/bin/bash
# Build and test the muxic fork in a container. See tools/muxic/README.md.
set -euo pipefail

IMAGE=mixxx-muxic-buildenv
JOBS=${MUXIC_JOBS:-6}
SLOTS=${MUXIC_BUILD_SLOTS:-2}

SRC=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
GIT_COMMON=$(cd "$SRC" && cd "$(git rev-parse --git-common-dir)" && pwd)
MAIN_REPO=$(dirname "$GIT_COMMON")
BUILD="$SRC/build"
CCACHE="${MUXIC_CCACHE_DIR:-$HOME/.cache/mixxx-muxic-ccache}"
LOCKS="$CCACHE/locks"

mkdir -p "$BUILD" "$CCACHE" "$LOCKS"

in_container() {
    docker run --rm -i \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e CCACHE_BASEDIR="$SRC" \
        -v "$CCACHE:/ccache" \
        -v "$MAIN_REPO:$MAIN_REPO" \
        -v "$SRC:$SRC" \
        -w "$SRC" \
        "$IMAGE" "$@"
}

# A heavy step takes one of $SLOTS locks, so parallel worktrees do not
# exhaust the memory of the machine.
with_slot() {
    local n
    while true; do
        for n in $(seq 1 "$SLOTS"); do
            exec {fd}>"$LOCKS/slot$n"
            if flock -n "$fd"; then
                "$@"
                return
            fi
            exec {fd}>&-
        done
        sleep 5
    done
}

case "${1:-}" in
    image)
        docker build -t "$IMAGE" -f "$SRC/tools/muxic/Dockerfile" "$SRC/tools"
        ;;
    configure)
        shift
        in_container cmake -G Ninja -S "$SRC" -B "$BUILD" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
            "$@"
        ;;
    build)
        shift
        with_slot in_container nice cmake --build "$BUILD" --parallel "$JOBS" "$@"
        ;;
    test)
        shift
        with_slot in_container "$BUILD/mixxx-test" "$@"
        ;;
    run)
        shift
        in_container "$@"
        ;;
    *)
        echo "Usage: $0 image | configure [cmake args] | build [cmake --build args] | test [gtest args] | run <command>" >&2
        exit 1
        ;;
esac
