# Economy history collector

The collector retains the original SQLite database, schema migrations, protocol version 1, flags and environment variables.
It runs independently of Studio and filter compilation. Raw observations are not pruned by the evidence policy.

## Existing container on another machine or qube

Keep the existing Compose file, project name, volume, token, bind address and collection flags. After the first stable release of
this implementation, change only the image in the existing `.env`:

```dotenv
RULEFORGE_ECONOMY_IMAGE=ghcr.io/lordmartron94/ruleforge-economy:latest
```

Run the existing `update.sh` or `docker compose pull economy-history` followed by
`docker compose up -d --no-deps economy-history`. Do not delete or recreate the named history volume. For a pinned installation use
`:X.Y.Z`. Stable release tags publish that version and update `latest`; prereleases publish only their version. The old
`:codex-filter-economy-330` tag is deliberately left unchanged. No image is published until a release workflow runs successfully.

If the existing daily systemd timer already invokes `update.sh`, changing the `.env` image reference is enough for it to receive
subsequent stable releases. Otherwise install the included service and timer, adapting `/opt/ruleforge/deployment/economy` to your
existing deployment directory, then enable `ruleforge-economy-update.timer`. The update script pulls successfully before recreating
the service and locks overlapping invocations with `flock` when available. Updates do not require inbound access to the machine.

## New installation

Copy this directory to the service machine. Copy `.env.example` to `.env`, generate a private token with `openssl rand -hex 32`,
set `RULEFORGE_HISTORY_TOKEN`, and select the desired leagues and patch regime. Then run `docker compose up -d`. Bind to loopback
when accessing through an SSH tunnel. Docker logs are rotated using `RULEFORGE_LOG_MAX_SIZE` and `RULEFORGE_LOG_KEEP`.

The container runs as UID/GID 65532 with a read-only root, dropped capabilities and a persistent `/data` volume. The health check
only reads `/healthz`; it does not collect or modify history. `-collect=false` serves an existing history without polling poe.ninja.
`RULEFORGE_HISTORY_TOKEN_FILE` is supported as an alternative to the token environment variable.

For a local source build, from this directory run
`docker compose -f compose.yaml -f compose.build.yaml build`. The build context is the Studio Go module, so the monorepo is not sent to Docker. CI builds the image from
`projects/ruleforge/deployment/economy/Dockerfile` and publishes Linux amd64 and arm64 images after release assets are published.

## Client configuration

Studio's `economyHistory` project setting names the URL, local SQLite path, token environment variable, leagues and evidence policy.
The existing API remains read-only and authenticated. Synchronization resumes interrupted pages, pins a high watermark so a live
collector cannot make a sync run forever, and resets its cursor when the server's dataset identity changes.

A failed or unsupported feed never silently becomes zero prices. The collector commits complete league collections; optional
unsupported categories are recorded explicitly. Evidence gates distinguish stash listings from exchange turnover, bucket repeated
polls, isolate patch regimes and keep confidence per league. Source observations remain available for future policy changes.
