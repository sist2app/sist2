#!/usr/bin/env bash
# Rebuild the public demo from scratch: wipe the old indices, scan every corpus,
# generate the embeddings the user scripts add, push everything to Elasticsearch and
# restart the frontend. Run it after a release, once.
#
# Host-specific settings live in demo/.env (see env.example); it is not in git.

set -e
cd "$(dirname "$0")"

pull=1
clip=1
sbert=1
rebuild_scripts_image=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-pull) pull=0 ;;
    --skip-clip) clip=0 ;;
    --skip-sbert) sbert=0 ;;
    --rebuild-scripts-image) rebuild_scripts_image=1 ;;
    -h|--help)
      echo "usage: $0 [--no-pull] [--skip-clip] [--skip-sbert] [--rebuild-scripts-image]"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
  shift
done

if [ ! -f .env ]; then
  echo "demo/.env is missing. Copy env.example and fill it in." >&2
  exit 1
fi

set -a
. ./.env
set +a

for var in SIST2_IMAGE DATA_DIR INDEX_DIR ES_URL ES_INDEX DOCKER_NETWORK DEMO_HOSTS; do
  if [ -z "${!var}" ]; then
    echo "$var is not set in demo/.env" >&2
    exit 1
  fi
done

# <index file name>|<folder under DATA_DIR>|<display name>
# The file names decide the order the indices are listed in, so they are numbered.
corpora=(
  "10-bhl|bhl|Biodiversity Heritage Library"
  "20-encyclopedias|demo_encyclopedias|Encyclopedias"
  "30-linux|linux|Linux kernel source"
  "40-test-files|demo_files|Test files"
  "50-clip|clip-demo-images|CLIP demo"
)
# The index each user script runs on
clip_index="50-clip"
sbert_index="20-encyclopedias"

scripts_image="sist2-demo-scripts:${SIST2_IMAGE##*:}"
threads=${THREADS:-$(nproc)}

sist2() {
  docker run --rm \
    --network "$DOCKER_NETWORK" \
    -v "$DATA_DIR:/data:ro" \
    -v "$INDEX_DIR:/indices" \
    "$SIST2_IMAGE" "$@"
}

if [ "$pull" = 1 ]; then
  echo "==> Pulling $SIST2_IMAGE"
  docker pull "$SIST2_IMAGE"
fi

if [ "$clip" = 1 ] || [ "$sbert" = 1 ]; then
  if [ "$rebuild_scripts_image" = 1 ] || ! docker image inspect "$scripts_image" > /dev/null 2>&1; then
    echo "==> Building $scripts_image (torch, CLIP and sentence-transformers on top of the sist2 image)"
    docker build --build-arg "SIST2_IMAGE=$SIST2_IMAGE" -t "$scripts_image" -f scripts.Dockerfile .
  fi
fi

echo "==> Stopping the frontend"
docker compose down --remove-orphans

echo "==> Deleting the old indices"
mkdir -p "$INDEX_DIR"
rm -rf "${INDEX_DIR:?}"/*.sist2

for corpus in "${corpora[@]}"; do
  IFS="|" read -r index folder name <<< "$corpus"

  if [ ! -d "$DATA_DIR/$folder" ]; then
    echo "$DATA_DIR/$folder does not exist" >&2
    exit 1
  fi

  echo "==> Scanning $name"
  sist2 scan \
    --threads="$threads" \
    --name="$name" \
    --output="/indices/${index}.sist2" \
    "/data/$folder"
done

if [ "$clip" = 1 ]; then
  echo "==> Generating the CLIP embeddings"
  docker run --rm \
    --entrypoint /bin/bash \
    -v "$DATA_DIR:/data:ro" \
    -v "$INDEX_DIR:/indices" \
    "$scripts_image" \
    -c "cd /opt/sist2-script-clip && exec python run.py '/indices/${clip_index}.sist2' \
        --num-tags=1 --tags-file=general.txt --color='#dcd7ff'"
fi

if [ "$sbert" = 1 ]; then
  # One embedding per chunk: an embeddings search scores the passage that matched and quotes it
  # back as the excerpt of the result, which needs Elasticsearch 8.11 or later
  echo "==> Generating the sentence embeddings"
  docker run --rm \
    --entrypoint /bin/bash \
    -v "$INDEX_DIR:/indices" \
    "$scripts_image" \
    -c "cd /opt/sist2-script-sbert && exec python run.py '/indices/${sbert_index}.sist2'"
fi

# The first push resets the mappings, which drops every document of the previous run
reset="--force-reset"
for corpus in "${corpora[@]}"; do
  IFS="|" read -r index folder name <<< "$corpus"

  echo "==> Indexing $name"
  sist2 index \
    --threads="$threads" \
    --es-url="$ES_URL" \
    --es-index="$ES_INDEX" \
    $reset \
    "/indices/${index}.sist2"
  reset=""
done

echo "==> Starting the frontend"
docker compose up -d

echo
# The first name of the traefik rule, without its backticks
primary_host=${DEMO_HOSTS%%,*}
echo "The demo is up to date: https://${primary_host//\`/}/"
