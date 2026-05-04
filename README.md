# orbis-store

Project layout:

- `ps4/` — PS4 client app (OpenOrbis build).
- `worker/` — Cloudflare Worker API.
- `website/` — Static web UI (generator + redeem pages).

Root `index.html` and `redeem.html` redirect to the pages in `website/` to keep existing entrypoints working.

## PS4 prerelease workflow

Run the **PS4 prerelease** workflow (Actions tab) to build `ps4/OrbisStore.pkg` and publish a GitHub prerelease.
The workflow downloads the OpenOrbis toolchain release (`v0.5.4`), builds the PKG, and creates/updates the `ps4-prerelease` tag by default.
