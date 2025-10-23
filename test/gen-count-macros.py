#!/usr/bin/env python3
################################################################################
# gen-count-macros.py
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#
################################################################################

def gen_arg_count_macros(n):
    # Generate parameter list for PP_ARG_N
    params = ",".join(f"_{i}" for i in range(1, n + 1))
    print(f"#define PP_ARG_N({params}, N, ...) N\n")

    # Generate reverse sequence for PP_RSEQ_N
    numbers = ",".join(str(i) for i in range(n, -1, -1))
    print(f"#define PP_RSEQ_N() {numbers}")

gen_arg_count_macros(256)

