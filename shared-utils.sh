################################################################################
# shared-utils.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

################################################################################
# Backup a file if it differs from new content
backup_file() {
    local target_file="$1"
    local new_file="$2"
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

        # Only back up if file differs from new content
        if [ -n "$new_file" ] && ! cmp -s "$target_file" "$new_file"; then
            echo "Backing up existing $target_file → $backup_file"
            sudo cp -afv "$target_file" "$backup_file"
        #else
        #    echo "No changes in $target_file; skipping backup."
        fi
    fi
}

################################################################################
# Global search/replace on a tag within a file.  Does not change the file
# modification time.
set_repo_dir() {
    local target_file="$1"
    local repo_dir="$2"
    local repo_tag="$3"

    if [ -f "$target_file" ] && [ -n "$repo_dir" ] && [ -n "$repo_tag" ]; then
        # Save original timestamp
        local timestamp
        timestamp=$(stat -c %Y "$target_file")

        # Only replace if the tag exists
        if grep -q "$repo_tag" "$target_file"; then
            sed -i "s|$repo_tag|$repo_dir|g" "$target_file"
            # Restore timestamp
            touch -d @"$timestamp" "$target_file"
        fi
    fi
}

################################################################################
# bundle_script
#
# Usage:
#   bundle_script <input_script> <output_script> [verbose] [VAR=VAL...]
#
# Purpose:
#   Recursively inlines all `source` dependencies in a Bash script to produce
#   a single, self-contained output script. Removes original `source` lines.
#   Also preserves the newest timestamp across all inlined files.

# Behavior:
#   • Finds lines starting with `source "file"` (quoted paths supported).
#   • Expands variables inside source paths (e.g., $VAR, ${VAR}).
#   • Resolves relative paths based on the including script’s directory.
#   • Recurses into each dependency once (cycles are skipped).
#   • Preserves indentation when inlining.
#   • Marks each inlined block with:
#         # >>> begin inlined: filename
#         ...contents...
#         # <<< end inlined: filename
#   • Tracks the most recent mtime across all inputs and applies it
#     to the final bundled output file.
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
#   bundle_script uninstall.sh /usr/local/bin/uninstall.sh true "SCRIPT_DIR=/home/pi/ntp-gps"
#
# Verbose mode:
#   • If `verbose=true`, progress messages print to stderr:
#       - Which files are being exported and inlined
#       - Which files are skipped (already inlined)
#   • If `verbose=false`, bundling is silent except on errors.
#
# Workflow:
#   1. Developer calls: bundle_script input.sh output.sh [verbose] [VAR=VAL...]
#   2. Script is processed, with all sources inlined recursively.
#   3. Output is a single standalone script — ready to install or deploy.
#
# Notes:
#   • All `source` lines are removed.
#   • Dependencies are expanded in place.
#   • Script is now fully standalone and executable.
#   • The function preserves the structure of inlined files, marking the
#     beginning and end of each inlined section with comments:
#         # >>> begin inlined: filename
#         # <<< end inlined: filename
#   • This makes the output script portable and independent of external
#     shared scripts.
#
################################################################################
bundle_script() {
    local input="$1"          # Input script to bundle
    local output="$2"         # Output file for bundled script
    local verbose="${3:-false}" # Optional verbose flag
    shift 3
    local varlist=("$@")      # Any remaining arguments are variable assignments to export

    local -A seen=()          # Associative array to track already inlined files (avoid recursion)
    local newest=0            # Epoch time of the most recently modified file

    # Export variables so they are available for expansion inside 'source' lines
    for var in "${varlist[@]}"; do
        export "$var"
        if [[ "$verbose" == true ]]; then
            echo "# Exported: $var" >&2
        fi
    done

    # Internal helper: update 'newest' timestamp if this file is more recent
    _track_time() {
        local f="$1"
        local t
        t=$(stat -c %Y "$f")   # Get last modification time in seconds
        if (( t > newest )); then
            newest=$t
        fi
    }

    # Core recursion function: reads a file, inlines any 'source' dependencies
    _bundle_inner() {
        local file="$1"
        local prefix="$2"      # Indentation for nested files

        # Skip files we've already processed (prevents infinite recursion)
        if [[ -v seen["$file"] ]]; then
            if [[ "$verbose" == true ]]; then
                echo "${prefix}# !!! Skipping already inlined: $file" >&2
            fi
            return 0
        fi
        seen["$file"]=1

        _track_time "$file"  # Track newest modification time

        if [[ "$verbose" == true ]]; then
            echo "${prefix}# Inlining file: $file" >&2
        fi

        # Process each line of the file
        while IFS= read -r line || [[ -n "$line" ]]; do
            # Match 'source' lines inside quotes
            if [[ "$line" =~ ^([[:space:]]*)source[[:space:]]+\"([^\"]+)\" ]]; then
                local indent="${BASH_REMATCH[1]}"
                local src="${BASH_REMATCH[2]}"

                # Expand any shell variables in the source path ($VAR or ${VAR})
                local resolved
                resolved=$(eval echo "\"$src\"")

                # Resolve relative path based on current file if needed
                local dir
                dir="$(dirname "$file")"
                if [[ ! -f "$resolved" && -f "$dir/$resolved" ]]; then
                    resolved="$dir/$resolved"
                fi

                if [[ -f "$resolved" ]]; then
                    if [[ "$verbose" == true ]]; then
                        echo "${prefix}# >>> inlining $resolved" >&2
                    fi
                    # Mark inlined section in the output
                    echo "${indent}# >>> begin inlined: $src"
                    _bundle_inner "$resolved" "$indent"  # Recursive call
                    echo "${indent}# <<< end inlined: $src"
                else
                    echo "ERROR: source file not found: $resolved" >&2
                    exit 1
                fi
            else
                # Normal line: just print as-is
                echo "$line"
            fi
        done < "$file"
    }

    # Run the recursive bundler and write output to $output
    {
        _bundle_inner "$input" ""
    } > "$output"

    chmod +x "$output"  # Make the bundled script executable

    # Apply the newest timestamp across all inlined files to preserve modification time
    if (( newest > 0 )); then
        touch -d @"$newest" "$output"
    fi

    # Optional verbose message
    if [[ "$verbose" == true ]]; then
        echo "Bundling complete: $output (timestamp set to newest source)" >&2
    fi
}

