/**
 * UNIX Seoul frontend
 *
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2013 Jacek Galowicz, Intel Corporation.
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
#include <host/dma.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>

#include <vector>

#include <seoul/unix.h>
#include <nul/migration.h>

#define USE_IOTHREAD

#ifdef USE_IOTHREAD
#include "iothread.h"
#endif

const char version_str[] =
#include "version.inc"
  ;

// Configuration

static char  *ram;
static size_t ram_size = 128 << 20; // 128 MB
static int    tap_fd;               // TAP device. If 0, network packets go to /dev/null.

static const char *pc_ps2[] = {
  // Unix backend
  "ncurses",
  "logging",
  // Models
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
  // "intel82576vf",
  "rtl8029:,9,0x300",
  "ahci:0xe0800000,14",
  "pmtimer:0x8000",
  // 4 vCPUs
  "vcpu", "halifax", "vbios", "lapic",
  "vcpu", "halifax", "vbios", "lapic",
  "vcpu", "halifax", "vbios", "lapic",
  "vcpu", "halifax", "vbios", "lapic",
  NULL,
  };

// Globals

static TimeoutList<32, void> timeouts;
static timevalue             last_to = ~0ULL;
static timer_t               timer_id;

Motherboard                 *mb;
Clock                       *mb_clock;
#ifdef USE_IOTHREAD
IOThread                    *iothread_obj;
#endif

// Multiboot module data

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

static std::vector<Module> modules;

// Disk data

struct Disk {
  const char *name;
  int         fd;
  size_t      size;

  static Disk from_file(const char *filename)
  {
    Disk d;
    struct stat st;

    d.name = filename;
    if (0  > (d.fd = open(filename, O_RDWR)) or
        0 != fstat(d.fd, &st)) {
      perror("open disk"); exit(EXIT_FAILURE);
    }

    d.size = (st.st_size + 511) & ~511; // Round to sector size

    printf("Added '%s' (%zu bytes) as disk.\n", filename, d.size);
    return d;
  }
};

static std::vector<Disk> disks;

// Used to serialize all operations (for now).
pthread_mutex_t irq_mtx;

// Relevant to live migration

Migration *_migrator;
Migration::RestoreModes _restore_mode = Migration::MODE_OFF;
unsigned _migration_ip;
unsigned _migration_port;

// the memory remapping procedure should only
// remap memory in page size granularity, if set
bool _track_page_usage = false;

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

  pthread_mutex_lock(&irq_mtx);
  handle_vcpu(false, CpuMessage::TYPE_HLT, vcpu, &cpu_state);
  pthread_mutex_unlock(&irq_mtx);

  while (true) {
    pthread_mutex_lock(&irq_mtx);

    if (_restore_mode == Migration::MODE_RECEIVE)
        // This will block until everything is restored
        _migrator->listen(_migration_port, &cpu_state);
    else if (_restore_mode == Migration::MODE_SEND)
        // This will block if the last memory resend round is reached
        _migrator->save_guestregs(&cpu_state);

    handle_vcpu(false, CpuMessage::TYPE_SINGLE_STEP, vcpu, &cpu_state);
    // Logging::printf("eip %x\n", cpu_state.eip);

    if (_restore_mode == Migration::MODE_RECEIVE) {
        _restore_mode = Migration::MODE_OFF;
        delete _migrator;
        _migrator = NULL;
        cpu_state.mtd = MTD_ALL;
    }
    pthread_mutex_unlock(&irq_mtx);
  }

  // NOTREACHED
  return NULL;
}

struct  Vcpu_info {
  pthread_t tid;
  sem_t     block;
};

static std::vector<Vcpu_info> vcpu_info;

static void *migration_thread_fn(void *)
{
    _migrator = new Migration(mb);
    _migrator->send(_migration_ip, _migration_port);

    delete _migrator;
    _migrator = nullptr;

    return nullptr;
}

static void start_migration_to(unsigned ip, unsigned port)
{
    _migration_ip = ip;
    _migration_port = port;
    _restore_mode = Migration::MODE_SEND;

    pthread_t migthread;
    if (0 != pthread_create(&migthread, NULL, migration_thread_fn, NULL)) {
        perror("pthread_create");
        return;
    }
    pthread_setname_np(migthread, "migration");
}

#ifdef USE_IOTHREAD
void * iothread_worker(void *) {
  iothread_obj->worker();

  return NULL;
}
#endif

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
      pthread_setname_np(vcpu_info[msg.value].tid, "vcpu");

      break;
    }
    case MessageHostOp::OP_VCPU_BLOCK:
      pthread_mutex_unlock(&irq_mtx);
      sem_wait(&vcpu_info[msg.value].block);
      pthread_mutex_lock(&irq_mtx);
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
    case MessageHostOp::OP_NEXT_DIRTY_PAGE: {
        /*
         * What this does when it is properly implemented:
         * - There is a variable "pageptr" which points
         *   to a page number.
         * - The user emits this message host op when
         *   he wants a dirty page region
         * - pageptr is moved incrementally until
         *   a dirty page region is found.
         *   This page region is then remapped RO
         *   and returned to the user as a CRD description
         * - pageptr wraps around if it exceeds guest mem size.
         */
#if PORTED_TO_UNIX
        const unsigned physpages = _physsize >> 12;
        static unsigned long pageptr = 0;

        _track_page_usage = true;

        Crd reg = nova_lookup(Crd(pageptr, 0, DESC_MEM_ALL));
        // There will be several mappings, but we want to see the ones
        // which are set to "writable by the guest"

        unsigned increment = 0;
        do {
            if (increment >= physpages) {
                // That's it for now. Come back later.
                msg.value = 0;
                return true;
            }
            MessageMemRegion mmsg(pageptr);
            if (!_mb->bus_memregion.send(mmsg, true)) {
                // No one claims this region. Do not track.
                pageptr = (pageptr + 1) % physpages;
                ++increment;
                continue;
            }
            if (!mmsg.actual_physmem) {
                // This is no physmem.
                pageptr += mmsg.count;
                increment += mmsg.count;
                if (pageptr > physpages) pageptr = 0;
                continue;
            }
            reg = nova_lookup(Crd(pageptr, 0, DESC_MEM_ALL));
            if (!(reg.attr() & DESC_RIGHT_W)) {
                // Not write-mapped, hence not dirty.
                pageptr += 1 << reg.order();
                increment += 1 << reg.order();
                if (pageptr > physpages) pageptr = 0;
                continue;
            }

            break;
        } while (1);

        // reg now describes a region which is guest-writable
        // This means that the guest wrote to it before and it is now considered "dirty"

        // Tell the user "where" and "how many"
        msg.phys    = pageptr << 12;
        msg.phys_len = reg.order();
        msg.value = reg.value();

        // Make this page read-only for the guest, so it is considered "clean" now.
        nova_revoke(Crd((reg.base() + _physmem) >> 12, reg.order(),
                    DESC_RIGHT_W | DESC_TYPE_MEM), false);
        pageptr += 1 << reg.order();
        if (pageptr >= physpages) pageptr = 0;

#endif
        return true;
    }
    break;
    case MessageHostOp::OP_GET_CONFIG_STRING: {
        char *cmdline = NULL;

#if PORTED_TO_UNIX
        // Retrieve the command line string length from sigma0
        MessageConsole cmsg(MessageConsole::TYPE_START, cmdline);
        cmsg.read = true;
        cmsg.mem = 0;
        unsigned ret = Sigma0Base::console(cmsg);
        if (ret) {
            Logging::printf("Error retrieving the command line"
                    " string length from sigma0.\n");
            return false;
        }

        // Retrieve the command line itself
        cmdline = new char[cmsg.mem+1];
        cmsg.mem += 1;
        cmsg.cmdline = cmdline;
        ret = Sigma0Base::console(cmsg);
        if (ret) {
            Logging::printf("Error retrieving the command line string sigma0.\n");
            return false;
        }
#endif

        msg.obj = cmdline;
    }
    break;

    case MessageHostOp::OP_MIGRATION_RETRIEVE_INIT: {
        _migration_port = msg.value;
        _restore_mode = Migration::MODE_RECEIVE;
        _migrator = new Migration(mb);
    }
    break;
    case MessageHostOp::OP_MIGRATION_START: {
        start_migration_to(msg.value, 9000);
        return true;
    }
    break;

    default:
      Logging::panic("%s - unimplemented operation %#x\n",
                       __PRETTY_FUNCTION__, msg.type);
    }
    return res;
}


static void timeout_trigger()
{
  timevalue now = mb_clock->time();

  // Force time reprogramming. Otherwise, we might not reprogram a
  // timer, if the timeout event reached us too early.
  last_to = ~0ULL;

  // trigger all timeouts that are due
  unsigned nr;
  while ((nr = timeouts.trigger(now))) {
    MessageTimeout msg(nr, timeouts.timeout());
    timeouts.cancel(nr);
    mb->bus_timeout.send(msg);
  }
}

// Update or program pending timeout.
static void timeout_request()
{
  timevalue next_to = timeouts.timeout();
  if (next_to != ~0ULL) {
    unsigned long long delta = mb_clock->delta(next_to, 1000000000UL);

    if (delta == 0) {
      // Timeout pending NOW. Skip programming a timeout.
      timeout_trigger();

      // We might have a new timeout pending.
      timeout_request();
    } else if (next_to != last_to) {
      // New timeout. Reprogram timer.

      last_to = next_to;

      // Logging::printf("Programming timer for %lluns.\n", delta);

      struct itimerspec t = {
        .it_interval = {0, 0},
        .it_value = {long(delta / 1000000000L), (long)(delta % 1000000000L)}
      };
      int res = timer_settime(timer_id, 0, &t, NULL);
      assert(!res);
    }
  }
}

static void timeout_handler_fn(union sigval)
{
  pthread_mutex_lock(&irq_mtx);
  timeout_trigger();
  timeout_request();
  pthread_mutex_unlock(&irq_mtx);
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
  msg.timestamp = mb_clock->clock(MessageTime::FREQUENCY);

  assert(MessageTime::FREQUENCY == 1000000U);
  msg.wallclocktime = (uint64)tv.tv_sec * 1000000 + tv.tv_usec;
  return true;
}

// Network support

static unsigned char network_pbuf[2048];

static void *network_io_thread_fn(void *)
{
  fd_set set;
  FD_ZERO(&set);
  FD_SET(tap_fd, &set);

  while (true) {
    fd_set rset = set;
    if (0 > select(tap_fd + 1, &rset, nullptr, nullptr, nullptr)) {
      perror("select");
      break;
    }

    int  res = read(tap_fd, network_pbuf, sizeof(network_pbuf));
    if (res <= 0) break;
    printf("tap: read %u bytes.\n", res);
    MessageNetwork msg(network_pbuf, res, 0);

    pthread_mutex_lock(&irq_mtx);
    mb->bus_network.send(msg);
    pthread_mutex_unlock(&irq_mtx);
  }

  return nullptr;
}

static bool receive(Device *, MessageNetwork &msg)
{
  int res;

  switch (msg.type) {
  case MessageNetwork::PACKET:
    Logging::printf("packet %zu bytes\n", msg.len);
    if (tap_fd and msg.buffer != network_pbuf) {
      res = write(tap_fd, msg.buffer, msg.len);
      if (res != static_cast<int>(msg.len)) perror("write to tap");
    }
    return true;
  case MessageNetwork::QUERY_MAC:
  default:
    return false;
  }

}

static bool receive(Device *, MessageDisk &msg)
{
  if (msg.disknr >= disks.size()) return false;

  Disk               &disk   = disks[msg.disknr];
  MessageDisk::Status status = MessageDisk::DISK_OK;
  unsigned long long  offset = msg.sector << 9;

  switch (msg.type) {
  case MessageDisk::DISK_READ:
  case MessageDisk::DISK_WRITE:
    for (unsigned i=0; i < msg.dmacount; i++) {
      size_t  start = offset;
      size_t  end   = start + msg.dma[i].bytecount;
      ssize_t bytes;

      if (end > disk.size or start > disk.size or
          msg.dma[i].byteoffset > msg.physsize or
          msg.dma[i].byteoffset + msg.dma[i].bytecount > msg.physsize) {
        status = MessageDisk::Status(MessageDisk::DISK_STATUS_DEVICE |
                                     (i << MessageDisk::DISK_STATUS_SHIFT));
        break;
      }

      // XXX Workaround, use hostop GUEST_MEM.
      msg.physoffset = reinterpret_cast<uintptr_t>(ram);

      typedef int (*RWFn)(int,void *,size_t,off_t);
      bytes = ((msg.type == MessageDisk::DISK_READ) ? (RWFn)pread : (RWFn)pwrite)
        (disk.fd, reinterpret_cast<void *>(msg.dma[i].byteoffset + msg.physoffset),
         end - start, start);

      if (bytes < ssize_t(end - start)) {
        Logging::printf("short read/write: %zd instead of %zd\n", bytes, end - start);
      }

      offset += end - start;
    }
    break;
  case MessageDisk::DISK_GET_PARAMS:
    {
      msg.params->flags = DiskParameter::FLAG_HARDDISK;
      msg.params->sectors = disk.size >> 9;
      msg.params->sectorsize = 512;
      msg.params->maxrequestcount = msg.params->sectors;
      strncpy(msg.params->name, disk.name, sizeof(msg.params->name));
      return true;
    }
  case MessageDisk::DISK_FLUSH_CACHE:
    break;
  default:
    assert(0);
  }

  MessageDiskCommit cmsg(msg.disknr, msg.usertag, status);
  mb->bus_diskcommit.send(cmsg);

  return true;
}

static void usage()
{
  fprintf(stderr, "Usage: seoul [-m RAM] [-n tap-device] [-d disk-image]\n"
                  "             [kernel parameters] [module1 parameters] ...\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  printf("Seoul %s booting.\n"
         "Visit https://github.com/TUD-OS/seoul for information.\n\n",
         version_str);

  if (argc < 2) {
    fprintf(stderr, "No parameters given.\n");
    usage();
  }

  int ch;
  while ((ch = getopt(argc, argv, "hm:n:d:")) != -1) {
    switch (ch) {
    case 'm':
      ram_size = atoi(optarg) << 20;
      break;
    case 'n':
      tap_fd = open(optarg, O_RDWR);
      if (tap_fd < 0) {
        perror("open tap device");
        return EXIT_FAILURE;
      }
      break;
    case 'd':
      disks.push_back(Disk::from_file(optarg));
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  if ((argc - optind) % 2) {
    fprintf(stderr, "Module and command line parameters must be matched.\n");
    usage();
  }

  for (int i = optind; i+1 < argc; i += 2) {
    modules.push_back(Module::from_file(argv[i], argv[i+1]));
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

  mb_clock = new Clock(get_tsc_frequency());
  mb = new Motherboard(mb_clock, NULL);

#ifdef USE_IOTHREAD
  iothread_obj = new IOThread(mb);
  pthread_t iothread_worker_thread;
  if (0 != pthread_create(&iothread_worker_thread, NULL, iothread_worker, NULL)) {
    perror("create iothread_worker failed");
    return EXIT_FAILURE;
  }
  pthread_setname_np(iothread_worker_thread, "iothread_worker");
#endif

  mb->bus_hostop .add(nullptr, receive);
  mb->bus_timer  .add(nullptr, receive);
  mb->bus_time   .add(nullptr, receive);

  mb->bus_network.add(nullptr, receive);
  mb->bus_disk   .add(nullptr, receive);

  mb->bus_restore.add(&timeouts, TimeoutList<32, void>::receive_static<MessageRestore>);

  // Synchronization initialization
  if (0 != pthread_mutex_init(&irq_mtx, nullptr)) {
    perror("pthread_mutex_init");
    return EXIT_FAILURE;
  }
  pthread_mutex_lock(&irq_mtx);

  // Create standard PC
  for (const char **dev = pc_ps2; *dev != NULL; dev++) {
    mb->handle_arg(*dev);
  }

  Logging::printf("Devices and %zu virtual CPU%s started successfully.\n",
                  vcpu_info.size(), vcpu_info.size() == 1 ? "" : "s");

#ifdef USE_IOTHREAD
  // Init I/O thread (vCPU local busses)
  iothread_obj->init();
#endif

  // init VCPUs
  for (VCpu *vcpu = mb->last_vcpu; vcpu; vcpu=vcpu->get_last()) {
    Logging::printf("Initializing virtual CPU %p.\n", vcpu);

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
  mb->bus_legacy.send_fifo(msg2);

  if (_restore_mode != Migration::MODE_OFF) {
      /*
       * The following UNLOCK message helps the VCPU out of the lock
       * it is blocked by and catches it into the recall handler.
       */
       MessageLegacy msg3(MessageLegacy::UNLOCK, 0);
       mb->bus_legacy.send_fifo(msg3);
  }

  pthread_t iothread;
  if (tap_fd) {
    Logging::printf("Starting background threads.\n");
    if (0 != pthread_create(&iothread, NULL, network_io_thread_fn, NULL)) {
      perror("pthread_create");
      return EXIT_FAILURE;
    }
    pthread_setname_np(iothread, "io");
  }

  Logging::printf("Virtual CPUs starting.\n");
  pthread_mutex_unlock(&irq_mtx);

  // Waiting for CPUs to exit.
  for (Vcpu_info &i : vcpu_info)
    if (0 != pthread_join(i.tid, nullptr))
      perror("pthread_join");

  // Force IO thread to exit.
  if (tap_fd) {
    close(tap_fd);
    pthread_join(iothread, nullptr);
  }

  printf("Terminating.\n");
  return EXIT_SUCCESS;
}

// EOF
