#!/usr/bin/env python3
################################################################################
# gen-csum-macros.py
#
# Purpose:
#   Generate C preprocessor macros for computing weighted cumulative sums
#   at compile time. This is useful for generating UBX message checksums
#   during compile-time assembly. The script produces macros of the form:
#     - PP_CSUM_1, PP_CSUM_2, ..., PP_CSUM_N
#   Each macro computes the sum: (N*a1 + (N-1)*a2 + ... + 1*aN)
#
# Usage:
#   Run this script to output macro definitions directly to stdout, which
#   can then be redirected into a header file (e.g., pp_csum.h).
#
# Example:
#   $ ./gen-csum-macros.py >pp_csum.h
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################

def gen_csum_macros(n):
    for i in range(1, n + 1):
        args = ",".join(f"a{j}" for j in range(1, i + 1))
        terms = "+".join(f"{i - j + 1}*a{j}" for j in range(1, i + 1))
        print(f"#define PP_CSUM_{i}({args}) ({terms})")

gen_csum_macros(256)

