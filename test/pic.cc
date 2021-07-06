/**
 * PIC Unit Test
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

#include "pic.h"

static LogBuffer logger;

static Clock mb_clock(1000000);
static Motherboard mb(&mb_clock, NULL);

timevalue tsc_start;

unsigned char IRQS[2] = { 3, 12 };

void outb(unsigned short port, unsigned short value) {
  MessageIOOut msg(MessageIOOut::TYPE_OUTB, port, value);
  mb.bus_ioout.send(msg);
}
unsigned short inb(unsigned short port) {
  MessageIOIn msg(MessageIOIn::TYPE_INB, port);
  mb.bus_ioin.send(msg);
  return msg.value;
}
unsigned char _get_irr() {
  outb(0x20, 0x0a);
  return inb(0x20);
}

// worker threads
pthread_t receiver, trigger1, trigger2;
unsigned irq_received_1=0, irq_received_2=0;
bool irq_1_free=true, irq_2_free=true;
unsigned long intr = 0;

// message handlers
static bool receive(Device *, MessageLegacy &msg) {
  if (msg.type == MessageLegacy::INTR) {
    __sync_fetch_and_add(&intr, 4);
    if (!(__sync_fetch_and_or(&intr, 1) & 0x1)) {
      logger.log(LOG_INTR);
    }
  } else if (msg.type == MessageLegacy::DEASS_INTR) {
    logger.log(LOG_DEASS, msg.value);
  }
}

static bool receive(Device *, MessageIrqNotify &msg) {
  logger.log(LOG_NOTIFY, msg.baseirq << 8 | msg.mask);
  if (msg.baseirq == (IRQS[0] & 0x8) && msg.mask & (1 << (IRQS[0] & 0x7))) {
    // First IRQ can be re-raised
    __sync_bool_compare_and_swap(&irq_1_free, false, true);
  }
  else if (msg.baseirq == (IRQS[1] & 0x8) && msg.mask & (1 << (IRQS[1] & 0x7))) {
    // Second IRQ can be re-raised
    __sync_bool_compare_and_swap(&irq_2_free, false, true);
  }
  else Logging::panic("w00t %x:%x\n", msg.baseirq, msg.mask);
}

static void * receiver_fn(void *) {
  while (Cpu::rdtsc() < tsc_start+10000000);
  logger.log(LOG_INIT);
  sleep(1);
  unsigned long waitcount = 0;
  unsigned long current;
  while (true) {
    if (!(__sync_fetch_and_or(&intr, 1) & 0x1)) {
      if (waitcount++ > 1000000000 || (irq_received_1 == IRQ_COUNT && irq_received_2 == IRQ_COUNT)) break;
      continue;
    }

    waitcount = 0;

    // Double-check due to race
    current = intr;
    MessageLegacy check(MessageLegacy::CHECK_INTR);
    mb.bus_legacy.send(check);
    if (!(check.value & 0xff00)) {
      logger.log(LOG_SKIP, check.value);
      __sync_bool_compare_and_swap(&intr, current, (current + 4) & ~1ULL);
      continue;
    }

    logger.log(LOG_INTA_TX, check.value);
    MessageLegacy inta(MessageLegacy::INTA, 0);
    waitcount = 0;
    mb.bus_legacy.send(inta);
    logger.log(LOG_INTA_RX, inta.value);

    if (inta.value == IRQS[0]) irq_received_1++;
    if (inta.value == IRQS[1]) irq_received_2++;

    if (inta.value >= 8) outb(0xa0, 0x20);
    outb(0x20, 0x20);
    logger.log(LOG_EOI, (intr << 8) | inta.value);
  }
}


template <unsigned char IRQ>
static void * trigger_fn(void *) {
  while (Cpu::rdtsc() < tsc_start+10000000);
  logger.log(LOG_INIT);
  MessageIrqLines msg(MessageIrq::ASSERT_NOTIFY, IRQS[IRQ-1]);
  unsigned sent = 0, ignored = 0;
  bool * waiter = (IRQ == 1) ? &irq_1_free : &irq_2_free;
  while (sent < IRQ_COUNT) {
    while (!__sync_bool_compare_and_swap(waiter, true, false)) asm volatile ("pause" : : : "memory");
    asm volatile ("":::"memory");

    logger.log(LOG_SEND, IRQS[IRQ-1]);
    if (mb.bus_irqlines.send(msg)) {
      sent++;
      ignored = 0;
    } else {
      assert(false);
      logger.log(LOG_IGNORE, IRQS[IRQ-1]);
      *waiter = true;
      if (ignored++ >= 1000) return nullptr;
    }
  }
  return nullptr;
}

int runPicTest() {
  // attach handlers
  mb.bus_irqnotify.add(nullptr, receive);
  mb.bus_legacy.add(nullptr, receive);

  tsc_start = Cpu::rdtsc();

  // create PIC
  mb.handle_arg("pic:0x20,,0x4d0");
  mb.handle_arg("pic:0xa0,2,0x4d1");

  // init PICs (sequence according to http://wiki.osdev.org/PIC)
  outb(0x20, 0x10+0x01);
  outb(0xa0, 0x10+0x01);
  outb(0x21, 0);
  outb(0xa1, 8);
  outb(0x21, 4);
  outb(0xa1, 2);

  outb(0x21, 0x01);
  outb(0xa1, 0x01);

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

  pthread_create(&trigger1, NULL, trigger_fn<1>, NULL);
  pthread_setaffinity_np(trigger1, sizeof(cpu_set_t), &cpuset_trigger1);

  pthread_create(&trigger2, NULL, trigger_fn<2>, NULL);
  pthread_setaffinity_np(trigger2, sizeof(cpu_set_t), &cpuset_trigger2);

  pthread_create(&receiver, NULL, receiver_fn, NULL);
  pthread_setaffinity_np(receiver, sizeof(cpu_set_t), &cpuset_receiver);

  pthread_join(receiver, nullptr);
  pthread_join(trigger1, nullptr);
  pthread_join(trigger2, nullptr);

  timevalue cycles = Cpu::rdtsc() - tsc_start;

  printf("Test completed. Received (%u, %u) interrupts (expected %u, %u).\nTest took %llu cycles.\n",
         irq_received_1, irq_received_2, IRQ_COUNT, IRQ_COUNT, cycles);

  //logger.dump();

  return 0;
}
