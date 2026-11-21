# Economy history on server-media

This service collects raw poe.ninja observations continuously without PoB or
filter compilation. Desktop Ruleforge downloads the observations and computes
its own build/theme-independent economy decisions. Each league is collected at
most once every four hours by default. Requests are spaced one second apart;
failures retry after five minutes with exponential backoff up to one hour and
small positive jitter. Restarting the service respects the last collection time.

## Install

Use Docker Engine with the Compose plugin on server-media. The image supports
Linux amd64 and arm64. From a checkout of the published feature branch:

```sh
git clone --branch codex/filter-economy-330 https://github.com/LordMartron94/Ruleforge.git /opt/ruleforge
cd /opt/ruleforge/deployment/economy
umask 077
cp .env.example .env
# Fill RULEFORGE_HISTORY_TOKEN in .env using this generated value:
openssl rand -hex 32
```

Keep `.env` private (mode 600). To connect directly from your desktop, set
`RULEFORGE_BIND_ADDRESS` to server-media's LAN address. The default binds only
localhost, suitable for a tunnel or reverse proxy. Do not expose a plain HTTP
bearer token on an untrusted network; use HTTPS at your reverse proxy or an SSH
tunnel. No Docker socket is mounted into this container.

```sh
docker compose pull
docker compose up -d
docker compose logs --tail=100 economy-history
```

GHCR packages may require making the package public once in GitHub's package
settings. Alternatively authenticate Docker to GHCR with package read access.
To build directly from this checkout instead of pulling:

```sh
docker compose -f compose.yaml -f compose.build.yaml build
docker compose -f compose.yaml -f compose.build.yaml up -d
```

The `history` named volume holds `/data/economy.db` and its SQLite WAL files.
Never use `docker compose down -v` unless you intend to delete that history.
The process runs as uid/gid 65532 and needs write access only to this volume.
The root filesystem is read-only. A 768 MiB container memory limit is supplied.

`RULEFORGE_LEAGUES=auto` follows the provider's active trade league list, excluding
SSF and Ruthless. Set a comma-separated list to constrain it. Configure
`RULEFORGE_GAME_VERSION` deliberately at a patch transition; it is stored with
observations, not guessed from item names. Match the desktop's `EconomyMemory`
version and start timestamp after a generation/economy regime change.

## Connect Ruleforge

In desktop `config.toml`, uncomment/set:

```toml
EconomyHistoryURL = "http://server-media:8087"
EconomyHistoryTokenEnv = "RULEFORGE_HISTORY_TOKEN"
```

Set the environment variable named there to the same secret from the server's
`.env`, then launch Ruleforge normally. For GUI/Studio launches, alternatively
store just the token in a private, mode-600 file and set
`EconomyHistoryTokenFile = ".ruleforge-cache/history-token"` in the desktop
configuration. Paths resolve beside the config file; a nonempty environment
variable overrides the file. Keep the token out of tracked files.
With a configured service, Ruleforge
synchronizes raw snapshots at startup instead of polling poe.ninja itself.
`-refresh-economy` also synchronizes; it does not remotely force extra polls.
An unavailable service uses the local history with a warning if any configured
league has cached data. Without cached data the run fails clearly. The client's
freshness/patch policy still applies when offline; stale quotes are not current
market evidence. Clear `EconomyHistoryURL` to resume desktop collection.

For a tunnel, keep the server bound to localhost:

```sh
ssh -N -L 8087:127.0.0.1:8087 server-media
```

Then use `http://127.0.0.1:8087` as the desktop URL. The token still applies.

The SQLite database is not shared over the network. `/v1/snapshots` is a paged,
read-only JSON API, with a stable database identity, bounded high watermark and
monotonic append cursor. Each imported page and its local checkpoint commit in
one transaction. An interrupted sync resumes; a replaced server database has a
new identity and starts a fresh cursor while retaining the client's old data.
Local snapshots collected before installing the server remain in the local DB.
For multiple desktops, each gets its own checkpoint and local cache.

## Collection status and category capabilities

`GET /healthz` is unauthenticated liveness, not a freshness claim.
`GET /v1/status` and `GET /v1/snapshots` require
`Authorization: Bearer <secret>`. Status includes per-league counts, latest
successful snapshot, category capability, last attempt and last error. There
are no remote write or forced-refresh endpoints. Pass `-collect=false` to
serve an archive without making external polling requests.

Default feeds include uniques, gems, cards, tattoos, currency, delirium orbs and
omens. The additional Fragment, AllflameEmber, Essence, Oil, Fossil and Viscera
feeds are explicit optional capabilities. Their API type identifiers must be
confirmed by the provider's response: a 400/404 is recorded as `unsupported`,
an empty successful result as `empty`, and a valid nonempty result as
`available`. They are not presented as live-verified at implementation time:
poe.ninja returned HTTP 403 from this environment. Other HTTP errors, including
403, 429 and 5xx, fail that league collection; no partial league snapshot is
committed. Existing observations and curated filter classifications survive.
Logs and status make unsupported feeds visible.

To replace feed identifiers or add a provider-supported category, mount a TOML
file and pass `-feeds=/path/to/feeds.toml`. This replaces the complete feed list:

```toml
[[Categories]]
Kind = "Currency"
Class = "Currency"
Endpoint = "exchange"
Optional = false

[[Categories]]
Kind = "Viscera"
Class = "Viscera"
Endpoint = "stash"
Optional = true
```

The category's `Class` is Ruleforge's economy family, not necessarily a game
filter class. Exchange prices must be denominated in chaos; incompatible
responses fail explicitly. Exchange turnover is stored separately from stash
listing counts. IDs and known variant/level/link/quality/corruption fields are
retained with every observation. Unknown legacy fields remain unknown.

## Automatic image updates

Collection is automatic inside the service. For automatic *software* updates,
install the supplied host systemd service and timer after reviewing their
`/opt/ruleforge` path:

```sh
sudo install -m 644 ruleforge-economy-update.service ruleforge-economy-update.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ruleforge-economy-update.timer
```

Once daily, with up to one hour of random delay, the timer pulls the configured
image tag and recreates only `economy-history` if needed. A failed pull leaves
the running service untouched. This requires the host account running the unit
to have Docker access (the supplied system unit runs as root). It never deletes
the persistent volume or updates unrelated services. `update.sh` can also be
run manually. Use a floating branch/`latest` tag for updates; use an immutable
`sha-...` image tag or digest for a pinned deployment. `latest` is published
only from the default branch; the initial feature image uses
`codex-filter-economy-330`.

GitHub Actions tests the affected Go modules and smoke-tests the native image
before publishing the two-platform
image to `ghcr.io/lordmartron94/ruleforge-economy`. Pull requests build but do not
publish. A weekly default-branch rebuild refreshes the base image once this
workflow is merged to the default branch. Until then, pushes on this feature
branch trigger its image builds.

## Backups and rollback

For a simple consistent backup, stop the service before copying the volume's
entire contents, then restart it. For online backups use SQLite's backup API,
not a copy of just the main DB file while WAL writes are active. Preserve the
backup outside the volume. The API is intentionally read-only; restoring a
backup is an operator action on server-media.

To roll back an image, set `RULEFORGE_ECONOMY_IMAGE` to a previously tested
`sha-...` tag and run `update.sh`. Database migrations add columns and preserve
old raw observations. Back up before upgrading across future incompatible
schema changes; changing an image tag does not itself roll back data.
