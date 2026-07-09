# Legend of Pebble

A first-person dungeon crawler for **Pebble Time 2** (and Round 2).

You were checking the time. Then the watch pulled you through.

Ten levels. Merchants. Grimrock-style puzzles. Find the way home.

**1.2.1** (balance/RAM polish on the 1.2.0 feature set): Signal Magic (Heal / Purge / Decode), The Keeper mid-boss, weapons, and permanent chests.

## Credits

**Legend of Pebble** is a fork of the open-source game
[**Dungeon Escape**](https://apps.repebble.com/dungeon-escape_4d6fde68a2b047ef83381221)
by **Brian Ouellette** (GPL-3.0).

Thank you Brian for releasing Dungeon Escape as open source — this project
would not exist without it.

- Bluesky: [@brianenders.bsky.social](https://bsky.app/profile/brianenders.bsky.social)
- Original artwork / JS lineage credited in Dungeon Escape to
  [Clint Bellanger](https://clintbellanger.net/)

See `LICENSE` (GPL-3.0).

## Build

```bash
# with the pebble-test Docker/Podman image
podman run --rm --platform linux/amd64 -v "$PWD:/app" -w /app pebble-test pebble build
```

## Store listing

Rebble Appstore assets and paste-ready description live in [`store/`](store/LISTING.md).

```bash
.venv/bin/python scripts/gen_store_assets.py
```
