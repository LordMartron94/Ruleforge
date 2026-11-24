# 0007. Preserve the existing economy history storage and synchronization protocol

## Status

Proposed; Mr. Hoorn finalizes the record.

## Decision

In the context of replacing the original engine while an economy collector already runs on another machine, facing the cost of
losing historical observations or deploying incompatible clients, we decided to carry forward the SQLite schema and migrations,
protocol version 1, bounded synchronization cursors, authentication, flags and environment variables into the Go host's `history`
package and `economy-history` command. Its tests include legacy migration, interrupted synchronization and incomplete collections.

Raw observations remain in SQLite. The host publishes patch-filtered, time-bucketed snapshots and liquidity evidence as the
`economy.history` dataset; documented Ruleforge functions consume that evidence for filter policy. Missing observations never become
zero prices. Confidence remains per league, and stash listing counts remain distinct from exchange turnover.

Release automation publishes versioned collector images and updates `latest` only for stable releases. Existing deployments change
the image reference while retaining their volume and service configuration. This avoids a database conversion or remote-control
update mechanism; the tradeoff is retaining compatibility code and requiring the existing deployment's pull timer to be enabled.
