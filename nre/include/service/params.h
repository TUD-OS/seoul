/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Copyright (C) 2009, Bernhard Kauer <bk@vmmon.org>
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

#include <collection/SList.h>
#include <arch/Defines.h>

class Motherboard;
typedef void (*ParameterFn)(Motherboard &, unsigned long *, const char *, unsigned);

struct Parameter {
    ParameterFn func;
    const char *name;
};

/* Defined in linker script: */
extern Parameter __param_table_start, __param_table_end;

/**
 * Defines strings and functions for a parameter with the given
 * name. The variadic part is used to store a help text.
 *
 * PARAM_HANDLER(example, "example - this is just an example for parameter Passing",
 *       		  "Another help line...")
 * { Logging::printf("example parameter function called!\n"); }
 */
#define PARAM_HANDLER(NAME, ...) \
    char __parameter_##NAME##_name[] asm (                              \
        "__parameter_" #NAME "_name"                                    \
    ) = #NAME;                                                          \
    asm volatile (                                                      \
        ".section .param;"                                              \
        ASM_WORD_TYPE" __parameter_" #NAME "_function, "                \
        "__parameter_" #NAME "_name; "                                  \
        ".previous;"                                                    \
    );                                                                  \
    EXTERN_C void __parameter_##NAME##_function(UNUSED Motherboard &mb, \
        UNUSED unsigned long *argv, UNUSED const char *args,            \
        UNUSED size_t args_len)

#define PARAM_ITER(p)                                                   \
    for(Parameter *p = &__param_table_start; p < &__param_table_end; ++p)

#define PARAM_DEREF(p)                                                  \
    (*p)

// EOF
