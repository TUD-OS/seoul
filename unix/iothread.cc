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

#include "iothread.h"

void IOThread::init() {
  for(VCpu *vcpu = _mb->last_vcpu; vcpu; vcpu = vcpu->get_last()) {
    vcpu->mem.set_iothread_enqueue(this, enqueue_static<MessageMem>, vcpu);
    vcpu->executor.set_iothread_enqueue(this, enqueue_static<CpuMessage>, vcpu);
  }
}

sem_t *IOThread::get_notify_sem(pthread_t tid) {
  for (auto it = _notify.begin(); it != _notify.end(); it++) {
    if (it->tid == pthread_self()) return &it->sem;
  }
  Notify *new_notify = new Notify;
  new_notify->tid = pthread_self();
  sem_init(&new_notify->sem, 0, 0);
  _notify.push_back(*new_notify);
  return &new_notify->sem;
}

template <typename M>
static void sync_msg(MessageIOThread &iomsg) {
  // We have to keep the message when it is synchronous. The receiver will delete it.
  if (iomsg.sync == MessageIOThread::SYNC_SYNC) {
    // Wake enqueuer
    assert(iomsg.sem != nullptr);
    sem_post(reinterpret_cast<sem_t*>(iomsg.sem));
  } else {
    delete (M*) iomsg.ptr;
  }
}

void IOThread::syncify_message(MessageIOThread &msg) {
  if (msg.sync == MessageIOThread::SYNC_SYNC) {
    msg.sem = this->get_notify_sem(pthread_self());
    assert(msg.sem != nullptr);
  }
}

template <typename M>
void IOThread::sync_message(MessageIOThread &msg, MessageIOThread::Sync sync) {
  if (sync == MessageIOThread::SYNC_SYNC) {
    // Wait for signal from worker
    sem_wait(reinterpret_cast<sem_t*>(msg.sem));
  }
}

bool IOThread::enq(MessageIOThread &msg) {
  pthread_mutex_lock(&_lock);
  _queue->push(msg);
  sem_post(&_block);
  pthread_mutex_unlock(&_lock);
  return true;
}

bool IOThread::enqueue(MessageDisk &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  // Disk is always sync because of error check
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThread enq(MessageIOThread::TYPE_DISK, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageDisk>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageDiskCommit &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageDiskCommit *ptr = new MessageDiskCommit(msg.disknr, msg.usertag, msg.status);
  MessageIOThread enq(MessageIOThread::TYPE_DISKCOMMIT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageDiskCommit>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTime &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  // Time must be sync
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThread enq(MessageIOThread::TYPE_TIME, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTime>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTimer &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageTimer *ptr;
  if (msg.type == MessageTimer::TIMER_NEW) sync = MessageIOThread::SYNC_SYNC;
  else Logging::panic("MessageTimer request nr %u\n", msg.nr);
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageTimer;
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_TIMER, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTimer>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageTimeout &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageTimeout *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageTimeout(msg.nr, msg.time);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_TIMEOUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageTimeout>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIOOut &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageIOOut *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIOOut(msg.type, msg.port, msg.value);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_IOOUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIOOut>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIOIn &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  // I/O port reads are always sync
  sync = MessageIOThread::SYNC_SYNC;
  MessageIOThread enq(MessageIOThread::TYPE_IOIN, mode, sync, value, &msg);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIOIn>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageMem &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
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
  MessageIOThread enq(MessageIOThread::TYPE_MEM, mode, sync, value, ptr);
  enq.vcpu = vcpu;
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageMem>(enq, sync);
  return true;
}

bool IOThread::enqueue(CpuMessage &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  if (msg.type != CpuMessage::TYPE_RDMSR && msg.type != CpuMessage::TYPE_WRMSR && msg.type != CpuMessage::TYPE_CHECK_IRQ)
    return false;

  // These messages are always sync
  sync = MessageIOThread::SYNC_SYNC;
  CpuMessage *ptr;
  ptr = &msg;
  MessageIOThread enq(MessageIOThread::TYPE_CPU, mode, sync, value, ptr);
  enq.vcpu = vcpu;
  syncify_message(enq);
  this->enq(enq);
  sync_message<CpuMessage>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageInput &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageInput *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageInput(msg.device, msg.data);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_INPUT, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageInput>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrqLines &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageIrqLines *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrqLines(msg.type, msg.line);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_IRQLINES, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrqLines>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrqNotify &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageIrqNotify *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrqNotify(msg.baseirq, msg.mask);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_IRQNOTIFY, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrqNotify>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageIrq &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  MessageIrq *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageIrq(msg.type, msg.line);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_IRQ, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageIrq>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageLegacy &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  if (msg.type == MessageLegacy::INTA || msg.type == MessageLegacy::DEASS_INTR) sync = MessageIOThread::SYNC_SYNC;
  MessageLegacy *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageLegacy(msg.type, msg.value);
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_LEGACY, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageLegacy>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageNetwork &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  if (msg.type == MessageNetwork::QUERY_MAC) sync = MessageIOThread::SYNC_SYNC;
  MessageNetwork *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageNetwork(msg.type, msg.client);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_NETWORK, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageNetwork>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessagePciConfig &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid) return false;
  // Reads are sync
  if (msg.type == MessagePciConfig::TYPE_READ) sync = MessageIOThread::SYNC_SYNC;
  MessagePciConfig *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessagePciConfig(msg.bdf);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_PCICFG, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessagePciConfig>(enq, sync);
  return true;
}

bool IOThread::enqueue(MessageHostOp &msg, MessageIOThread::Mode mode, MessageIOThread::Sync sync, unsigned *value, VCpu *vcpu) {
  if (pthread_self() == own_tid || msg.type != MessageHostOp::OP_VCPU_RELEASE) return false;
  MessageHostOp *ptr;
  if (sync == MessageIOThread::SYNC_ASYNC) {
    ptr = new MessageHostOp(msg.vcpu);
    memcpy(ptr, &msg, sizeof(msg));
  } else {
    ptr = &msg;
  }
  MessageIOThread enq(MessageIOThread::TYPE_HOSTOP, mode, sync, value, ptr);
  syncify_message(enq);
  this->enq(enq);
  sync_message<MessageHostOp>(enq, sync);
  return true;
}


void IOThread::worker() {
  own_tid = pthread_self();

  while (1) {
    sem_wait(&_block);
    pthread_mutex_lock(&_lock);

    MessageIOThread msg = _queue->front();
    _queue->pop();

    pthread_mutex_unlock(&_lock);

    // Send message on appropriate bus
    switch (msg.type) {
      case MessageIOThread::TYPE_DISK:
        {
          MessageDisk *msg2 = reinterpret_cast<MessageDisk*>(msg.ptr);
          _mb->bus_disk.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageDisk>(msg);
        }
        break;
      case MessageIOThread::TYPE_DISKCOMMIT:
        {
          MessageDiskCommit *msg2 = reinterpret_cast<MessageDiskCommit*>(msg.ptr);
          _mb->bus_diskcommit.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageDiskCommit>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIME:
        {
          MessageTime *msg2 = reinterpret_cast<MessageTime*>(msg.ptr);
          _mb->bus_time.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageTime>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIMER:
        {
          MessageTimer *msg2 = reinterpret_cast<MessageTimer*>(msg.ptr);
          _mb->bus_timer.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageTimer>(msg);
        }
        break;
      case MessageIOThread::TYPE_TIMEOUT:
        {
          MessageTimeout *msg2 = reinterpret_cast<MessageTimeout*>(msg.ptr);
          _mb->bus_timeout.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageTimeout>(msg);
        }
        break;
      case MessageIOThread::TYPE_IOOUT:
        {
          MessageIOOut *msg2 = reinterpret_cast<MessageIOOut*>(msg.ptr);
          _mb->bus_ioout.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageIOOut>(msg);
        }
        break;
      case MessageIOThread::TYPE_IOIN:
        {
          MessageIOIn *msg2 = reinterpret_cast<MessageIOIn*>(msg.ptr);
          _mb->bus_ioin.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageIOIn>(msg);
        }
        break;
      case MessageIOThread::TYPE_MEM:
        {
          MessageMem *msg2 = reinterpret_cast<MessageMem*>(msg.ptr);
          if (msg.vcpu) {
            msg.vcpu->mem.send_direct(*msg2, msg.mode, msg.value);
          } else
            _mb->bus_mem.send_direct(*msg2, msg.mode, msg.value);
          // Special case: delete saved value
          if (msg.sync == MessageIOThread::SYNC_ASYNC) delete msg2->ptr;
          sync_msg<MessageMem>(msg);
        }
        break;
      case MessageIOThread::TYPE_CPU:
        {
          CpuMessage *msg2 = reinterpret_cast<CpuMessage*>(msg.ptr);
          if (msg.vcpu)
            msg.vcpu->executor.send_direct(*msg2, msg.mode, msg.value);
          else
            Logging::panic("TYPE_CPU needs a vcpu pointer!\n");
          sync_msg<CpuMessage>(msg);
        }
        break;
      case MessageIOThread::TYPE_INPUT:
        {
          MessageInput *msg2 = reinterpret_cast<MessageInput*>(msg.ptr);
          _mb->bus_input.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageInput>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQLINES:
        {
          MessageIrqLines *msg2 = reinterpret_cast<MessageIrqLines*>(msg.ptr);
          _mb->bus_irqlines.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageIrqLines>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQNOTIFY:
        {
          MessageIrqNotify *msg2 = reinterpret_cast<MessageIrqNotify*>(msg.ptr);
          _mb->bus_irqnotify.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageIrqNotify>(msg);
        }
        break;
      case MessageIOThread::TYPE_IRQ:
        {
          MessageIrq *msg2 = reinterpret_cast<MessageIrq*>(msg.ptr);
          _mb->bus_hostirq.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageIrq>(msg);
        }
        break;
      case MessageIOThread::TYPE_LEGACY:
        {
          MessageLegacy *msg2 = reinterpret_cast<MessageLegacy*>(msg.ptr);
          _mb->bus_legacy.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageLegacy>(msg);
        }
        break;
      case MessageIOThread::TYPE_NETWORK:
        {
          MessageNetwork *msg2 = reinterpret_cast<MessageNetwork*>(msg.ptr);
          _mb->bus_network.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageNetwork>(msg);
        }
        break;
      case MessageIOThread::TYPE_PCICFG:
        {
          MessagePciConfig *msg2 = reinterpret_cast<MessagePciConfig*>(msg.ptr);
          _mb->bus_pcicfg.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessagePciConfig>(msg);
        }
        break;
      case MessageIOThread::TYPE_HOSTOP:
        {
          MessageHostOp *msg2 = reinterpret_cast<MessageHostOp*>(msg.ptr);
          _mb->bus_hostop.send_direct(*msg2, msg.mode, msg.value);
          sync_msg<MessageHostOp>(msg);
        }
        break;

      default: Logging::panic("Cannot handle type %x %x (size was %lx)!\n", msg.type, msg.mode, _queue->size());
    }
  }
}
