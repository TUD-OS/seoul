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

#include "VCPUBackend.h"
#include "Vancouver.h"

using namespace nre;

typedef ::VCpu SeoulVCPU;

Motherboard *VCPUBackend::_mb = 0;
bool VCPUBackend::_tsc_offset = false;
bool VCPUBackend::_rdtsc_exit = false;
VCPUBackend::Portal VCPUBackend::_portals[] = {
    // the VMX portals
    {PT_VMX + 2,    vmx_triple,     Mtd::ALL},
    {PT_VMX + 3,    vmx_init,       Mtd::ALL},
    {PT_VMX + 7,    vmx_irqwin,     Mtd::IRQ},
    {PT_VMX + 10,   vmx_cpuid,      Mtd::RIP_LEN | Mtd::GPR_ACDB | Mtd::STATE | Mtd::CR | Mtd::IRQ},
    {PT_VMX + 12,   vmx_hlt,        Mtd::RIP_LEN | Mtd::IRQ},
    {PT_VMX + 16,   vmx_rdtsc,      Mtd::RIP_LEN | Mtd::GPR_ACDB | Mtd::TSC | Mtd::STATE},
    {PT_VMX + 18,   vmx_vmcall,     Mtd::RIP_LEN},
    {PT_VMX + 30,   vmx_ioio,       Mtd::RIP_LEN | Mtd::QUAL | Mtd::GPR_ACDB | Mtd::STATE | Mtd::RFLAGS},
    {PT_VMX + 31,   vmx_rdmsr,      Mtd::RIP_LEN | Mtd::GPR_ACDB | Mtd::TSC | Mtd::SYSENTER | Mtd::STATE},
    {PT_VMX + 32,   vmx_wrmsr,      Mtd::RIP_LEN | Mtd::GPR_ACDB | Mtd::SYSENTER | Mtd::STATE | Mtd::TSC},
    {PT_VMX + 33,   vmx_invalid,    Mtd::ALL},
    {PT_VMX + 40,   vmx_pause,      Mtd::RIP_LEN | Mtd::STATE},
    {PT_VMX + 48,   vmx_mmio,       Mtd::ALL},
    {PT_VMX + 0xfe, vmx_startup,    Mtd::IRQ},
#ifdef EXPERIMENTAL
    {PT_VMX + 0xff, do_recall,      Mtd::IRQ | Mtd::RIP_LEN | Mtd::GPR_BSD | Mtd::GPR_ACDB},
#else
    {PT_VMX + 0xff, do_recall,      Mtd::IRQ},
#endif
    // the SVM portals
    {PT_SVM + 0x64, svm_vintr,      Mtd::IRQ},
    {PT_SVM + 0x72, svm_cpuid,      Mtd::RIP_LEN | Mtd::GPR_ACDB | Mtd::IRQ | Mtd::CR},
    {PT_SVM + 0x78, svm_hlt,        Mtd::RIP_LEN | Mtd::IRQ},
    {PT_SVM + 0x7b, svm_ioio,       Mtd::RIP_LEN | Mtd::QUAL | Mtd::GPR_ACDB | Mtd::STATE},
    {PT_SVM + 0x7c, svm_msr,        Mtd::ALL},
    {PT_SVM + 0x7f, svm_shutdwn,    Mtd::ALL},
    {PT_SVM + 0xfc, svm_npt,        Mtd::ALL},
    {PT_SVM + 0xfd, svm_invalid,    Mtd::ALL},
    {PT_SVM + 0xfe, svm_startup,    Mtd::ALL},
    {PT_SVM + 0xff, svm_recall,     Mtd::IRQ},
};

capsel_t VCPUBackend::get_portals(bool use_svm) {
    capsel_t caps = CapSelSpace::get().allocate(0x100);
    for(size_t i = 0; i < ARRAY_SIZE(_portals); ++i) {
        // if VMX is used, just create the VMX-portals. the same for SVM
        if(use_svm == (_portals[i].offset < PT_SVM))
            continue;
        Pt *pt = new Pt(_ec, caps + (_portals[i].offset & 0xFF),
               reinterpret_cast<Pt::portal_func>(_portals[i].func), Mtd(_portals[i].mtd));
        pt->set_id(_portals[i].offset & 0xFF);
    }
    return caps;
}

void VCPUBackend::handle_io(bool is_in, unsigned io_order, unsigned port) {
    UtcbExcFrameRef uf;
    SeoulVCPU *vcpu = Thread::current()->get_tls<SeoulVCPU*>(Thread::TLS_PARAM);

    CpuMessage msg(is_in, reinterpret_cast<CpuState *>(Thread::current()->utcb()),
                   io_order, port, &uf->eax, uf->mtd);
    skip_instruction(msg);
    {
        ScopedLock<UserSm> guard(&globalsm);
        if(!vcpu->executor.send(msg, true))
            Util::panic("nobody to execute %s at %x:%x\n", __func__, msg.cpu->cs.sel, msg.cpu->eip);
    }
    /* TODO if(service_events && !msg.consumed)
       service_events->send_event(*utcb,EventsProtocol::EVENT_UNSERVED_IOACCESS,sizeof(port),
       &port);*/
}

void VCPUBackend::handle_vcpu(capsel_t pid, bool skip, CpuMessage::Type type) {
    UtcbExcFrameRef uf;
    SeoulVCPU *vcpu = Thread::current()->get_tls<SeoulVCPU*>(Thread::TLS_PARAM);

    CpuMessage msg(type, reinterpret_cast<CpuState*>(Thread::current()->utcb()), uf->mtd);
    if(skip)
        skip_instruction(msg);

    ScopedLock<UserSm> guard(&globalsm);

    /**
     * Send the message to the VCpu.
     */
    if(!vcpu->executor.send(msg, true))
        Util::panic("nobody to execute %s at %x:%x pid %d\n", __func__, msg.cpu->cs.sel, msg.cpu->eip,
                    pid);

    /**
     * Check whether we should inject something...
     */
    if((msg.mtr_in & Mtd::INJ) && msg.type != CpuMessage::TYPE_CHECK_IRQ) {
        msg.type = CpuMessage::TYPE_CHECK_IRQ;
        if(!vcpu->executor.send(msg, true))
            Util::panic("nobody to execute %s at %x:%x pid %d\n", __func__, msg.cpu->cs.sel,
                        msg.cpu->eip,
                        pid);
    }

    /**
     * If the IRQ injection is performed, recalc the IRQ window.
     */
    if(msg.mtr_out & Mtd::INJ) {
        vcpu->inj_count++;

        msg.type = CpuMessage::TYPE_CALC_IRQWINDOW;
        if(!vcpu->executor.send(msg, true))
            Util::panic("nobody to execute %s at %x:%x pid %d\n", __func__, msg.cpu->cs.sel,
                        msg.cpu->eip,
                        pid);
    }
    msg.cpu->mtd = msg.mtr_out;
}

Crd VCPUBackend::lookup(uintptr_t base, size_t size, uintptr_t hotspot) {
    Crd crd((base + hotspot) >> ExecEnv::PAGE_SHIFT, nre::Math::next_pow2_shift(size), Crd::MEM);
    Crd res = Syscalls::lookup(crd);
    if(res.is_null()) {
        // TODO perhaps dataspaces should provide something like touch?
        asm volatile ("lock orl $0, (%0)" : : "r" (base + hotspot) : "memory");
        res = Syscalls::lookup(crd);
        assert(!res.is_null());
    }

    // restrict it to a region that fits into [start, start+size)
    // XXX avoid the loop
    for(int i = res.order(); i >= 0; i--) {
        Crd x(((base + hotspot) & ~((1 << (i + ExecEnv::PAGE_SHIFT)) - 1)) >> ExecEnv::PAGE_SHIFT, i,
              res.attr());
        uintptr_t start = x.offset() << ExecEnv::PAGE_SHIFT;
        if((start >= base) && (start + (1 << (ExecEnv::PAGE_SHIFT + x.order()))) <= (base + size))
            return x;
    }
    return res;
}

bool VCPUBackend::handle_memory(bool need_unmap) {
    UtcbExcFrameRef uf;
    SeoulVCPU *vcpu = Thread::current()->get_tls<SeoulVCPU*>(Thread::TLS_PARAM);
    //Serial::get().writef("NPT fault @ %p for %#Lx, error %#Lx\n",uf->eip,uf->qual[1],uf->qual[0]);

    MessageMemRegion msg(uf->qual[1] >> ExecEnv::PAGE_SHIFT);

    // XXX use a push model on _startup instead
    // do we have not mapped physram yet?
    if(_mb->bus_memregion.send(msg, true) && msg.ptr) {
        uintptr_t hostaddr = reinterpret_cast<uintptr_t>(msg.ptr);
        uintptr_t guestbase = msg.start_page << ExecEnv::PAGE_SHIFT;
        uintptr_t hotspot = uf->qual[1] - guestbase;
        Crd own = lookup(hostaddr, msg.count << ExecEnv::PAGE_SHIFT, hotspot);

        if(need_unmap)
            CapRange(own.offset(), 1 << own.order(), Crd::MEM_ALL).revoke(false);

        uf->mtd = 0;
        uintptr_t delhot =
            (guestbase + (own.offset() << ExecEnv::PAGE_SHIFT) - hostaddr) >> ExecEnv::PAGE_SHIFT;
        CapRange range(own.offset(), 1 << own.order(), Crd::MEM_ALL, delhot);
        //Serial::get() << "Mapping " << range << "\n";
        uf.delegate(range, UtcbFrame::UPD_GPT);
        // TODO (_dpci ? MAP_DPT : 0)

        // EPT violation during IDT vectoring?
        if(uf->inj_info & 0x80000000) {
            uf->mtd |= Mtd::INJ;
            CpuMessage msg(CpuMessage::TYPE_CALC_IRQWINDOW,
                           reinterpret_cast<CpuState*>(Thread::current()->utcb()), uf->mtd);
            msg.mtr_out = Mtd::INJ;
            if(!vcpu->executor.send(msg, true))
                Util::panic("nobody to execute %s at %x:%x\n", __func__, uf->cs.sel, uf->eip);
        }
        return true;
    }
    return false;
}

void VCPUBackend::force_invalid_gueststate_amd(UtcbExcFrameRef &uf) {
    uf->ctrl[1] = 0;
    uf->mtd = Mtd::CTRL;
}

void VCPUBackend::force_invalid_gueststate_intel(UtcbExcFrameRef &uf) {
    assert(uf->mtd & Mtd::RFLAGS);
    uf->efl &= ~2;
    uf->mtd = Mtd::RFLAGS;
}

void VCPUBackend::skip_instruction(CpuMessage &msg) {
    // advance EIP
    assert(msg.mtr_in & Mtd::RIP_LEN);
    msg.cpu->eip += msg.cpu->inst_len;
    msg.mtr_out |= Mtd::RIP_LEN;

    // cancel sti and mov-ss blocking as we emulated an instruction
    assert(msg.mtr_in & Mtd::STATE);
    if(msg.cpu->intr_state & 3) {
        msg.cpu->intr_state &= ~3;
        msg.mtr_out |= Mtd::STATE;
    }
}

void VCPUBackend::vmx_triple(capsel_t pid) {
    handle_vcpu(pid, false, CpuMessage::TYPE_TRIPLE);
}
void VCPUBackend::vmx_init(capsel_t pid) {
    handle_vcpu(pid, false, CpuMessage::TYPE_INIT);
}
void VCPUBackend::vmx_irqwin(capsel_t pid) {
    COUNTER_INC("irqwin");
    handle_vcpu(pid, false, CpuMessage::TYPE_CHECK_IRQ);
}
void VCPUBackend::vmx_cpuid(capsel_t pid) {
    COUNTER_INC("cpuid");
    bool res = false;
    /* TODO if(_donor_net)
       res = handle_donor_request(utcb,pid,tls);*/
    if(!res)
        handle_vcpu(pid, true, CpuMessage::TYPE_CPUID);
}
void VCPUBackend::vmx_hlt(capsel_t pid) {
    handle_vcpu(pid, true, CpuMessage::TYPE_HLT);
}
void VCPUBackend::vmx_rdtsc(capsel_t pid) {
    COUNTER_INC("rdtsc");
    handle_vcpu(pid, true, CpuMessage::TYPE_RDTSC);
}
void VCPUBackend::vmx_vmcall(capsel_t) {
    UtcbExcFrameRef uf;
    uf->eip += uf->inst_len;
}
void VCPUBackend::vmx_ioio(capsel_t) {
    UtcbExcFrameRef uf;
    if(uf->qual[0] & 0x10) {
        COUNTER_INC("IOS");
        force_invalid_gueststate_intel(uf);
    }
    else {
        unsigned order = uf->qual[0] & 7;
        if(order > 2)
            order = 2;
        handle_io(uf->qual[0] & 8, order, uf->qual[0] >> 16);
    }
}
void VCPUBackend::vmx_rdmsr(capsel_t pid) {
    COUNTER_INC("rdmsr");
    handle_vcpu(pid, true, CpuMessage::TYPE_RDMSR);
}
void VCPUBackend::vmx_wrmsr(capsel_t pid) {
    COUNTER_INC("wrmsr");
    handle_vcpu(pid, true, CpuMessage::TYPE_WRMSR);
}
void VCPUBackend::vmx_invalid(capsel_t pid) {
    UtcbExcFrameRef uf;
    uf->efl |= 2;
    handle_vcpu(pid, false, CpuMessage::TYPE_SINGLE_STEP);
    uf->mtd |= Mtd::RFLAGS;
}
void VCPUBackend::vmx_pause(capsel_t) {
    UtcbExcFrameRef uf;
    CpuMessage msg(CpuMessage::TYPE_SINGLE_STEP, reinterpret_cast<CpuState*>(Thread::current()->utcb()),
                   uf->mtd);
    skip_instruction(msg);
    COUNTER_INC("pause");
}
void VCPUBackend::vmx_mmio(capsel_t pid) {
    UtcbExcFrameRef uf;
    COUNTER_INC("MMIO");
    /**
     * Idea: optimize the default case - mmio to general purpose register
     * Need state: GPR_ACDB, GPR_BSD, RIP_LEN, RFLAGS, CS, DS, SS, ES, RSP, CR, EFER
     */
    if(!handle_memory(uf->qual[0] & 0x38))
        // this is an access to MMIO
        handle_vcpu(pid, false, CpuMessage::TYPE_SINGLE_STEP);
}
void VCPUBackend::vmx_startup(capsel_t pid) {
    UtcbExcFrameRef uf;
    Serial::get() << "startup\n";
    handle_vcpu(pid, false, CpuMessage::TYPE_HLT);
    uf->mtd |= Mtd::CTRL;
    uf->ctrl[0] = 0;
    if(_tsc_offset)
        uf->ctrl[0] |= (1 << 3 /* tscoff */);
    if(_rdtsc_exit)
        uf->ctrl[0] |= (1 << 12 /* rdtsc */);
    uf->ctrl[1] = 0;
}
void VCPUBackend::do_recall(capsel_t pid) {
    UtcbExcFrameRef uf;
    COUNTER_INC("recall");
    COUNTER_SET("REIP", uf->eip);
    handle_vcpu(pid, false, CpuMessage::TYPE_CHECK_IRQ);
}

void VCPUBackend::svm_vintr(capsel_t pid) {
    vmx_irqwin(pid);
}
void VCPUBackend::svm_cpuid(capsel_t pid) {
    UtcbExcFrameRef uf;
    uf->inst_len = 2;
    vmx_cpuid(pid);
}
void VCPUBackend::svm_hlt(capsel_t pid) {
    UtcbExcFrameRef uf;
    uf->inst_len = 1;
    vmx_hlt(pid);
}
void VCPUBackend::svm_ioio(capsel_t) {
    UtcbExcFrameRef uf;
    if(uf->qual[0] & 0x4) {
        COUNTER_INC("IOS");
        force_invalid_gueststate_amd(uf);
    }
    else {
        unsigned order = ((uf->qual[0] >> 4) & 7) - 1;
        if(order > 2)
            order = 2;
        uf->inst_len = uf->qual[1] - uf->eip;
        handle_io(uf->qual[0] & 1, order, uf->qual[0] >> 16);
    }
}
void VCPUBackend::svm_msr(capsel_t pid) {
    svm_invalid(pid);
}
void VCPUBackend::svm_shutdwn(capsel_t pid) {
    vmx_triple(pid);
}
void VCPUBackend::svm_npt(capsel_t pid) {
    UtcbExcFrameRef uf;
    if(!handle_memory(uf->qual[0] & 1))
        svm_invalid(pid);
}
void VCPUBackend::svm_invalid(capsel_t pid) {
    UtcbExcFrameRef uf;
    COUNTER_INC("invalid");
    handle_vcpu(pid, false, CpuMessage::TYPE_SINGLE_STEP);
    uf->mtd |= Mtd::CTRL;
    uf->ctrl[0] = 1 << 18; // cpuid
    uf->ctrl[1] = 1 << 0; // vmrun
}
void VCPUBackend::svm_startup(capsel_t pid) {
    vmx_irqwin(pid);
}
void VCPUBackend::svm_recall(capsel_t pid) {
    do_recall(pid);
}
