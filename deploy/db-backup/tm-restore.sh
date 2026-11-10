#!/bin/bash
# Restore of a backup made by tm-backup.sh. The private key is NOT on the server, so decrypting happens on YOUR machine
# (where the private key is) and the plain SQL travels to the server over SSH:
#
#   server:   tm-restore.sh --list                         copies on R2
#   server:   tm-restore.sh --fetch latest DIR             download the encrypted copy (also: --fetch NAME)
#   anywhere: tm-restore.sh --decrypt FILE.sql.gz.gpg      plain SQL to stdout; needs the private key in the local gpg
#   server:   tm-restore.sh --load --yes                   read plain SQL (gzip or not) on stdin, load it into the database
#
# From your laptop, in one line (see README.md, Restore):
#   tm-restore.sh --decrypt dump.sql.gz.gpg | ssh tmadmin@HOST 'sudo /opt/toymayhem/bin/tm-restore.sh --load --yes'
#
# --load stops the three game servers, loads the dump into microvolts-db (the dump drops and recreates every table in it)
# and starts the servers again. It asks for --yes because the current data is replaced.
set -euo pipefail

env_file=${TM_BACKUP_ENV:-/etc/toymayhem/backup.env}
project=${TM_COMPOSE_PROJECT:-toymayhem}
mode=
arg1=
arg2=
yes=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --list) mode=list; shift ;;
        --fetch) mode=fetch; arg1=${2:?name or latest}; arg2=${3:?target dir}; shift 3 ;;
        --decrypt) mode=decrypt; arg1=${2:?file}; shift 2 ;;
        --load) mode=load; shift ;;
        --yes) yes=1; shift ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$mode" ]] || { sed -n '2,15p' "$0"; exit 2; }

r2_setup() {
    # shellcheck disable=SC1090
    [[ -f "$env_file" ]] && . "$env_file"
    : "${R2_ACCOUNT_ID:?not set in $env_file}" "${R2_ACCESS_KEY_ID:?not set}" "${R2_SECRET_ACCESS_KEY:?not set}" "${R2_BUCKET:?not set}"
    export RCLONE_CONFIG_R2_TYPE=s3 RCLONE_CONFIG_R2_PROVIDER=Cloudflare RCLONE_CONFIG_R2_ENV_AUTH=false
    export RCLONE_CONFIG_R2_ACCESS_KEY_ID=$R2_ACCESS_KEY_ID RCLONE_CONFIG_R2_SECRET_ACCESS_KEY=$R2_SECRET_ACCESS_KEY
    export RCLONE_CONFIG_R2_ENDPOINT="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com" RCLONE_CONFIG_R2_NO_CHECK_BUCKET=true
    remote="R2:${R2_BUCKET}/db"
}

case "$mode" in
list)
    r2_setup
    rclone lsl "$remote" --include 'toymayhem-*.sql.gz.gpg' | sort -k4
    ;;

fetch)
    r2_setup
    name=$arg1
    if [[ "$name" == latest ]]; then
        name=$(rclone lsf "$remote" --include 'toymayhem-*.sql.gz.gpg' | sort | tail -n1)
        [[ -n "$name" ]] || { echo "no backups on R2" >&2; exit 1; }
    fi
    [[ "$name" =~ ^toymayhem-[0-9]{8}T[0-9]{6}Z\.sql\.gz\.gpg$ ]] || { echo "not a backup name: $name" >&2; exit 2; }
    mkdir -p "$arg2"
    rclone copyto "$remote/$name" "$arg2/$name"
    rclone copyto "$remote/$name.sha256" "$arg2/$name.sha256"
    (cd "$arg2" && sha256sum --check "$name.sha256")
    echo "downloaded and checked: $arg2/$name"
    ;;

decrypt)
    [[ -f "$arg1" ]] || { echo "no such file: $arg1" >&2; exit 1; }
    # the checksum file next to it, when present, is checked first
    if [[ -f "$arg1.sha256" ]]; then (cd "$(dirname "$arg1")" && sha256sum --check --status "$(basename "$arg1").sha256") || { echo "checksum does not match" >&2; exit 1; }; fi
    gpg --batch --quiet --decrypt "$arg1" | gunzip -c
    ;;

load)
    ((yes)) || { echo "this replaces the data in microvolts-db. Run again with --yes." >&2; exit 2; }
    [[ -t 0 ]] && { echo "--load reads the SQL from stdin: pipe it in" >&2; exit 2; }
    db=$(docker ps --quiet --filter "label=com.docker.compose.project=$project" --filter "label=com.docker.compose.service=db" --filter status=running | head -n1)
    [[ -n "$db" ]] || { echo "the database container of project $project is not running" >&2; exit 1; }
    servers=$(docker ps --all --quiet --filter "label=com.docker.compose.project=$project" \
        --filter "label=com.docker.compose.service=auth" --filter "label=com.docker.compose.service=main" --filter "label=com.docker.compose.service=cast")
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    # Read everything first: a broken pipe must not leave the game stopped with a half loaded database
    cat > "$tmp"
    if [[ "$(head -c2 "$tmp" | od -An -tx1 | tr -d ' ')" == 1f8b ]]; then gunzip -c "$tmp" > "$tmp.sql"; mv "$tmp.sql" "$tmp"; fi
    grep -q 'CREATE TABLE' "$tmp" || { echo "this does not look like a mariadb-dump file" >&2; exit 1; }
    # shellcheck disable=SC2086
    [[ -z "$servers" ]] || docker stop $servers >/dev/null
    echo "servers stopped, loading $(stat -c %s "$tmp") bytes"
    status=0
    docker exec -i "$db" sh -c 'MYSQL_PWD=$(cat /run/secrets/db_app_password) exec mariadb --user=mvapp microvolts-db' < "$tmp" || status=$?
    # shellcheck disable=SC2086
    [[ -z "$servers" ]] || docker start $servers >/dev/null
    if ((status)); then echo "LOAD FAILED (exit $status). The servers were started again; the database may be partly restored." >&2; exit "$status"; fi
    echo "restore finished, servers started"
    ;;
esac
