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

#include <kobj/UserSm.h>
#include <kobj/GlobalThread.h>
#include <collection/SList.h>

class MessageIOThreadEle : public MessageIOThread, public nre::SListItem {
public:
  MessageIOThreadEle(Type _type, Mode _mode, Sync _sync, void *_ptr) : MessageIOThread(_type, _mode, _sync, _ptr) {}
  MessageIOThreadEle(Type _type, Mode _mode, Sync _sync, unsigned *_value, void *_ptr) : MessageIOThread(_type, _mode, _sync, _value, _ptr) {}
};

class Notify : public nre::SListItem {
public:
  nre::Utcb *utcb;
  nre::UserSm *sem;
};

class IOThread : public StaticReceiver<IOThread>, public nre::SListItem {
private:
  nre::UserSm _lock;
  nre::UserSm _block;
  bool blocking;
  nre::SList<MessageIOThreadEle> _queue;
  Motherboard *_mb;

  nre::SList<Notify> _notify;
  nre::Utcb *own_utcb;

public:
  bool enq(MessageIOThreadEle *msg);
  void syncify_message(MessageIOThreadEle *msg);
  template <typename M>
  void sync_message(MessageIOThreadEle *msg, MessageIOThread::Sync sync);

  void stats();

  void reset();

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
  nre::UserSm *get_notify_sem(nre::Utcb *utcb);

  IOThread(Motherboard *mb) : _lock(1), _block(0), blocking(false), _queue(), _mb(mb), _notify() {
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
    mb->bus_hostirq.set_iothread_enqueue(this, enqueue_static<MessageIrq>);
    mb->bus_legacy.set_iothread_enqueue(this, enqueue_static<MessageLegacy>);
    mb->bus_network.set_iothread_enqueue(this, enqueue_static<MessageNetwork>);
    mb->bus_pcicfg.set_iothread_enqueue(this, enqueue_static<MessagePciConfig>);
  }
};
