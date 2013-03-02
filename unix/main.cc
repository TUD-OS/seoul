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
#include <nul/vcpu.h>
#include <service/profile.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>

#include <vector>

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

struct Module {
  char       *memory;
  size_t      size;
  const char *cmdline;

  static Module from_file(const char *filename, const char *cmdline)
  {
    Module m;
    int fd;
    struct stat info;

    if ((0 > (fd = open(filename, O_RDONLY))) or
        (0 > fstat(fd, &info))) {
      fprintf(stderr, "open %s: %s\n", filename, strerror(errno));
      exit(EXIT_FAILURE);
    }

    m.cmdline = cmdline;
    m.size    = info.st_size;

    m.memory  = reinterpret_cast<char *>(mmap(NULL, m.size, PROT_READ, MAP_PRIVATE,
                                              fd, 0));
    if (m.memory == MAP_FAILED) {
      perror("mmap");
      exit(EXIT_FAILURE);
    }

    return m;
  }
};

static std::vector<Module>   modules;

// Used to serialize all operations (for now)
static sem_t                 vcpu_sem;

static void skip_instruction(CpuMessage &msg)
{
  // advance EIP
  assert(msg.mtr_in & MTD_RIP_LEN);
  msg.cpu->eip += msg.cpu->inst_len;
  msg.mtr_out |= MTD_RIP_LEN;

  // cancel sti and mov-ss blocking as we emulated an instruction
  assert(msg.mtr_in & MTD_STATE);
  if (msg.cpu->intr_state & 3) {
    msg.cpu->intr_state &= ~3;
    msg.mtr_out |= MTD_STATE;
  }
}


static void handle_vcpu(bool skip, CpuMessage::Type type, VCpu *vcpu, CpuState *utcb)
{
  assert(vcpu);
  CpuMessage msg(type, static_cast<CpuState *>(utcb), utcb->mtd);
  msg.mtr_in = ~0U;
  if (skip) skip_instruction(msg);

  /**
   * Send the message to the VCpu.
   */
  if (!vcpu->executor.send(msg, true))
    Logging::panic("nobody to execute %s at %x:%x\n", __func__, msg.cpu->cs.sel, msg.cpu->eip);

  /**
   * Check whether we should inject something...
   */
  if (msg.mtr_in & MTD_INJ && msg.type != CpuMessage::TYPE_CHECK_IRQ) {
    msg.type = CpuMessage::TYPE_CHECK_IRQ;
    if (!vcpu->executor.send(msg, true))
      Logging::panic("nobody to execute %s at %x:%x\n", __func__, msg.cpu->cs.sel, msg.cpu->eip);
  }

  /**
   * If the IRQ injection is performed, recalc the IRQ window.
   */
  if (msg.mtr_out & MTD_INJ) {
    vcpu->inj_count ++;

    msg.type = CpuMessage::TYPE_CALC_IRQWINDOW;
    if (!vcpu->executor.send(msg, true))
      Logging::panic("nobody to execute %s at %x:%x\n", __func__, msg.cpu->cs.sel, msg.cpu->eip);
  }
  msg.cpu->mtd = msg.mtr_out;

}


static void *vcpu_thread_fn(void *arg)
{
  VCpu * vcpu = static_cast<VCpu *>(arg);
  CpuState cpu_state;
  memset(&cpu_state, 0, sizeof(cpu_state));

  handle_vcpu(false, CpuMessage::TYPE_HLT, vcpu, &cpu_state);

  while (true) {
    sem_wait(&vcpu_sem);
    handle_vcpu(false, CpuMessage::TYPE_SINGLE_STEP, vcpu, &cpu_state);
    sem_post(&vcpu_sem);
  }

  // NOTREACHED
  return NULL;
}

struct  Vcpu_info {
  pthread_t tid;
  sem_t     block;
};

static std::vector<Vcpu_info> vcpu_info;

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
        Logging::printf("host: Allocating from guest %08zx+%lx\n", ram_size, msg.value);
      } else res = false;
      break;
    case MessageHostOp::OP_VCPU_CREATE_BACKEND: {
      msg.value = vcpu_info.size();

      vcpu_info.push_back(Vcpu_info());

      if ((0 != sem_init(&vcpu_info[msg.value].block, 0, 0)) or
          (0 != pthread_create(&vcpu_info[msg.value].tid, NULL, vcpu_thread_fn, msg.vcpu))) {
        perror("sem_init/pthread_create");
        res = false;
        break;
      }

      break;
    }
    case MessageHostOp::OP_VCPU_BLOCK:
      sem_post(&vcpu_sem);
      sem_wait(&vcpu_info[msg.value].block);
      sem_wait(&vcpu_sem);
      break;
    case MessageHostOp::OP_VCPU_RELEASE:
      sem_post(&vcpu_info[msg.value].block);
      break;
    case MessageHostOp::OP_GET_MODULE:
      // For historical reasons, modules numbers start with 1.
      msg.module --;

      if (msg.module < modules.size() and
          msg.size   > modules[msg.module].size) {
        memcpy(msg.start, modules[msg.module].memory, modules[msg.module].size);

        // Align the end of the module to get the cmdline on a new page.
        uintptr_t s = reinterpret_cast<uintptr_t>(msg.start) + modules[msg.module].size;
        s = (s + 0xFFFUL) & ~0xFFFUL;

        msg.size    = modules[msg.module].size;
        msg.cmdline = reinterpret_cast<char *>(s);
        msg.cmdlen  = strlen(modules[msg.module].cmdline) + 1;

        strcpy(msg.cmdline, modules[msg.module].cmdline);
      } else
        res = false;
      break;
    case MessageHostOp::OP_GET_MAC: {
      static unsigned long long mac_prefix = 0x42000000;
      static unsigned long long mac_host   = random();
      msg.mac = mac_prefix << 16 | mac_host;
      break;
    }
    default:
      Logging::panic("%s - unimplemented operation %#x\n",
                       __PRETTY_FUNCTION__, msg.type);
    }
    return res;
}

static void timeout_trigger()
{
  timevalue now = mb.clock()->time();

  // Force time reprogramming. Otherwise, we might not reprogram a
  // timer, if the timeout event reached us too early.
  last_to = ~0ULL;

  // trigger all timeouts that are due
  unsigned nr;
  while ((nr = timeouts.trigger(now))) {
    MessageTimeout msg(nr, timeouts.timeout());
    timeouts.cancel(nr);
    mb.bus_timeout.send(msg);
  }
}

// Update or program pending timeout.
static void timeout_request()
{
  while (timeouts.timeout() != ~0ULL) {
    timevalue next_to = timeouts.timeout();
    if (next_to != last_to) {
      last_to = next_to;

      unsigned long long delta = mb_clock.delta(next_to, 1000000000UL);
      if (delta != 0) {
        //printf("Programming timer for %lluns.\n", delta);
        struct itimerspec t = {
          .it_interval = {0, 0},
          .it_value = {long(delta / 1000000000L), (long)(delta % 1000000000L)}
        };
        int res = timer_settime(timer_id, 0, &t, NULL);
        assert(!res);
        break;
      }
      else {
        // if the delta is 0, it means that the timeout has already been reached.
        timeout_trigger();
        // no break here because there might be other timeouts, so that we need
        // to program the timer for them
      }
    }
  }
}


static void timeout_handler_fn(union sigval)
{
  sem_wait(&vcpu_sem);
  timeout_trigger();
  timeout_request();
  sem_post(&vcpu_sem);
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

static unsigned num_views = 0;

static bool receive(Device *, MessageConsole &msg)
{
  switch (msg.type)
    {
    case MessageConsole::TYPE_ALLOC_CLIENT:
      Logging::panic("console: ALLOC_CLIENT not supported.\n");
    case MessageConsole::TYPE_ALLOC_VIEW:
      assert(msg.ptr and msg.regs);
      msg.view = num_views++;
      Logging::printf("console: ALLOC_VIEW not implemented.\n");
      return true;
    case MessageConsole::TYPE_GET_MODEINFO:
    case MessageConsole::TYPE_GET_FONT:
    case MessageConsole::TYPE_KEY:
    case MessageConsole::TYPE_RESET:
    case MessageConsole::TYPE_START:
    case MessageConsole::TYPE_KILL:
    case MessageConsole::TYPE_DEBUG:
    default:
      break;
    }
  return false;
}

static void usage()
{
  fprintf(stderr, "Usage: seoul [-m RAM] [kernel parameters] [module1 parameters] ...\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  printf("Seoul %s booting.\n"
         "Visit https://github.com/TUD-OS/seoul for information.\n\n",
         version_str);

  int ch;
  while ((ch = getopt(argc, argv, "hm:")) != -1) {
    switch (ch) {
    case 'm':
      ram_size = atoi(optarg) << 20;
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  if ((argc - optind) % 2) {
    printf("Module and command line parameters must be matched.\n");
    exit(EXIT_FAILURE);
  }

  for (int i = optind; i+1 < argc; i += 2) {
    modules.push_back(Module::from_file(argv[i], argv[i+1]));
  }

  printf("\nStarting devices:\n");
  printf("Devices included:\n");
  PARAM_ITER(p) {
    printf("\t%s\n", (*p)->name);
  }

  // Allocating RAM.

  ram = reinterpret_cast<char *>(mmap(nullptr, ram_size, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANON, -1, 0));
  if (ram == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }

  // Creating timer. I hate C++: No useful initializers...
  struct sigevent ev;
  ev.sigev_notify            = SIGEV_THREAD;
  ev.sigev_notify_attributes = NULL;
  ev.sigev_notify_function   = timeout_handler_fn;

  if (0 != timer_create(CLOCK_MONOTONIC, &ev, &timer_id)) {
    perror("timer_create");
    return EXIT_FAILURE;
  }


  mb.bus_hostop .add(nullptr, receive);
  mb.bus_timer  .add(nullptr, receive);
  mb.bus_time   .add(nullptr, receive);
  mb.bus_console.add(nullptr, receive);

  // Synchronization initialization
  if (0 != sem_init(&vcpu_sem, 0, 0)) {
    perror("sem_init");
    return EXIT_FAILURE;
  }

  // Create standard PC
  for (const char **dev = pc_ps2; *dev != NULL; dev++) {
    mb.handle_arg(*dev);
  }

  printf("Devices and %u virtual CPU%s started successfully.\n",
         vcpu_info.size(), vcpu_info.size() == 1 ? "" : "s");

  // init VCPUs
  for (VCpu *vcpu = mb.last_vcpu; vcpu; vcpu=vcpu->get_last()) {
    printf("Initializing virtual CPU %p.\n", vcpu);

    // init CPU strings
    static const char *short_name = "NOVA microHV";
    vcpu->set_cpuid(0, 1, reinterpret_cast<const unsigned *>(short_name)[0]);
    vcpu->set_cpuid(0, 3, reinterpret_cast<const unsigned *>(short_name)[1]);
    vcpu->set_cpuid(0, 2, reinterpret_cast<const unsigned *>(short_name)[2]);
    static const char *long_name = "Vancouver VMM proudly presents this VirtualCPU. ";
    for (unsigned i=0; i<12; i++)
      vcpu->set_cpuid(0x80000002 + (i / 4), i % 4, reinterpret_cast<const unsigned *>(long_name)[i]);

    // propagate feature flags from the host
    unsigned ebx_1=0, ecx_1=0, edx_1=0;
    Cpu::cpuid(1, ebx_1, ecx_1, edx_1);
    vcpu->set_cpuid(1, 1, ebx_1 & 0xff00, 0xff00ff00); // clflush size
    vcpu->set_cpuid(1, 2, ecx_1, 0x00000201); // +SSE3,+SSSE3
    vcpu->set_cpuid(1, 3, edx_1, 0x0f80a9bf | (1 << 28)); // -PAE,-PSE36, -MTRR,+MMX,+SSE,+SSE2,+SEP
  }

  Logging::printf("RESET device state\n");
  MessageLegacy msg2(MessageLegacy::RESET, 0);
  mb.bus_legacy.send_fifo(msg2);

  sem_post(&vcpu_sem);

  for (Vcpu_info &i : vcpu_info)
    if (0 != pthread_join(i.tid, NULL))
      perror("pthread_join");

  printf("Terminating.\n");
  return EXIT_SUCCESS;
}

// EOF
