/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <services/Timer.h>
#include <services/Console.h>
#include <util/Clock.h>
#include <util/Math.h>

#include "ConsoleBackend.h"
#include "Vancouver.h"

using namespace nre;

void ConsoleBackend::thread(void*) {
    ConsoleBackend *c = Thread::current()->get_tls<ConsoleBackend*>(Thread::TLS_PARAM);
    TimerSession timer("timer");
    ConsoleSession &cons = c->_vc->console();
    nre::Clock clock(1000);
    nre::Console::ModeInfo modeinfo;
    size_t mode = 0;
    memset(&modeinfo, 0, sizeof(modeinfo));
    if(!cons.get_mode_info(mode, modeinfo))
        VTHROW(Exception, E_FAILURE, "Mode info failed");
    while(1) {
        if(c->_current != MAX_VIEWS) {
            ConsoleView &view = c->_views[c->_current];
            nre::Console::Register regs;
            regs.mode = view.regs->mode;
            regs.cursor_pos = view.regs->cursor_pos;
            regs.cursor_style = view.regs->cursor_style;
            regs.offset = view.regs->offset;
            if(mode != regs.mode) {
                if(!cons.get_mode_info(regs.mode, modeinfo))
                    VTHROW(Exception, E_FAILURE, "Mode info failed");
                size_t size = modeinfo.resolution[0] * modeinfo.resolution[1] * (modeinfo.bpp / 8);
                cons.set_mode(regs.mode, size);
                mode = regs.mode;
            }
            cons.set_regs(regs);

            size_t offset = mode == 0 ? (regs.offset << 1) : 0;
            size_t size = nre::Math::min<size_t>(c->_max_size,
                    modeinfo.resolution[0] * modeinfo.resolution[1] * (modeinfo.bpp / 8));
            memcpy(reinterpret_cast<char*>(cons.screen().virt()) + offset, view.ptr + offset, size);
        }
        timer.wait_until(clock.source_time(25, 1000));
    }
}
