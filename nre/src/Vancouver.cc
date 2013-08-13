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
#include <kobj/Ports.h>
#include <services/Reboot.h>
#include <stream/IStringStream.h>
#include <stream/VGAStream.h>
#include <util/TimeoutList.h>
#include <util/Util.h>

#include <nul/motherboard.h>
#include <nul/vcpu.h>
#include <service/params.h>

#include "Vancouver.h"
#include "Timeouts.h"
#include "VCPUBackend.h"

using namespace nre;

static bool initialized = false;
static size_t ncpu = 1;
static DataSpace *guest_mem = nullptr;
static size_t guest_size = 0;
static size_t console = 1;
static String constitle("VM");
nre::UserSm globalsm(0);

PARAM_HANDLER(PC_PS2, "an alias to create an PS2 compatible PC") {
    static const char *pcps2_params[] = {
        "mem:0,0xa0000", "mem:0x100000", "ioio", "nullio:0x80", "pic:0x20,,0x4d0", "pic:0xa0,2,0x4d1",
        "pit:0x40,0", "scp:0x92,0x61", "kbc:0x60,1,12", "keyb:0,0x10000", "mouse:1,0x10001",
        "rtc:0x70,8", "serial:0x3f8,0x4,0x4711", "hostsink:0x4712,80", "vga:0x03c0",
        "vbios_disk", "vbios_keyboard", "vbios_mem", "vbios_time", "vbios_reset", "vbios_multiboot",
        "msi", "ioapic", "pcihostbridge:0,0x10,0xcf8,0xe0000000", "pmtimer:0x8000", "vcpus",
    };
    for(size_t i = 0; i < ARRAY_SIZE(pcps2_params); ++i)
        mb.handle_arg(pcps2_params[i]);
}

PARAM_HANDLER(ncpu, "ncpu - change the number of vcpus that are created") {
    ncpu = argv[0];
}
PARAM_HANDLER(m, "m - specify the amount of memory for the guest in MiB") {
    guest_size = argv[0] * 1024 * 1024;
    guest_mem = new DataSpace(guest_size, DataSpaceDesc::ANONYMOUS,
                              DataSpaceDesc::RWX | DataSpaceDesc::BIGPAGES, 0, 0,
                              nre::Math::next_pow2_shift(ExecEnv::BIG_PAGE_SIZE) - ExecEnv::PAGE_SHIFT);
}
PARAM_HANDLER(vcpus, " vcpus - instantiate the vcpus defined with 'ncpu'") {
    const char *vcpu_params[] = {
        "vcpu", "halifax", "vbios", "lapic"
    };
    for(size_t count = 0; count < ncpu; count++) {
        for(size_t i = 0; i < ARRAY_SIZE(vcpu_params); ++i)
            mb.handle_arg(vcpu_params[i]);
    }
}

void Vancouver::reset() {
    if(initialized)
        globalsm.down();
    Serial::get() << "RESET device state\n";
    MessageLegacy msg2(MessageLegacy::RESET, 0);
    _mb.bus_legacy.send_fifo(msg2);
    initialized = true;
    globalsm.up();
}

bool Vancouver::receive(CpuMessage &msg) {
    if(msg.type != CpuMessage::TYPE_CPUID)
        return false;

    // XXX locking?
    // XXX use the reserved CPUID regions
    switch(msg.cpuid_index) {
        case 0x40000020:
            // NOVA debug leaf
            // TODO nova_syscall(15,msg.cpu->ebx,0,0,0);
            break;
        case 0x40000021:
            // Vancouver debug leaf
            _mb.dump_counters();
            break;
        case 0x40000022: {
            // time leaf
            timevalue_t tsc = Util::tsc();
            msg.cpu->eax = tsc;
            msg.cpu->edx = tsc >> 32;
            msg.cpu->ecx = nre::Hip::get().freq_tsc;
        }
        break;

        default:
            /*
             * We have to return true here, to make handle_vcpu happy.
             * The values are already set in VCpu.
             */
            return true;
    }
    return true;
}

bool Vancouver::receive(MessageHostOp &msg) {
    bool res = true;
    switch(msg.type) {
        case MessageHostOp::OP_ALLOC_IOIO_REGION: {
            new Ports(msg.value >> 8, 1 << (msg.value & 0xff));
            Serial::get() << "alloc ioio region " << fmt(msg.value, "x") << "\n";
        }
        break;

        case MessageHostOp::OP_ALLOC_IOMEM: {
            DataSpace *ds = new DataSpace(msg.len, DataSpaceDesc::LOCKED, DataSpaceDesc::RW, msg.value);
            msg.ptr = reinterpret_cast<char*>(ds->virt());
        }
        break;

        case MessageHostOp::OP_GUEST_MEM:
            if(msg.value >= guest_size)
                msg.value = 0;
            else {
                msg.len = guest_size - msg.value;
                msg.ptr = reinterpret_cast<char*>(guest_mem->virt() + msg.value);
            }
            break;

        case MessageHostOp::OP_ALLOC_FROM_GUEST:
            assert((msg.value & 0xFFF) == 0);
            if(msg.value <= guest_size) {
                guest_size -= msg.value;
                msg.phys = guest_size;
                Serial::get() << "Allocating from guest "
                              << fmt(guest_size, "0x", 8) << "+" << fmt(msg.value, "x") << "\n";
            }
            else
                res = false;
            break;

        case MessageHostOp::OP_NOTIFY_IRQ:
            assert(false);
            // TODO res = NOVA_ESUCCESS == nova_semup(_shared_sem[msg.value & 0xff]);
            break;

        case MessageHostOp::OP_ASSIGN_PCI:
            assert(false);
            /* TODO res = !Sigma0Base::hostop(msg);
               _dpci |= res;
               Logging::printf("%s\n",_dpci ? "DPCI device assigned" : "DPCI failed");*/
            break;

        case MessageHostOp::OP_GET_MODULE: {
            const nre::Hip &hip = nre::Hip::get();
            uintptr_t destaddr = reinterpret_cast<uintptr_t>(msg.start);
            uint module = msg.module - 1;
            nre::Hip::mem_iterator it;
            for(it = hip.mem_begin(), ++it; it != hip.mem_end(); ++it) {
                if(it->type == HipMem::MB_MODULE && module-- == 0)
                    break;
            }
            if(it == hip.mem_end())
                return false;

            msg.size = it->size;
            msg.cmdline = msg.start + it->size;
            msg.cmdlen = strlen(it->cmdline()) + 1;

            // does it fit in guest mem?
            if(destaddr >= guest_mem->virt() + guest_mem->size() ||
               destaddr + it->size + msg.cmdlen > guest_mem->virt() + guest_mem->size()) {
                Serial::get() << "Can't copy module " << fmt(it->addr, "#x") << ".."
                              << fmt(it->addr + it->size + msg.cmdlen, "#x") << " to "
                              << fmt(reinterpret_cast<void*>(destaddr - guest_mem->virt()))
                              << " (RAM is only 0.."
                              << fmt(reinterpret_cast<void*>(guest_size)) << ")\n";
                return false;
            }

            DataSpace ds(it->size, DataSpaceDesc::LOCKED, DataSpaceDesc::R, it->addr);
            memcpy(msg.start, reinterpret_cast<void*>(ds.virt()), ds.size());
            memcpy(msg.cmdline, it->cmdline(), msg.cmdlen);
            return true;
        }
        break;

        case MessageHostOp::OP_GET_MAC:
            assert(false);
            // TODO res = !Sigma0Base::hostop(msg);
            break;

        case MessageHostOp::OP_ATTACH_MSI:
        case MessageHostOp::OP_ATTACH_IRQ: {
            assert(false);
            /* TODO
               unsigned irq_cap = alloc_cap();
               myutcb()->head.crd = Crd(irq_cap,0,DESC_CAP_ALL).value();
               res = !Sigma0Base::hostop(msg);
               create_irq_thread(
                    msg.type == MessageHostOp::OP_ATTACH_IRQ ? msg.value : msg.msi_gsi,irq_cap,
                    do_gsi,"irq");
             */
        }
        break;

        case MessageHostOp::OP_VCPU_CREATE_BACKEND: {
            cpu_t cpu = CPU::current().log_id();
            VCPUBackend *v = new VCPUBackend(&_mb, msg.vcpu, nre::Hip::get().has_svm(), cpu);
            msg.value = reinterpret_cast<ulong>(v);
            msg.vcpu->executor.add(this, receive_static<CpuMessage> );
            _vcpus.append(v);
        }
        break;

        case MessageHostOp::OP_VCPU_BLOCK: {
            VCPUBackend *v = reinterpret_cast<VCPUBackend*>(msg.value);
            globalsm.up();
            v->sm().down();
            globalsm.down();
            res = true;
        }
        break;

        case MessageHostOp::OP_VCPU_RELEASE: {
            VCPUBackend *v = reinterpret_cast<VCPUBackend*>(msg.value);
            if(msg.len)
                v->sm().up();
            v->vcpu().recall();
            res = true;
        }
        break;

        case MessageHostOp::OP_ALLOC_SERVICE_THREAD: {
            assert(false);
            /* TODO
               phy_cpu_no cpu = myutcb()->head.nul_cpunr;
               unsigned ec_cap = create_ec_helper(msg._alloc_service_thread.work_arg,cpu,_pt_irq,0,
                    reinterpret_cast<void *>(msg._alloc_service_thread.work));
               AdmissionProtocol::sched sched(AdmissionProtocol::sched::TYPE_SPORADIC); //Qpd(2, 10000)
               return !service_admission->alloc_sc(*myutcb(),ec_cap,sched,cpu,"service");
             */
        }
        break;

        case MessageHostOp::OP_VIRT_TO_PHYS:
        case MessageHostOp::OP_ALLOC_SERVICE_PORTAL:
        case MessageHostOp::OP_WAIT_CHILD:
        default:
            Util::panic("%s - unimplemented operation %#x", __PRETTY_FUNCTION__, msg.type);
            break;
    }
    return res;
}

bool Vancouver::receive(MessagePciConfig &) {
    return false; // TODO !Sigma0Base::pcicfg(msg);
}

bool Vancouver::receive(MessageAcpi &) {
    return false; // TODO !Sigma0Base::acpi(msg);
}

bool Vancouver::receive(MessageTimer &msg) {
    COUNTER_INC("requestTO");
    switch(msg.type) {
        case MessageTimer::TIMER_NEW:
            msg.nr = _timeouts.alloc();
            return true;
        case MessageTimer::TIMER_REQUEST_TIMEOUT:
            _timeouts.request(msg.nr, msg.abstime);
            break;
        default:
            return false;
    }
    return true;
}

bool Vancouver::receive(MessageTime &msg) {
    timevalue_t ts, wallclock;
    _timeouts.time(ts, wallclock);
    msg.timestamp = ts;
    msg.wallclocktime = wallclock;
    return true;
}

bool Vancouver::receive(MessageLegacy &msg) {
    if(msg.type != MessageLegacy::RESET)
        return false;
    // TODO ??
    return true;
}

bool Vancouver::receive(MessageConsole &msg) {
    switch(msg.type) {
        case MessageConsole::TYPE_ALLOC_CLIENT:
            ::Logging::panic("console: ALLOC_CLIENT not supported.\n");
        case MessageConsole::TYPE_ALLOC_VIEW: {
            assert(msg.ptr and msg.regs);
            Console::Register regs = _conssess.get_regs();
            msg.regs->mode = regs.mode;
            msg.regs->cursor_pos = regs.cursor_pos;
            msg.regs->cursor_style = regs.cursor_style;
            msg.regs->offset = regs.offset;
            msg.view = _console.add_view(msg.name, msg.ptr, msg.size, msg.regs);
            _console.set_view(msg.view);
            return true;
        }
        case MessageConsole::TYPE_SWITCH_VIEW:
            _console.set_view(msg.view);
            return true;
        case MessageConsole::TYPE_GET_MODEINFO:
            nre::Console::ModeInfo info;
            if(_conssess.get_mode_info(msg.index, info)) {
                memcpy(msg.info, &info, sizeof(*msg.info));
                return true;
            }
            break;
        case MessageConsole::TYPE_GET_FONT:
        case MessageConsole::TYPE_KEY:
        case MessageConsole::TYPE_RESET:
        case MessageConsole::TYPE_START:
        case MessageConsole::TYPE_KILL:
        case MessageConsole::TYPE_DEBUG:
        default:
            break;
    }
    return false;
}

bool Vancouver::receive(MessageDisk &msg) {
    if(msg.disknr >= ARRAY_SIZE(_stdevs))
        return false;

    // storage is optional
    if(!_stdevs[msg.disknr]) {
        try {
            _stdevs[msg.disknr] = new StorageDevice(_mb.bus_diskcommit, *guest_mem, msg.disknr);
        }
        catch(const Exception &e) {
            Serial::get() << "Disk connect failed: " << e.msg() << "\n";
            msg.error = MessageDisk::DISK_STATUS_DEVICE;
            return false;
        }
    }

    switch(msg.type) {
        case MessageDisk::DISK_GET_PARAMS:
            msg.error = _stdevs[msg.disknr]->get_params(*msg.params);
            return true;

        case MessageDisk::DISK_READ:
            _stdevs[msg.disknr]->read(msg.usertag, msg.sector, msg.dma, msg.dmacount);
            msg.error = MessageDisk::DISK_OK;
            return true;

        case MessageDisk::DISK_WRITE:
            _stdevs[msg.disknr]->write(msg.usertag, msg.sector, msg.dma, msg.dmacount);
            msg.error = MessageDisk::DISK_OK;
            return true;

        case MessageDisk::DISK_FLUSH_CACHE:
            _stdevs[msg.disknr]->flush_cache(msg.usertag);
            msg.error = MessageDisk::DISK_OK;
            return true;
    }
    return false;
}

void Vancouver::keyboard_thread(void*) {
    Vancouver *vc = Thread::current()->get_tls<Vancouver*>(Thread::TLS_PARAM);
    while(1) {
        nre::Console::ReceivePacket pk = vc->_conssess.receive();

        if((pk.flags & Keyboard::RELEASE) && (pk.flags & Keyboard::LCTRL)) {
            switch(pk.keycode) {
                case Keyboard::VK_HOME: {
                    vc->reset();
                    continue;
                }
                break;

                case Keyboard::VK_D: {
                    vc->_mb.dump_counters();
                    continue;
                }
                break;

                case Keyboard::VK_S: {
                    CpuEvent msg(::VCpu::EVENT_DEBUG);
                    for(::VCpu *vcpu = vc->_mb.last_vcpu; vcpu; vcpu = vcpu->get_last())
                        vcpu->bus_event.send(msg);
                    continue;
                }
                break;
            }
        }

        ScopedLock<UserSm> guard(&globalsm);
        MessageInput msg(0x10000, pk.scancode | pk.flags);
        vc->_mb.bus_input.send(msg);
    }
}

void Vancouver::vmmng_thread(void*) {
    Vancouver *vc = Thread::current()->get_tls<Vancouver*>(Thread::TLS_PARAM);
    Consumer<VMManager::Packet> &cons = vc->_vmmng->consumer();
    while(1) {
        VMManager::Packet *pk = cons.get();
        switch(pk->cmd) {
            case VMManager::RESET:
                vc->reset();
                break;
            case VMManager::KILL:
            case VMManager::TERMINATE:
                // TODO
                break;
        }
        cons.next();
    }
}

void Vancouver::create_devices(const char **args, size_t count) {
    _mb.bus_hostop.add(this, receive_static<MessageHostOp> );
    _mb.bus_console.add(this,receive_static<MessageConsole>);
    _mb.bus_disk.add(this, receive_static<MessageDisk> );
    _mb.bus_timer.add(this, receive_static<MessageTimer> );
    _mb.bus_time.add(this, receive_static<MessageTime> );
    // TODO _mb.bus_network.add(this,receive_static<MessageNetwork>);
    _mb.bus_hwpcicfg.add(this, receive_static<MessageHwPciConfig> );
    _mb.bus_acpi.add(this, receive_static<MessageAcpi> );
    _mb.bus_legacy.add(this, receive_static<MessageLegacy> );
    for(size_t i = 0; i < count; ++i)
        _mb.handle_arg(args[i]);
}

void Vancouver::create_vcpus() {
    // init VCPUs
    for(::VCpu *vcpu = _mb.last_vcpu; vcpu; vcpu = vcpu->get_last()) {
        // init CPU strings
        const char *short_name = "NOVA microHV";
        vcpu->set_cpuid(0, 1, reinterpret_cast<const unsigned *>(short_name)[0]);
        vcpu->set_cpuid(0, 3, reinterpret_cast<const unsigned *>(short_name)[1]);
        vcpu->set_cpuid(0, 2, reinterpret_cast<const unsigned *>(short_name)[2]);
        const char *long_name = "Vancouver VMM proudly presents this VirtualCPU. ";
        for(unsigned i = 0; i < 12; i++)
            vcpu->set_cpuid(0x80000002 + (i / 4), i % 4,
                            reinterpret_cast<const unsigned *>(long_name)[i]);

        // propagate feature flags from the host
        uint32_t ebx_1 = 0, ecx_1 = 0, edx_1 = 0;
        Util::cpuid(1, ebx_1, ecx_1, edx_1);
        vcpu->set_cpuid(1, 1, ebx_1 & 0xff00, 0xff00ff00); // clflush size
        vcpu->set_cpuid(1, 2, ecx_1, 0x00000201); // +SSE3,+SSSE3
        vcpu->set_cpuid(1, 3, edx_1, 0x0f80a9bf | (1 << 28)); // -PAE,-PSE36, -MTRR,+MMX,+SSE,+SSE2,+SEP
    }
}

int main(int argc, char **argv) {
    size_t fbsize = ExecEnv::PAGE_SIZE * nre::VGAStream::PAGES;
    for(int i = 1; i < argc; ++i) {
        if(strncmp(argv[i], "vga_fbsize:", 11) == 0)
            fbsize = IStringStream::read_from<size_t>(argv[i] + 11) * 1024;
        if(strncmp(argv[i], "console:", 8) == 0)
            console = IStringStream::read_from<size_t>(argv[i] + 8);
        else if(strncmp(argv[i], "constitle:", 10) == 0)
            constitle = String(argv[i] + 10);
    }

    Vancouver *v = new Vancouver(const_cast<const char**>(argv + 1),
            argc - 1, console, constitle, fbsize);
    v->reset();

    Sm sm(0);
    sm.down();
    return 0;
}
