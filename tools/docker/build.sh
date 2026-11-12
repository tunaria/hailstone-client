#!/bin/bash
# Builds, tests and installs the client for one platform, or all, in containers (compose.yaml, docs/ci.md
# "Builds in containers").
#   tools/docker/build.sh linux-x64|linux-arm64|windows-x64|macos-arm64|all [--no-test]
# Each platform's install lands in dist/<platform>/hailstone, ready for tools/make_update.py --dist.
# JOBS sets the parallel jobs. windows-x64 downloads MSVC on its first run and needs MSVC_ACCEPT_LICENSE=1.
# macos-arm64 starts the macOS workflow on GitHub for HEAD (pushed) and unpacks its artifact; it needs gh.
# A server plugin built in (docs/plugins.md): PLUGIN_DIR=<its clone> PLUGIN_NAME=<folder under plugins/>.
# A server's own build: BUILD_TAG=<name> keeps its build folders (build-<platform>-<name>) and installs
# (dist/<name>/<platform>) apart, and CMAKE_ARGS holds its settings, for example
#   CMAKE_ARGS="-DHAILSTONE_UPDATE_FEED=https://example.org/hailstone/hailstone-update.txt -DHAILSTONE_UPDATE_KEY=<hex>"
# The ccache is shared, so a second server's build compiles little.
set -euo pipefail
cd "$(dirname "$0")/../.."
what=${1:?usage: tools/docker/build.sh linux-x64|linux-arm64|windows-x64|macos-arm64|all [--no-test]}
test=1; [ "${2:-}" = --no-test ] && test=0
# The containers run as the checkout's owner, also when root starts this: Chromium (the browser tests) refuses to run as
# root, and nothing in the checkout should end up owned by root.
export HOST_UID=${HOST_UID:-$(stat -c %u . 2>/dev/null || stat -f %u .)} HOST_GID=${HOST_GID:-$(stat -c %g . 2>/dev/null || stat -f %g .)}
if [ "$HOST_UID" = 0 ]; then echo "warning: the checkout belongs to root, so the browser tests will fail (Chromium refuses root)"; fi
mkdir -p .docker/ccache/linux-x64 .docker/ccache/linux-arm64 .docker/msvc .docker/msvc-home .docker/no-plugin dist
# Started by root, the folders it just made are still root's: hand them to the containers' user (Wine refuses a home
# folder it does not own).
if [ "$(id -u)" = 0 ]; then chown "$HOST_UID:$HOST_GID" .docker .docker/ccache .docker/ccache/* .docker/msvc .docker/msvc-home .docker/no-plugin dist; fi
tag=${BUILD_TAG:+-$BUILD_TAG}
out=dist${BUILD_TAG:+/$BUILD_TAG}
read -ra cmake_args <<< "${CMAKE_ARGS:-}"

# Compose v2 (docker compose) or v1 (docker-compose); BuildKit so linux-arm64's image can be built for its platform.
if docker compose version >/dev/null 2>&1; then dc() { docker compose "$@"; }; else dc() { docker-compose "$@"; }; fi
export DOCKER_BUILDKIT=1 COMPOSE_DOCKER_CLI_BUILD=1

install_tree() { # <platform> <build dir> <service>
    rm -rf "$out/$1"
    dc run --rm --no-deps "$3" cmake --install "$2" --prefix "$out/$1/hailstone"
    # The default Windows install also writes zlib's bin/, include/ and lib/ (docs/updater.md step 2).
    rm -rf "$out/$1/hailstone/bin" "$out/$1/hailstone/include" "$out/$1/hailstone/lib"
    # A server plugin built in (PLUGIN_NAME) has no install rule of its own (plugins reach players as add-ons):
    # copy its folder from the build tree, so a build made for one server carries its plugin.
    if [ -n "${PLUGIN_NAME:-}" ] && [ -d "$2/hailstone/plugins/$PLUGIN_NAME" ]; then
        mkdir -p "$out/$1/hailstone/plugins"
        cp -a "$2/hailstone/plugins/$PLUGIN_NAME" "$out/$1/hailstone/plugins/"
        echo "with plugin $PLUGIN_NAME"
    fi
    echo "installed $out/$1/hailstone"
}

linux() { # <platform>
    if [ "$1" = linux-arm64 ] && [ "$(uname -m)" != aarch64 ] && [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
        echo "registering QEMU for arm64 (until the next reboot)"
        docker run --privileged --rm tonistiigi/binfmt --install arm64
    fi
    dc build "$1"
    dc run --rm "$1" tools/docker/linux_build.sh "build-$1$tag" "${cmake_args[@]}"
    if [ $test = 1 ]; then dc run --rm --no-deps -e EQ_GAME_DIR= "$1" "build-$1$tag/hailstone_tests"; fi
    install_tree "$1" "build-$1$tag" "$1"
}

windows() {
    dc build linux-x64
    dc --profile setup build msvc-setup
    if [ ! -x .docker/msvc/bin/x64/cl ]; then
        if [ "${MSVC_ACCEPT_LICENSE:-}" != 1 ]; then
            echo "MSVC is downloaded once: set MSVC_ACCEPT_LICENSE=1 to accept Microsoft's license"; exit 1
        fi
        dc --profile setup run --rm msvc-setup
    fi
    dc run --rm windows-x64 tools/docker/msvc_build.sh . "build-windows-x64$tag" "${cmake_args[@]}"
    if [ $test = 1 ]; then
        # Microsoft's runtime DLLs next to the tests: Wine's own msvcp140_atomic_wait lacks __std_calloc_crt and the
        # run crashes.
        cp -f .docker/msvc/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT/*.dll "build-windows-x64$tag/"
        # Left out under Wine only (GitHub's Windows runner runs them all): the browser tests (Chromium does not run
        # under Wine), "cancelled ends a transfer stalled..." (Wine's WinHTTP never ends the call: docs/ci.md says
        # about 21 s, in a container it hangs), allow_url (Wine's WinHTTP reports another address to the check) and
        # "File time" (MSVC's time zones need Windows' icu.dll: "Module not found").
        dc run --rm --no-deps -e EQ_GAME_DIR= \
            -e WINEDLLOVERRIDES="msvcp140,msvcp140_1,msvcp140_2,msvcp140_atomic_wait,msvcp140_codecvt_ids,vcruntime140,vcruntime140_1,concrt140=n,b" \
            windows-x64 wine "build-windows-x64$tag/hailstone_tests.exe" '-sfe=*browser_host_test*' \
            '-tce=*stalled?waiting?for?the?answer*,*allow_url?refuses?an?empty*,File?time:*'
    fi
    install_tree windows-x64 "build-windows-x64$tag" windows-x64
}

macos() {
    command -v gh >/dev/null || { echo "macos-arm64 needs the GitHub CLI (gh)"; exit 1; }
    sha=$(git rev-parse HEAD)
    if [ -z "$(git branch -r --contains "$sha")" ]; then echo "push $sha first: the workflow builds a pushed commit"; exit 1; fi
    gh workflow run macos.yml -f ref="$sha"
    sleep 10
    run=$(gh run list --workflow macos.yml --limit 1 --json databaseId -q '.[0].databaseId')
    gh run watch "$run" --exit-status
    rm -rf "$out/macos-arm64" && mkdir -p "$out/macos-arm64"
    gh run download "$run" -n hailstone-macos-arm64 -D "$out/macos-arm64"
    tar -C "$out/macos-arm64" -xf "$out/macos-arm64/hailstone-macos-arm64.tar"
    rm "$out/macos-arm64/hailstone-macos-arm64.tar"
    echo "installed $out/macos-arm64/hailstone"
}

case $what in
    linux-x64|linux-arm64) linux "$what" ;;
    windows-x64) windows ;;
    macos-arm64) macos ;;
    all) linux linux-x64; windows; linux linux-arm64; macos ;;
    *) echo "unknown platform $what"; exit 1 ;;
esac
