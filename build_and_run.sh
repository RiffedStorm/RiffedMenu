#!/usr/bin/env bash
# RiffedMenu one-click: build -> deploy -> (re)launch GTA:SA through Bottles -> tail logs.
#
#   ./build_and_run.sh              build, deploy, launch, follow logs until the game exits
#   ./build_and_run.sh --build-only build + deploy, don't launch
#   ./build_and_run.sh --no-tail    launch and return immediately
#   ./build_and_run.sh --clean      wipe the build dir first
#
# Env overrides: BOTTLE (default Gry), GAME_EXE (default gta_sa.exe, else gta-sa.exe),
#                WINEDEBUG (default fixme-all,err+all), BUILD_TYPE (default RelWithDebInfo)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(cd "$HERE/.." && pwd)"
BUILD_DIR="$HERE/build"
LOG_DIR="$HERE/logs"
BOTTLE="${BOTTLE:-Gry}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
export WINEDEBUG="${WINEDEBUG:-fixme-all,err+all}"
FLATPAK_APP=com.usebottles.bottles

BUILD_ONLY=0; TAIL=1
for a in "$@"; do
    case "$a" in
        --build-only) BUILD_ONLY=1 ;;
        --no-tail)    TAIL=0 ;;
        --clean)      rm -rf "$BUILD_DIR" ;;
        -h|--help)    sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

c_info() { printf '\e[1;36m[RiffedMenu]\e[0m %s\n' "$*"; }
c_ok()   { printf '\e[1;32m[RiffedMenu]\e[0m %s\n' "$*"; }
c_err()  { printf '\e[1;31m[RiffedMenu]\e[0m %s\n' "$*" >&2; }

# Commands that must run on the Fedora host (flatpak, process control) go through distrobox.
if [ -f /run/.containerenv ] || [ -n "${CONTAINER_ID:-}" ]; then
    host() { distrobox-host-exec "$@"; }
else
    host() { "$@"; }
fi

# ---------------------------------------------------------------- toolchain
need=()
command -v i686-w64-mingw32-g++-win32 >/dev/null || need+=(mingw-w64)
command -v cmake >/dev/null || need+=(cmake)
command -v ninja >/dev/null || need+=(ninja-build)
if [ ${#need[@]} -gt 0 ]; then
    c_info "Installing: ${need[*]}"
    sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${need[@]}"
fi

[ -d "$HERE/external/imgui" ]   || git clone -q --depth 1 --branch v1.91.5 https://github.com/ocornut/imgui.git "$HERE/external/imgui"
[ -d "$HERE/external/minhook" ] || git clone -q --depth 1 --branch v1.3.3 https://github.com/TsudaKageyu/minhook.git "$HERE/external/minhook"

# ---------------------------------------------------------------- build
c_info "Building ($BUILD_TYPE)..."
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/mingw-i686.cmake" >/dev/null
fi
cmake --build "$BUILD_DIR"
ASI="$BUILD_DIR/RiffedMenu.asi"
[ -f "$ASI" ] || { c_err "build produced no $ASI"; exit 1; }

# ---------------------------------------------------------------- pick exe
if [ -z "${GAME_EXE:-}" ]; then
    if   [ -f "$GAME_DIR/gta_sa.exe" ]; then GAME_EXE=gta_sa.exe
    elif [ -f "$GAME_DIR/gta-sa.exe" ]; then GAME_EXE=gta-sa.exe
    else c_err "no gta_sa.exe / gta-sa.exe in $GAME_DIR"; exit 1; fi
fi
EXE_PATH="$GAME_DIR/$GAME_EXE"
# "[g]ta_sa\.exe" style regex: matches the game but never the pgrep/pkill command line itself
PROC_RE="[${GAME_EXE:0:1}]${GAME_EXE:1}"
PROC_RE="${PROC_RE//./\\.}"

# ---------------------------------------------------------------- stop old instance
stop_game() {
    if host pgrep -f "$PROC_RE" >/dev/null 2>&1; then
        c_info "Stopping running $GAME_EXE..."
        host pkill -f "$PROC_RE" || true
        for _ in $(seq 1 20); do host pgrep -f "$PROC_RE" >/dev/null 2>&1 || break; sleep 0.25; done
        host pkill -9 -f "$PROC_RE" 2>/dev/null || true
    fi
}
stop_game

# ---------------------------------------------------------------- deploy
# Keep the unstripped build (with symbols) for crash analysis, ship a stripped copy.
i686-w64-mingw32-objcopy --only-keep-debug "$ASI" "$BUILD_DIR/RiffedMenu.asi.debug"
i686-w64-mingw32-strip -o "$GAME_DIR/RiffedMenu.asi" "$ASI"
rm -f "$GAME_DIR/RiffedMenu.log" "$GAME_DIR/RiffedMenu-crash.dmp"
c_ok "Deployed RiffedMenu.asi ($(du -h "$GAME_DIR/RiffedMenu.asi" | cut -f1)) -> $GAME_DIR"

if [ ! -f "$GAME_DIR/vorbisFile.dll" ] && [ ! -f "$GAME_DIR/d3d9.dll" ] && [ ! -f "$GAME_DIR/dinput8.dll" ]; then
    c_err "No ASI loader (vorbisFile.dll / d3d9.dll / dinput8.dll) found - the .asi won't load."
fi
[ "$BUILD_ONLY" = 1 ] && exit 0

# ---------------------------------------------------------------- launch
mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
WINE_LOG="$LOG_DIR/wine-$STAMP.log"
ln -sfn "$(basename "$WINE_LOG")" "$LOG_DIR/wine-latest.log"

c_info "Launching $GAME_EXE in bottle '$BOTTLE' (WINEDEBUG=$WINEDEBUG)"
host flatpak run --env=WINEDEBUG="$WINEDEBUG" --env=DXVK_LOG_LEVEL=warn \
     --command=bottles-cli "$FLATPAK_APP" run -b "$BOTTLE" -e "$EXE_PATH" >"$WINE_LOG" 2>&1 &
LAUNCH_PID=$!
c_info "Wine/Bottles output -> $WINE_LOG"

[ "$TAIL" = 0 ] && exit 0

# ---------------------------------------------------------------- follow logs
TRAINER_LOG="$GAME_DIR/RiffedMenu.log"
for _ in $(seq 1 60); do [ -f "$TRAINER_LOG" ] && break; sleep 0.5; done
[ -f "$TRAINER_LOG" ] || c_err "RiffedMenu.log never appeared - the ASI loader did not load the plugin."
touch "$TRAINER_LOG"

# Trainer log lines in cyan, interesting Wine lines in red
tail -n +1 -F "$TRAINER_LOG" 2>/dev/null | sed -u 's/^/\x1b[36m[asi]\x1b[0m /' &
T1=$!
tail -n 0 -F "$WINE_LOG" 2>/dev/null | grep --line-buffered -E 'err:|wine: |Unhandled exception|page fault on|Backtrace:|backtrace|^ *[0-9]+ 0x|DXVK.*(err|fail)' \
    | sed -u 's/^/\x1b[31m[wine]\x1b[0m /' &
T2=$!
cleanup() { kill "$T1" "$T2" 2>/dev/null || true; }
trap 'cleanup; c_info "Stopped tailing (game keeps running)."; exit 0' INT

# Wait until the process has appeared and then gone again.
for _ in $(seq 1 60); do host pgrep -f "$PROC_RE" >/dev/null 2>&1 && break; sleep 0.5; done
while host pgrep -f "$PROC_RE" >/dev/null 2>&1; do sleep 1; done
wait "$LAUNCH_PID" 2>/dev/null || true
sleep 1; cleanup

# ---------------------------------------------------------------- post-mortem
echo
if grep -q "UNHANDLED EXCEPTION" "$TRAINER_LOG" 2>/dev/null || grep -qE "Unhandled exception|page fault on" "$WINE_LOG"; then
    c_err "Game CRASHED. Summary:"
    grep -A40 "UNHANDLED EXCEPTION" "$TRAINER_LOG" 2>/dev/null | head -45 || true
    grep -m1 -A30 -E "Unhandled exception|page fault on" "$WINE_LOG" || true
    cp -f "$TRAINER_LOG" "$LOG_DIR/RiffedMenu-$STAMP.log" 2>/dev/null || true
    if [ -f "$GAME_DIR/RiffedMenu-crash.dmp" ]; then
        mv "$GAME_DIR/RiffedMenu-crash.dmp" "$LOG_DIR/crash-$STAMP.dmp"
        c_err "Minidump: $LOG_DIR/crash-$STAMP.dmp  (symbols: $BUILD_DIR/RiffedMenu.asi.debug)"
    fi
    exit 1
fi
cp -f "$TRAINER_LOG" "$LOG_DIR/RiffedMenu-$STAMP.log" 2>/dev/null || true
c_ok "Game exited normally. Logs saved in $LOG_DIR"
