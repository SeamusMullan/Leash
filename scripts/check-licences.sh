#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
set -euo pipefail

manifest="vcpkg.json"
if [[ "${1:-}" == "--manifest" ]]; then
    manifest="${2:?usage: check-licences.sh [--manifest PATH]}"
fi
strict=0
if [[ "$manifest" == "vcpkg.json" ]]; then
    strict=1
fi

python3 - "$manifest" "$(dirname "$0")/dependency-licences.tsv" "$strict" <<'PY'
import json
import sys

manifest_path, table_path = sys.argv[1], sys.argv[2]
strict_rows = sys.argv[3] == "1"
allowed = {
    "Apache-2.0", "BSL-1.0", "MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC",
    "Zlib", "CC0-1.0", "Unicode-3.0", "Apache-2.0 WITH LLVM-exception",
    "MPL-2.0",
}

rows = {}
with open(table_path, encoding="utf-8") as table:
    for line in table:
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        port, licence, source = line.split("\t")
        rows[port] = (licence, source)

with open(manifest_path, encoding="utf-8") as handle:
    document = json.load(handle)
dependencies = document.get("dependencies", [])
names = [d if isinstance(d, str) else d.get("name") for d in dependencies]

errors = []
for name in names:
    if name not in rows:
        errors.append(f"no licence row for dependency: {name}")
        continue
    for alternative in (part.strip() for part in rows[name][0].split(" OR ")):
        if alternative not in allowed:
            errors.append(f"{name}: licence not allowed: {alternative}")
for name in rows:
    if strict_rows and name not in names:
        errors.append(f"licence row without a dependency: {name}")

if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print(f"licences ok: {len(names)} dependencies")
PY
