/**
 * UNIX Seoul frontend
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

#include <nul/motherboard.h>

#include <stdio.h>
#include <stdlib.h>

const char version_str[] =
#include "version.inc"
  ;

int main(int argc, char **argv)
{
  Clock       clock(1000000);   // XXX Use correct frequency
  Motherboard mb(&clock, NULL);

  printf("Seoul %s booting.\n"
         "Visit https://github.com/TUD-OS/seoul for information.\n\n",
         version_str);

  printf("Modules included:\n");
  PARAM_ITER(p) {
    printf("\t%s\n", (*p)->name);
  }
  printf("\n");

  for (int i = 1; i < argc; i++)
    mb.handle_arg(argv[i]);


  printf("Terminating successfully.\n");
  return EXIT_SUCCESS;
}

// EOF
