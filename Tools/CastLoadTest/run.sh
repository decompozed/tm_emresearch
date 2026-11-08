#!/usr/bin/env bash
# Usage: run.sh <CastServer binary> <CastLoadTest binary> <report file> [config.ini]
set -u

testKey=0000000000000000000000000000000000000000000000000000000000000000
export MV_DB_PW="${MV_DB_PW:-loadtest}" MV_JWT="${MV_JWT:-loadtest}" MV_EMAIL_TOKEN="${MV_EMAIL_TOKEN:-loadtest}"
export MVEMAIL_SECRET="${MVEMAIL_SECRET:-$testKey}" MV2FA_SECRET="${MV2FA_SECRET:-$testKey}"
export MV_IPC_SECRET="${MV_IPC_SECRET:-$testKey}"

cast="$(realpath "$1")"
tool="$(realpath "$2")"
report="$(realpath -m "$3")"
config="$(realpath "${4:-$(dirname "$cast")/Setup/config.ini}")"
localIp="$(ip route get 8.8.8.8 2>/dev/null | awk '{for (i = 1; i < NF; ++i) if ($i == "src") print $(i + 1)}')"
root="$(mktemp -d)"
workdir="$root/Output"
mkdir -p "$workdir" "$root/Setup"
sed -E "s/^(LocalIp[[:space:]]*=).*/\1 ${localIp:-127.0.0.1}/" "$config" > "$root/Setup/config.ini"
: > "$report"
runs=0

gate=(--clients 100 --room 16 --tick 10 --seconds 20 --min-delivery 99 --max-p99 100)
scenarios=(
    "--clients 200 --room 16 --tick 10 --seconds 20"
    "--clients 300 --room 16 --tick 10 --seconds 20"
    "--clients 500 --room 16 --tick 10 --seconds 20"
    "--clients 300 --room 16 --tick 30 --seconds 20"
    "--clients 300 --room 16 --tick 10 --seconds 20 --hit-requests 4 --item-requests 0.5 --pickups 0.5 --host-hits 10 --explosions 2 --damage 250000 --respawn 2"
)
# Same runs with TLS on the CastServer <-> MainServer link, to compare with their plaintext twins above
secureScenarios=(
    "--clients 500 --room 16 --tick 10 --seconds 20"
    "--clients 300 --room 16 --tick 10 --seconds 20 --hit-requests 4 --item-requests 0.5 --pickups 0.5 --host-hits 10 --explosions 2 --damage 250000 --respawn 2"
)

run() {
    "$tool" "$@" --process "$(basename "$cast")" > tool.log 2>&1 &
    local toolPid=$!
    sleep 1
    (cd "$workdir" && exec "$cast" > cast.log 2>&1) &
    local castPid=$!
    wait "$toolPid"
    local rc=$?
    kill "$castPid" 2>/dev/null
    wait "$castPid" 2>/dev/null
    grep -v "^waiting" tool.log | tee -a "$report"
    grep -E "Error|AntiCheat|terminate" "$workdir/cast.log" | head -20 | tee -a "$report"
    runs=$((runs + 1))
    cp "$workdir/cast.log" "$(dirname "$report")/cast-$runs.log"
    sleep 2
    return $rc
}

run "${gate[@]}"
gateRc=$?
for scenario in "${scenarios[@]}"; do
    run $scenario || true
done

printf '\n[Ipc]\nSecretEnvironmentName = MV_IPC_SECRET\n' >> "$root/Setup/config.ini"
run "${gate[@]}" --ipc-secret-env MV_IPC_SECRET
secureGateRc=$?
for scenario in "${secureScenarios[@]}"; do
    run $scenario --ipc-secret-env MV_IPC_SECRET || true
done
[ $gateRc -eq 0 ] && [ $secureGateRc -eq 0 ]
