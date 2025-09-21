#!/bin/bash
#-------------------------------------------------------------------------------
# shared-posix.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#-------------------------------------------------------------------------------

toupper() {
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

toupper2() { 
    printf "%s" "$1" | tr a-z A-Z
    return 0
}

matchfile() {
    file="$1"
    pattern="$2"
    case "$(basename "$file")" in
        $pattern) return 0 ;;  # match → success
        *) return 1 ;;         # no match → failure
    esac
}

