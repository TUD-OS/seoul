/** -*- Mode: C++ -*-
 * UNIX Seoul frontend
 *
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
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

#pragma once

#include <pthread.h>

// Serialize access for devices. Currently also used to serialize
// everything else.
extern pthread_mutex_t irq_mtx;

static unsigned long long int rdtsc(void)
{
  unsigned long long tsc;
  asm volatile ("rdtsc" : "=A" (tsc));
  return tsc;
}

static unsigned get_tsc_frequency()
{
  struct timezone tz;
  memset(&tz, 0, sizeof(tz));

  struct timeval start, stop;
  unsigned long cycles[2], ms, hz;

  cycles[0] = rdtsc();
  gettimeofday(&start, &tz);

  usleep(250000);

  cycles[1] = rdtsc();
  gettimeofday(&stop, &tz);

  ms = ((stop.tv_sec - start.tv_sec)*1000000) + (stop.tv_usec - start.tv_usec);

  hz = (cycles[1]-cycles[0]) / ms * 1000000;

  return hz;
}

// EOF
