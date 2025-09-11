#!/bin/bash
################################################################################
# sync-system.sh
#
# Developer-only utility script.
# Not intended for end users or production environments.
# Used by developers on test machines to sync the project source tree with
# installed system files.
#
# Behavior:
#   • Performs bi-directional sync between the project tree and system files.
#   • Only syncs files in subdirectories (root-level project files are skipped).
#   • File timestamps determine which version is newer (last modified wins).
#   • Deletions are tracked using a reference file list:
#       - Files missing on one side are removed from the other.
#       - If the reference file list does not exist, deletions are skipped
#         (first sync is always safe).
#   • Reference file list is updated after each successful sync.
#
# Reference file:
#   $PROJECT_DIR/.sync-system-filelist (hidden in project root)
#   - Stores the list of known files from the last sync.
#   - Deleting this file resets deletion tracking. The next sync behaves
#     as if it were the first run.
#
# Intended workflow:
#   1. Edit files in the project tree or system.
#   2. Run sync-system.sh to push/pull changes both ways.
#   3. Deletions propagate automatically after the first sync.
#
# SAFETY NOTE:
#   Running this script outside the development workflow may delete
#   critical system or project files. Only run it if you understand the
#   synchronization mechanism.
#   Requires sudo for system-side changes.
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

################################################################################
backup_file() {
    local target_file="$1"
    local timestamp backup_file

    if [ -f "$target_file" ]; then
        # Try GNU date -r first
        if ! timestamp=$(date -r "$target_file" +"%Y%m%d%H%M%S" 2>/dev/null); then
            # Fallback for BSD/macOS
            local ts
            ts=$(stat -c %Y "$target_file" 2>/dev/null || stat -f %m "$target_file")
            timestamp=$(date -d @"$ts" +"%Y%m%d%H%M%S" 2>/dev/null || date -r "$ts" +"%Y%m%d%H%M%S")
        fi

        backup_file="${target_file}.${timestamp}.bak"
        echo "Backing up existing $target_file → $backup_file"
        sudo cp -afv "$target_file" "$backup_file"
    fi
}

sync_file() {
    local src
    local repo_name
    local dest
    local repo_tag

    if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
        echo "[!] Usage: sync_file <source> <destination>" >&2
        exit 1
    fi

    src="$PROJECT_DIR/$1"
    repo_name=$(basename "$PROJECT_DIR" | tr -cd '[:alnum:]')
    dest="${2//%PROJECT_NAME/$repo_name}"
    repo_tag="${3:-}"

    if [ -f "$src" ]; then
        # Copy new file and backup old file
        backup_file "$dest"
        sudo rsync -azui --ignore-missing-args --omit-dir-times \
            --out-format='[UPDATE] '$dest \
            --no-o --no-g \
            "$src" "$dest"

        if [ -n "$repo_tag" ]; then
            if grep -q "$repo_tag" "$dest"; then
                sudo sed -i "s|$repo_tag|$PROJECT_DIR|g" "$dest"
            fi
        fi
    else
        echo "[!] $src not found, skipping."
    fi
}

# Custom one-way sync for specific top-level files
sync_file uninstall.sh /usr/local/bin/uninstall-%PROJECT_NAME.sh __REPO_DIR__

