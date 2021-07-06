/*
 * LAPIC Unit Test
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

#include "lapic.h"

static LogBuffer logger;

static Clock mb_clock(1000000);
static Motherboard mb(&mb_clock, NULL);

VCpu *vcpu;

timevalue tsc_start;

void doIO(bool read, uintptr_t phys, unsigned *ptr) {
  MessageMem msg(read, phys, ptr);
  vcpu->mem.send(msg);
}

void readIO(uintptr_t phys, unsigned *ptr) {
  doIO(true, phys, ptr);
}

void writeIO(uintptr_t phys, unsigned *ptr) {
  doIO(false, phys, ptr);
}

// worker threads
pthread_t receiver, trigger, ipi;
bool irq_free = false;
bool ipi_free = false;
unsigned irq_sent_timer = 0, irq_sent_ipi = 0;
unsigned irq_received_timer = 0, irq_received_ipi = 0;
unsigned long intr = 0;
bool wakeup = false;

static bool receive(Device *, CpuEvent &msg) {
  if (msg.value == VCpu::EVENT_INTR) {
    __sync_fetch_and_add(&intr, 4);
    if (!(__sync_fetch_and_or(&intr, 1) & 0x1)) {
      logger.log(LOG_INTR);
    }
    wakeup = true;
  } else if (msg.value == VCpu::DEASS_INTR) {
    logger.log(LOG_DEASS);
  }
}

static bool receive(Device *, MessageHostOp &msg) {
  if (msg.type == MessageHostOp::OP_VCPU_CREATE_BACKEND) {
    vcpu = msg.vcpu;
    vcpu->bus_event.add(nullptr, receive);
    msg.value = 0;
    return true;
  } else if (msg.type == MessageHostOp::OP_VCPU_RELEASE) {
    // Release: CHECK_IRQ (unused)
  } else {
    Logging::printf("Hostop msg type %u\n", msg.type);
  }
}

static bool receive(Device *, MessageTimer &msg) {
  if (msg.type == MessageTimer::TIMER_NEW) {
    msg.nr = 0;
  } else {
    // Ready to fire new
    logger.log(LOG_NOTIFY, TIMER_VEC);
    __sync_bool_compare_and_swap(&irq_free, false, true);
  }
  return true;
}

static void * receiver_fn(void *) {
  // Run magic
  unsigned long waitcount = 0;
  unsigned long current;

  while (Cpu::rdtsc() < tsc_start+10000000);

  __sync_bool_compare_and_swap(&ipi_free, false, true);

  while (true) {
    if (!__sync_fetch_and_and(&wakeup, 0)) {
      if (irq_received_timer == IRQ_COUNT_TIMER && irq_received_ipi == IRQ_COUNT_IPI || waitcount++ > 100000000) break;
      asm volatile ("pause");
      continue;
    }
    waitcount = 0;

    // Double-check due to race
    current = intr;
    LapicEvent check(LapicEvent::CHECK_INTR);
    check.value = 0;
    vcpu->bus_lapic.send(check, true);
    if (!check.value) {
      logger.log(LOG_SKIP, check.value);
      __sync_bool_compare_and_swap(&intr, current, (current + 4) & ~1ULL);
      continue;
    }

    // INTA
    logger.log(LOG_INTA_TX);
    LapicEvent msg(LapicEvent::INTA);
    vcpu->bus_lapic.send(msg);
    logger.log(LOG_INTA_RX, msg.value);
    if (msg.value == TIMER_VEC) irq_received_timer++;
    else if (msg.value == IPI_VEC) irq_received_ipi++;
    else Logging::panic("Spurious IRQ! %x\n", msg.value);

    // EOI
    unsigned val = 0x0;
    logger.log(LOG_EOI, msg.value);
    writeIO(LAPIC_BASE + 0xb0, &val);

    if (msg.value == TIMER_VEC) {
      // Rearm
      unsigned val = 1U;
      writeIO(LAPIC_BASE + 0x380, &val);
    } else if (msg.value == IPI_VEC) {
      // Free IPI mutex
      __sync_bool_compare_and_swap(&ipi_free, false, true);
    }
  }
  Logging::printf("Receiver finished with %u,%u interrupts. (waitcount %lu)\n", irq_received_timer, irq_received_ipi, waitcount);
  Logging::printf("They sent %u,%u interrupts.\n", irq_sent_timer, irq_sent_ipi);
  return nullptr;
}

static void * trigger_fn(void *) {
  // Trigger timer interrupt at lapic
  while (irq_sent_timer < IRQ_COUNT_TIMER) {
    while (!__sync_bool_compare_and_swap(&irq_free, true, false)) asm volatile ("pause");

    logger.log(LOG_SEND, TIMER_VEC);

    MessageTimeout msg(0, Cpu::rdtsc());
    assert(mb.bus_timeout.send(msg));

    irq_sent_timer++;
  }
  Logging::printf("Timer thread exits.\n");
  return nullptr;
}

static void * ipi_fn(void *) {
  while (irq_sent_ipi < IRQ_COUNT_IPI) {
    while (!__sync_bool_compare_and_swap(&ipi_free, true, false)) asm volatile ("pause");

    logger.log(LOG_SEND, IPI_VEC);

    MessageApic msg(0x4000 | IPI_VEC, 0xff, 0);
    assert(mb.bus_apic.send(msg));

    irq_sent_ipi++;
  }
  Logging::printf("IPI thread exits.\n");
  return nullptr;
}

int runLAPICTest() {
  // attach handlers
  mb.bus_hostop.add(nullptr, receive);
  mb.bus_timer.add(nullptr, receive);

  // parse args
  //mb.handle_arg("ioapic");
  mb.handle_arg("vcpu");
  mb.handle_arg("lapic");

  // init LAPIC
  //software enable, map spurious interrupt to dummy isr
  unsigned val = 39 | 0x100;
  writeIO(LAPIC_BASE + 0xf0, &val);
  //map APIC timer to an interrupt, and by that enable it
  val = TIMER_VEC;
  writeIO(LAPIC_BASE + 0x320, &val);
  //set up divide value to 16
  val = 0x03;
  writeIO(LAPIC_BASE + 0x3e0, &val);
  //reset APIC timer (set counter)
  val = 1000U;
  writeIO(LAPIC_BASE + 0x380, &val);

  tsc_start = Cpu::rdtsc();

  cpu_set_t cpuset_receiver, cpuset_trigger, cpuset_ipi;
  pthread_t self = pthread_self();
  CPU_ZERO(&cpuset_receiver);
  CPU_ZERO(&cpuset_trigger);
  CPU_ZERO(&cpuset_ipi);
  CPU_SET(1, &cpuset_receiver);
  CPU_SET(2, &cpuset_trigger);
  CPU_SET(3, &cpuset_ipi);

  pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset_trigger);

  pthread_create(&trigger, NULL, trigger_fn, NULL);
  pthread_setaffinity_np(trigger, sizeof(cpu_set_t), &cpuset_trigger);

  pthread_create(&ipi, NULL, ipi_fn, NULL);
  pthread_setaffinity_np(ipi, sizeof(cpu_set_t), &cpuset_ipi);

  pthread_create(&receiver, NULL, receiver_fn, NULL);
  pthread_setaffinity_np(receiver, sizeof(cpu_set_t), &cpuset_receiver);

  //pthread_join(trigger, nullptr);
  //pthread_join(ipi, nullptr);
  pthread_join(receiver, nullptr);

  timevalue cycles = Cpu::rdtsc() - tsc_start;

  printf("Test completed. Received (%u, %u) interrupts (expected %u, %u).\nTest took %llu cycles.\n",
         irq_received_timer, irq_received_ipi, IRQ_COUNT_TIMER, IRQ_COUNT_IPI, cycles);

  if (irq_received_timer != irq_sent_timer || irq_received_ipi != irq_sent_ipi || irq_received_timer != IRQ_COUNT_TIMER || irq_received_ipi != IRQ_COUNT_IPI) {
    logger.dump();
    printf("Error. Log dumped, going to spin...\n");
    for (;;);
  }

  //logger.dump();

  return 0;
}
