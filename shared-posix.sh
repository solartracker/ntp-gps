#!/bin/bash
#-------------------------------------------------------------------------------
# shared-posix.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#-------------------------------------------------------------------------------

# matchfile -- test whether a filename matches a shell pattern
#
# Arguments:
#   $1  file path (will be reduced to its basename)
#   $2  shell pattern (glob) to match against, e.g. "ntpkey_*"
#
# Returns:
#   0 (success) if the basename of the file matches the pattern
#   1 (failure) otherwise
#
# Notes:
#   - Uses POSIX shell case/globbing (no Bash-specific [[ ]] required).
#   - Only compares the basename, not the full path.
#
# Example:
#     if matchfile "$target_file" ntpkey_*; then
#         echo "Matched"
#     else
#         echo "No match"
#     fi
matchfile() {
    file="$1"
    pattern="$2"
    case "$(basename "$file")" in
        $pattern) return 0 ;;  # match → success
        *)        return 1 ;;  # no match → failure
    esac
}

# toupper -- convert a string to uppercase (using tr)
#
# Example usage:
#     result=$(toupper "raspberrypi")
#     echo "$result"   # Outputs: RASPBERRYPI
#
# Arguments:
#   $1  input string
#
# Output:
#   Prints the string converted to uppercase
#
# Notes:
#   - Uses the standard utility `tr`.
#   - Faster and simpler than `toupper2()`.
#   - Requires `tr` (POSIX systems provide it by default).
toupper() {
    printf "%s" "$1" | tr a-z A-Z
    return 0
}

# toupper2 -- convert a string to uppercase (manual case conversion)
#
# Example usage:
#     result=$(toupper2 "raspberrypi")
#     echo "$result"   # Outputs: RASPBERRYPI
#
# Arguments:
#   $1  input string
#
# Output:
#   Prints the string converted to uppercase
#
# Notes:
#   - Uses a character-by-character loop with explicit case mapping.
#   - Portable but slower than using `tr`.
#   - Useful on minimal systems without `tr`.
toupper2() {
    input="$1"
    result=""
    i=1
    while [ $i -le ${#input} ]; do
        c=$(printf "%s" "${input:$((i-1)):1}")
        case "$c" in
            a) c=A ;;
            b) c=B ;;
            c) c=C ;;
            d) c=D ;;
            e) c=E ;;
            f) c=F ;;
            g) c=G ;;
            h) c=H ;;
            i) c=I ;;
            j) c=J ;;
            k) c=K ;;
            l) c=L ;;
            m) c=M ;;
            n) c=N ;;
            o) c=O ;;
            p) c=P ;;
            q) c=Q ;;
            r) c=R ;;
            s) c=S ;;
            t) c=T ;;
            u) c=U ;;
            v) c=V ;;
            w) c=W ;;
            x) c=X ;;
            y) c=Y ;;
            z) c=Z ;;
        esac
        result="$result$c"
        i=$((i+1))
    done
    printf "%s" "$result"
    return 0
}

