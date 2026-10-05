#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Iinclude src/transport.c tests/test_transport.c -o "$task_tmp/transport"
"$task_tmp/transport"
cc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc -Ithird_party/fatfs src/storage.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c tests/test_storage.c -o "$task_tmp/storage"
"$task_tmp/storage"
python3 tests/test_patterns.py
