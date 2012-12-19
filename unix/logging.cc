/* Logging support.
 *
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
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

#include <service/logging.h>

#include <stdlib.h>
#include <stdio.h>

void Logging::panic(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  ::vfprintf(stderr, format, ap);
  ::fprintf(stderr, "\n");
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
  ::vprintf(format, ap);
}

// EOF
