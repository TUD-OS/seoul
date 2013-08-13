/** @file
 * Fixed-width integer types.
 *
 * Copyright (C) 2010, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Vancouver.
 *
 * Vancouver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vancouver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

/* Include stddef to get proper definition of NULL. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <nul/compiler.h>

VMM_BEGIN_EXTERN_C
#ifdef __MMX__
#include <mmintrin.h>
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
VMM_END_EXTERN_C

/* Constant-width integer types. */
typedef uint64_t  uint64;
typedef uint32_t  uint32;
typedef uint16_t  uint16;
typedef uint8_t   uint8;
typedef uintptr_t mword;

typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t  int8;

/* NUL specific types */

typedef unsigned log_cpu_no;
typedef unsigned phy_cpu_no;
typedef unsigned cap_sel;       /* capability selector */

/* EOF */
