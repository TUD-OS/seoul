/**
 * PIC Test header file
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

#include <nul/motherboard.h>
#include <nul/vcpu.h>

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

int runPicTest();

enum { IRQ_COUNT = 500000 };

enum LogItem {
  LOG_INIT = 0x0,
  LOG_SEND,
  LOG_INTR,
  LOG_INTA_RX,
  LOG_INTA_TX,
  LOG_EOI,
  LOG_NOTIFY,
  LOG_DEASS,
  LOG_IGNORE,
  LOG_SKIP
};

class LogBuffer {
private:
  unsigned long logbuffer[40*IRQ_COUNT];
  unsigned logindex=0;

public:

  void log(LogItem type, unsigned value=0) {
    unsigned logindex_tmp = __sync_fetch_and_add(&logindex, 2);
    if (logindex_tmp >= sizeof(logbuffer)) return;
    logbuffer[logindex_tmp] = Cpu::rdtsc();
    logbuffer[logindex_tmp+1] = (pthread_self() << 32) | (value << 16) | type;
  }

  void dump() {
    Logging::printf("\nLog output follows:\n---------------------------------------\n\n");
    for (unsigned i=0; i<logindex; i+=2) {
      const char * event;
      switch (logbuffer[i+1] & 0xffff) {
        case LOG_INIT: event = "\tINIT\t\t"; break;
        case LOG_SEND: event = "\tIRQ\t\t"; break;
        case LOG_INTR: event = "\tINTR\t\t"; break;
        case LOG_INTA_TX: event = "\tINTA TX\t\t"; break;
        case LOG_INTA_RX: event = "\tINTA RX\t\t"; break;
        case LOG_EOI: event = "\tEOI\t\t"; break;
        case LOG_NOTIFY: event = "\tNOTIFY\t\t"; break;
        case LOG_DEASS: event = "\tDEASS\t\t"; break;
        case LOG_IGNORE: event = "\tIGNORE\t\t"; break;
        case LOG_SKIP: event = "\tSKIP\t\t"; break;
        default: event = "n/a"; break;
      }
      Logging::printf("%lx\tT%lx\t%s %lx\n",
                      logbuffer[i],
                      (logbuffer[i+1] >>32),
                      event,
                      (logbuffer[i+1] >> 16)& 0xffff
      );
    }
    Logging::printf("\n---------------------------------------\n\nPrinted %u events.\n", logindex/2);
  }
};
