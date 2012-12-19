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


static const char *pc_ps2[] = {
  "mem:0,0xa0000",
  "mem:0x100000",
  "ioio",
  "nullio:0x80",
  "pic:0x20,,0x4d0",
  "pic:0xa0,2,0x4d1",
  "pit:0x40,0",
  "scp:0x92,0x61",
  "kbc:0x60,1,12",
  "keyb:0,0x10000",
  "mouse:1,0x10001",
  "rtc:0x70,8",
  "serial:0x3f8,0x4,0x4711",
  "hostsink:0x4712,80",
  "vga:0x03c0",
  "vbios_disk", "vbios_keyboard", "vbios_mem", "vbios_time", "vbios_reset", "vbios_multiboot",
  "msi",
  "ioapic",
  "pcihostbridge:0,0x10,0xcf8,0xe0000000",
  "pmtimer:0x8000",
  "vcpus",
  NULL,
};

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

  for (const char **dev = pc_ps2; *dev != NULL; dev++) {
    printf("parsing: %s\n", *dev);
    mb.handle_arg(*dev);
  }

  printf("Devices started successfully.\n");

  // TODO: Emulate!

  printf("Terminating.\n");
  return EXIT_SUCCESS;
}

// EOF
