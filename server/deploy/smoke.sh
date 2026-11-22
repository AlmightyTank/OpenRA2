#!/usr/bin/env bash
# S0.1F disposable loopback smoke; exits non-zero on failed health/API checks.
set -euo pipefail
BIN="${1:?usage: smoke.sh /path/to/ra2lobbyd}"
[[ -x "$BIN" ]] || { echo "missing executable: $BIN" >&2; exit 2; }
command -v curl >/dev/null
command -v openssl >/dev/null

ROOT="$(mktemp -d)"
PID=""
cleanup() {
  [[ -n "$PID" ]] && kill "$PID" 2>/dev/null || true
  [[ -n "$PID" ]] && wait "$PID" 2>/dev/null || true
  rm -rf "$ROOT"
}
trap cleanup EXIT
TOKEN="$(openssl rand -hex 32)"
export RA2_ADMIN_TOKEN="$TOKEN"
HOST="127.0.0.1"
ADMIN="http://$HOST:18081"
"$BIN" --version > "$ROOT/version.txt"
"$BIN" --check-config --listen "$HOST:50175" --v3 "$HOST:50176" --admin "$HOST:18081" --relay-ports 50185-50189 > "$ROOT/config.txt"
if "$BIN" --check-config --admin "0.0.0.0:18081" > "$ROOT/bad.txt" 2>&1; then
  echo "unsafe wildcard admin listener was accepted" >&2; exit 1
fi
"$BIN" --listen "$HOST:50175" --v3 "$HOST:50176" --admin "$HOST:18081" \
  --relay-ports 50185-50189 --allow-loopback >"$ROOT/server.log" 2>&1 &
PID="$!"
ready=0
for _ in {1..50}; do
  if curl --silent --fail --max-time 1 "$ADMIN/healthz" >"$ROOT/health"; then ready=1;break;fi
  if ! kill -0 "$PID" 2>/dev/null;then
    echo "server exited during startup" >&2;cat "$ROOT/server.log" >&2;exit 1
  fi
  sleep 0.2
done
[[ "$ready" == 1 ]] || { echo "health endpoint not ready" >&2;cat "$ROOT/server.log" >&2;exit 1; }
grep -q '^ok rooms=0$' "$ROOT/health"
curl --fail --silent --show-error --max-time 3 "$ADMIN/metrics" > "$ROOT/metrics"
grep -q '^ra2lobbyd_rooms 0$' "$ROOT/metrics"
grep -q '^ra2lobbyd_v3_clients 0$' "$ROOT/metrics"
grep -q '^ra2lobbyd_relay_allocations 0$' "$ROOT/metrics"
curl --fail --silent --show-error "$ADMIN/admin/" > "$ROOT/page.html"
grep -q 'Server operations' "$ROOT/page.html"
code="$(curl --silent --output /dev/null --write-out '%{http_code}' "$ADMIN/admin/api/status")"
[[ "$code" == 401 ]] || { echo "unauthenticated status returned $code" >&2;exit 1; }
code="$(curl --silent --output /dev/null --write-out '%{http_code}' \
  -H "Authorization: Bearer invalid" "$ADMIN/admin/api/rooms")"
[[ "$code" == 401 ]] || { echo "incorrect token returned $code" >&2;exit 1; }
curl --fail --silent --show-error -H "Authorization: Bearer $TOKEN" \
  "$ADMIN/admin/api/status" >"$ROOT/status.json"
grep -q '"status":"healthy"' "$ROOT/status.json"
# Full operator script preflight and stage/rollback are tested only in this
# disposable temporary directory. No service restart, systemctl, or VPS access.
sha256sum "$BIN" >"$ROOT/checksum"
RA2_ADMIN_URL="$ADMIN" bash server/deploy/release.sh preflight "$BIN" "$ROOT/checksum" > "$ROOT/preflight"
if RA2_ADMIN_URL="$ADMIN" RA2_RELEASE_ROOT="$ROOT/releases-root" \
   bash server/deploy/release.sh stage "$BIN" "$ROOT/checksum" --apply >"$ROOT/block.log" 2>&1; then
  echo "stage succeeded without required maintenance acknowledgement" >&2;exit 1
fi
RA2_ADMIN_URL="$ADMIN" RA2_RELEASE_ROOT="$ROOT/releases-root" \
  RA2_MAINTENANCE_ACK=YES RA2_RELEASE_NAME=smoke-v1 \
  bash server/deploy/release.sh stage "$BIN" "$ROOT/checksum" --apply >"$ROOT/staged"
[[ -x "$ROOT/releases-root/current/ra2lobbyd" ]] ||
  { echo "staged release not available" >&2;exit 1; }
# The systemd unit uses DynamicUser: parent/release directories must allow
# traversal to a non-root account. Do not store secrets in this tree.
for dir in "$ROOT/releases-root" "$ROOT/releases-root/releases" "$ROOT/releases-root/releases/smoke-v1"; do
  [[ -x "$dir" ]] || { echo "release directory not traversable" >&2;exit 1; }
  mode="$(stat -c %a "$dir")"
  [[ "${mode: -1}" =~ [1357] ]] ||
    { echo "release directory not traversable by DynamicUser (mode $mode)" >&2;exit 1; }
done
RA2_ADMIN_URL="$ADMIN" RA2_RELEASE_ROOT="$ROOT/releases-root" \
  RA2_MAINTENANCE_ACK=YES RA2_RELEASE_NAME=smoke-v2 \
  bash server/deploy/release.sh stage "$BIN" "$ROOT/checksum" --apply >>"$ROOT/staged"
RA2_ADMIN_URL="$ADMIN" RA2_RELEASE_ROOT="$ROOT/releases-root" \
  RA2_MAINTENANCE_ACK=YES bash server/deploy/release.sh rollback --apply >>"$ROOT/staged"
[[ "$(readlink -f "$ROOT/releases-root/current")" == *"smoke-v1" ]] ||
  { echo "rollback did not select previous release" >&2;exit 1; }
# First install has a separate explicit acknowledgement and must have NO
# running admin endpoint. Exercise it on a different unused loopback port.
if RA2_ADMIN_URL="http://127.0.0.1:19999" RA2_RELEASE_ROOT="$ROOT/bootstrap-root" \
  RA2_MAINTENANCE_ACK=YES bash server/deploy/release.sh bootstrap "$BIN" "$ROOT/checksum" --apply \
  >"$ROOT/bootstrap-block.log" 2>&1; then
  echo "bootstrap succeeded without RA2_BOOTSTRAP_ACK" >&2;exit 1
fi
RA2_ADMIN_URL="http://127.0.0.1:19999" RA2_RELEASE_ROOT="$ROOT/bootstrap-root" \
  RA2_BOOTSTRAP_ACK=YES RA2_MAINTENANCE_ACK=YES RA2_RELEASE_NAME=initial \
  bash server/deploy/release.sh bootstrap "$BIN" "$ROOT/checksum" --apply >"$ROOT/bootstrap"
[[ -x "$ROOT/bootstrap-root/current/ra2lobbyd" ]] ||
  { echo "explicit first-install stage missing" >&2;exit 1; }
echo "PASS: health, operator auth, metrics, dry-run preflight, guarded staging, bootstrap and rollback"
