# Running ToyMayhem on the VPS

How to build, deploy and look after the server for the hacking week. Everything here is meant for one Ubuntu 24.04 VPS.
Placeholders: the domain is `example.invalid` until the real one is bought. It is set in one place, `TM_DOMAIN` in
`/opt/toymayhem/tm.env`, and the compose file derives the web origins and mail addresses from it. The Cloudflare
dashboard and your SSH config also need the hostname, those you type by hand.

## How it fits together

```
 players (game client)           Cloudflare                       the VPS
 ---------------------           ----------                       -------
 TCP 13000 / 13005 / 13006 --------------------------------->  published by Docker (not proxied, see below)
 browser -> api.<domain> -----> Cloudflare -> tunnel "tm-api" -> cloudflared container -> MainServer :8080
 admin   -> ssh.<domain> -----> Cloudflare Access -> tunnel "tm-ssh" -> host cloudflared -> sshd :22
 backups: VPS -> encrypted dump -> R2 bucket (the private key is only on your laptop)
```

Containers (one image `ghcr.io/decompozed/tm_emulator`, started three times with a different server name):

| container | what | notes |
|---|---|---|
| `netns` | owns the network namespace, publishes 13000, 13005, 13006 | only runs `sleep`, so it never crashes |
| `auth`, `main`, `cast` | AuthServer, MainServer, CastServer | share the namespace of `netns`, one IP (`172.28.0.10`) |
| `cloudflared` | tunnel for the account API | same namespace, reads the tunnel token from a secret file |
| `db` | MariaDB 11.4 | internal network only, no published port, account `mvapp` (rights on `microvolts-db` only) |

Why the three servers share one IP: at start every server looks itself up in the config by its local IP
(`SetupParser`), and the Main and Cast entry must have the same one. One IP is the only way that works with one container per server.

All containers: not root, `read_only`, `cap_drop: ALL`, `no-new-privileges`, memory, CPU and pid limits, secrets as files in `/run/secrets`.
`Setup/config.ini` is written at start by `docker-entrypoint.sh` into a tmpfs, from the environment. It only holds the *names*
of the secret variables, never a value.

### Ports and protocols (checked in the code)

* Game: **TCP** 13000 (Auth), 13005 (Main), 13006 (Cast). Published on the VPS IP. Cloudflare's free plan only proxies HTTP(S),
  so these hostnames must be **DNS only** (grey cloud) A records. That means the VPS IP is visible to everybody.
* **No UDP.** None of the three servers opens a UDP socket. The only UDP in the code is a `connect()` to 8.8.8.8 to learn the local IP
  (`getLocalIp`, no packet is sent). It needs a default route, which is why the `app` network is not `internal`.
* **13001 (`GradedPort`)** is a second TCP listener of AuthServer, only opened with `EnhancedSecurity = true`, bound to `VpnIp`
  (here the container IP). Graded (staff) accounts must log in on it: a graded login arriving on the normal port is refused and mailed as an alert.
  It is **not published**. See "Staff login" below.
* 14005 and 14006 (IPC between the servers, TLS plus the shared `ipc_secret`) and 8080 (account API) are not published either.
  8080 goes out through the tunnel only.

### About the firewall

`ufw` is on (default deny inbound, only the three game ports and SSH). **But Docker publishes ports with its own iptables rules that
run before ufw**, so ufw does not filter published container ports. What really limits the published ports is the `ports:` list of
the `netns` service, which has exactly the three game ports. Do not add ports there casually. After the first deploy, scan from
another machine (`nmap -Pn -p- <vps-ip>`) and compare with that list.

## What you create once (not done yet, needs the VPS and the domain)

Cloudflare (dashboard, free plan is enough):

1. **Domain** on Cloudflare DNS (nameservers pointed to Cloudflare).
2. **Tunnel `tm-api`** (Zero Trust, Networks, Tunnels, create, Cloudflared). Public hostname `api.<domain>`, **path `^/account/`**,
   -> service `HTTP` `172.28.0.10:8080`. Copy the **tunnel token**. The path is important: the same listener also serves the staff
   commands (`/ban`, `/announce`, ...), and with the path set the tunnel answers 404 for them instead of passing them on.
3. **Tunnel `tm-ssh`**. Public hostname `ssh.<domain>` -> service `SSH` `localhost:22`. Copy its **token** too.
4. **Access application** (Zero Trust, Access, Applications, Self-hosted) for `ssh.<domain>`. Policy 1: Allow, the e-mail addresses of
   the admins. Policy 2: Service Auth, for the CI token (next point). Without this application the SSH tunnel would be open to anyone.
5. **Service token** (Access, Service Auth, Create). Keep **Client ID** and **Client Secret** for GitHub.
6. **DNS records**: `A play.<domain>` (or one name per server) -> VPS IP, **DNS only**. The tunnel hostnames `api` and `ssh` are
   created for you. `www` and the apex belong to Cloudflare Pages (the website worker).
7. **Turnstile widget** (Turnstile, Add widget) for the hostnames `<domain>` and `www.<domain>`, mode Managed. The **site key** goes into
   the website (`js/config.js` of tm_website), the **secret key** into the secret file `turnstile_secret` (below). MainServer does not
   start while that file still holds the placeholder, so registration can never be open without the captcha by mistake.
8. **R2 bucket** `toymayhem-backups`. Create an **API token** (Object Read and Write, only this bucket); note the **Account ID**, **Access Key ID**,
   **Secret Access Key**. Optional but good: a bucket lock rule (retention 14 days), so a hacked VPS cannot delete the backups.

On your own machine:

9. **Admin SSH key**: `ssh-keygen -t ed25519 -f ~/.ssh/tm_admin`. The public key goes to the bootstrap script.
10. **CI SSH key**: `ssh-keygen -t ed25519 -f tm_ci -N ""`. Public key to the bootstrap script, private key to GitHub.
11. **Backup key pair** (GnuPG): `gpg --quick-generate-key "ToyMayhem backup" rsa4096 encrypt 2y`, then
    `gpg --export --armor <fingerprint> > backup-public.asc`. Keep the private key offline (and a backup of it); losing it means
    the backups cannot be read.

GitHub (repository decompozed/tm_emulator):

12. **Package visibility**: after the first push of the image, make the package `tm_emulator` public, or the VPS needs `docker login ghcr.io`
    with a read-only token.
13. **Environment** `production` with required reviewers (the deploy job uses it).
14. **Variables**: `DEPLOY_ENABLED=true` (this switches the deploy workflow on, until then it is skipped), `DEPLOY_SSH_HOST=ssh.<domain>`, `DEPLOY_USER=deploy`.
15. **Secrets**: `CF_ACCESS_CLIENT_ID`, `CF_ACCESS_CLIENT_SECRET`, `DEPLOY_SSH_KEY` (private CI key), `DEPLOY_SSH_KNOWN_HOSTS`
    (output of `ssh-keyscan -t ed25519 localhost` run on the VPS, with `localhost` replaced by `ssh.<domain>`).

The image itself needs no secrets: the workflow pushes to ghcr.io with the built-in `GITHUB_TOKEN`.

## First deploy

The VPS is a fresh Ubuntu 24.04 where you are root or can use sudo, and you still have the provider's web console as a fallback.

```bash
# 1. get the files (git clone or scp of the deploy folder and microvolts-db.sql), then look at the plan first
sudo deploy/host/bootstrap-host.sh                 # dry run: prints everything it would do, changes nothing

# 2. apply. Port 22 stays open (rate limited) in this run, so you cannot lock yourself out
sudo deploy/host/bootstrap-host.sh --apply --admin-pubkey-file tm_admin.pub --ci-pubkey-file tm_ci.pub

# 3. settings and secrets
sudo nano /opt/toymayhem/tm.env                                         # TM_DOMAIN, TM_IMAGE (the sha- tag you want)
sudo install -m 0600 /dev/stdin /etc/toymayhem/cloudflared-ssh.token    # paste the tm-ssh token, then Ctrl-D
printf '%s' 'THE-TM-API-TOKEN' | sudo tee /tmp/cf.token > /dev/null
printf '%s' 'THE-TURNSTILE-SECRET-KEY' | sudo tee /tmp/ts.key > /dev/null
sudo deploy/scripts/make-secrets.sh --cf-tunnel-token-file /tmp/cf.token --turnstile-secret-file /tmp/ts.key --email-token-file mail-password.txt
sudo shred -u /tmp/cf.token /tmp/ts.key
# for the hacking week, where many players share one school address: TM_REGISTER_LIMIT_PER_HOUR=60 in tm.env

# 4. start the SSH tunnel and prove Access SSH works from your laptop (next section) BEFORE closing port 22
sudo systemctl enable --now cloudflared-ssh.service

# 5. start the game
cd /opt/toymayhem/release
sudo docker compose --env-file ../tm.env up -d --wait
sudo docker compose --env-file ../tm.env ps

# 6. only now close public SSH
sudo deploy/host/bootstrap-host.sh --apply --lock-ssh-to-access --admin-pubkey-file tm_admin.pub

# 7. backup (section Backups), then scan the VPS from outside and compare with the port list
```

`~/.ssh/config` on your laptop, with `cloudflared` installed there:

```
Host tm
  HostName ssh.<domain>
  User tmadmin
  IdentityFile ~/.ssh/tm_admin
  ProxyCommand cloudflared access ssh --hostname %h
```

After that, deploys come from the pipeline: **Actions, Deploy to the VPS, Run workflow**, tag `sha-<40 hex of a commit on main>`. The CI key on
the VPS can only run `tm-deploy.sh`, which accepts `deploy sha-<hex>`, takes two files on stdin, pulls that tag and starts it.
By hand the same thing is `TM_IMAGE=ghcr.io/decompozed/tm_emulator:sha-<hex>` in `/opt/toymayhem/release/image.env`, then `docker compose --env-file ../tm.env --env-file image.env up -d`.

### If you lose SSH (console fallback)

Open the web console (VNC) of the VPS provider and log in with whatever the provider offers for root (reset password or rescue mode).
There is no password login for the normal users, and sshd only accepts keys, so from the console you can:

* open public SSH again: `ufw limit 22/tcp`, then SSH in with your admin key;
* check the tunnel: `systemctl status cloudflared-ssh ssh`, `journalctl -u cloudflared-ssh -e`;
* if the tunnel token is the problem, put a new one in `/etc/toymayhem/cloudflared-ssh.token` and `systemctl restart cloudflared-ssh`;
* if you lost the admin key, add a new public key to `/home/tmadmin/.ssh/authorized_keys`.

This is the part most worth rehearsing on the real VPS before the event.

## Staff login (port 13001)

The staff (graded) login only exists inside the VPS: AuthServer listens on `172.28.0.10:13001`, nothing publishes it. Staff reach it with an SSH
tunnel through Cloudflare Access, from their own machine:

```bash
ssh -N -L 13001:172.28.0.10:13001 tm        # the Host "tm" from above, leave it running
```

The staff game client then connects to `127.0.0.1:13001` as its Auth server. Nothing was tested with a real staff client here; the
server side checks that the connection arrived on the graded listener. This does need an SSH account with a shell, so only admins (`tm-ssh`
group) can do it. Blue team accounts are SFTP only and cannot forward ports.

Needs `TM_ENHANCED_SECURITY=true` (the default) and the mail settings in `tm.env`, because that mode also e-mails alerts.

## Logs for the Blue team

All three servers write to `/srv/toymayhem/logs` on the host (`TB_LOG_DIR`):

* `Auth.log`, `Main.log`, `Cast.log` (rotated at 50 MB, 10 files)
* `security/<Server>-YYYY-MM-DD.log` JSON lines, one security event per line
* `modes/` party game logs, `crash-<server>-<pid>.txt` after a crash

The folder belongs to the group `blueteam` (mode 2750, read only for them). Add a Blue student:

```bash
sudo adduser --disabled-password --gecos "" blue1
sudo usermod -aG blueteam blue1
sudo install -d -m 700 -o blue1 -g blue1 /home/blue1/.ssh
sudo install -m 600 -o blue1 -g blue1 blue1.pub /home/blue1/.ssh/authorized_keys
```

Add their e-mail to the Access policy of `ssh.<domain>`. They get SFTP only (same `ProxyCommand` as above, `sftp tm-blue:/srv/toymayhem/logs`), read only, and can copy the folder with `sftp`. Container output is also in `docker compose logs` (admins only).

## Rotate a secret

Secrets are files in `/opt/toymayhem/secrets`. Always: make the new value, then restart what uses it.

| secret | how |
|---|---|
| `jwt_secret` | `make-secrets.sh --rotate jwt_secret`, then `docker compose --env-file ../tm.env up -d --force-recreate auth main cast`. Everybody is logged out of the website. |
| `ipc_secret` | same, rotate and recreate `auth main cast` **together** (they must share it). |
| `password_pepper` | **careful**: players' stored password hashes depend on it. Use the pepper versioning of `config.ini` instead of replacing it blindly (`PasswordPepperVersion`, `PasswordPepperPrevious`), which this deployment does not wire up yet. |
| `db_app_password` | `docker compose exec db sh -c 'MYSQL_PWD=$(cat /run/secrets/db_root_password) mariadb -uroot -e "ALTER USER mvapp@\"%\" IDENTIFIED BY \"NEW\""'` with the new value, write the same into the file (`make-secrets.sh --rotate db_app_password` first, read it from the file), then recreate `db auth main cast`. |
| `db_root_password` | `ALTER USER root@localhost IDENTIFIED BY '...'` inside the DB, then rotate the file. Only needed for admin work. |
| `email_token` | `make-secrets.sh --email-token-file new.txt --rotate email_token`, recreate `auth main cast`. |
| `cf_tunnel_token` | in the dashboard, refresh the tunnel token, `make-secrets.sh --cf-tunnel-token-file new.txt --rotate cf_tunnel_token`, `docker compose up -d --force-recreate cloudflared`. |
| `turnstile_secret` | in the dashboard, rotate the secret key of the widget, `make-secrets.sh --turnstile-secret-file new.txt --rotate turnstile_secret`, recreate `main`. |
| `email_secret`, `twofa_secret` | change what they protect (stored e-mail addresses, 2FA seeds), so do not rotate during the event. |
| GitHub secrets | change in the repository settings, nothing on the VPS. CI SSH key: replace the line in `/home/deploy/.ssh/authorized_keys`. |

Check afterwards with `docker compose ps` (all `healthy`) and the log folder.

## Backups and restore

Every night 03:15 UTC (`tm-backup.timer`): `mariadb-dump` of `microvolts-db` as the limited account, gzip, **encrypted with the public key**
(GnuPG, AES256), uploaded to R2 with rclone, copies older than `KEEP_DAYS` removed (the newest three always stay). The private key is not on the VPS and the
script refuses to run if it finds one in its keyring.

Set up once on the VPS:

```bash
sudo install -m 600 deploy/db-backup/backup.env.example /etc/toymayhem/backup.env     # fill in R2 values and GPG_RECIPIENT (fingerprint)
sudo install -m 644 backup-public.asc /etc/toymayhem/backup-public.asc
sudo /opt/toymayhem/bin/tm-backup.sh               # first run by hand, watch it work
sudo systemctl enable --now tm-backup.timer
systemctl list-timers tm-backup.timer; cat /var/lib/toymayhem-backup/last-success
```

**Restore** (the private key stays on your laptop, so you decrypt there):

```bash
ssh tm 'sudo /opt/toymayhem/bin/tm-restore.sh --list'
ssh tm 'sudo /opt/toymayhem/bin/tm-restore.sh --fetch latest /tmp/restore'
scp tm:/tmp/restore/toymayhem-*.sql.gz.gpg* .                     # the .gpg and its .sha256
deploy/db-backup/tm-restore.sh --decrypt toymayhem-<stamp>.sql.gz.gpg | ssh tm 'sudo /opt/toymayhem/bin/tm-restore.sh --load --yes'
```

`--load` stops auth, main and cast, loads the dump, starts them again. **Test a restore at least once before the event** (the pipeline does a
round trip with a throwaway key on every run, but not against R2 and your real key).

## Reset after the hacking week

1. Keep the evidence first: copy `/srv/toymayhem/logs` off the VPS (`rsync`), run `tm-backup.sh` once more, and note `docker compose ps` / `docker inspect` of what ran.
2. Stop and wipe the data:
   ```bash
   cd /opt/toymayhem/release
   sudo docker compose --env-file ../tm.env down --volumes        # removes the database volume
   sudo find /srv/toymayhem/logs -mindepth 1 -delete               # only after you copied them
   ```
3. Fresh secrets for the next round: `sudo rm /opt/toymayhem/secrets/*`, run `make-secrets.sh` again (and the tunnel and mail tokens), because the Red team may have seen the old ones.
4. `docker compose --env-file ../tm.env up -d --wait`. The database starts empty and is filled from `microvolts-db.sql` (the first start of a new volume).
5. Rotate or delete the Access policy entries of the Red/Blue students, and the Blue accounts (`sudo deluser --remove-home blue1`).
6. To shut down for good: delete the tunnels and the Access application in Cloudflare, the R2 bucket, the DNS records, then the VPS.

## What was and was not tested

Tested in the pipeline (branch `deploy/containers-7`, run 42805825144, all jobs green, publish job skipped because it is not main):
the image builds from the pinned vcpkg baseline; the compose file starts `db`, `netns`, `auth`, `main`, `cast` from that image and all are healthy;
the three game ports answer on the host, 13001 does not answer on 127.0.0.1 but does on the container address; the database has no published port and `mvapp`
is refused `CREATE DATABASE`; the servers write `Auth.log`, `Main.log`, `Cast.log` and the JSON security log; restarting `main` brings it back healthy;
an encrypted dump with a throwaway key contains no readable table names and restores a deleted table; shellcheck and the bootstrap dry run on Ubuntu 24.04.

Not testable without the VPS: the real bootstrap (`--apply`), Cloudflare Access SSH, both tunnels, the R2 upload and retention, the deploy workflow,
the external port scan, behaviour under load, and whether the memory and CPU limits are the right size (they are guesses).
Also not checked: a plain `GET /` on port 8080 inside the container got "connection reset", so try a real API path through the tunnel
(`curl https://api.<domain>/...`) after the first deploy.

Two things the schema needed that are easy to forget: MariaDB runs with `lower-case-table-names=1` (the code asks for `Users`, the dump creates `users`),
and `db-init/10-schema.sh` loads the dump, because the dump file lost its header lines and would otherwise stop with a TIME_ZONE error.
