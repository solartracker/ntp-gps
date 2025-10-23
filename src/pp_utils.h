#ifndef PP_UTILS_H_
#define PP_UTILS_H_
/*******************************************************************************
 pp_utils.h - Preprocessor metaprogramming utilities for argument mapping and transforms

 This header defines the core variadic preprocessor utilities:
   - Token concatenation (PP_JOIN)
   - Argument counting (PP_NARG)
   - Argument transformation (PP_SUM, PP_CSUM)

 It includes:
   - pp_arg.h      ->  argument counting helpers
   - pp_sum.h      ->  summation macros
   - pp_csum.h     ->  cumulative summation macros

 Copyright (C) 2025 Richard Elwell

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

*******************************************************************************/

// --- Helper macros ---
#define PP_JOIN_INNER(a,b) a##b
#define PP_JOIN(a,b) PP_JOIN_INNER(a,b)

// --- Count arguments ---
#define PP_NARG(...) PP_NARG_(__VA_ARGS__, PP_RSEQ_N())
#define PP_NARG_(...) PP_ARG_N(__VA_ARGS__)
#include "pp_arg.h"

// --- Transform macros ---
#define PP_SUM(...) PP_JOIN(PP_SUM_, PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define PP_CSUM(...) PP_JOIN(PP_CSUM_, PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#include "pp_sum.h"
#include "pp_csum.h"

#endif // PP_UTILS_H_

