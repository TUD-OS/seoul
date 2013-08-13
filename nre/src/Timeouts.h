/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <kobj/UserSm.h>
#include <kobj/GlobalThread.h>
#include <kobj/Sc.h>
#include <services/Timer.h>
#include <util/TimeoutList.h>
#include <util/ScopedLock.h>

#include <nul/motherboard.h>

extern nre::UserSm globalsm;

class Timeouts {
    enum {
        NO_TIMEOUT  = ~0ULL
    };

public:
    Timeouts(Motherboard &mb, cpu_t cpu)
        : _mb(mb), _cpu(cpu), _sm(), _timeouts(), _timer("timer"), _last_to(NO_TIMEOUT) {
        nre::Reference<nre::GlobalThread> gt = nre::GlobalThread::create(
            timer_thread, _cpu, "vmm-timeouts");
        gt->set_tls<Timeouts*>(nre::Thread::TLS_PARAM, this);
        gt->start();
    }

    nre::TimerSession &session() {
        return _timer;
    }

    size_t alloc() {
        nre::ScopedLock<nre::UserSm> guard(&_sm);
        return _timeouts.alloc();
    }

    void request(size_t nr, timevalue_t to) {
        nre::ScopedLock<nre::UserSm> guard(&_sm);
        _timeouts.request(nr, to);
        program();
    }

    void time(timevalue_t &uptime, timevalue_t &unixtime) {
        _timer.get_time(uptime, unixtime);
    }

private:
    NORETURN static void timer_thread(void*);
    void trigger();
    void program();

    Motherboard &_mb;
    cpu_t _cpu;
    nre::UserSm _sm;
    nre::TimeoutList<64, void> _timeouts;
    nre::TimerSession _timer;
    timevalue_t _last_to;
};
