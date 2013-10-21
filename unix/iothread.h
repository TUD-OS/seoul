/**
 * I/O Thread
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

#include <nul/message.h>
#include <nul/motherboard.h>
#include <nul/vcpu.h>
#include <service/logging.h>

#include <stdlib.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

class IOThread : public StaticReceiver<IOThread> {
private:
  pthread_mutex_t _lock;
  sem_t _block;
  bool blocking;
  std::queue<MessageIOThread> *_queue;
  Motherboard *_mb;

  struct Notify {
    pthread_t tid;
    sem_t sem;
  };
  std::vector<Notify> _notify;

  pthread_t own_tid;

public:
  bool enq(MessageIOThread &msg);
  void syncify_message(MessageIOThread &msg);
  template <typename M>
  void sync_message(MessageIOThread &msg, MessageIOThread::Sync sync);

  void init();

  bool enqueue(MessageDisk &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageDiskCommit &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageTime &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageTimer &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageTimeout &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageIOOut &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageIOIn &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageMem &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(CpuMessage &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageInput &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageIrqLines &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageIrqNotify &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageIrq &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageLegacy &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageNetwork &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessagePciConfig &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);
  bool enqueue(MessageHostOp &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu);

  void worker();
  sem_t *get_notify_sem(pthread_t tid);

  IOThread(Motherboard *mb) : blocking(false), _queue(nullptr), _mb(mb) {
    _queue = new std::queue<MessageIOThread>;
    if (0 != pthread_mutex_init(&_lock, nullptr)) perror("Could not init mutex.");
    if (0 != sem_init(&_block, 0, 0)) perror("Could not init sem.");

    mb->bus_disk.set_iothread_enqueue(this, enqueue_static<MessageDisk>);
    mb->bus_diskcommit.set_iothread_enqueue(this, enqueue_static<MessageDiskCommit>);
    mb->bus_time.set_iothread_enqueue(this, enqueue_static<MessageTime>);
    mb->bus_timer.set_iothread_enqueue(this, enqueue_static<MessageTimer>);
    mb->bus_timeout.set_iothread_enqueue(this, enqueue_static<MessageTimeout>);
    mb->bus_ioout.set_iothread_enqueue(this, enqueue_static<MessageIOOut>);
    mb->bus_ioin.set_iothread_enqueue(this, enqueue_static<MessageIOIn>);
    mb->bus_mem.set_iothread_enqueue(this, enqueue_static<MessageMem>);
    mb->bus_input.set_iothread_enqueue(this, enqueue_static<MessageInput>);
    mb->bus_irqlines.set_iothread_enqueue(this, enqueue_static<MessageIrqLines>);
    mb->bus_irqnotify.set_iothread_enqueue(this, enqueue_static<MessageIrqNotify>);
    mb->bus_legacy.set_iothread_enqueue(this, enqueue_static<MessageLegacy>);
    mb->bus_network.set_iothread_enqueue(this, enqueue_static<MessageNetwork>);
    mb->bus_pcicfg.set_iothread_enqueue(this, enqueue_static<MessagePciConfig>);
    mb->bus_hostop.set_iothread_enqueue(this, enqueue_static<MessageHostOp>);
  }
};
