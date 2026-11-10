#!/bin/bash
# Nightly backup of microvolts-db: mariadb-dump, gzip, encrypted with a PUBLIC key (GnuPG, AES256), uploaded to
# Cloudflare R2, old copies removed. The private key is not on this server, so a stolen VPS cannot read the backups.
#
#   tm-backup.sh                       dump, encrypt, upload to R2, prune
#   tm-backup.sh --local-only DIR      dump and encrypt into DIR, no upload (used by the tests)
#
# Settings come from /etc/toymayhem/backup.env (root only, see backup.env.example):
#   R2_ACCOUNT_ID  R2_ACCESS_KEY_ID  R2_SECRET_ACCESS_KEY  R2_BUCKET   Cloudflare R2 (S3 API token limited to that bucket)
#   GPG_RECIPIENT   fingerprint of the backup public key
#   KEEP_DAYS       days to keep (default 14); the newest 3 copies are always kept
# The public key is read from /etc/toymayhem/backup-public.asc into a keyring that holds no private key.
set -euo pipefail
umask 0077

env_file=${TM_BACKUP_ENV:-/etc/toymayhem/backup.env}
pubkey_file=${TM_BACKUP_PUBKEY:-/etc/toymayhem/backup-public.asc}
gnupg_home=${TM_BACKUP_GNUPGHOME:-/etc/toymayhem/gnupg}
state_dir=${TM_BACKUP_STATE:-/var/lib/toymayhem-backup}
project=${TM_COMPOSE_PROJECT:-toymayhem}
local_only=

while [[ $# -gt 0 ]]; do
    case "$1" in
        --local-only) local_only=${2:?}; shift 2 ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# shellcheck disable=SC1090
[[ -f "$env_file" ]] && . "$env_file"
KEEP_DAYS=${KEEP_DAYS:-14}
: "${GPG_RECIPIENT:?GPG_RECIPIENT is not set in $env_file}"
if [[ -z "$local_only" ]]; then
    : "${R2_ACCOUNT_ID:?not set in $env_file}" "${R2_ACCESS_KEY_ID:?not set}" "${R2_SECRET_ACCESS_KEY:?not set}" "${R2_BUCKET:?not set}"
fi
[[ "$KEEP_DAYS" =~ ^[0-9]+$ && "$KEEP_DAYS" -ge 1 ]] || { echo "KEEP_DAYS must be a whole number of days" >&2; exit 2; }

log() { echo "$(date -u '+%F %T') tm-backup: $*"; }

# Keyring with only the public key
install -d -m 0700 "$gnupg_home"
if ! gpg --homedir "$gnupg_home" --batch --list-keys "$GPG_RECIPIENT" >/dev/null 2>&1; then
    [[ -s "$pubkey_file" ]] || { echo "no public key: $pubkey_file is missing" >&2; exit 1; }
    gpg --homedir "$gnupg_home" --batch --import "$pubkey_file" 2>&1 | sed 's/^/gpg: /'
fi
if gpg --homedir "$gnupg_home" --batch --list-secret-keys 2>/dev/null | grep -q '^sec'; then
    echo "REFUSING: $gnupg_home holds a private key. The private key must stay off the server." >&2
    exit 1
fi

db=$(docker ps --quiet --filter "label=com.docker.compose.project=$project" --filter "label=com.docker.compose.service=db" --filter status=running | head -n1)
[[ -n "$db" ]] || { echo "the database container of project $project is not running" >&2; exit 1; }

dest=${local_only:-$state_dir}
install -d -m 0700 "$dest"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
name="toymayhem-$stamp.sql.gz.gpg"
partial="$dest/$name.partial"

log "dumping microvolts-db from container ${db:0:12}"
# The application account is enough: it owns every table of the schema. --single-transaction gives a consistent copy of
# the InnoDB tables without locking the game.
docker exec -i "$db" sh -c 'MYSQL_PWD=$(cat /run/secrets/db_app_password) exec mariadb-dump --user=mvapp --single-transaction --quick --routines --triggers --events --hex-blob microvolts-db' \
    | gzip -9 \
    | gpg --homedir "$gnupg_home" --batch --yes --trust-model always --recipient "$GPG_RECIPIENT" \
          --cipher-algo AES256 --compress-algo none --encrypt --output "$partial"

size=$(stat -c %s "$partial")
[[ "$size" -gt 500 ]] || { rm -f "$partial"; echo "backup is only $size bytes, something is wrong" >&2; exit 1; }
mv "$partial" "$dest/$name"
(cd "$dest" && sha256sum "$name" > "$name.sha256")
log "encrypted copy: $dest/$name ($size bytes)"

if [[ -n "$local_only" ]]; then
    log "local only, nothing uploaded"
    exit 0
fi

# Cloudflare R2 through rclone, configured from the environment (no rclone.conf with keys on disk)
export RCLONE_CONFIG_R2_TYPE=s3 RCLONE_CONFIG_R2_PROVIDER=Cloudflare RCLONE_CONFIG_R2_ENV_AUTH=false
export RCLONE_CONFIG_R2_ACCESS_KEY_ID=$R2_ACCESS_KEY_ID RCLONE_CONFIG_R2_SECRET_ACCESS_KEY=$R2_SECRET_ACCESS_KEY
export RCLONE_CONFIG_R2_ENDPOINT="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"
export RCLONE_CONFIG_R2_NO_CHECK_BUCKET=true     # the token cannot create buckets, and should not
remote="R2:${R2_BUCKET}/db"

rclone copyto --checksum "$dest/$name" "$remote/$name"
rclone copyto --checksum "$dest/$name.sha256" "$remote/$name.sha256"
rclone lsf "$remote" | grep -qx "$name" || { echo "upload not found on R2" >&2; exit 1; }
log "uploaded to $remote/$name"

# Retention: delete copies older than KEEP_DAYS, but never the newest three, so a long outage of the backup does not
# empty the bucket. If the bucket has a retention lock, the delete is refused: that is fine, it is only logged.
cutoff=$(date -u -d "-${KEEP_DAYS} days" +%Y%m%dT%H%M%SZ)
mapfile -t all < <(rclone lsf "$remote" --include 'toymayhem-*.sql.gz.gpg' | sort)
keep_from=$(( ${#all[@]} > 3 ? ${#all[@]} - 3 : 0 ))
for ((i = 0; i < keep_from; i++)); do
    f=${all[$i]}
    ts=${f#toymayhem-}; ts=${ts%.sql.gz.gpg}
    if [[ "$ts" < "$cutoff" ]]; then
        log "removing old copy $f"
        rclone deletefile "$remote/$f" || log "could not delete $f (bucket lock?)"
        rclone deletefile "$remote/$f.sha256" || true
    fi
done

# Local copies: keep the newest three
mapfile -t local_copies < <(ls -1 "$state_dir"/toymayhem-*.sql.gz.gpg 2>/dev/null | sort)
if ((${#local_copies[@]} > 3)); then
    for f in "${local_copies[@]:0:${#local_copies[@]}-3}"; do rm -f "$f" "$f.sha256"; done
fi
date -u +%FT%TZ > "$state_dir/last-success"
log "done"
