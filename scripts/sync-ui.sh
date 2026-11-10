#!/bin/sh
# Copies the prototype page into the plugin and gives it a full HTML head (UTF-8 for the accented labels).
set -e
cd "$(dirname "$0")/.."
SRC="${1:-../kg16/index.html}"
{ printf '<!doctype html>\n<html lang="hu"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">\n';
  printf '<style>html,body{margin:0}</style>\n'; cat "$SRC"; printf '\n</html>\n'; } > ui/index.html
echo "ui/index.html updated"
