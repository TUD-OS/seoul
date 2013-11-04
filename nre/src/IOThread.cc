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

#include "IOThread.h"

#define IOTHREAD_DEBUG

#ifdef IOTHREAD_DEBUG
static unsigned long msgcount[20] = {};
static unsigned long maxqueue=0;
static unsigned long sync=0, async=0;
#endif

static bool iothread_init = false;

void IOThread::stats() {
#ifdef IOTHREAD_DEBUG
  for (unsigned i=0; i<17; i++) {
    Logging::printf("Type %u: Count %lu\n", i, msgcount[i]);
  }
  Logging::printf("Max queue size: %lu\n", maxqueue);
  Logging::printf(" Sync messages: %lu\n", sync);
  Logging::printf("ASync messages: %lu\n", async);
#endif
}

void IOThread::reset() {
  stats();
  if (iothread_init) return;
  for(VCpu *vcpu = _mb->last_vcpu; vcpu; vcpu = vcpu->get_last()) {
    vcpu->mem.set_iothread_enqueue(this, enqueue_static<MessageMem>, vcpu);
    vcpu->executor.set_iothread_enqueue(this, enqueue_static<CpuMessage>, vcpu);
  }
  iothread_init = true;
}

nre::UserSm *IOThread::get_notify_sem(nre::Utcb *utcb) {
  assert(utcb != 0);
  for (auto it = _notify.begin(); it != _notify.end(); it++) {
    if (it->utcb == utcb) return it->sem;
  }
  Notify *new_notify = new Notify;
  new_notify->utcb = utcb;
  new_notify->sem = new nre::UserSm(0);
  _notify.append(new_notify);
  return new_notify->sem;
}

template <typename M>
static void sync_msg(MessageIOThreadEle *iomsg) {
  // We have to keep the message when it is synchronous. The receiver will delete it.
  if (iomsg->sync == MessageIOThread::SYNC_SYNC) {
    // Wake enqueuer
    assert(iomsg->sem != nullptr);
    reinterpret_cast<nre::UserSm*>(iomsg->sem)->up();
  } else {
    delete (M*) iomsg->ptr;
  }
}

void IOThread::syncify_message(MessageIOThreadEle *msg) {
  if (msg->sync == MessageIOThread::SYNC_SYNC) {
    msg->sem = this->get_notify_sem(nre::Thread::current()->utcb());
    assert(msg->sem != nullptr);
  }
}

template <typename M>
void IOThread::sync_message(MessageIOThreadEle *msg, MessageIOThread::Sync sync) {
  if (sync == MessageIOThread::SYNC_SYNC) {
    reinterpret_cast<nre::UserSm*>(msg->sem)->down();
    delete msg;
  }
}

bool IOThread::enq(MessageIOThreadEle *msg) {
  nre::ScopedLock<nre::UserSm> lock(&_lock);

#ifdef IOTHREAD_DEBUG
  msgcount[msg->type]++;
  if (msg->sync == MessageIOThread::SYNC_SYNC) sync++;
  else async++;
#endif
  _queue.append(msg);
  _block.up();
  return true;
}

bool IOThread::enqueue(MessageDisk &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  // Disk is always sync because of error check
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_DISK, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageDisk>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageDiskCommit &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageDiskCommit *ptr = new MessageDiskCommit(msg.disknr, msg.usertag, msg.status);
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_DISKCOMMIT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageDiskCommit>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTime &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  // Time must be sync
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_TIME, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTime>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTimer &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  /*
   * Timer slot requests are always sync.
   * Because they are a result of an earlier message, timeout requests should never be enqueued.
   */
  if (msg.type == MessageTimer::TIMER_NEW) sync = MessageIOThread::SYNC_SYNC;
  MessageTimer *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageTimer;
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_TIMER, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTimer>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTimeout &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageTimeout *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageTimeout(msg.nr, msg.time);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_TIMEOUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTimeout>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIOOut &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageIOOut *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIOOut(msg.type, msg.port, msg.value);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_IOOUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIOOut>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIOIn &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  // I/O port reads are always sync
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_IOIN, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIOIn>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageMem &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  // Mem reads are always sync
  if (msg.read) sync = MessageIOThread::SYNC_SYNC;
  MessageMem *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    assert(!msg.read);
    // We need to save the value pointed to by msg.ptr!
    unsigned *val = new unsigned;
    *val = *msg.ptr;
    ptr = new MessageMem(msg.read, msg.phys, val);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_MEM, mode, sync, value, ptr);
  enq->vcpu = vcpu;
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageMem>(enq, sync);
  return true;
}
bool IOThread::enqueue(CpuMessage &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  if (msg.type != CpuMessage::TYPE_RDMSR && msg.type != CpuMessage::TYPE_WRMSR && msg.type != CpuMessage::TYPE_CHECK_IRQ) return false;
  // These messages are always sync
  sync = MessageIOThread::SYNC_SYNC;
  CpuMessage *ptr;
  ptr = &msg;
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_CPU, mode, sync, value, ptr);
  enq->vcpu = vcpu;
  syncify_message(enq);
  this->enq(enq);
  sync_message<CpuMessage>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageInput &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageInput *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageInput(msg.device, msg.data);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_INPUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageInput>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrqLines &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageIrqLines *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrqLines(msg.type, msg.line);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_IRQLINES, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrqLines>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrqNotify &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageIrqNotify *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrqNotify(msg.baseirq, msg.mask);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_IRQNOTIFY, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrqNotify>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrq &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  MessageIrq *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrq(msg.type, msg.line);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_IRQ, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrq>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageLegacy &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  if (msg.type == MessageLegacy::INTA || msg.type == MessageLegacy::DEASS_INTR) sync = MessageIOThread::SYNC_SYNC;
  MessageLegacy *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageLegacy(msg.type, msg.value);
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_LEGACY, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageLegacy>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageNetwork &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  if (msg.type == MessageNetwork::QUERY_MAC) sync = MessageIOThread::SYNC_SYNC;
  MessageNetwork *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageNetwork(msg.type, msg.client);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_NETWORK, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageNetwork>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessagePciConfig &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb) return false;
  // Reads are sync
  if (msg.type == MessagePciConfig::TYPE_READ) sync = MessageIOThread::SYNC_SYNC;
  MessagePciConfig *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessagePciConfig(msg.bdf);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_PCICFG, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessagePciConfig>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageHostOp &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu*) {
  if (nre::Thread::current()->utcb() == own_utcb || msg.type != MessageHostOp::OP_VCPU_RELEASE) return false;
  MessageHostOp *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageHostOp(msg.vcpu);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThreadEle *enq = new MessageIOThreadEle(MessageIOThread::TYPE_HOSTOP, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageHostOp>(enq, sync);
  return true;
}

void IOThread::worker() {
  // Set own UTCB. With this we can detect when sending message ourselves. They must not be enqueued.
  own_utcb = nre::Thread::current()->utcb();

  MessageIOThreadEle *msg;
  MessageIOThread::Sync sync;
  MessageIOThread::Type type;
  while (1) {
    _block.down();

    {
      nre::ScopedLock<nre::UserSm> lock(&_lock);
      assert(_queue.length() > 0);
#ifdef IOTHREAD_DEBUG
      if (_queue.length() > maxqueue) maxqueue = _queue.length();
#endif

      auto it = _queue.begin();
      msg = &*it;
      _queue.remove(msg);
      sync = msg->sync;
      type = msg->type;
    }

    // Send message on appropriate bus
    switch (msg->type) {
      case MessageIOThread::TYPE_DISK:
        {
          MessageDisk *msg2 = reinterpret_cast<MessageDisk*>(msg->ptr);
          _mb->bus_disk.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageDisk>(msg);
        }
        break;
      case MessageIOThread::TYPE_DISKCOMMIT:
        {
          MessageDiskCommit *msg2 = reinterpret_cast<MessageDiskCommit*>(msg->ptr);
          _mb->bus_diskcommit.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageDiskCommit>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIME:
        {
          MessageTime *msg2 = reinterpret_cast<MessageTime*>(msg->ptr);
          _mb->bus_time.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageTime>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIMER:
        {
          MessageTimer *msg2 = reinterpret_cast<MessageTimer*>(msg->ptr);
          _mb->bus_timer.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageTimer>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIMEOUT:
        {
          MessageTimeout *msg2 = reinterpret_cast<MessageTimeout*>(msg->ptr);
          _mb->bus_timeout.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageTimeout>(msg);
        }
        break;
      case MessageIOThread::TYPE_IOOUT:
        {
          MessageIOOut *msg2 = reinterpret_cast<MessageIOOut*>(msg->ptr);
          _mb->bus_ioout.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageIOOut>(msg);
        }
        break;
      case MessageIOThread::TYPE_IOIN:
        {
          MessageIOIn *msg2 = reinterpret_cast<MessageIOIn*>(msg->ptr);
          _mb->bus_ioin.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageIOIn>(msg);
        }
        break;
      case MessageIOThread::TYPE_MEM:
        {
          MessageMem *msg2 = reinterpret_cast<MessageMem*>(msg->ptr);
          if (msg->vcpu)
            msg->vcpu->mem.send_direct(*msg2, msg->mode, msg->value);
          else
            _mb->bus_mem.send_direct(*msg2, msg->mode, msg->value);
          // Special case: delete saved value
          if (msg->sync == MessageIOThread::SYNC_ASYNC) delete msg2->ptr;
          sync_msg<MessageMem>(msg);
        }
        break;
      case MessageIOThread::TYPE_CPU:
        {
          CpuMessage *msg2 = reinterpret_cast<CpuMessage*>(msg->ptr);
          if (msg->vcpu)
            msg->vcpu->executor.send_direct(*msg2, msg->mode, msg->value);
          else
            Logging::panic("TYPE_CPU needs a vcpu pointer!\n");
          sync_msg<CpuMessage>(msg);
        }
        break;
      case MessageIOThread::TYPE_INPUT:
        {
          MessageInput *msg2 = reinterpret_cast<MessageInput*>(msg->ptr);
          _mb->bus_input.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageInput>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQLINES:
        {
          MessageIrqLines *msg2 = reinterpret_cast<MessageIrqLines*>(msg->ptr);
          _mb->bus_irqlines.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageIrqLines>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQNOTIFY:
        {
          MessageIrqNotify *msg2 = reinterpret_cast<MessageIrqNotify*>(msg->ptr);
          _mb->bus_irqnotify.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageIrqNotify>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQ:
        {
          MessageIrq *msg2 = reinterpret_cast<MessageIrq*>(msg->ptr);
          _mb->bus_hostirq.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageIrq>(msg);
        }
        break;
      case MessageIOThread::TYPE_LEGACY:
        {
          MessageLegacy *msg2 = reinterpret_cast<MessageLegacy*>(msg->ptr);
          _mb->bus_legacy.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageLegacy>(msg);
        }
        break;
      case MessageIOThread::TYPE_NETWORK:
        {
          MessageNetwork *msg2 = reinterpret_cast<MessageNetwork*>(msg->ptr);
          _mb->bus_network.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageNetwork>(msg);
        }
        break;
      case MessageIOThread::TYPE_PCICFG:
        {
          MessagePciConfig *msg2 = reinterpret_cast<MessagePciConfig*>(msg->ptr);
          _mb->bus_pcicfg.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessagePciConfig>(msg);
        }
        break;
      case MessageIOThread::TYPE_HOSTOP:
        {
          MessageHostOp *msg2 = reinterpret_cast<MessageHostOp*>(msg->ptr);
          _mb->bus_hostop.send_direct(*msg2, msg->mode, msg->value);
          sync_msg<MessageHostOp>(msg);
        }
        break;

      default:
        Logging::panic("Cannot handle type %x %x (size is %lx)!\n", type, sync, _queue.length());
    }
    if (sync == MessageIOThread::SYNC_ASYNC) {
      delete msg;
    }
  }
}
