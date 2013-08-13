/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2013, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <nul/types.h>

EXTERN_C void* dlmemalign(size_t, size_t);

struct Aligned {
    void *alloc(size_t size) const {
        return dlmemalign(size, alignment);
    }

    explicit Aligned(size_t alignment) : alignment(alignment) {
    }

    size_t alignment;
};

void *operator new(size_t size, Aligned const alignment) {
    return alignment.alloc(size);
}
void *operator new[](size_t size, Aligned const alignment) {
    return alignment.alloc(size);
}
