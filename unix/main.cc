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

/**
 * Parts of this file are derived from the original Vancouver
 * implementation in NUL, which is:
 *
 * Copyright (C) 2007-2010, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 */

#include <nul/motherboard.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

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
  // 1 vCPU
  "vcpu", "halifax", "vbios", "lapic",
  NULL,
  };

// Globals

static TimeoutList<32, void> timeouts;
static timevalue             last_to = ~0ULL;
static timer_t               timer_id;


static Clock                 mb_clock(1000000);   // XXX Use correct frequency
static Motherboard           mb(&mb_clock, NULL);

static void usage()
{
  fprintf(stderr, "Usage: seoul [-m RAM]\n");
  exit(EXIT_FAILURE);
}

static bool receive(Device *, MessageHostOp &msg)
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
    case MessageHostOp::OP_ALLOC_FROM_GUEST:
      assert((msg.value & 0xFFF) == 0);
      if (msg.value <= ram_size) {
        ram_size -= msg.value;
        msg.phys  = ram_size;
        Logging::printf("Allocating from guest %08zx+%lx\n", ram_size, msg.value);
      } else res = false;
      break;
    default:
      Logging::panic("%s - unimplemented operation %#x\n", __PRETTY_FUNCTION__, msg.type);
    }
    return res;
}

// Update or program pending timeout.
static void timeout_request()
{
  if (timeouts.timeout() != ~0ULL) {
    timevalue next_to = timeouts.timeout();
    if (next_to != last_to) {
      last_to = next_to;

      unsigned long long delta = mb_clock.delta(next_to, 1000000000UL);

      printf("Programming timer for %lluns.\n", delta);

      struct itimerspec t = {
        .it_interval = {0, 0},
        .it_value = {long(delta / 1000000000L), (long)(delta % 1000000000L)}
      };
      int res = timer_settime(timer_id, 0, &t, NULL);
      assert(!res);
    }
  }
}

static bool receive(Device *, MessageTimer &msg)
{
  switch (msg.type)
    {
    case MessageTimer::TIMER_NEW:
      msg.nr = timeouts.alloc();
      return true;
    case MessageTimer::TIMER_REQUEST_TIMEOUT:
      timeouts.request(msg.nr, msg.abstime);
      timeout_request();
      break;
    default:
      return false;
    }
  return true;
}

static bool receive(Device *, MessageTime &msg)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  msg.timestamp = mb_clock.clock(MessageTime::FREQUENCY);

  assert(MessageTime::FREQUENCY == 1000000U);
  msg.wallclocktime = (uint64)tv.tv_sec * 1000000 + tv.tv_usec;
  return true;
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

  // Allocating RAM.

  ram = reinterpret_cast<char *>(mmap(nullptr, ram_size, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANON, -1, 0));
  if (ram == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }

  // Creating timer. I hate C++: No useful initializers...
  struct sigevent ev;
  ev.sigev_notify = SIGEV_SIGNAL;
  ev.sigev_signo  = SIGALRM;

  if (0 != timer_create(CLOCK_MONOTONIC, &ev, &timer_id)) {
    perror("timer_create");
    return EXIT_FAILURE;
  }


  mb.bus_hostop.add(nullptr, receive);
  mb.bus_timer .add(nullptr, receive);
  mb.bus_time  .add(nullptr, receive);

  for (const char **dev = pc_ps2; *dev != NULL; dev++) {
    mb.handle_arg(*dev);
  }

  printf("Devices started successfully.\n");

  // TODO: Emulate!

  printf("Terminating.\n");
  return EXIT_SUCCESS;
}

// EOF
