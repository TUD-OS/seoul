/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2007-2009, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
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

#include <kobj/Sm.h>
#include <CPU.h>

#include <service/profile.h>

#include "Timeouts.h"

using namespace nre;

void Timeouts::timer_thread(void*) {
    Timeouts *to = Thread::current()->get_tls<Timeouts*>(Thread::TLS_PARAM);
    Sm &sm = to->_timer.sm(CPU::current().log_id());
    while(1) {
        sm.down();
        COUNTER_INC("timer");
        to->trigger();
    }
}

void Timeouts::trigger() {
    ScopedLock<UserSm> guard(&globalsm);
    // TODO it can't be correct to not grab _sm here, because we might access stuff from
    // different threads here. but if we grab it here, we deadlock ourself because the devices
    // on the bus might call e.g. alloc().
    timevalue_t now = _mb.clock()->time();
    // Force time reprogramming. Otherwise, we might not reprogram a
    // timer, if the timeout event reached us too early.
    _last_to = NO_TIMEOUT;

    // trigger all timeouts that are due
    size_t nr;
    while((nr = _timeouts.trigger(now))) {
        MessageTimeout msg(nr, _timeouts.timeout());
        _timeouts.cancel(nr);
        _mb.bus_timeout.send(msg);
    }
    program();
}

void Timeouts::program() {
    if(_timeouts.timeout() != NO_TIMEOUT) {
        timevalue next_to = _timeouts.timeout();
        if(next_to != _last_to) {
            _last_to = next_to;
            _timer.program(next_to);
        }
    }
}
