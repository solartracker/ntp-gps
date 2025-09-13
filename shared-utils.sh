################################################################################
# shared.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

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

# Recursively inlines all 'source' dependencies in a Bash script to produce
# a single, self-contained output script. Removes original 'source' lines.
bundle_script() {
    local input="$1"
    local output="$2"
    local verbose="${3:-false}"   # optional verbose flag
    shift 3
    local varlist=("$@")          # remaining arguments are variable assignments

    local -A seen=()
    local newest=0                # epoch time of most recent file

    # Export variables so they can be expanded inside eval
    for var in "${varlist[@]}"; do
        export "$var"
        if [[ "$verbose" == true ]]; then
            echo "# Exported: $var" >&2
        fi
    done

    # Track newest time across files
    _track_time() {
        local f="$1"
        local t
        t=$(stat -c %Y "$f")
        if (( t > newest )); then
            newest=$t
        fi
    }

    _bundle_inner() {
        local file="$1"
        local prefix="$2"

        # Prevent infinite recursion
        # Already visited? Skip
        if [[ -v seen["$file"] ]]; then
            if [[ "$verbose" == true ]]; then
                echo "${prefix}# !!! Skipping already inlined: $file" >&2
            fi
            return 0
        fi
        seen["$file"]=1

        _track_time "$file"  # update newest seen

        if [[ "$verbose" == true ]]; then
            echo "${prefix}# Inlining file: $file" >&2
        fi

        while IFS= read -r line || [[ -n "$line" ]]; do
            # Match source lines with either $VAR or ${VAR} inside quotes
            if [[ "$line" =~ ^([[:space:]]*)source[[:space:]]+\"([^\"]+)\" ]]; then
                local indent="${BASH_REMATCH[1]}"
                local src="${BASH_REMATCH[2]}"

                # Expand all shell variables ($VAR or ${VAR})
                local resolved
                resolved=$(eval echo "\"$src\"")

                # Resolve relative to current file if necessary
                local dir
                dir="$(dirname "$file")"
                if [[ ! -f "$resolved" && -f "$dir/$resolved" ]]; then
                    resolved="$dir/$resolved"
                fi

                if [[ -f "$resolved" ]]; then
                    if [[ "$verbose" == true ]]; then
                        echo "${prefix}# >>> inlining $resolved" >&2
                    fi
                    echo "${indent}# >>> begin inlined: $src"
                    _bundle_inner "$resolved" "$indent"
                    echo "${indent}# <<< end inlined: $src"
                    # original `source` line removed
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

    # Apply newest timestamp to output
    if (( newest > 0 )); then
        touch -d @"$newest" "$output"
    fi

    if [[ "$verbose" == true ]]; then
        echo "Bundling complete: $output (timestamp set to newest source)" >&2
    fi
}

