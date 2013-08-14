/** @file
 * Compiler-specific annotations
 *
 * Copyright (C) 2010-2011, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
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

#if !defined(__GNUC__)
#error Your platform is not supported.
#endif

#define VMM_REGPARM(x) __attribute__((regparm(x)))

/* We can use [[ noreturn ]] here, when we all move to C++ 11. */
#define VMM_NORETURN   __attribute__((noreturn))
#define VMM_UNUSED     __attribute__((unused))

#ifdef __cplusplus
# define VMM_BEGIN_EXTERN_C extern "C" {
# define VMM_END_EXTERN_C   }
# define VMM_EXTERN_C       extern "C"
#else
# define VMM_BEGIN_EXTERN_C
# define VMM_END_EXTERN_C
# define VMM_EXTERN_C
#endif

/* Sadly GCC specific */

#define VMM_MAX(a, b) ({ decltype (a) _a = (a); \
      decltype (b) _b = (b);		  \
      _a > _b ? _a : _b; })

#define VMM_MIN(a, b) ({ decltype (a) _a = (a); \
      decltype (b) _b = (b);		  \
      _a > _b ? _b : _a; })

/* EOF */
