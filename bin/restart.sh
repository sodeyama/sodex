#!/bin/sh

find_repo_root() {
  dir="$1"

  while [ "$dir" != "/" ]; do
    if [ -f "$dir/makefile" ] && [ -f "$dir/makefile.inc" ] && [ -d "$dir/src" ]; then
      printf '%s\n' "$dir"
      return 0
    fi
    dir=$(dirname "$dir")
  done

  return 1
}

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(find_repo_root "$SCRIPT_DIR")" || {
  echo "リポジトリルートが見つかりません: $SCRIPT_DIR" >&2
  exit 1
}

make -C "$REPO_ROOT" clean || exit 1
make -C "$REPO_ROOT" || exit 1
exec "$REPO_ROOT/bin/start.sh" "$@"
