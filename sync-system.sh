#!/bin/bash
################################################################################
# sync-system.sh
#
# Developer-only utility script.
# Not intended for end users — used by project developers to keep the source
# tree and installed system files in sync.
#
# Behavior:
#   • Performs bi-directional sync between project tree and system files.
#   • Only syncs files in subdirectories (root-level project files are skipped).
#   • File timestamps decide which side is newer (last modified wins).
#   • Deletions are tracked with a reference file list:
#       - If a file existed previously but is missing on one side, it will be
#         deleted from the other side as well.
#       - If the reference file list does not exist, no deletions occur
#         (first sync is always safe).
#   • Reference file list is updated after each successful sync.
#
# Reference file:
#   .sync-system-filelist
#   - Stores the list of known files from the last sync.
#   - Deleting this file resets deletion tracking. The next sync will act as
#     if it were the first run (no deletions until list is rebuilt).
#
# Intended workflow:
#   1. Edit files in the system (or project).
#   2. Run sync-system.sh to push/pull changes both ways.
#   3. Deletions propagate automatically after the first sync.
#
# SAFETY NOTE:
#   Running this script outside of the development workflow may delete
#   important system or project files. Do not use unless you understand
#   the project’s sync mechanism.
#
# Copyright (C) 2025 Richard Elwell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
################################################################################
finish() { echo "Sync complete[$?]"; }
trap finish EXIT
#set -x # debug switch
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
REFERENCE_FILE="$PROJECT_DIR/.sync-system-filelist"
MAX_FILES=20

update_reference_file() {
    local tmpfile
    tmpfile=$(mktemp)

    cd "$PROJECT_DIR"
    find . -mindepth 2 \( \
        -path "./.git/*" \
        \) -prune -o -type f -print \
    | cut -c3- >"$tmpfile"

    # Safety check.  Increase this limit as needed.
    NUM_FILES=$(wc -l <"$tmpfile")
    if (( NUM_FILES > MAX_FILES )); then
        echo "ERROR: Too many files ($NUM_FILES). Aborting to prevent accidental mass overwrite." >&2
        rm -f "$tmpfile"
        exit 1
    else
        # Atomically replace reference file
        mv "$tmpfile" "$REFERENCE_FILE"
    fi
}

cd "$PROJECT_DIR"
#echo "Working directory: $(pwd)"

# If we already have a reference list, detect deletions
if [ -f "$REFERENCE_FILE" ]; then
    # Safety check.  Increase this limit as needed.
    NUM_FILES=$(wc -l <"$REFERENCE_FILE")
    if (( NUM_FILES > MAX_FILES )); then
        echo "ERROR: Too many files ($NUM_FILES). Aborting to prevent accidental mass deletion." >&2
        exit 1
    fi

    while read -r f; do
        if [ ! -f "./$f" ] && [ -f "/$f" ]; then
            echo "[REMOVE] /$f"
            sudo rm -f "/$f"
        elif [ -f "./$f" ] && [ ! -f "/$f" ]; then
            echo "[REMOVE] $PROJECT_DIR/$f"
            rm -f "$PROJECT_DIR/$f"
        fi
    done <"$REFERENCE_FILE"
else
    echo "[!] No reference file found, skipping deletions this run."
fi

# Update reference file list to new state
update_reference_file

# Now do a normal two-way rsync
#echo "Syncing system -> project (only newer files)"
rsync -azui --relative --ignore-missing-args --omit-dir-times \
    --out-format='[UPDATE] '$PROJECT_DIR'/%f' \
    --no-o --no-g \
    --files-from="$REFERENCE_FILE" / "$PROJECT_DIR"/

#echo "Syncing project -> system (only newer files)"
sudo rsync -azui --relative --ignore-missing-args --omit-dir-times \
    --out-format='[UPDATE] /%f' \
    --no-o --no-g \
    --files-from="$REFERENCE_FILE" "$PROJECT_DIR"/ /

# Update reference file list to new state
update_reference_file

