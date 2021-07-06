/**
 * Logging stubs
 *
 * Copyright (C) 2013 Markus Partheymueller, Intel Corporation.
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

#include <service/logging.h>
#include <nul/motherboard.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

void Logging::panic(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);

  Logging::vprintf(format, ap);
  Logging::printf("\n");

  va_end(ap);
  abort();
}

void Logging::printf(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  Logging::vprintf(format, ap);
  va_end(ap);
}


void Logging::vprintf(const char *format, va_list &ap)
{
    ::vfprintf(stderr, format, ap);
}
