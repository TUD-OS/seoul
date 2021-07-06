/** @file
 * Bus infrastucture and generic Device class.
 *
 * Copyright (C) 2007-2009, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2013 Markus Partheymueller, Intel Corporation.
 *
 * This file is part of Vancouver.
 *
 * Vancouver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vancouver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */
#pragma once

#include "message.h"
#include "service/logging.h"
#include "service/string.h"

/**
 * The generic Device used in generic bus transactions.
 */
class Device
{
  const char *_debug_name;
public:
  void debug_dump() {
    Logging::printf("\t%s\n", _debug_name);
  }
  Device(const char *debug_name) :_debug_name(debug_name) {}
};


/**
 * A bus is a way to connect devices.
 */
template <class M>
class DBus
{
  typedef bool (*ReceiveFunction)(Device *, M&);
  typedef bool (*EnqueueFunction)(Device *, M&, MessageIOThread::Mode, MessageIOThread::Sync, unsigned*, VCpu *vcpu);
  struct Entry
  {
    Device *_dev;
    ReceiveFunction _func;
  };
  struct EnqEntry
  {
    Device *_dev;
    VCpu *_vcpu;
    EnqueueFunction _func;
  };

  unsigned long _debug_counter;
  unsigned _list_count;
  unsigned _list_size;
  struct Entry *_list;

  unsigned _callback_count;
  unsigned _callback_size;
  struct Entry *_iothread_callback;
  struct EnqEntry *_iothread_enqueue;

  /**
   * To avoid bugs we disallow the copy constuctor.
   */
  DBus(const DBus<M> &) { Logging::panic("%s copy constructor called", __func__); }

  void set_size(unsigned new_size)
  {
    Entry *n = new Entry[new_size];
    memcpy(n, _list, _list_count * sizeof(*_list));
    if (_list)  delete [] _list;
    _list = n;
    _list_size = new_size;
  };
  void set_callback_size(unsigned new_size)
  {
    Entry *n = new Entry[new_size];
    memcpy(n, _iothread_callback, _callback_count * sizeof(*_iothread_callback));
    if (_iothread_callback)  delete [] _iothread_callback;
    _iothread_callback = n;
    _callback_size = new_size;
  };
public:

  void add(Device *dev, ReceiveFunction func)
  {
    if (_list_count >= _list_size)
      set_size(_list_size > 0 ? _list_size * 2 : 1);
    _list[_list_count]._dev    = dev;
    _list[_list_count]._func = func;
    _list_count++;
  }

  void add_iothread_callback(Device *dev, ReceiveFunction func)
  {
    if (_callback_count >= _callback_size)
      set_callback_size(_callback_size > 0 ? _callback_size * 2 : 1);
    _iothread_callback[_callback_count]._dev    = dev;
    _iothread_callback[_callback_count]._func = func;
    _callback_count++;
  }

  void set_iothread_enqueue(Device *dev, EnqueueFunction func, VCpu *vcpu=nullptr)
  {
    if (_iothread_enqueue == nullptr) {
      delete [] _iothread_enqueue;
      _iothread_enqueue = new EnqEntry;
    }
    _iothread_enqueue->_dev = dev;
    _iothread_enqueue->_vcpu = vcpu;
    _iothread_enqueue->_func = func;
  }

  /**
   * Send message directly.
   */
  bool  send_direct_fifo(M &msg)
  {
    _debug_counter++;
    bool res = false;
    for (unsigned i = 0; i < _list_count; i++)
      res |= _list[i]._func(_list[i]._dev, msg);
    return res;
  }
  bool  send_direct_rr(M &msg, unsigned *value) {
    for (unsigned i = 0; i < _list_count; i++)
      if (_list[i]._func(_list[(i + *value) % _list_count]._dev, msg)) {
	*value = (i + *value + 1) % _list_count;
	return true;
      }
    return false;
  }
  bool  send_direct(M &msg, MessageIOThread::Mode mode, unsigned *value=nullptr)
  {
    if (mode == MessageIOThread::MODE_FIFO) return send_direct_fifo(msg);
    if (mode == MessageIOThread::MODE_RR) return send_direct_rr(msg, value);

    _debug_counter++;
    bool res = false;
    bool earlyout = (mode == MessageIOThread::MODE_EARLYOUT);
    for (unsigned i = _list_count; i-- && !(earlyout && res);)
      res |= _list[i]._func(_list[i]._dev, msg);
    return res;
  }

  /**
   * Send message LIFO synchronously.
   */
  bool  send_sync(M &msg, bool earlyout = false)
  {
    bool res = false;
    if (_iothread_callback) {
      for (unsigned i = _callback_count; i-- && !res;) {
        res |= _iothread_callback[i]._func(_iothread_callback[i]._dev, msg);
      }
    }
    if (!res && _iothread_enqueue != nullptr) {
      // No one wants the message directly, enqueue it.
      if (_iothread_enqueue->_func(_iothread_enqueue->_dev, msg, earlyout ? MessageIOThread::MODE_EARLYOUT : MessageIOThread::MODE_NORMAL, MessageIOThread::SYNC_SYNC, nullptr, _iothread_enqueue->_vcpu))
        return true;
    }
    _debug_counter++;
    res = false;
    for (unsigned i = _list_count; i-- && !(earlyout && res);)
      res |= _list[i]._func(_list[i]._dev, msg);
    return res;
  }

  /**
   * Send message LIFO asynchronously.
   */
  bool  send(M &msg, bool earlyout = false)
  {
    bool res = false;
    if (_iothread_callback) {
      for (unsigned i = _callback_count; i-- && !res;) {
        res |= _iothread_callback[i]._func(_iothread_callback[i]._dev, msg);
      }
    }
    if (!res && _iothread_enqueue != nullptr) {
      // No one wants the message directly, enqueue it.
      if (_iothread_enqueue->_func(_iothread_enqueue->_dev, msg, earlyout ? MessageIOThread::MODE_EARLYOUT : MessageIOThread::MODE_NORMAL, MessageIOThread::SYNC_ASYNC, nullptr, _iothread_enqueue->_vcpu))
        return true;
    }
    _debug_counter++;
    res = false;
    for (unsigned i = _list_count; i-- && !(earlyout && res);)
      res |= _list[i]._func(_list[i]._dev, msg);
    return res;
  }

  /**
   * Send message in FIFO order
   */
  bool  send_fifo(M &msg)
  {
    bool res = false;
    if (_iothread_callback) {
      for (unsigned i = _callback_count; i-- && !res;) {
        res |= _iothread_callback[i]._func(_iothread_callback[i]._dev, msg);
      }
    }
    if (!res && _iothread_enqueue != nullptr) {
      // No one wants the message directly, enqueue it.
      if (_iothread_enqueue->_func(_iothread_enqueue->_dev, msg, MessageIOThread::MODE_FIFO, MessageIOThread::SYNC_ASYNC, nullptr, _iothread_enqueue->_vcpu))
        return true;
    }
    _debug_counter++;
    res = false;
    for (unsigned i = 0; i < _list_count; i++)
      res |= _list[i]._func(_list[i]._dev, msg);
    return 0;
  }


  /**
   * Send message first hit round robin and return the number of the
   * next one that accepted the message.
   */
  bool  send_rr(M &msg, unsigned &start)
  {
    bool res = false;
    if (_iothread_callback) {
      for (unsigned i = _callback_count; i-- && !res;) {
        res |= _iothread_callback[i]._func(_iothread_callback[i]._dev, msg);
      }
    }
    if (!res && _iothread_enqueue != nullptr) {
      // No one wants the message directly, enqueue it.
      if (_iothread_enqueue->_func(_iothread_enqueue->_dev, msg, MessageIOThread::MODE_RR, MessageIOThread::SYNC_ASYNC, &start, _iothread_enqueue->_vcpu))
        return true;
    }
    _debug_counter++;
    for (unsigned i = 0; i < _list_count; i++)
      if (_list[i]._func(_list[(i + start) % _list_count]._dev, msg)) {
	start = (i + start + 1) % _list_count;
	return true;
      }
    return false;
  }



  /**
   * Return the number of entries in the list.
   */
  unsigned count() { return _list_count; };

  /**
   * Debugging output.
   */
  void debug_dump()
  {
    Logging::printf("%s: Bus used %ld times.", __PRETTY_FUNCTION__, _debug_counter);
    for (unsigned i = 0; i < _list_count; i++)
      {
	Logging::printf("\n%2d:\t", i);
	_list[i]._dev->debug_dump();
      }
    Logging::printf("\n");
  }

  /** Default constructor. */
  DBus() : _debug_counter(0), _list_count(0), _list_size(0), _list(nullptr), _callback_count(0), _callback_size(0), _iothread_callback(nullptr), _iothread_enqueue(nullptr) {}
};
