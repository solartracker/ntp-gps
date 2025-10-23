#!/usr/bin/env python3
################################################################################
# gen-sum-macros.py
#
# Purpose:
#   Generate C preprocessor macros for computing the sum of a list of
#   numeric arguments at compile time. This script produces macros of the
#   form:
#     - PP_SUM_1, PP_SUM_2, ..., PP_SUM_N
#   Each macro computes the sum of its arguments, enabling compile-time
#   evaluation in UBX message assembly.
#
# Usage:
#   Run this script to output macro definitions directly to stdout, which
#   can then be redirected into a header file (e.g., pp_sum.h).
#
# Example:
#   $ ./gen-sum-macros.py >pp_sum.h
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################

def gen_sum_macros(n):
    for i in range(1, n + 1):
        args = ",".join(f"a{j}" for j in range(1, i + 1))
        terms = "+".join(f"a{j}" for j in range(1, i + 1))
        print(f"#define PP_SUM_{i}({args}) ({terms})")

gen_sum_macros(256)

