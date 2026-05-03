# orbis-store

Project layout:

- `ps4/` — PS4 client app (OpenOrbis build).
- `worker/` — Cloudflare Worker API.
- `website/` — Static web UI (generator + redeem pages).

Root `index.html` and `redeem.html` redirect to the pages in `website/` to keep existing entrypoints working.
