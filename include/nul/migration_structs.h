/**
 * Migration protocol structures
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


struct MigrationInit {
#define MAGIC_STRING_MIGINIT 0xb00b00
    mword cmdlen;
    mword magic_string;

    MigrationInit() : cmdlen(0), magic_string(MAGIC_STRING_MIGINIT) {}
    MigrationInit(mword _cmdlen) : cmdlen(_cmdlen), magic_string(MAGIC_STRING_MIGINIT) {}
    bool magic_string_check() { return magic_string == MAGIC_STRING_MIGINIT; }
};

struct MigrationAnswer {
#define MAGIC_STRING_MIGANSWER 0xfeeb1ed0
    mword success;
    mword port;
    mword magic_string;

    MigrationAnswer() : success(0), port(0), magic_string(MAGIC_STRING_MIGANSWER) {}
    MigrationAnswer(unsigned _port) : success(1), port(_port), magic_string(MAGIC_STRING_MIGANSWER) {}
    bool magic_string_check() { return magic_string == MAGIC_STRING_MIGANSWER; }
};

/*
 * This is an index structure telling us how many memory pages and device pages
 * are saved to the hard disk, enabling us to calculate offsets later.
 */
struct RestoreIndex {
    unsigned mem_pages;
    unsigned dev_pages;
    char space[0x1000 - 2*sizeof(unsigned)];
};

struct MigrationHeader {
#define MAGIC_STRING_HEADER 0xb0015366
    mword magic_string;
    mword version;
    mword videomode;

    MigrationHeader() : magic_string(MAGIC_STRING_HEADER) {}
    MigrationHeader(mword _videomode)
        : magic_string(MAGIC_STRING_HEADER), videomode(_videomode) {}
    bool magic_string_check() { return magic_string == MAGIC_STRING_HEADER; }
};

struct AddressSpaceIndex {
#define MAGIC_STRING_ADDR_SPACE 0xBADB0B
    unsigned long magic_string;
    unsigned long num_pages;

    AddressSpaceIndex() {}
    AddressSpaceIndex(unsigned long pages) : magic_string(MAGIC_STRING_ADDR_SPACE), num_pages(pages) {}
    bool magic_string_check() { return magic_string == MAGIC_STRING_ADDR_SPACE; }
};

struct PageTransferIndex {
#define MAGIC_STRING_PAGE_INDEX 0x51CD06
    unsigned long magic_string;
    unsigned long desc_num;
    unsigned long total_bytes;

    PageTransferIndex()
        : magic_string(MAGIC_STRING_PAGE_INDEX) {}
    PageTransferIndex(unsigned long descs, unsigned long bytes)
        : magic_string(MAGIC_STRING_PAGE_INDEX), desc_num(descs), total_bytes(bytes) {}
    bool magic_string_check() { return magic_string == MAGIC_STRING_PAGE_INDEX; }
};

static unsigned long checksum_pages(void *offset, unsigned long count)
{
    if (offset == 0) return 0;
    assert(! (reinterpret_cast<unsigned long>(offset) & 0xfff) );

    unsigned long chksum = 0;
    unsigned long *ptr = reinterpret_cast<unsigned long*>(offset);

    for (unsigned i=0; i < count * 0x1000 / sizeof(unsigned long); i++)
        chksum += ptr[i] * ptr[i];

    return chksum;
}

struct PageTransferDesc {
#define MAGIC_STRING_PAGE_DESC 0xDEADC0DE
    unsigned long magic_string;
    unsigned long offset;
    unsigned long count;
    unsigned long checksum;

    PageTransferDesc() {}
    PageTransferDesc(unsigned long _offset, unsigned long _count)
        : magic_string(MAGIC_STRING_PAGE_DESC), offset(_offset), count(_count),
        checksum(checksum_pages(reinterpret_cast<void*>(_offset), _count)) { }
    unsigned long recalculate_checksums()
    { return (checksum = checksum_pages(reinterpret_cast<void*>(offset), count)); }
    bool magic_string_check() { return magic_string == MAGIC_STRING_PAGE_DESC; }
};

#define MAGIC_STRING_PAGE_BORDER 0xC03DD00D
