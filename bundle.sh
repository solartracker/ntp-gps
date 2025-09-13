#!/bin/bash
################################################################################
# bundle.sh
#
# Recursively inlines all `source` dependencies in a Bash script to produce
# a single, self-contained output script. Removes original `source` lines.
#
# Features:
#   - Recursively inlines scripts referenced via `source` (both quoted and unquoted)
#   - Expands variables in `source` paths, e.g., $SCRIPT_DIR or ${LIB_DIR}
#   - Accepts a list of variable assignments for expansion
#   - Detects and prevents infinite loops from circular `source` references
#   - Preserves formatting, indentation, blank lines, and comments
#   - Removes original `source` lines so the output is truly self-contained
#   - Optional verbose mode to show inlined and skipped files
#   - Fails immediately if any referenced file is missing
#
# Usage:
#   bundle_script <input_script> <output_script> [verbose] [VAR1=value1 VAR2=value2 ...]
#
#   <input_script>   : Path to the script to bundle
#   <output_script>  : Path for the bundled output script
#   [verbose]        : Optional, 'true' or 'false' (default: false)
#   [VAR=value ...]  : Optional variable assignments to expand in source paths
#
# Example:
#   bundle_script uninstall.sh /usr/local/bin/uninstall.sh true "SCRIPT_DIR=/tmp/scripts" "LIB_DIR=/tmp/lib"
#
# Notes:
#   - The function preserves the structure of inlined files, marking the
#     beginning and end of each inlined section with comments:
#         # >>> begin inlined: filename
#         # <<< end inlined: filename
#   - This makes the output script portable and independent of external
#     shared scripts.
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
#finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
#enter() { echo "[ENTERING] $(basename "$0")"; }
#enter
#set -x #debug switch
set -e

bundle_script() {
    local input="$1"
    local output="$2"
    local verbose="${3:-false}"   # optional verbose flag
    shift 3
    local varlist=("$@")          # remaining arguments are variable assignments

    local -A seen=()

    # Export variables so they can be expanded inside eval
    for var in "${varlist[@]}"; do
        export "$var"
        $verbose && echo "# Exported: $var" >&2
    done

    _bundle_inner() {
        local file="$1"
        local prefix="$2"

        # Prevent infinite recursion
        if [[ -n "${seen[$file]}" ]]; then
            $verbose && echo "${prefix}# !!! Skipping already inlined: $file" >&2
            return
        fi
        seen["$file"]=1

        $verbose && echo "${prefix}# Inlining file: $file" >&2

        while IFS= read -r line || [[ -n "$line" ]]; do
            # Match source lines with either $VAR or ${VAR} inside quotes
            if [[ "$line" =~ ^([[:space:]]*)source[[:space:]]+\"([^\"]+)\" ]]; then
                local indent="${BASH_REMATCH[1]}"
                local src="${BASH_REMATCH[2]}"

                # Expand all shell variables ($VAR or ${VAR})
                local resolved
                resolved=$(eval echo "\"$src\"")

                # Resolve relative to current file if necessary
                local dir="$(dirname "$file")"
                if [[ ! -f "$resolved" && -f "$dir/$resolved" ]]; then
                    resolved="$dir/$resolved"
                fi

                if [[ -f "$resolved" ]]; then
                    $verbose && echo "${prefix}# >>> inlining $resolved" >&2
                    echo "${indent}# >>> begin inlined: $src"
                    _bundle_inner "$resolved" "$indent"
                    echo "${indent}# <<< end inlined: $src"
                    # original `source` line is removed
                else
                    echo "ERROR: source file not found: $resolved" >&2
                    exit 1
                fi
            else
                # Print normal lines unchanged
                echo "$line"
            fi
        done < "$file"
    }

    {
        _bundle_inner "$input" ""
    } > "$output"

    chmod +x "$output"

    $verbose && echo "Bundling complete: $output" >&2
}

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <input_script> <output_script> [verbose] [VAR1=value1 VAR2=value2 ...]"
    echo
    echo "  <input_script>   : path to the script to bundle"
    echo "  <output_script>  : path for the bundled output script"
    echo "  [verbose]        : optional, 'true' or 'false' (default: false)"
    echo "  [VAR=value ...]  : optional variable assignments to expand in source paths"
    echo
    exit 1
fi

bundle_script "$@"

