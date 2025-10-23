#!/usr/bin/env python3
################################################################################
# gen-csum-macros.py
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#
################################################################################

def gen_csum_macros(n):
    for i in range(1, n + 1):
        args = ",".join(f"a{j}" for j in range(1, i + 1))
        terms = "+".join(f"{i - j + 1}*a{j}" for j in range(1, i + 1))
        print(f"#define PP_CSUM_{i}({args}) ({terms})")

gen_csum_macros(256)

