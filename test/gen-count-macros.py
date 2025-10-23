#!/usr/bin/env python3
################################################################################
# gen-count-macros.py
#
# Purpose:
#   Generate C preprocessor macros for counting the number of arguments
#   passed to variadic macros. Specifically, this script generates:
#     - PP_ARG_N: extracts the Nth argument from a list of arguments
#     - PP_RSEQ_N: a reverse sequence of numbers for macro argument counting
#
# Usage:
#   Run this script to output macro definitions directly to stdout, which
#   can then be redirected into a header file (e.g., pp_arg.h) for use in
#   compile-time assembly of UBX message structures.
#
# Example:
#   $ ./gen-count-macros.py >pp_arg.h
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################

def gen_arg_count_macros(n):
    # Generate parameter list for PP_ARG_N
    params = ",".join(f"_{i}" for i in range(1, n + 1))
    print(f"#define PP_ARG_N({params}, N, ...) N\n")

    # Generate reverse sequence for PP_RSEQ_N
    numbers = ",".join(str(i) for i in range(n, -1, -1))
    print(f"#define PP_RSEQ_N() {numbers}")

gen_arg_count_macros(256)

