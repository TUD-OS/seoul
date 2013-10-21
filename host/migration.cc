/**
 * Base migration code
 *
 * Copyright (C) 2013 Jacek Galowicz, Intel Corporation.
 *
 * This file is part of Seoul.
 *
 * Seoul is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Seoul is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */


#include <stdio.h> // snprintf

#include <nul/motherboard.h>
#include <nul/vcpu.h>

#include <nul/migration.h>
#include <service/vprintf.h>
#include <service/time.h>

Migration::Migration(Motherboard *mb)
: _mb(mb),
    _vcpu_utcb(NULL),
#if PORTED_TO_UNIX
    _vcpu_blocked_sem(cap, true),
    _vcpu_sem(cap+1, true),
#endif
    _vcpu_should_block(false),
    _socket(NULL),
    _sendmem(0), _sendmem_total(0)
{
    MessageHostOp msg(MessageHostOp::OP_GUEST_MEM, 0UL);
    if (!_mb->bus_hostop.send(msg))
        Logging::panic("%s failed to get physical memory\n",
                __PRETTY_FUNCTION__);

    _physmem_start = msg.ptr;
    _physmem_size  = msg.len;

    _dirtman = DirtManager(_physmem_size >> 12);

    _vcpu_utcb = new CpuState;
}

Migration::~Migration()
{
}

void Migration::save_guestregs(CpuState *utcb)
{
    /* After Migration::freeze_vcpus() was called, the VCPU will
     * arrive in the recall handler and call this method here.
     * Its register states are saved and then it hangs in
     * our lock.
     */
    if (!_vcpu_should_block) return;

    mword vcpu_bytes = reinterpret_cast<mword>(&utcb->id+1);
    vcpu_bytes -= reinterpret_cast<mword>(&utcb->mtd);

    memcpy(&_vcpu_utcb->mtd, &utcb->mtd, vcpu_bytes);

#if PORTED_TO_UNIX
    // Release the waiting migration thread
    _vcopu_blocked_sem.up();
    // Freeze VCPU
    _vcpu_sem.downmulti();
#endif
}

/* This is used to print messages onto the screen
 * just after the VMM has started and waits for incoming
 * guest state data.
 */
bool Migration::puts_guestscreen(const char *str, bool reset_screen)
{
    MessageRestore msg(MessageRestore::VGA_DISPLAY_GUEST,
            const_cast<char*>(str), reset_screen);
    return _mb->bus_restore.send(msg, true);
}

void Migration::print_welcomescreen()
{
    char welcome_msg[255];
    mword ip = IpHelper::instance().get_ip();

    snprintf(welcome_msg, sizeof(welcome_msg),
            "   Waiting for guest to migrate. IP: %lu.%lu.%lu.%lu\n\n",
            ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    puts_guestscreen(welcome_msg, true);
}

void Migration::freeze_vcpus()
{
    Logging::printf("Stopping vcpu.\n");

    _vcpu_should_block = true;

    CpuEvent smsg(VCpu::EVENT_RESUME);
    for (VCpu *vcpu = _mb->last_vcpu; vcpu; vcpu=vcpu->get_last())
        vcpu->bus_event.send(smsg);

#if PORTED_TO_UNIX
    _vcpu_blocked_sem.downmulti();
#endif
}

void Migration::unfreeze_vcpus()
{
    _vcpu_should_block = false;
#if PORTED_TO_UNIX
    /* After releasing the VCPU it will continue
     * through the rest of the recall handler.
     */
    _vcpu_sem.up();
#endif
}

bool Migration::chksum_page(unsigned page_nr, mword &their_chksum, bool compare)
{
    mword my_chksum = 0;
    assert(page_nr < (_physmem_size >> 12));

    mword *ptr = reinterpret_cast<mword*>(_physmem_start + (page_nr << 12));

    for (unsigned i=0; i < 4096 / sizeof(ptr[0]); ++i)
        // checksum = sum over (address_i * value_i^2)
        my_chksum += reinterpret_cast<mword>(ptr+1) * (ptr[i]) * (ptr[i]);

    // Use case one: return true if given memory range is correct
    if (compare) return my_chksum == their_chksum;

    // Second use case: Provide a checksum for a given memory range
    their_chksum = my_chksum;
    return true;
}

bool Migration::checksums(bool retrieve)
{
    unsigned entries = _physmem_size >> 12;
    bool success = true;

    mword *chksum = new mword[entries];
    if (!chksum) Logging::panic("Allocating checksum list error\n");

    Logging::printf("Checksumming the area [%8lx - %8lx)\n",
            reinterpret_cast<mword>(_physmem_start),
            reinterpret_cast<mword>(_physmem_start + 4096 * entries));

    if (retrieve) {
        // Receiver. Check the existing checksum list against our memory
        _socket->receive(chksum, entries * sizeof(chksum[0]));

        unsigned err = 0;

        for (unsigned i=0; i < entries; ++i) {
            bool ret = chksum_page(i, chksum[i], true);
            if (!ret) {
                ++err;
                Logging::printf("bad page received. page number: %8x\n", i);
            }
            success &= ret;
        }

        Logging::printf("Erroneous pages: %u\n", err);
    }
    else {
        // Sender. Make a list of checksums and send it away.

        for (unsigned i=0; i < entries; ++i)
            chksum_page(i, chksum[i], false);

        success &= _socket->send(chksum, entries * sizeof(chksum[0]));
    }

    delete [] chksum;

    return success;
}

/***********************************************************************
 * Guest receiving part
 ***********************************************************************/

bool Migration::receive_ping()
{
    mword ping_msg = 0;

    _socket->receive(&ping_msg, sizeof(ping_msg));

    if (ping_msg != 0xc0ffee) {
        Logging::printf("Received bad ping message.\n");
        return false;
    }

    ping_msg *= 3;
    _socket->send(&ping_msg, sizeof(ping_msg));

    return true;
}

void Migration::receive_header()
{
    MigrationHeader mig_header;

    Logging::printf("Receiving guest information.\n");

    _socket->receive(&mig_header, sizeof(mig_header));
    if (!mig_header.magic_string_check())
        Logging::panic("Magic string check failed: MigrationHeader\n");

    MessageRestore vgamsg(MessageRestore::VGA_VIDEOMODE, NULL, true);
    vgamsg.bytes = mig_header.videomode;
    _mb->bus_restore.send(vgamsg, true);
}

void Migration::receive_memory()
{
    StopWatch watch(_mb->clock());
    Logging::printf("Receiving guest memory.\n");

    Prd current;
    unsigned long bytes = 0;

    watch.start();
    while (1) {
        _socket->receive(&current, sizeof(current));
        if (!current.value())
            // Receiving an empty range descriptor means "EOF"
            break;

        _socket->receive(current.base() + _physmem_start, current.size());
        bytes += current.size();
    }
    watch.stop();

    Logging::printf("Received %lu MB. RX Rate: %u KB/s\n",
            bytes / 1024 / 1024, watch.rate(bytes));
}

/* Being equipped with a pointer to the stopped VCPU's
 * register state structure, its registers will be overwritten
 * and devices restored.
 */
bool Migration::receive_guestdevices(CpuState *vcpu_utcb)
{
    Logging::printf("Receiving UTCB.\n");

    CpuState *buf = new CpuState;

    mword utcb_end = reinterpret_cast<mword>(&buf->id+1);
    mword utcb_start = reinterpret_cast<mword>(&buf->mtd);
    mword utcb_bytes =  utcb_end - utcb_start;

    _socket->receive(&buf->mtd, utcb_bytes);

    memcpy(&vcpu_utcb->mtd, &buf->mtd, utcb_bytes);

    delete buf;

    Logging::printf("Receiving Devices.\n");

    // This works quite similar to the device saving procedure
    MessageRestore *rmsg = new MessageRestore(MessageRestore::RESTORE_RESTART,
            NULL, false);
    _mb->bus_restore.send_fifo(*rmsg);

    // no while(someone_responds_true) approach here because we know
    // what we want to restore and how many.
    bool ret;
    while (1) {
        _socket->receive(rmsg, sizeof(*rmsg));
        assert(rmsg->magic_string_check());

        if (rmsg->devtype == 0xdead)
            break;

        char *device_buffer = new char[rmsg->bytes];
        _socket->receive(device_buffer, rmsg->bytes);

        rmsg->space = device_buffer;
        rmsg->write = false;
        ret = _mb->bus_restore.send(*rmsg, true);
        if (!ret) Logging::printf("No device replied on restore message!"
                " VMM-Configuration mismatch?\n");

        delete [] device_buffer;
    }

    delete rmsg;

    /* Fix TSC offset.
     * The guest would freeze for some time or skip some timesteps otherwise.
     */
    unsigned long long sender_rdtsc;
    _socket->receive(&sender_rdtsc, sizeof(sender_rdtsc));

    CpuMessage rdtsc_msg(CpuMessage::TYPE_ADD_TSC_OFF, NULL, 0);
    rdtsc_msg.current_tsc_off = sender_rdtsc - Cpu::rdtsc();

    for (VCpu *vcpu = _mb->last_vcpu; vcpu; vcpu=vcpu->get_last())
        vcpu->executor.send(rdtsc_msg);

    return true;
}

bool Migration::listen(unsigned port, CpuState *vcpu_utcb)
{
    print_welcomescreen();

    _socket = IpHelper::instance().listen(port);
    if (_socket == NULL) Logging::panic("Got no TCP receiver.\n");

    receive_ping();

    receive_header();

    receive_memory();

    receive_guestdevices(vcpu_utcb);

#if 0
    // Checksumming really makes the migration gap larger
    if (!checksums(true)) {
        Logging::printf("Error while comparing checksums.\n");
        return false;
    }
#endif

    _socket->close();

    MessageRestore replug_msg(MessageRestore::PCI_PLUG, NULL, true);
    _mb->bus_restore.send(replug_msg, false);

    Logging::printf("That's it. Waking up VCPUs.\n");
    unfreeze_vcpus();

    return true;
}

/***********************************************************************
 * Guest sending part
 ***********************************************************************/

unsigned Migration::negotiate_port()
{
    char *cmdline = NULL;

    MessageHostOp msg(MessageHostOp::OP_GET_CONFIG_STRING, 0ul);
    if (!_mb->bus_hostop.send(msg))
        return 0;
    assert(msg.obj != NULL);
    cmdline = reinterpret_cast<char*>(msg.obj);

    /* Send the listener service our configuration string.
     * It will try to start an identically configured VMM
     * instance and then tell us on what port it is waiting
     * for state input.
     */
    MigrationInit mig_init(strlen(cmdline));
    if (!_socket->send(&mig_init, sizeof(mig_init))) return 0;
    if (!_socket->send(cmdline, mig_init.cmdlen)) return 0;

    MigrationAnswer mig_ans;
    _socket->receive(&mig_ans, sizeof(mig_ans));
    if (!mig_ans.magic_string_check()) {
        Logging::printf("Magic string check failed: MigrationAnswer");
        return 0;
    }

    if (!mig_ans.success) {
        Logging::printf("Configuration is not suitable for target machine.\n");
        return 0;
    }

    delete [] cmdline;
    return mig_ans.port;
}

bool Migration::send_header()
{
    /* Sending the listening VMM the video mode setting will allow it
     * to switch the framebuffer to the right setting before migration.
     * The screen would flicker and display ugly symbols if the
     * framebuffer state is restored, but the host doesn't display it
     * the right way, otherwise.
     */
    MessageRestore vgamsg(MessageRestore::VGA_VIDEOMODE, NULL, false);
    _mb->bus_restore.send(vgamsg, true);

    MigrationHeader mig_header(vgamsg.bytes);
    return _socket->send(&mig_header, sizeof(mig_header));
}

timevalue Migration::send_ping()
{
    StopWatch ping_timer(_mb->clock());

    mword ping_msg = 0xc0ffee;
    mword pong_msg = 0;

    ping_timer.start();
    _socket->send(&ping_msg, sizeof(ping_msg));
    _socket->receive(&pong_msg, sizeof(pong_msg));
    ping_timer.stop();

    if (pong_msg != 3 * ping_msg) {
        Logging::printf("Error during latency check\n");
        return 0;
    }

    return ping_timer.delta();
}

#define NEXT_DIRTY_PAGE() \
({ \
        MessageHostOp msg(MessageHostOp::OP_NEXT_DIRTY_PAGE, 0ul); \
        _mb->bus_hostop.send(msg); \
        msg.value; \
})

unsigned Migration::enqueue_all_dirty_pages(longrange_data &async_data)
{
    Prd *crds = async_data.crds;
    unsigned crds_sent=0;

    Prd first_crd, last_crd;

    /* This loop will cycle through the memory space
     * until it ends up without any new dirty regions
     * or it has done a full cycle.
     */
    while (1) {
        Prd current(NEXT_DIRTY_PAGE());

        if (!current.value() || // Nothing dirty
            // Next round through the memspace
            (first_crd.value() && current.base() == first_crd.base()) ||
            (last_crd.value() && current.base() == last_crd.base()))
            break;

        /* These pages are just _marked_ dirty in another data structure,
         * the dirt manager.
         * This structure might be able to apply some smart optimizations
         * in the future like e.g. "don't resend pages too often which are dirtied
         * with high access-frequency to reduce traffic", etc.
         */
        _dirtman.mark_dirty(current);

        if (!first_crd.value()) first_crd = current;
        last_crd = current;
    }

    unsigned pages_enqueued = 0;
    while (_dirtman.dirty_pages() > 0 && crds_sent < async_data.crd_count) {
        Prd current = crds[crds_sent] = _dirtman.next_dirty();
        if (!current.value())
            // That's it for now.
            break;

        _dirtman.mark_clean(current);

        if (!_socket->send_nonblocking(&crds[crds_sent], sizeof(*crds)) ||
            !_socket->send_nonblocking(current.base() + _physmem_start,
                current.size()))
            return 0;

        ++crds_sent;
        pages_enqueued += 1 << current.order();
    }

    return pages_enqueued;
}

bool Migration::send_memory(longrange_data &async_data)
{
    StopWatch lap_time(_mb->clock());
    StopWatch last_lap(_mb->clock());

    unsigned transfer_rate;
    unsigned dirtying_rate;

    /* The underlying socket architecture works a little bit different than
     * BSD sockets, where you stuff data to be sent into the send buffer
     * until it replies with "buffer is full, wait a bit".
     * These sockets here asynchronously manage lists of pointers to memory ranges
     * and their size and will pick up this data when it is actually needed.
     * And because of this we have to preserve all memory ranges to be sent
     * until they are ACKed.
     */

    const unsigned page_limit = 1000;
    unsigned pages_transferred;
    unsigned round = 0;
    async_data.crds = new Prd[page_limit];
    async_data.crd_count = page_limit;

    MessageRestore unplug_msg(MessageRestore::PCI_PLUG, NULL, false);
    _mb->bus_restore.send(unplug_msg, false);

    do {
        last_lap = lap_time;
        lap_time.start();

        if (!(pages_transferred = enqueue_all_dirty_pages(async_data)) ||
            !_socket->wait_complete())
            return false;

        lap_time.stop();

        transfer_rate = lap_time.rate(pages_transferred << 12);
        dirtying_rate = last_lap.rate(pages_transferred << 12);
        Logging::printf("RND %u PAGE_CNT %5u TX %5u KB/s DRT %5u KB/s DELTA"
                " %llu START %llu\n",
                round, pages_transferred, transfer_rate, dirtying_rate,
                lap_time.delta(), lap_time.abs_start());

        assert(pages_transferred);

        _sendmem_total += pages_transferred << 12;
        if (_sendmem == 0) _sendmem = _sendmem_total;
        ++round;
    } while (transfer_rate >= dirtying_rate);

    // The last transfer round with a frozen guest system will follow now
    freeze_vcpus();

    static Prd end_of_crds;
    pages_transferred = enqueue_all_dirty_pages(async_data);
    Logging::printf("pages_dirty: %x\n", pages_transferred);
    if (!pages_transferred ||
        !_socket->send_nonblocking(&end_of_crds, sizeof(end_of_crds)))
        return false;

    Logging::printf("Enqueued the last %u dirty pages\n", pages_transferred);
    return true;
}

bool Migration::send_devices(longrange_data dat)
{
    // Send VCPU state
#if PORTED_TO_UNIX
    unsigned vcpu_bytes = reinterpret_cast<unsigned>(&_vcpu_utcb->id+1);
    vcpu_bytes -= reinterpret_cast<unsigned>(&_vcpu_utcb->mtd);

    if (!_socket->send(&_vcpu_utcb->mtd, vcpu_bytes))
        return false;
#endif

    /* There are multiple RESTORE_xxx types of restore messages.
     * For each kind of device there is one.
     * So we throw messages of each type onto the bus.
     */
    MessageRestore restart_msg(MessageRestore::RESTORE_RESTART, NULL, true);
    _mb->bus_restore.send_fifo(restart_msg);

    mword restore_bytes = restart_msg.bytes;
    mword restore_bytes_consumed = 0;
    dat.restore_buf = new char[restore_bytes + sizeof(MessageRestore)];

    for (int i=MessageRestore::RESTORE_RESTART+1;
            i < MessageRestore::RESTORE_LAST;
            i++) {
        /* A device will receive this message, write its state into it and
         * return true. If it receives such a message again, it will return
         * false. That's why we sent this RESTORE_RESTART message before.
         * After the first time the bus returns false, we know that we saved
         * all devices of this particular type.
         */
        while (1) {
            char *msg_addr = dat.restore_buf + restore_bytes_consumed;
            char *device_space = dat.restore_buf + restore_bytes_consumed
                + sizeof(MessageRestore);

            MessageRestore *rmsg = reinterpret_cast<MessageRestore*>(msg_addr);
            memset(rmsg, 0, sizeof(*rmsg));

            rmsg->devtype = i;
            rmsg->write = true;
            rmsg->space = device_space;
            rmsg->magic_string = MessageRestore::MAGIC_STRING_DEVICE_DESC;

            if (!_mb->bus_restore.send(*rmsg, true)) break;

            restore_bytes_consumed += sizeof(*rmsg) + rmsg->bytes;
        }
    }
    assert(restore_bytes == restore_bytes_consumed);

    if (!_socket->send_nonblocking(dat.restore_buf, restore_bytes) ||
           // Send "end of devices"
        !_socket->send_nonblocking(&dat.end_of_devices,
            sizeof(dat.end_of_devices)) ||
        !_socket->wait_complete()) {
        Logging::printf("Error sending device states.\n");
        return false;
    }

    // Restore current tsc offset at destination
    dat.rdtsc  = Cpu::rdtsc();
    /* Compensate network latency.
     * This was tested with cloning a VM displaying animations
     * which were bound to TSC values. After migration,
     * they only ran in sync when the following line was applied.
     */
    dat.rdtsc += dat.latency * _mb->clock()->freq() / 1000;

    if (!_socket->send(&dat.rdtsc, sizeof(dat.rdtsc))) {
        Logging::printf("Error sending RDTSC\n");
        return false;
    }

    return true;
}

bool Migration::send(unsigned long addr, unsigned long port)
{
    StopWatch migration_timer(_mb->clock());
    StopWatch freeze_timer(_mb->clock());
    longrange_data async_data;

    Logging::printf("Trying to connect...\n");
    _socket = IpHelper::instance().connect(addr, port);
    if (_socket == NULL) {
        Logging::printf("Quitting: Got no TCP connection.\n");
        return false;
    }

    Logging::printf("Established connection.\n");

    unsigned mig_port = negotiate_port();

    _socket->close();

    if (!mig_port) return false;

    Logging::printf("Connecting to waiting target VM.\n");
    _socket = IpHelper::instance().connect(addr, mig_port);
    if (!_socket) {
        Logging::printf("Error connecting to target VM.\n");
        return false;
    }
    Logging::printf("OK, starting the actual migration.\n");

    migration_timer.start();

    async_data.latency = send_ping();
    if (!async_data.latency) {
        Logging::printf("Ping failed.\n");
        return false;
    }
    // Latency = round trip time / 2
    async_data.latency >>= 1;
    Logging::printf("Connection has a latency of %lu ms * freq %llu kHz"
            " = %llu ticks.\n",
            async_data.latency, _mb->clock()->freq() / 1000,
            async_data.latency * _mb->clock()->freq() / 1000);

    if (!send_header()) {
        Logging::printf("Sending header failed.\n");
        return false;
    }
    if (!send_memory(async_data)) {
        Logging::printf("Sending guest state failed.\n");
        return false;
    }
    freeze_timer.start();

    if (!send_devices(async_data)) {
        Logging::printf("Sending guest devices failed.\n");
        return false;
    }

#if 0
    // Checksumming really makes the freeze gap larger
    if (!checksums(false)) {
        Logging::printf("Error while sending checksums.\n");
        return false;
    }
#endif

    // Uncomment this to "clone" the VM instead of migrating it away.
    //unfreeze_vcpus();

    freeze_timer.stop();

    _socket->close();

    migration_timer.stop();

    Logging::printf("Done. VM was frozen for %llu ms.\n", freeze_timer.delta());
    Logging::printf("This migration took %llu seconds.\n",
            migration_timer.delta() / 1000);
    Logging::printf("%3lu%% (%lu MB) of guest memory resent due to change.\n",
            100u * (_sendmem_total - _sendmem) / _sendmem,
            (_sendmem_total - _sendmem) / 1024u / 1024u);

    _dirtman.print_stats();

    delete [] async_data.crds;
    delete [] async_data.restore_buf;
#if PORTED_TO_UNIX
    delete _vcpu_utcb;
#endif

    return true;
}

PARAM_HANDLER(retrieve_guest,
	      "retrieve_guest:<port> - Start a VMM instance which waits for guest",
          " state input over network listening on <port>")
{
    MessageHostOp msg(MessageHostOp::OP_MIGRATION_RETRIEVE_INIT, argv[0]);
    mb.bus_hostop.send(msg);
}
