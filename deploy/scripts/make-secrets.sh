#!/bin/bash
# Creates the secret files the compose file mounts under /run/secrets. Never overwrites a file that exists, so it is
# safe to run again; use --rotate NAME to replace one on purpose (README.md says what else a rotation needs).
#
#   make-secrets.sh [--dir DIR] [--cf-tunnel-token-file FILE] [--email-token-file FILE] [--rotate NAME]...
#
# A new directory (default /opt/toymayhem/secrets) is created with mode 0700. The files get mode 0444: the containers run
# as other users (10001 game, 999 database, 65532 cloudflared) and Docker mounts the file as it is. Only root can enter
# the directory, so only root and the containers (which each get only the files they need) can read them.
set -euo pipefail

dir=/opt/toymayhem/secrets
cf_token_file=
email_token_file=
rotate=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir) dir=${2:?}; shift 2 ;;
        --cf-tunnel-token-file) cf_token_file=${2:?}; shift 2 ;;
        --email-token-file) email_token_file=${2:?}; shift 2 ;;
        --rotate) rotate+=("${2:?}"); shift 2 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done

umask 0077
# A new folder is root only (0700). The bootstrap script makes it 0710 root:docker so the deploy user can start the stack;
# an existing folder keeps its mode.
[[ -d "$dir" ]] || { mkdir -p "$dir"; chmod 0700 "$dir"; }

hex()   { head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n'; }
alnum() { LC_ALL=C tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 40 || true; }

wanted_rotate() { local n; for n in "${rotate[@]}"; do [[ "$n" == "$1" ]] && return 0; done; return 1; }

put() { # name value
    local name=$1 value=$2 path="$dir/$1"
    if [[ -s "$path" ]] && ! wanted_rotate "$name"; then
        echo "kept     $name"
        return
    fi
    printf '%s\n' "$value" > "$path.new"
    chmod 0444 "$path.new"
    mv -f "$path.new" "$path"
    echo "written  $name"
}

put db_root_password "$(alnum)"
put db_app_password  "$(alnum)"
put jwt_secret       "$(hex)"
put ipc_secret       "$(hex)"        # 64 hex digits, shared by the three servers
put password_pepper  "$(hex)"        # 64 hex digits
put email_secret     "$(hex)"        # 64 hex digits
put twofa_secret     "$(hex)"        # 64 hex digits

if [[ -n "$email_token_file" ]]; then put email_token "$(tr -d '\r\n' < "$email_token_file")"
else [[ -s "$dir/email_token" ]] && echo "kept     email_token" || put email_token "not-configured-yet"; fi

if [[ -n "$cf_token_file" ]]; then put cf_tunnel_token "$(tr -d '\r\n' < "$cf_token_file")"
else [[ -s "$dir/cf_tunnel_token" ]] && echo "kept     cf_tunnel_token" || { put cf_tunnel_token "paste-the-tunnel-token-here"; echo "NOTE: cf_tunnel_token is a placeholder; cloudflared will not connect until you put the real token in"; }; fi

if [[ $(id -u) -eq 0 ]]; then chown root:root "$dir"/*; fi
echo "done: $dir"
