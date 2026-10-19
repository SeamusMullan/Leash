#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
set -euo pipefail

fix=0
[[ "${1:-}" == "--fix" ]] && fix=1

status=0
while IFS= read -r file; do
    case "$file" in
        LICENSE|NOTICE|AUTHORS|.mailmap|*.md|*.json|*.jsonl|*.ebnf|reference_paper/*|docs/*) continue ;;
    esac
    case "$file" in
        *.cpp|*.hpp|*.h|*.cc|*.cxx) mark='//' ;;
        *.sh|*.cmake|*.yml|*.yaml|CMakeLists.txt|justfile) mark='#' ;;
        *) continue ;;
    esac
    one="${mark} SPDX-License-Identifier: Apache-2.0"
    two="${mark} Copyright 2026 The Leash Authors"
    start=1
    if head -n 1 "$file" | grep -q '^#!'; then start=2; fi
    first=$(sed -n "${start}p" "$file")
    second=$(sed -n "$((start + 1))p" "$file")
    if [[ "$first" == "$one" && "$second" == "$two" ]]; then continue; fi
    if [[ $fix -eq 1 ]]; then
        tmp=$(mktemp)
        if [[ $start -eq 2 ]]; then
            { head -n 1 "$file"; echo "$one"; echo "$two"; } > "$tmp"
            [[ -n "$(sed -n '2p' "$file")" ]] && echo >> "$tmp"
            tail -n +2 "$file" >> "$tmp"
        else
            { echo "$one"; echo "$two"; } > "$tmp"
            [[ -n "$(sed -n '1p' "$file")" ]] && echo >> "$tmp"
            cat "$file" >> "$tmp"
        fi
        mv "$tmp" "$file"
    else
        echo "missing header: $file" >&2
        status=1
    fi
done < <(git ls-files)
exit $status
