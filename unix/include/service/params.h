/** @file
 * Parameter handling.
 *
 * Copyright (C) 2009, Bernhard Kauer <bk@vmmon.org>
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
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

#include <list>

class Motherboard;
typedef void (*ParameterFn)(Motherboard &, unsigned long *, const char *, unsigned);

class Parameter {

 public:

  const char *name;
  ParameterFn func;

  static std::list<Parameter *> &all_parameters();

  Parameter(const char *name, ParameterFn func) : name(name), func(func)
  {
     all_parameters().insert(all_parameters().end(), this);
  }
};

/**
 * Defines strings and functions for a parameter with the given
 * name. The variadic part is used to store a help text.
 *
 * PARAM_HANDLER(example, "example - this is just an example for parameter Passing",
 *       		  "Another help line...")
 * { Logging::printf("example parameter function called!\n"); }
 */
#define PARAM_HANDLER(NAME, ...)                                        \
  static void      __parameter_##NAME##_fn(Motherboard &mb, unsigned long *, const char *, unsigned); \
  static Parameter __parameter_##NAME (#NAME, __parameter_##NAME##_fn);  \
  static void      __parameter_##NAME##_fn(Motherboard &mb, unsigned long *argv, const char *args, unsigned args_len)


#define PARAM_ITER(p)                                               \
  for (auto p = Parameter::all_parameters().begin(); p != Parameter::all_parameters().end(); ++p)

// EOF
