# Demo deployment

The public demo (https://sist2.simon987.net/) is a single `sist2 web` process serving a
handful of read-only indices out of Elasticsearch. There is no sist2-admin: every index is
rebuilt from scratch by `update_demo.sh` after a release.

## Setup

1. Copy `env.example` to `.env` on the host and fill it in. `.env` holds the Elasticsearch
   password, so it stays out of git.
2. Put one folder per index under `DATA_DIR`, named as in the `corpora` list at the top of
   `update_demo.sh`.

## Updating

```bash
cd demo
./update_demo.sh
```

It pulls `SIST2_IMAGE`, stops the frontend, deletes the old `.sist2` files, scans every
corpus, generates the CLIP embeddings of the image corpus, pushes each index to
Elasticsearch (the first push resets the mappings, dropping the previous run) and starts
the frontend again.

Flags: `--no-pull`, `--skip-clip` (skip the embeddings), `--rebuild-clip-image` (rebuild the
torch + CLIP image, needed when `SIST2_IMAGE` changes).

To deploy a new release, edit `SIST2_IMAGE` in `.env` and run the script.

## Notes

- The frontend is given the index files as a `/indices/*.sist2` glob, so adding a corpus is
  a line in `corpora` and a folder under `DATA_DIR` — the compose file does not change.
- Elasticsearch and traefik are expected to run already, on `DOCKER_NETWORK`.
- `ES_URL` carries its credentials on the `sist2 web` command line, where anything that can
  read the container's process list can see them. Give the demo an Elasticsearch user of its
  own rather than `elastic`.
