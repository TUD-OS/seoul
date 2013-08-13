/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <kobj/GlobalThread.h>
#include <kobj/Sc.h>
#include <services/Storage.h>

#include <nul/motherboard.h>
#include <nul/message.h>
#include <host/dma.h>

class StorageDevice {
public:
    explicit StorageDevice(DBus<MessageDiskCommit> &bus, nre::DataSpace &guestmem, size_t no)
        : _no(no), _bus(bus), _sess("storage", guestmem, no) {
        char buffer[32];
        nre::OStringStream os(buffer, sizeof(buffer));
        os << "vmm-storage-" << no;
        nre::GlobalThread *gt = nre::GlobalThread::create(
            thread, nre::CPU::current().log_id(), buffer);
        gt->set_tls<StorageDevice*>(nre::Thread::TLS_PARAM, this);
        gt->start();
    }

    MessageDisk::Status get_params(DiskParameter &params) {
        nre::Storage::Parameter res = _sess.get_params();
        params.flags = res.flags;
        params.maxrequestcount = res.max_requests;
        params.sectors = res.sectors;
        params.sectorsize = res.sector_size;
        memcpy(params.name, res.name,
               nre::Math::min<size_t>(sizeof(params.name), sizeof(res.name)));
        params.name[sizeof(params.name) - 1] = '\0';
        return MessageDisk::DISK_OK;
    }

    void read(unsigned long tag, unsigned long long sector, const DmaDescriptor *dma, size_t count) {
        nre::Storage::dma_type sdma;
        convert_dma(sdma, dma, count);
        _sess.read(tag, sector, sdma);
    }
    void write(unsigned long tag, unsigned long long sector, const DmaDescriptor *dma, size_t count) {
        nre::Storage::dma_type sdma;
        convert_dma(sdma, dma, count);
        _sess.write(tag, sector, sdma);
    }

    void flush_cache(unsigned long tag) {
        _sess.flush(tag);
    }

private:
    void convert_dma(nre::Storage::dma_type &dst, const DmaDescriptor *dma, size_t count) {
        while(count-- > 0) {
            dst.push(nre::DMADesc(dma->byteoffset, dma->bytecount));
            dma++;
        }
    }

    static void thread(void*) {
        StorageDevice *sd = nre::Thread::current()->get_tls<StorageDevice*>(nre::Thread::TLS_PARAM);
        while(1) {
            nre::Storage::Packet *pk = sd->_sess.consumer().get();
            // the status isn't used anyway
            {
                nre::ScopedLock<nre::UserSm> guard(&globalsm);
                MessageDiskCommit msg(sd->_no, pk->tag, MessageDisk::DISK_OK);
                sd->_bus.send(msg);
            }
            sd->_sess.consumer().next();
        }
    }

    size_t _no;
    DBus<MessageDiskCommit> &_bus;
    nre::StorageSession _sess;
};
