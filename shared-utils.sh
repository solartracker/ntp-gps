#!/bin/bash
#-------------------------------------------------------------------------------
# shared-utils.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#-------------------------------------------------------------------------------

# Backup a file if it differs from new content
backup_file() {
    local verbose=false
    local target_file=""
    local new_file=""
    local timestamp=""
    local backup_file=""

    # --- Parse options ---
    while [ $# -gt 0 ]; do
        case "$1" in
            -v|--verbose)
                verbose=true
                shift
                ;;
            -*)
                echo "backup_file: unknown option: $1" >&2
                return 1
                ;;
            *)
                if [ -z "$target_file" ]; then
                    target_file="$1"
                elif [ -z "$new_file" ]; then
                    new_file="$1"
                else
                    echo "backup_file: too many arguments" >&2
                    return 1
                fi
                shift
                ;;
        esac
    done

    if [ -z "$target_file" ] || [ -z "$new_file" ]; then
        echo "Usage: backup_file [-v] <target_file> <new_file>" >&2
        return 1
    fi

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
            $verbose && echo "Backing up existing $target_file → $backup_file"
            sudo cp -af "$target_file" "$backup_file"
        else
            $verbose && echo "No changes in $target_file; skipping backup."
        fi
    fi

    return 0
}

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

    return 0
}

################################################################################
# bundle_script
#
# Usage:
#   bundle_script [-v|--verbose] [-DVAR=VAL ...] <input_script> <output_script>
#
# Arguments:
#   -v, --verbose     Optional. Enable verbose output (default: disabled).
#   -DVAR=VAL         Optional. Export variables for expanding in source paths.
#   <input_script>    Required. Path to the script to bundle.
#   <output_script>   Required. Path for the bundled output script.
#
# Purpose:
#   Recursively inlines all `source` dependencies in a Bash script to produce
#   a single, self-contained output script. The final script is executable and
#   preserves the newest modification timestamp across all inlined files.
#
# Behavior:
#   • Detects lines starting with `source "file"` (supports quoted paths).
#   • Expands shell variables inside source paths ($VAR or ${VAR}).
#   • Resolves relative paths based on the including script’s directory.
#   • Recursively inlines each dependency once. Already-inlined files are skipped
#     silently unless verbose mode is enabled.
#   • Preserves indentation of inlined code.
#   • Marks the start and end of each inlined block with comments:
#         # >>> begin inlined: filename
#         ...contents...
#         # <<< end inlined: filename
#   • Tracks the newest modification time among all input and inlined files
#     and applies it to the final bundled output script.
#
# Verbose mode:
#   • If verbose is enabled, progress messages are printed to stderr:
#       - Files being inlined
#       - Files skipped due to prior inclusion
#       - Exported variable assignments
#   • If verbose is not enabled, bundling is silent except on errors.
#
# Failure modes:
#   • If a source file cannot be found, the script exits immediately with an error.
#   • Nonexistent variables in paths are expanded as empty strings.
#
# Example:
#   bundle_script -v -DSCRIPT_DIR=/home/pi/ntp-gps uninstall.sh \
#       /usr/local/bin/uninstall.sh
#
# Notes:
#   • All `source` lines are removed and replaced with inlined content.
#   • Script becomes fully standalone and portable.
#   • Preserves structure and readability by marking inlined sections.
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################
bundle_script() {
    local input=""
    local output=""
    local verbose=false
    local -a varlist=()   # For -DVAR=VAL style variable assignments

    # Parse options
    while [ $# -gt 0 ]; do
        case "$1" in
            -v|--verbose)
                verbose=true
                shift
                ;;
            -D*)
                varlist+=("${1#-D}")  # Strip the -D prefix
                shift
                ;;
            --)  # End of options
                shift
                break
                ;;
            -*)  # Unknown option
                echo "Unknown option: $1" >&2
                return 1
                ;;
            *)  # Positional arguments
                if [ -z "$input" ]; then
                    input="$1"
                elif [ -z "$output" ]; then
                    output="$1"
                else
                    # Treat remaining as variable assignments (for backward compatibility)
                    varlist+=("$1")
                fi
                shift
                ;;
        esac
    done

    if [ -z "$input" ] || [ -z "$output" ]; then
        echo "Usage: bundle_script [-v|--verbose] [-DVAR=VAL ...] <input_script> <output_script>" >&2
        return 1
    fi

    local -A seen=()
    local newest=0

    # Export variables for source expansion
    for var in "${varlist[@]}"; do
        export "$var"
        $verbose && echo "# Exported: $var" >&2
    done

    _track_time() {
        local f="$1"
        local t
        t=$(stat -c %Y "$f")
        (( t > newest )) && newest=$t
        return 0
    }

    _bundle_inner() {
        local file="$1"
        local prefix="$2"

        [[ -v seen["$file"] ]] && { $verbose && echo "${prefix}# Skipping already inlined: $file" >&2; return 0; }
        seen["$file"]=1

        _track_time "$file"
        $verbose && echo "${prefix}# Inlining: $file" >&2

        while IFS= read -r line || [ -n "$line" ]; do
            if [[ "$line" =~ ^([[:space:]]*)source[[:space:]]+\"([^\"]+)\" ]]; then
                local indent="${BASH_REMATCH[1]}"
                local src="${BASH_REMATCH[2]}"
                local resolved
                resolved=$(eval echo "\"$src\"")
                local dir
                dir="$(dirname "$file")"
                [ ! -f "$resolved" ] && [ -f "$dir/$resolved" ] && resolved="$dir/$resolved"

                if [ -f "$resolved" ]; then
                    $verbose && echo "${prefix}# >>> inlining $resolved" >&2
                    echo "${indent}# >>> begin inlined: $src"
                    _bundle_inner "$resolved" "$indent"
                    echo "${indent}# <<< end inlined: $src"
                else
                    echo "ERROR: source file not found: $resolved" >&2
                    exit 1
                fi
            else
                echo "$line"
            fi
        done < "$file"
        return 0
    }

    { _bundle_inner "$input" ""; } > "$output"
    chmod +x "$output"
    (( newest > 0 )) && touch -d @"$newest" "$output"
    $verbose && echo "Bundling complete: $output (timestamp set to newest source)" >&2

    return 0
}

