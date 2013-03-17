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

#pragma once

#include <nul/message.h>
#include <kobj/GlobalThread.h>
#include <CPU.h>
#include <Exception.h>

class Vancouver;

class ConsoleBackend {
public:
    static const size_t MAX_VIEWS = 4;

private:
    struct ConsoleView {
        explicit ConsoleView() : name(), ptr(), size(), regs() {
        }
        explicit ConsoleView(const char *name, const char *ptr, size_t size, VgaRegs *regs)
            : name(name), ptr(ptr), size(size), regs(regs) {
        }

        const char *name;
        const char *ptr;
        size_t size;
        VgaRegs *regs;
    };

public:
    explicit ConsoleBackend(Vancouver *vc, size_t max_size)
            : _current(MAX_VIEWS), _max_size(max_size), _views(), _vc(vc) {
        nre::GlobalThread *gt = nre::GlobalThread::create(
            thread, nre::CPU::current().log_id(), "vmm-console");
        gt->set_tls<ConsoleBackend*>(nre::Thread::TLS_PARAM, this);
        gt->start();
    }

    size_t add_view(const char *name, const char *ptr, size_t size, VgaRegs *regs) {
        size_t view = get_free();
        _views[view] = ConsoleView(name, ptr, size, regs);
        return view;
    }
    void set_view(size_t view) {
        _current = view;
    }

private:
    size_t get_free() const {
        for(size_t i = 0; i < MAX_VIEWS; ++i) {
            if(_views[i].ptr == nullptr)
                return i;
        }
        VTHROW(Exception, E_CAPACITY, "All console views in use");
        return MAX_VIEWS;
    }

    static void thread(void*);

    size_t _current;
    size_t _max_size;
    ConsoleView _views[MAX_VIEWS];
    Vancouver *_vc;
};
