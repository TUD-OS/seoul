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

#pragma once

#include <kobj/UserSm.h>
#include <kobj/Thread.h>
#include <kobj/Sm.h>
#include <kobj/Pt.h>
#include <kobj/VCpu.h>
#include <kobj/Sc.h>
#include <utcb/UtcbFrame.h>
#include <collection/SList.h>
#include <Assert.h>
#include <Compiler.h>

#include <nul/motherboard.h>
#include <nul/vcpu.h>
#include <service/profile.h>

class VCPUBackend : public nre::SListItem {
    enum {
        PT_VMX = 0x100,
        PT_SVM = 0x200
    };
    struct Portal {
        capsel_t offset;
        PORTAL void (*func)(capsel_t);
        uint mtd;
    };

public:
    VCPUBackend(Motherboard *mb, VCpu *vcpu, bool use_svm, cpu_t cpu)
        : SListItem(), _ec(nre::LocalThread::create(cpu)), _caps(get_portals(use_svm)), _sm(0),
          _vcpu(cpu, _caps, "vmm-vcpu") {
        _ec->set_tls<VCpu*>(nre::Thread::TLS_PARAM, vcpu);
        _vcpu.start();
        _mb = mb;
    }

    nre::VCpu &vcpu() {
        return _vcpu;
    }
    nre::Sm &sm() {
        return _sm;
    }

private:
    capsel_t get_portals(bool use_svm);

    static void handle_io(bool is_in, unsigned io_order, unsigned port);
    static void handle_vcpu(capsel_t pid, bool skip, CpuMessage::Type type);
    static nre::Crd lookup(uintptr_t base, size_t size, uintptr_t hotspot);
    static bool handle_memory(bool need_unmap);

    static void force_invalid_gueststate_amd(nre::UtcbExcFrameRef &uf);
    static void force_invalid_gueststate_intel(nre::UtcbExcFrameRef &uf);
    static void skip_instruction(CpuMessage &msg);

    PORTAL static void vmx_triple(capsel_t pid);
    PORTAL static void vmx_init(capsel_t pid);
    PORTAL static void vmx_irqwin(capsel_t pid);
    PORTAL static void vmx_cpuid(capsel_t pid);
    PORTAL static void vmx_hlt(capsel_t pid);
    PORTAL static void vmx_rdtsc(capsel_t pid);
    PORTAL static void vmx_vmcall(capsel_t pid);
    PORTAL static void vmx_ioio(capsel_t pid);
    PORTAL static void vmx_rdmsr(capsel_t pid);
    PORTAL static void vmx_wrmsr(capsel_t pid);
    PORTAL static void vmx_invalid(capsel_t pid);
    PORTAL static void vmx_pause(capsel_t pid);
    PORTAL static void vmx_mmio(capsel_t pid);
    PORTAL static void vmx_startup(capsel_t pid);
    PORTAL static void do_recall(capsel_t pid);

    PORTAL static void svm_vintr(capsel_t pid);
    PORTAL static void svm_cpuid(capsel_t pid);
    PORTAL static void svm_hlt(capsel_t pid);
    PORTAL static void svm_ioio(capsel_t pid);
    PORTAL static void svm_msr(capsel_t pid);
    PORTAL static void svm_shutdwn(capsel_t pid);
    PORTAL static void svm_npt(capsel_t pid);
    PORTAL static void svm_invalid(capsel_t pid);
    PORTAL static void svm_startup(capsel_t pid);
    PORTAL static void svm_recall(capsel_t pid);

    nre::LocalThread *_ec;
    capsel_t _caps;
    nre::Sm _sm;
    nre::VCpu _vcpu;
    static Motherboard *_mb;
    static bool _tsc_offset;
    static bool _rdtsc_exit;
    static Portal _portals[];
};
