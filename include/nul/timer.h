/** @file
 * Timer infrastucture.
 *
 * Copyright (C) 2007-2008, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2013 Jacek Galowicz, Intel Corporation.
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
#include "service/cpu.h"
#include "service/math.h"

#include <nul/message.h>
#include <nul/bus.h>
#include <nul/templates.h>


typedef unsigned long long timevalue;

/**
 * A clock returns the time in different time domains.
 *
 * The reference clock is the CPUs TSC.
 */
class Clock
{
 protected:
  timevalue _source_freq;
 public:
#ifdef TESTING
  virtual
#endif
  timevalue time() { return Cpu::rdtsc(); }

  /**
   * Returns the current clock in freq-time.
   */
  timevalue clock(timevalue freq, timevalue t_cur = 0) { return Math::muldiv128(t_cur == 0 ? time() : t_cur, freq, _source_freq); }

  /**
   * Frequency of the clock.
   */
  timevalue freq() { return _source_freq; }

  /**
   * Returns a timeout in absolute TSC time.
   * @param thedelta The timeout
   * @param freq Scale of @a thedelta
   *
   * The timeout equals to thedelta/freq seconds.
   *
   * Example: abstime(5, 1000) returns the time of now plus 5 milliseconds.
   */
  timevalue abstime(timevalue thedelta, timevalue freq) {  return time() + Math::muldiv128(thedelta, _source_freq, freq); }


  /**
   * Returns a delta in another frequency domain from an absolute TSC value.
   */
  timevalue delta(timevalue theabstime, timevalue freq) {
    timevalue now = time();
    if (now > theabstime) return 0;
    return Math::muldiv128(theabstime - now, freq, _source_freq);
  }

  Clock(timevalue source_freq) : _source_freq(source_freq) {}
};




/**
 * Keeping track of the timeouts.
 */
template <unsigned ENTRIES, typename DATA>
class TimeoutList : public StaticReceiver<TimeoutList<ENTRIES, DATA>>
{
  class TimeoutEntry
  {
    friend class TimeoutList<ENTRIES, DATA>;
    TimeoutEntry *_next;
    TimeoutEntry *_prev;
    timevalue _timeout;
    DATA * data;
    bool      _free;
  };

  TimeoutEntry  _entries[ENTRIES];

  bool _restore_processed;
public:
  /**
   * Alloc a new timeout object.
   */
  unsigned alloc(DATA * _data = 0)
  {
    unsigned i;
    for (i=1; i < ENTRIES; i++) {
      if (not _entries[i]._free) continue;
      _entries[i].data  = _data;
      _entries[i]._free = false;
      return i;
    }
    Logging::panic("Can't alloc a timer!\n");
    return 0;
  }

  /**
   * Dealloc a timeout object.
   */
  unsigned dealloc(unsigned nr, bool withcancel = false) {
    if (!nr || nr > ENTRIES - 1) return 0;
    if (_entries[nr]._free) return 0;

    // should only be done when no no concurrent access happens ...
    if (withcancel) cancel(nr);
    _entries[nr]._free = true;
    _entries[nr].data = 0;
    return 1;
  }

  /**
   * Cancel a programmed timeout.
   */
  int cancel(unsigned nr)
  {
    if (!nr || nr >= ENTRIES)  return -1;
    TimeoutEntry *current = _entries+nr;
    if (current->_next == current) return -2;
    int res = _entries[0]._next != current;

    current->_next->_prev =  current->_prev;
    current->_prev->_next =  current->_next;
    current->_next = current->_prev = current;
    return res;
  }


  /**
   * Request a new timeout.
   */
  int request(unsigned nr, timevalue to)
  {
    if (!nr || nr > ENTRIES)  return -1;
    timevalue old = timeout();
    TimeoutEntry *current = _entries + nr;
    cancel(nr);

    // keep a sorted list here
    TimeoutEntry *t = _entries;
    do { t = t->_next; }  while (t->_timeout < to);

    current->_timeout = to;
    current->_next = t;
    current->_prev = t->_prev;
    t->_prev->_next = current;
    t->_prev = current;
    return timeout() == old;
  }

  /**
   * Get the head of the queue.
   */
  unsigned  trigger(timevalue now, DATA ** data = 0) {
    if (now >= timeout()) {
      unsigned i = _entries[0]._next - _entries;
      if (data)
        *data = _entries[i].data;
      return i;
    }
    return 0;
  }

  timevalue timeout() { assert(_entries[0]._next); return _entries[0]._next->_timeout; }
  void init()
  {
    for (unsigned i = 0; i < ENTRIES; i++)
      {
        _entries[i]._prev = _entries + i;
        _entries[i]._next = _entries + i;
        _entries[i].data  = 0;
        _entries[i]._free = true;
      }
    _entries[0]._timeout = ~0ULL;
  }

  TimeoutList() : _restore_processed(false) { init(); }

#define REL_PTR(ptr, offset) ( \
    reinterpret_cast<__typeof__(ptr)>( \
        reinterpret_cast<mword>(ptr) - reinterpret_cast<mword>(offset)) \
)
#define ABS_PTR(ptr, offset) ( \
    reinterpret_cast<__typeof__(ptr)>( \
        reinterpret_cast<mword>(ptr) + reinterpret_cast<mword>(offset)) \
)

  bool receive(MessageRestore &msg)
  {
      const mword bytes = reinterpret_cast<mword>(&_restore_processed)
          - reinterpret_cast<mword>(_entries);

      if (msg.devtype == MessageRestore::RESTORE_RESTART) {
          _restore_processed = false;
          msg.bytes += bytes + sizeof(msg);
          return false;
      }

      if (msg.devtype != MessageRestore::RESTORE_TIMEOUTLIST || _restore_processed) return false;

      unsigned long long rdtsc = Cpu::rdtsc();

      if (msg.write) {
          msg.bytes = bytes;
          memcpy(msg.space, reinterpret_cast<void*>(_entries), bytes);

          // Do not mess around with timeout entries of the running guest,
          // since we may want to let it continue after saving
          TimeoutEntry *entries = reinterpret_cast<TimeoutEntry*>(msg.space);
          for (unsigned i=0; i < ENTRIES; i++) {
              entries[i]._prev = REL_PTR(entries[i]._prev, _entries);
              entries[i]._next = REL_PTR(entries[i]._next, _entries);

              if (i == 0) continue;

              if (entries[i]._timeout <= rdtsc)
                  entries[i]._timeout = 0;
              else
                  entries[i]._timeout -= rdtsc;
          }
      }
      else {
          memcpy(reinterpret_cast<void*>(_entries), msg.space, bytes);
          for (unsigned i=0; i < ENTRIES; i++) {
              _entries[i]._prev = ABS_PTR(_entries[i]._prev, _entries);
              _entries[i]._next = ABS_PTR(_entries[i]._next, _entries);

              if (i == 0) continue;
              _entries[i]._timeout += rdtsc;
          }
      }

      //Logging::printf("%s Timeoutlist\n", msg.write ? "Saved" : "Restored");
      _restore_processed = true;
      return true;
  }
};
