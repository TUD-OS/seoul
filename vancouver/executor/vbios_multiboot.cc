/**
 * Multiboot support for the virtual BIOS.
 *
 * Copyright (C) 2009-2010, Bernhard Kauer <bk@vmmon.org>
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

#include "nul/motherboard.h"
#include "service/elf.h"


/**
 * Provide Multiboot support for the virtual BIOS.
 *
 * State: unstable
 * Features: CPU init, elf-decoding, MBI creation, memory-map, request
 *           modules from sigma0, modaddr
 */
class VirtualBiosMultiboot : public StaticReceiver<VirtualBiosMultiboot>
{
public:
  enum mbi_enum
    {
      MBI_MAGIC                  = 0x2badb002,
      MBI_FLAG_MEM               = 1 << 0,
      MBI_FLAG_CMDLINE           = 1 << 2,
      MBI_FLAG_MODS              = 1 << 3,
      MBI_FLAG_MMAP              = 1 << 6,
      MBI_FLAG_BOOT_LOADER_NAME  = 1 << 9,
      MBI_FLAG_VBE               = 1 << 11,
    };


  struct Mbi
  {
    unsigned flags;
    unsigned mem_lower;
    unsigned mem_upper;
    unsigned dummy1;
    unsigned cmdline;
    unsigned mods_count;
    unsigned mods_addr;
    unsigned dummy2[4];
    unsigned mmap_length;
    unsigned mmap_addr;
    unsigned dummy3[3];
    unsigned boot_loader_name;
    unsigned dummy4;
    unsigned vbe_control_info;
    unsigned vbe_mode_info;
    unsigned short vbe_mode;
    unsigned short vbe_interface_seg;
    unsigned short vbe_interface_off;
    unsigned short vbe_interface_len;
  };


  struct Module
  {
    unsigned mod_start;
    unsigned mod_end;
    unsigned string;
    unsigned reserved;
  };

  struct MbiMmap
  {
    unsigned size;
    unsigned long long base __attribute__((packed));
    unsigned long long length  __attribute__((packed));
    unsigned type;
  };
private:
  Motherboard &_mb;
  unsigned long _modaddr;
  unsigned _lowmem;
  const char *debug_getname() { return "VirtualBiosMultiboot"; };

  /**
   * Initialize an MBI from the hip.
   */
  unsigned long init_mbi(unsigned long &rip)
  {

    MessageHostOp msg1(MessageHostOp::OP_GUEST_MEM, 0);
    if (!(_mb.bus_hostop.send(msg1))) Logging::panic("could not find base address %x\n", 0);
    char *physmem = msg1.ptr;
    unsigned long memsize = msg1.len;
    unsigned long offset = _modaddr;
    unsigned long mbi = 0;
    Mbi *m = 0;

    // get modules from sigma0
    for (unsigned modcount = 0; ; modcount++)
      {
	offset = (offset + 0xfff) & ~0xffful;
	MessageHostOp msg(modcount + 1, physmem + offset);
	if (!(_mb.bus_hostop.send(msg)) || !msg.size)  break;
	Logging::printf("\tmodule %x start %p+%lx cmdline %40s\n", modcount, msg.start, msg.size, msg.cmdline);

	switch(modcount)
	  {
	  case 0:
	    if (Elf::decode_elf(msg.start, physmem, rip, offset, memsize, 0)) return 0;
	    offset = (offset + 0xfff) & ~0xffful;
	    mbi = offset;
	    offset += 0x1000;
	    m = reinterpret_cast<Mbi*>(physmem + mbi);
	    if (offset > memsize)  return 0;
	    memset(m, 0, sizeof(*m));
	    memmove(physmem + offset, msg.cmdline, msg.cmdlen);
	    m->cmdline = offset;
	    offset += msg.cmdlen;
	    m->flags |= MBI_FLAG_CMDLINE;
	    break;
	  default:
	    {
	      m->flags |= MBI_FLAG_MODS;
	      m->mods_addr = reinterpret_cast<char *>(m + 1) - physmem;
	      Module *mod = reinterpret_cast<Module *>(physmem + m->mods_addr) + m->mods_count;
	      m->mods_count++;
	      mod->mod_start = msg.start - physmem;
	      mod->mod_end = mod->mod_start + msg.size;
	      mod->string = msg.cmdline - physmem;
	      mod->reserved = msg.cmdlen;
	      if (offset < mod->mod_end) offset = mod->mod_end;
	      if (offset < mod->string + msg.cmdlen) offset = mod->string + msg.cmdlen;
	    }
	    break;
	  }
      }

    if (!m) return 0;

    // provide memory map
    MbiMmap mymap[] = {{20, 0, _lowmem, 0x1},
		       {20, 1<<20, memsize - (1<<20), 0x1}};
    m->mem_lower = 640;
    m->mem_upper = (memsize >> 10) - 1024;
    m->mmap_addr  = offset;
    m->mmap_length = sizeof(mymap);
    m->flags |= MBI_FLAG_MMAP | MBI_FLAG_MEM;
    memcpy(physmem + m->mmap_addr, mymap, m->mmap_length);
    return mbi;
  };


 public:
  bool  receive(MessageBios &msg)
  {
    if (msg.irq != 19) return false;
    Logging::printf(">\t%s mtr %x rip %x ilen %x cr0 %x efl %x\n", __PRETTY_FUNCTION__,
		    msg.cpu->head.mtr.value(), msg.cpu->eip, msg.cpu->inst_len, msg.cpu->cr0, msg.cpu->efl);

    unsigned long rip = 0xfffffff0;
    unsigned long mbi;

    if (!(mbi = init_mbi(rip)))  return false;
    msg.cpu->eip      = rip;
    msg.cpu->eax      = 0x2badb002;
    msg.cpu->ebx      = mbi;
    msg.cpu->cr0      = 0x11;
    msg.cpu->cs.ar    = 0xc9b;
    msg.cpu->cs.limit = 0xffffffff;
    msg.cpu->cs.base  = 0x0;
    msg.cpu->ss.ar    = 0xc93;
    msg.cpu->efl      = 2;
    msg.cpu->ds.ar = msg.cpu->es.ar = msg.cpu->fs.ar = msg.cpu->gs.ar = msg.cpu->ss.ar;
    msg.cpu->ld.ar    = 0x1000;
    msg.cpu->tr.ar    = 0x8b;
    msg.cpu->ss.base  = msg.cpu->ds.base  = msg.cpu->es.base  = msg.cpu->fs.base  = msg.cpu->gs.base  = msg.cpu->cs.base;
    msg.cpu->ss.limit = msg.cpu->ds.limit = msg.cpu->es.limit = msg.cpu->fs.limit = msg.cpu->gs.limit = msg.cpu->cs.limit;
    msg.cpu->tr.limit = msg.cpu->ld.limit = msg.cpu->gd.limit = msg.cpu->id.limit = 0xffff;
    msg.cpu->head.mtr = Mtd(MTD_ALL, 0);
    Cpu::atomic_or<volatile unsigned>(&msg.vcpu->hazard, VirtualCpuState::HAZARD_CRWRITE);
    return true;
  }

  VirtualBiosMultiboot(Motherboard &mb, unsigned long modaddr, unsigned lowmem) : _mb(mb), _modaddr(modaddr), _lowmem(lowmem) {}
};

PARAM(vbios_multiboot,
      {
	mb.bus_bios.add(new VirtualBiosMultiboot(mb,
						 argv[0]!= ~0ul ? argv[0] : 0x1000000,
						 argv[1]!= ~0ul ? argv[1] : 0xa0000),
			&VirtualBiosMultiboot::receive_static);
      },
      "vbios_multiboot:modaddr=0x1000000,lowmem=0xa0000 - create a BIOS extension that supports multiboot",
      "Example:  'vbios_multiboot'",
      "modaddr defines where the modules are loaded in guest memory.",
      "lowmem allows to restrict memory below 1M to less than 640k.");