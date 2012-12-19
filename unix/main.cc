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
#include <unistd.h>
#include <sys/mman.h>

const char version_str[] =
#include "version.inc"
  ;


// Configuration

static char  *ram;
static size_t ram_size = 128 << 20; // 128 MB

static const char *pc_ps2[] = {
  "mem:0,0xa0000",
  "mem:0x100000",
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

static void usage()
{
  fprintf(stderr, "Usage: seoul [-m RAM]\n");
  exit(EXIT_FAILURE);
}

static bool receive(Device *d, MessageHostOp &msg)
{
    bool res = true;
    switch (msg.type) {
    case MessageHostOp::OP_GUEST_MEM:
      if (msg.value >= ram_size) {
        msg.value = 0;
      } else {
        msg.len = ram_size - msg.value;
        msg.ptr = ram      + msg.value;
      }
      break;
    default:
      Logging::panic("%s - unimplemented operation %#x\n", __PRETTY_FUNCTION__, msg.type);
    }
    return res;
}

int main(int argc, char **argv)
{
  printf("Seoul %s booting.\n"
         "Visit https://github.com/TUD-OS/seoul for information.\n\n",
         version_str);

  int ch;
  while ((ch = getopt(argc, argv, "m:")) != -1) {
    switch (ch) {
    case 'm':
      ram_size = atoi(optarg) << 20;
      break;
    case '?':
    default:
      usage();
      break;
    }
  }

  printf("Devices included:\n");
  PARAM_ITER(p) {
    printf("\t%s\n", (*p)->name);
  }
  printf("\nStarting devices:\n");

  Clock       clock(1000000);   // XXX Use correct frequency
  Motherboard mb(&clock, NULL);

  ram = reinterpret_cast<char *>(mmap(nullptr, ram_size, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANON, -1, 0));
  if (ram == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  mb.bus_hostop.add(nullptr, receive);

  for (const char **dev = pc_ps2; *dev != NULL; dev++) {
    mb.handle_arg(*dev);
  }

  printf("Devices started successfully.\n");

  // TODO: Emulate!

  printf("Terminating.\n");
  return EXIT_SUCCESS;
}

// EOF
