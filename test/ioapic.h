/**
 * I/O APIC Test header file
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
#include <signal.h>

int runIOAPicTest();

enum { IRQ_COUNT = 10000000, IRQ1 = 20, IRQ2 = 21 };

enum LogItem {
  LOG_SEND = 0x1,
  LOG_INTR,
  LOG_INTA_RX,
  LOG_INTA_TX,
  LOG_EOI,
  LOG_NOTIFY,
  LOG_DEASS
};

#define IOAPIC_ADDR 0xfec00000
#define IOAPIC_IDX 0x00
#define IOAPIC_DATA 0x10
#define IOAPIC_EOI  0x40
#define LAPIC_ADDR 0xfee00000

class LogBuffer {
private:
  unsigned logbuffer[20*IRQ_COUNT];
  unsigned logindex=0;

public:

  void log(LogItem type, unsigned value=0) {
    unsigned logindex_tmp = __sync_fetch_and_add(&logindex, 1);
    logbuffer[logindex_tmp] = (value << 16) | type;
  }

  void dump() {
    Logging::printf("\nLog output follows:\n---------------------------------------\n\n");
    for (unsigned i=0; i<logindex; i++) {
      const char * event;
      switch (logbuffer[i] & 0xffff) {
        case LOG_SEND: event = "\tIRQ\t\t"; break;
        case LOG_INTR: event = "\tINTR\t\t"; break;
        case LOG_INTA_TX: event = "\tINTA TX\t\t"; break;
        case LOG_INTA_RX: event = "\tINTA RX\t\t"; break;
        case LOG_EOI: event = "\tEOI\t\t"; break;
        case LOG_NOTIFY: event = "\tNOTIFY\t\t"; break;
        case LOG_DEASS: event = "\tDEASS\t\t"; break;
        default: event = "n/a"; break;
      }
      Logging::printf("%s %x\n", event, logbuffer[i] >> 16);
    }
    Logging::printf("\n---------------------------------------\n\nPrinted %u events.\n", logindex);
  }
};
