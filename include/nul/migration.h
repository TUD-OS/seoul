/**
 * Base migration code declarations
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



#include <nul/motherboard.h>
#include <nul/iphelper.h>
#include <nul/migration_structs.h>

class Desc
{
    protected:
          unsigned _value;
            Desc(unsigned v) : _value(v) {}
    public:
              unsigned value() { return _value; }
};

/**
 *  A page range descriptor;
 *  Introduced, because NUL provided CRDs for this...
 **/
class Prd
{
    protected:
        unsigned _value;

    public:
        unsigned order() { return ((_value >> 7) & 0x1f); }
        unsigned size()  { return 1 << (order() + 12); }
        unsigned base()  { return _value & ~0xfff; }
        unsigned attr()  { return _value & 0x1f; }
        unsigned cap()   { return _value >> 12; }
        unsigned value() { return _value; }

        explicit Prd(unsigned offset, unsigned order, unsigned attr) : _value((offset << 12) | (order << 7) | attr) { }
        explicit Prd(unsigned v) : _value(v) {}
        explicit Prd() : Prd(0) {}
};

/* The DirtManager is feeded with CRDs of dirty page regions.
 * There's an internal bitmap which can be used for future resend-optimizations
 * as well as generating resend-statistics.
 */
class DirtManager
{
    private:
        unsigned *_map;
        unsigned  _pages;

        unsigned char *_cnt;

        unsigned _dirt_count;

    public:
        void mark_dirty(Prd dirty)
        {
            unsigned base  = dirty.base() >> 12;
            unsigned pages = 1 << dirty.order();
            for (unsigned i=base; i < base + pages; ++i) mark_dirty(i);
        }

        void mark_dirty(unsigned page)
        {
            if (!Cpu::get_bit(_map, page)) {
                ++_dirt_count;
                ++_cnt[page];
            }
            Cpu::set_bit(_map, page, true);
        }

        void mark_clean(Prd clean)
        {
            unsigned base  = clean.base() >> 12;
            unsigned pages = 1 << clean.order();
            for (unsigned i=base; i < base + pages; ++i) mark_clean(i);
        }

        void mark_clean(unsigned page)
        {
            --_dirt_count;
            Cpu::set_bit(_map, page, false);
        }

        unsigned dirty_pages() { return _dirt_count; }

        Prd next_dirty() {
            unsigned base, len;

            for (base = 0; base < _pages; ++base) {
                len = 0;
                while (Cpu::get_bit(_map, base + len)) ++len;

                if (len > 0) break;
            }

            if (len == 0) return Prd();

            Prd ret(base, Cpu::bsr(len), 0);
            return ret;
        }

        static inline unsigned char fir_max(unsigned char *in, unsigned limit, unsigned pos, int size)
        {
            int beg = pos - size;
            int end = pos + size;
            beg = VMM_MAX(beg, static_cast<int>(0));
            end = VMM_MIN(end, static_cast<int>(limit - 1));

            int width = end - beg;
            assert(width > 0);
            assert(width < 2 * size + 1);

            unsigned max = 0;
            for (int i=beg; i <= end; ++i) max = VMM_MAX(max, in[i]);

            return static_cast<unsigned char>(max);
        }

        void print_stats()
        {
            const unsigned size = 20;
            unsigned char bucket[size];

            unsigned sx = 0, sqx = 0;

            unsigned char *smooth[3];

            smooth[0] = new unsigned char[_pages];
            smooth[1] = new unsigned char[_pages];
            smooth[2] = new unsigned char[_pages];

            for (unsigned i=0; i < _pages; ++i) {
                unsigned faults = VMM_MIN(_cnt[i], size);
                ++bucket[faults];

                sx  += faults;
                sqx += faults * faults;

                for (unsigned j=0; j < 3; ++j)
                    smooth[j][i] = fir_max(_cnt, _pages, i, j*50+1);
            }

            float avg = sx / _pages;
            float var = sqx - _pages * avg * avg;

            Logging::printf("# avg = %u, var = %u\n",
                    static_cast<unsigned>(avg), static_cast<unsigned>(var));

#if 0
            /* This generates a really long list needed for plotting
             * statistics
             */
            Logging::printf("# Remaps per page:\n");
            for (unsigned i = 0; i < _pages; ++i)
                Logging::printf("REMAP %#x %u %u %u %u\n",
                        i, _cnt[i], smooth[0][i], smooth[1][i], smooth[2][i]);
#endif

            delete [] smooth[0];
            delete [] smooth[1];
            delete [] smooth[2];
        }

        DirtManager() : _map(NULL), _pages(0), _cnt(NULL), _dirt_count(0) {}
        DirtManager(unsigned pages) : _map(NULL), _pages(pages), _cnt(NULL), _dirt_count(0)
        {
            _map = new unsigned[(pages + sizeof(*_map) -1) / sizeof(*_map)];
            _cnt = new unsigned char[pages];
            memset(_cnt, 0, pages * sizeof(*_cnt));
        }
        ~DirtManager()
        {
            if (_map) delete [] _map;
            if (_cnt) delete [] _cnt;
        }
};

class Migration : public StaticReceiver<Migration>
{
    Motherboard     *_mb;
#if PORTED_TO_UNIX
    Hip             *_hip;
    CapAllocator    *_tls;
#endif

    char         *_physmem_start;
    unsigned long _physmem_size;

    CpuState         *_vcpu_utcb;
#if PORTED_TO_UNIX
    KernelSemaphore   _vcpu_blocked_sem;
    KernelSemaphore   _vcpu_sem;
#endif
    bool              _vcpu_should_block;

    TcpSocket       *_socket;

    unsigned long   _sendmem;
    unsigned long   _sendmem_total;

    /* Because of asynchronous send operations, all
     * data to be send has to be preserved somewhere until
     * it is ACKED. That's what this structure is for.
     */
    struct longrange_data {
        unsigned          crd_count;
        Prd              *crds;

        timevalue         rdtsc;
        char             *restore_buf;
        MessageRestore    end_of_devices;

        mword             latency;

        longrange_data() :
            crd_count(0), crds(NULL),
            rdtsc(0), restore_buf(NULL), end_of_devices(0xdead, NULL, true),
            latency(0) {}
    };

    DirtManager _dirtman;

    void init_memrange_info();
    void print_welcomescreen();
    bool puts_guestscreen(const char *str, bool reset_screen);

    void freeze_vcpus();
    void unfreeze_vcpus();

    unsigned negotiate_port();
    bool send_header();
    timevalue send_ping();
    bool send_devices(longrange_data dat);
    unsigned enqueue_all_dirty_pages(longrange_data &async_data);
    bool send_memory(longrange_data &async_data);

    void receive_header();
    bool receive_ping();
    void receive_memory();
    bool receive_guestdevices(CpuState *vcpu_utcb);

    bool chksum_page(unsigned page_nr, mword &their_chksum, bool compare);
    bool checksums(bool retrieve);

 public:
    enum RestoreModes {
        MODE_OFF = 0,
        MODE_SEND,
        MODE_RECEIVE
    };

    bool listen(unsigned port , CpuState *vcpu_utcb);
    bool send(unsigned long addr, unsigned long port);

    // To be called from do_recall
    void save_guestregs(CpuState *utcb);

	bool receive(MessageHostOp &msg);

    Migration(Motherboard *mb);
    ~Migration();
};
