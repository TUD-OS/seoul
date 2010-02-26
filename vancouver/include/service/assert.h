/**
 * Standard include file.
 *
 * Copyright (C) 2007-2008, Bernhard Kauer <bk@vmmon.org>
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

#include "stdlib.h"

#ifdef NDEBUG
#define assert(X) do { X; } while (0)
#else
#define do_string2(x) do_string(x)
#define do_string(x) #x
#define assert(X) do { if (!(X)) __exit((long)("assertion '" #X  "' failed in "  __FILE__  ":" do_string2(__LINE__) )); } while (0)
#endif