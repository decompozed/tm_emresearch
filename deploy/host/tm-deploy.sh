#!/bin/bash
# The only thing the CI key may run on the VPS (forced command in ~deploy/.ssh/authorized_keys).
#   ssh vps "deploy sha-<40 hex>"   with a tar of deploy/docker-compose.yml, deploy/db-init/10-schema.sh and microvolts-db.sql on stdin
# It can also be run by hand as the deploy user:
#   SSH_ORIGINAL_COMMAND="deploy sha-<40 hex>" /opt/toymayhem/bin/tm-deploy.sh < release.tar
# Refuses everything else. Pulls the image of exactly that tag and starts the stack.
set -euo pipefail

base=/opt/toymayhem
registry_image=ghcr.io/macrovolts/tm_emulator

cmd=${SSH_ORIGINAL_COMMAND:-}
if [[ ! "$cmd" =~ ^deploy\ (sha-[0-9a-f]{40})$ ]]; then
    echo "refused: this key can only run: deploy sha-<40 hex>" >&2
    exit 2
fi
tag=${BASH_REMATCH[1]}

stage=$(mktemp -d "$base/release/stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT

# Only these three members are accepted, at most 5 MB. tar refuses absolute paths and ".." by itself.
head -c 5242880 | tar -x --no-same-owner --no-same-permissions -C "$stage" deploy/docker-compose.yml deploy/db-init/10-schema.sh microvolts-db.sql
[[ -s "$stage/deploy/docker-compose.yml" && -s "$stage/deploy/db-init/10-schema.sh" && -s "$stage/microvolts-db.sql" ]]

printf 'TM_IMAGE=%s:%s\n' "$registry_image" "$tag" > "$stage/image.env"
export TM_SCHEMA_SQL="$stage/microvolts-db.sql" TM_SECRETS_DIR="$base/secrets"
docker compose -f "$stage/deploy/docker-compose.yml" --env-file "$base/tm.env" --env-file "$stage/image.env" config --quiet

# Valid: replace the live files (the schema only matters for a fresh database volume)
install -m 0644 "$stage/deploy/docker-compose.yml" "$base/release/docker-compose.yml"
install -m 0644 "$stage/microvolts-db.sql" "$base/release/microvolts-db.sql"
install -D -m 0644 "$stage/deploy/db-init/10-schema.sh" "$base/release/db-init/10-schema.sh"
install -m 0644 "$stage/image.env" "$base/release/image.env"

cd "$base/release"
export TM_SCHEMA_SQL="$base/release/microvolts-db.sql"
compose=(docker compose --env-file "$base/tm.env" --env-file "$base/release/image.env")
"${compose[@]}" pull --quiet
"${compose[@]}" up --detach --remove-orphans --wait --wait-timeout 300
"${compose[@]}" ps
echo "deployed $registry_image:$tag"
