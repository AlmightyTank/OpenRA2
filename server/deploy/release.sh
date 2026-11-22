#!/usr/bin/env bash
# S0.1F release helper. Defaults to verification-only. NEVER starts,
# stops, enables or restarts a service; production cutover is a separate
# operator-authorized action after maintenance-window review.
set -euo pipefail
# A DynamicUser systemd service must traverse release directories owned
# by root. Release packages contain only public binaries and checksums;
# operator secrets live separately in /etc/openra2-lobby (mode 0600).
umask 022

usage() {
  cat <<'USAGE'
Usage:
  bash server/deploy/release.sh preflight <candidate-binary> <sha256-file>
  bash server/deploy/release.sh bootstrap <candidate-binary> <sha256-file> --apply
  bash server/deploy/release.sh stage <candidate-binary> <sha256-file> --apply
  bash server/deploy/release.sh rollback --apply

Environment:
  RA2_RELEASE_ROOT     installation root (default /opt/openra2-lobby)
  RA2_ADMIN_URL        local admin base (default http://127.0.0.1:8080)
  RA2_MAINTENANCE_ACK  must be EXACTLY "YES" for bootstrap/stage/rollback
  RA2_BOOTSTRAP_ACK    must also be "YES" for a first installation
  RA2_EMERGENCY_ACK    must also be "YES" for an offline rollback when health fails
  RA2_RELEASE_NAME     optional version label (safe alphanumeric . _ -)
The script never restarts a service. After stage, the operator must
separately perform the approved systemd restart and health check.
USAGE
}

ROOT="${RA2_RELEASE_ROOT:-/opt/openra2-lobby}"
ADMIN="${RA2_ADMIN_URL:-http://127.0.0.1:8080}"
MODE="${1:-}"
case "$MODE" in preflight|bootstrap|stage|rollback) ;; *) usage; exit 2 ;; esac
if [[ "$ROOT" != /* || "$ROOT" == "/" || "$ROOT" == *"/../"* || "$ROOT" == *"/./"* ]]; then
  echo "ERROR: release root must be a specific absolute directory" >&2; exit 2
fi
if [[ ! "$ADMIN" =~ ^http://(127[.]0[.]0[.]1|localhost):([0-9]{1,5})$ ]]; then
  echo "ERROR: admin URL must be a local HTTP host and numeric port without a path" >&2; exit 2
fi
admin_port="${BASH_REMATCH[2]}"
if (( 10#$admin_port < 1 || 10#$admin_port > 65535 )); then
  echo "ERROR: invalid local admin port" >&2; exit 2
fi

require_idle() {
  command -v curl >/dev/null || { echo "ERROR: curl required" >&2; return 1; }
  local health metrics key value
  health="$(curl -fsS --max-time 3 "$ADMIN/healthz")" ||
    { echo "ERROR: local health endpoint unreachable; refusing cutover" >&2; return 1; }
  [[ "$health" == ok\ rooms=* ]] ||
    { echo "ERROR: unexpected /healthz response" >&2; return 1; }
  metrics="$(curl -fsS --max-time 3 "$ADMIN/metrics")" ||
    { echo "ERROR: local metrics unreachable; refusing cutover" >&2; return 1; }
  for key in ra2lobbyd_rooms ra2lobbyd_v3_clients ra2lobbyd_relay_allocations; do
    value="$(printf '%s\n' "$metrics" | awk -v key="$key" '$1==key {print $2;found=1} END {if(!found)exit 1}')" ||
      { echo "ERROR: missing metric $key; refusing cutover" >&2; return 1; }
    [[ "$value" =~ ^[0-9]+$ ]] ||
      { echo "ERROR: invalid metric $key; refusing cutover" >&2; return 1; }
    if (( value != 0 )); then
      echo "BLOCKED: $key=$value; live sessions or mappings may exist" >&2
      return 1
    fi
  done
  echo "Preflight: local health OK; rooms, V3 mappings and relay allocations all zero"
  echo "NOTE: zero metrics do not prove no active gameplay. Confirm maintenance window independently."
}

verify_candidate() {
  local binary="$1" sums="$2"
  [[ -f "$binary" && -x "$binary" ]] ||
    { echo "ERROR: executable candidate not found" >&2; return 1; }
  [[ -f "$sums" ]] || { echo "ERROR: checksum file missing" >&2; return 1; }
  command -v sha256sum >/dev/null || { echo "ERROR: sha256sum required" >&2; return 1; }
  local digest expected
  expected="$(awk 'NR==1 {print $1}' "$sums")"
  [[ "$expected" =~ ^[0-9a-fA-F]{64}$ ]] ||
    { echo "ERROR: checksum must start with SHA-256 hex digest" >&2; return 1; }
  digest="$(sha256sum "$binary" | awk '{print $1}')"
  [[ "${digest,,}" == "${expected,,}" ]] ||
    { echo "ERROR: candidate checksum mismatch" >&2; return 1; }
  "$binary" --check-config >/dev/null ||
    { echo "ERROR: candidate --check-config failed" >&2; return 1; }
  echo "Preflight: checksum and configuration validated"
}

if [[ "$MODE" == preflight || "$MODE" == stage || "$MODE" == bootstrap ]]; then
  [[ $# -ge 3 ]] || { usage; exit 2; }
  BINARY="$2"
  SUMS="$3"
  verify_candidate "$BINARY" "$SUMS"
  if [[ "$MODE" == bootstrap ]]; then
    if [[ -e "$ROOT/current" || -L "$ROOT/current" ]]; then
      echo "BLOCKED: existing release present; use stage with idle preflight instead" >&2; exit 1
    fi
    if command -v curl >/dev/null && curl -fsS --max-time 2 "$ADMIN/healthz" >/dev/null 2>&1; then
      echo "BLOCKED: admin endpoint already serves a running service" >&2; exit 1
    fi
    if command -v systemctl >/dev/null && systemctl is-active --quiet ra2lobbyd; then
      echo "BLOCKED: ra2lobbyd service already active" >&2; exit 1
    fi
    echo "Bootstrap preflight: no current release or responsive admin endpoint found."
    echo "OPERATOR MUST independently verify that the dedicated ports are unused."
  else
    require_idle
  fi
fi

if [[ "$MODE" == preflight ]]; then
  echo "DRY RUN complete. No files or services changed."
  exit 0
fi

if [[ "${RA2_MAINTENANCE_ACK:-}" != YES || "${@: -1}" != "--apply" ]]; then
  echo "BLOCKED: bootstrap/stage/rollback requires --apply and RA2_MAINTENANCE_ACK=YES" >&2
  exit 2
fi
if [[ "$MODE" == bootstrap && "${RA2_BOOTSTRAP_ACK:-}" != YES ]]; then
  echo "BLOCKED: first installation additionally requires RA2_BOOTSTRAP_ACK=YES" >&2
  exit 2
fi
if [[ "$MODE" == rollback ]]; then
  if ! require_idle; then
    if [[ "${RA2_EMERGENCY_ACK:-}" != YES ]]; then
      echo "BLOCKED: unresponsive server requires explicit RA2_EMERGENCY_ACK=YES" >&2
      exit 1
    fi
    if ! command -v systemctl >/dev/null ||
       [[ "$(systemctl show -p LoadState --value ra2lobbyd 2>/dev/null || true)" != loaded ]] ||
       systemctl is-active --quiet ra2lobbyd; then
      echo "BLOCKED: offline rollback only when systemd confirms ra2lobbyd is installed and inactive" >&2
      exit 1
    fi
    echo "OFFLINE EMERGENCY: systemd reports ra2lobbyd inactive; selecting previous release only."
  fi
  [[ -L "$ROOT/previous" && -d "$ROOT/previous" ]] ||
    { echo "ERROR: no previous release to restore" >&2; exit 1; }
  candidate="$(readlink -f "$ROOT/previous")"
  [[ "$candidate" == "$ROOT/releases/"* && -x "$candidate/ra2lobbyd" ]] ||
    { echo "ERROR: previous release target invalid" >&2; exit 1; }
  old=""
  [[ -L "$ROOT/current" ]] && old="$(readlink -f "$ROOT/current")"
  ln -s "$candidate" "$ROOT/.current-next"
  mv -Tf "$ROOT/.current-next" "$ROOT/current"
  if [[ -n "$old" && "$old" == "$ROOT/releases/"* ]]; then
    ln -s "$old" "$ROOT/.previous-next"
    mv -Tf "$ROOT/.previous-next" "$ROOT/previous"
  fi
  echo "Rollback link selected: $candidate"
  echo "No services were restarted. Operator must restart ra2lobbyd and verify health."
  exit 0
fi

NAME="${RA2_RELEASE_NAME:-$(date -u +%Y%m%dT%H%M%SZ)}"
[[ "$NAME" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$ ]] ||
  { echo "ERROR: invalid release name" >&2; exit 2; }
dest="$ROOT/releases/$NAME"
[[ ! -e "$dest" ]] || { echo "ERROR: release already exists" >&2; exit 2; }
mkdir -p "$ROOT/releases" "$dest"
install -m 0755 "$BINARY" "$dest/ra2lobbyd"
sha256sum "$dest/ra2lobbyd" > "$dest/SHA256SUMS"
old=""
if [[ -L "$ROOT/current" ]]; then
  old="$(readlink -f "$ROOT/current")"
  [[ "$old" == "$ROOT/releases/"* ]] ||
    { echo "ERROR: existing current release target not managed here" >&2; exit 1; }
fi
if [[ -n "$old" ]]; then
  ln -s "$old" "$ROOT/.previous-next"
  mv -Tf "$ROOT/.previous-next" "$ROOT/previous"
fi
ln -s "$dest" "$ROOT/.current-next"
mv -Tf "$ROOT/.current-next" "$ROOT/current"
echo "Staged $dest; current and previous pointers updated."
echo "NO service was restarted. Perform approved maintenance-window restart, then health check."
