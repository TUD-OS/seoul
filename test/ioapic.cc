/**
 * I/O APIC Unit Test
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

#include "ioapic.h"

static LogBuffer logger;

static Clock mb_clock(1000000);
static Motherboard mb(&mb_clock, NULL);

void doIO(bool read, uintptr_t phys, unsigned *ptr) {
  MessageMem msg(read, phys, ptr);
  mb.bus_mem.send(msg);
}

void readIO(uintptr_t phys, unsigned *ptr) {
  doIO(true, phys, ptr);
}

void writeIO(uintptr_t phys, unsigned *ptr) {
  doIO(false, phys, ptr);
}

// worker threads
pthread_t receiver, trigger1, trigger2;
unsigned irq_received_1=0, irq_received_2=0;
bool irq1free=true, irq2free=true;
unsigned irq_pending=0;

// message handlers
static bool receive(Device *, MessageMem &msg) {
  if (msg.phys == LAPIC_ADDR) {
    // set bit for IRQ
    logger.log(LOG_INTR, *msg.ptr);
    __sync_fetch_and_or(&irq_pending, 1 << (*msg.ptr & 0xff));
  }
}
static bool receive(Device *, MessageIrqNotify &msg) {
  logger.log(LOG_NOTIFY, msg.baseirq << 16 | msg.mask);
  if (msg.baseirq == (IRQ1 & ~0x7) && msg.mask & (1 << (IRQ1 & 0x7))) {
    // IRQ1 can be re-raised
    __sync_bool_compare_and_swap(&irq1free, false, true);
  }
  if (msg.baseirq == (IRQ2 & ~0x7) && msg.mask & (1 << (IRQ2 & 0x7))) {
    // IRQ2 can be re-raised
    __sync_bool_compare_and_swap(&irq2free, false, true);
  }
}

static void * receiver_fn(void *) {
  unsigned waitcount = 0;
  while (true) {
    if (!__sync_fetch_and_or(&irq_pending, 0)) {
      if (waitcount++ > 1000000 || (irq_received_1 == IRQ_COUNT && irq_received_2 == IRQ_COUNT)) break;
      continue;
    }
    waitcount = 0;

    unsigned vec;
    if (irq_pending & (1 << IRQ2)) {
      vec = IRQ2;
      irq_received_2++;
    } else if (irq_pending & (1 << IRQ1)) {
      vec = IRQ1;
      irq_received_1++;
    }

    __sync_fetch_and_and(&irq_pending, ~(1 << vec));

    // EOI
    logger.log(LOG_EOI, vec);
    writeIO(IOAPIC_ADDR | IOAPIC_EOI, &vec);
  }
}

static void * trigger_1_fn(void *) {
  MessageIrqLines msg(MessageIrq::ASSERT_NOTIFY, IRQ1);
  unsigned sent = 0;
  while (sent++ < IRQ_COUNT) {
    while (!__sync_bool_compare_and_swap(&irq1free, true, false));

    logger.log(LOG_SEND, IRQ1);
    mb.bus_irqlines.send(msg);
  }
  return nullptr;
}

static void * trigger_2_fn(void *) {
  MessageIrqLines msg(MessageIrq::ASSERT_NOTIFY, IRQ2);
  unsigned sent = 0;
  while (sent++ < IRQ_COUNT) {
    while (!__sync_bool_compare_and_swap(&irq2free, true, false));

    logger.log(LOG_SEND, IRQ2);
    mb.bus_irqlines.send(msg);
  }
  return nullptr;
}

int runIOAPicTest() {
  // attach handlers
  mb.bus_irqnotify.add(nullptr, receive);
  mb.bus_mem.add(nullptr, receive);

  // create I/O APIC
  mb.handle_arg("ioapic");

  // init two IRQs
  unsigned index1 = 0x10+IRQ1*2;
  unsigned irq1 = 0x8000 | (IRQ1 & 0xff);
  unsigned index2 = 0x10+IRQ2*2;
  unsigned irq2 = 0x8000 | (IRQ2 & 0xff);

  writeIO(IOAPIC_ADDR | IOAPIC_IDX, &index1);
  writeIO(IOAPIC_ADDR | IOAPIC_DATA, &irq1);
  writeIO(IOAPIC_ADDR | IOAPIC_IDX, &index2);
  writeIO(IOAPIC_ADDR | IOAPIC_DATA, &irq2);

  // create threads for triggering and receiving interrupts
  cpu_set_t cpuset_receiver, cpuset_trigger1, cpuset_trigger2;
  pthread_t self = pthread_self();
  CPU_ZERO(&cpuset_receiver);
  CPU_ZERO(&cpuset_trigger1);
  CPU_ZERO(&cpuset_trigger2);
  CPU_SET(1, &cpuset_receiver);
  CPU_SET(2, &cpuset_trigger1);
  CPU_SET(3, &cpuset_trigger2);

  pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset_trigger1);

  timevalue tsc_start = Cpu::rdtsc();

  pthread_create(&receiver, NULL, receiver_fn, NULL);
  pthread_setaffinity_np(receiver, sizeof(cpu_set_t), &cpuset_receiver);

  pthread_create(&trigger1, NULL, trigger_1_fn, NULL);
  pthread_setaffinity_np(trigger1, sizeof(cpu_set_t), &cpuset_trigger1);

  pthread_create(&trigger2, NULL, trigger_2_fn, NULL);
  pthread_setaffinity_np(trigger2, sizeof(cpu_set_t), &cpuset_trigger2);

  pthread_join(receiver, nullptr);
  pthread_join(trigger1, nullptr);
  pthread_join(trigger2, nullptr);

  timevalue cycles = Cpu::rdtsc() - tsc_start;

  printf("Test completed. Received (%u, %u) interrupts (expected %u, %u).\nTest took %llu cycles.\n",
         irq_received_1, irq_received_2, IRQ_COUNT, IRQ_COUNT, cycles);

  //logger.dump();

  return 0;
}
