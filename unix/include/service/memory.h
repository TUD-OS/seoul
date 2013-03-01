/** @file
 * Aligned memory allocation
 *
 * Copyright (C) 2013, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Seoul.
 *
 * Seoul is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Seoul is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <nul/types.h>
#include <stdlib.h>

struct Aligned {
  size_t alignment;

  void *alloc(size_t size) const
  {
    void *ret;
    return (0 == posix_memalign(&ret, alignment, size)) ? ret : nullptr;
  }

  Aligned(size_t alignment) : alignment(alignment) {}
};

void *operator new   (size_t size, Aligned const alignment) {
  return alignment.alloc(size); }

void *operator new[] (size_t size, Aligned const alignment) {
  return alignment.alloc(size); }

/* EOF */
