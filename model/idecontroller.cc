/**
 * IDE emulation.
 *
 * Copyright (C) 2011, Bernhard Kauer <bk@vmmon.org>
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

#ifndef VMM_REGBASE
#include "nul/motherboard.h"
#include "model/pci.h"
#include "host/dma.h"

//#define DEBUG
#ifdef DEBUG
#   define LOG(fmt, ...)        Logging::printf(fmt, ## __VA_ARGS__)
#else
#   define LOG(fmt, ...)
#endif

/**
 * An IDE controller on a PCI card.
 *
 * State: unstable
 * Features: PCI cfg space, IDE Regs, Disk access, IRQ
 * Missing: MSI
 * Documentation: pciide.pdf, d1697r0c-ATA8-AST.pdf AnnexE
 */
class IdeController : public StaticReceiver<IdeController>
{
public:
  enum {
    BUFFER_SIZE = 4096,
  };
private:
  DBus<MessageDisk> &_bus_disk;
  DBus<MessageIrqLines>  &_bus_irqlines;
  unsigned char      _irq;
  unsigned           _bdf;
  unsigned           _disknr;
  DiskParameter      _params;
  union {
    struct {
      unsigned short _features,  _count, _lbalow, _lbamid, _lbahigh, _drive;
    };
    unsigned short   _regs[6];
  };
  unsigned char      _command, _error, _status, _control;
  char              *_buffer;
  unsigned long      _baddr;
  unsigned           _bufferoffset;

#define  VMM_REGBASE "../model/idecontroller.cc"
#include "model/reg.h"


  unsigned long long get_sector(bool lba48) {
    unsigned long long res = (_lbalow & 0xff) | (_lbamid & 0xff) << 8 | (_lbahigh & 0xff) << 16;
    if (lba48)
      res |= (_lbalow & 0xff00) << (24-8) | static_cast<unsigned long long>(_lbamid & 0xff00) << (32-8) | static_cast<unsigned long long>(_lbahigh & 0xff00) << (40-8);
    return res;
  }

  void set_sector(unsigned long long sector) {
    _lbalow  = ((sector  >> 0) & 0xff) | ((sector >>  (24-8)) & 0xff00);
    _lbamid  = ((sector  >> 8) & 0xff) | ((sector >>  (32-8)) & 0xff00);
    _lbahigh = ((sector  >>16) & 0xff) | ((sector >>  (40-8)) & 0xff00);
  }


  void reset_device() {
    _status = 0x50;
    _error  = 0;
    _count  = _lbalow = 0x1;
    _lbamid = _lbahigh = 0;
    _drive  = 0xa0;
    _command = 0;
  }

  void update_irq(bool assert) {
    assert = assert && (~_control & 2);
    LOG("update irq %x\n", assert);
    MessageIrqLines msg(assert ? MessageIrqLines::ASSERT_IRQ : MessageIrqLines::DEASSERT_IRQ, _irq);
    _bus_irqlines.send(msg);
  }

  void build_identify_buffer(unsigned short *identify)
  {
    memset(identify, 0, 512);
    identify[0] = 0x0040;// fixed disk
    identify[1] = 16383; // maximum cyclinders
    identify[6] = 63; // sectors per track
    // heads
    identify[3] = (_params.sectors > 255u*identify[1]*identify[6]) ? 255 : (static_cast<unsigned>( _params.sectors) / identify[1]*identify[6]);

    identify[10] = 'S';// SN
    identify[23] = 'F';// FW
    for (unsigned i=0; i<20; i++)
      identify[27+i] = _params.name[2*i] << 8 | _params.name[2*i+1];
    identify[48] = 0x0001; // dword IO
    identify[49] = 0x0200; // lba supported
    identify[53] = 0x0006; // bytes 64-70, 88 are valid
    identify[54] = identify[1]; // current cylinders
    identify[55] = identify[3]; // current heads
    identify[56] = identify[6]; // current sectors per track
    identify[57] = 512; // current sectors capacity

    unsigned maxlba28 = (_params.sectors >> 28) ? 0x0fffffff :  _params.sectors;
    Cpu::move<2>(identify + 60, &maxlba28);
    identify[65] = identify[66] = identify[67] = identify[68] = 120; // PIO timing
    identify[80] = 0x7e;   // major version number: up to ata-6
    identify[83] = 0x4400; // LBA48 supported
    identify[84] = 0x4000; // shall be set
    identify[85] = 0x4000; // shall be set
    identify[86] = 0x4400; // LBA48 enabled
    identify[87] = 0x4000; // shall be set
    identify[93] = 0x6001; // hardware reset result
    Cpu::move<3>(identify+100, &_params.sectors);
    identify[0xff] = 0xa5;

    unsigned char checksum = 0;
    for (unsigned i=0; i<512; i++) checksum += reinterpret_cast<unsigned char *>(identify)[i];
    identify[0xff] -= checksum << 8;
  }

  void do_read(bool initial, unsigned long long sector) {
    if (!initial and !_count)  {
      _status &= ~0x88; // no data anymore
      return;
    }

    _status = _status  & ~0x81 | 0x80;
    _bufferoffset = 0;
    memset(_buffer, 0xff, 512);
    DmaDescriptor dma = { _baddr, 512};
    MessageDisk msg(MessageDisk::DISK_READ, _disknr, 0, sector, 1, &dma, 0, ~0ul);
    if (!_bus_disk.send(msg)) {
      _status = _status  & ~0x80 | 0x1;
      _error  |= 1<<5; // device fault
      return;
    }
  }

  void issue_command(bool initial) {
    // reset asserted?
    if (_control & 4) return;
    // slave?
    if (_drive & 0x10) {
      _status |= 1;
      _error  = 0x7e;
      update_irq(true);
      return;
    }
    switch (_command) {
    case 0x20: // READ_SECTOR
      do_read(initial, get_sector(false));
      break;
    case 0x24: // READ_SECTOR_EXT
      do_read(initial, get_sector(true));
      break;
    case 0xec: // IDENTIFY
      if (!initial) {
	_status &= ~0x89; // no data anymore
	break;
      }
      build_identify_buffer(reinterpret_cast<unsigned short *>(_buffer));
      _bufferoffset = 0;
      _status = _status  & ~0x89 | 0x8;
      _error  = 0;
      update_irq(true);
      break;
    case 0xa1: // packet identify
    case 0xc6: // multiple count
      _status = _status  & ~0x89 | 1;
      _error |= 4; // abort
      update_irq(true);
      break;
    case 0x08: // RESET DEVICE
      reset_device();
      update_irq(true);
      break;
    case 0xef: // SET FEATURES
      LOG("SET FEATURES %x sc %x\n", _features, _count);
      _status = _status  & ~0x89;
      update_irq(true);
      break;
   case 0x27: // READ_NATIVE_MAX_ADDRESS48
     set_sector(_params.sectors - 1);
     update_irq(true);
     break;
    default:
      Logging::panic("unimplemented command %x\n", _command);
    }
  }

 public:
  bool receive(MessageDiskCommit &msg)
  {
    if (msg.disknr != _disknr) return false;
    // XXX abort command
    assert(!msg.status);
    // some operation completed, clear the busy flag and set the DRQ on reads
    switch (_command) {
    case 0x20: // READ_SECTOR
    case 0x24: // READ_SECTOR_EXT
      _status = _status & ~0x80 | 0x8; // we have data

      // increment sector and decrement count
      set_sector(get_sector(_command == 0x24)+1);
      _count--;
      update_irq(true);
      return true;
    }
    return false;
  }

  bool  receive(MessageIOIn &msg)
  {
    if (!((msg.port ^ PCI_BAR0) & PCI_BAR0_mask)) {
      unsigned port = msg.port & ~PCI_BAR0_mask;
      if (port and msg.type != MessageIOIn::TYPE_INB) return false;
      switch (port) {
      case 0:
	if (_bufferoffset >= 512) return false;
	Cpu::move(&msg.value, _buffer + _bufferoffset, msg.type);
	if (!_bufferoffset) { LOG("data[%d] = %04x\n", _bufferoffset, msg.value); }
	_bufferoffset += 1 << msg.type;
	// reissue the command if work left
	if (_bufferoffset >= 512)  issue_command(false);
	break;
      case 1:
	msg.value = _error;
	break;
      case 2 ... 6:
	Cpu::move<0>(&msg.value, reinterpret_cast<unsigned char *>(_regs + port - 1) + ((_control & 0x80) >> 7));
	break;
      case 7:
	msg.value = _status;
	update_irq(false);
	break;
      default:
	assert(0);
      }
      if (port) { LOG("in<%d>[%d] = %x\n", msg.type, port, msg.value); }
      return true;
    }
    // alternate status register
    if (!((msg.port ^ PCI_BAR1) & PCI_BAR1_mask) and msg.type == MessageIOIn::TYPE_INB and ((msg.port & ~PCI_BAR1_mask) == 2)) {
      LOG("alternate status %x\n", _status);
      msg.value = _status;
      return true;
    }
    return false;
  }


  bool  receive(MessageIOOut &msg)
  {
    if (!((msg.port ^ PCI_BAR0) & PCI_BAR0_mask)) {
      unsigned port = msg.port & ~PCI_BAR0_mask;
      if (port and msg.type != MessageIOOut::TYPE_OUTB) return false;
      LOG("out<%d>[%d] = %x\n", msg.type, port, msg.value);
      switch (port) {
      case 0:
	if (_bufferoffset >= 512) return false;
	Cpu::move(_buffer+_bufferoffset, &msg.value, msg.type);
	_bufferoffset += 1 << msg.type;
	return true;
      case 1 ... 6:
	_regs[port - 1] = (_regs[port - 1] << 8) | (msg.value & 0xff);
	if (port == 6) {
	  //_drive |= 0xa0;
	  if (_drive & 0x10) _status &= ~0x40;  else _status |= 0x40;
	}
	return true;
      case 7:
	_command = msg.value;
	LOG("issue command %x\n", _command);
	issue_command(true);
	return true;
      }
    }
    if (!((msg.port ^ PCI_BAR1) & PCI_BAR1_mask) and msg.type == MessageIOOut::TYPE_OUTB and ((msg.port & ~PCI_BAR1_mask) == 2)) {
      // toggle reset?
      if (_control & 4 && ~msg.value & 4) reset_device();
      _control = msg.value;
      LOG("control %x\n", _control);
      return true;
    }
    return false;
  }


  bool receive(MessagePciConfig &msg) { return PciHelper::receive(msg, this, _bdf); }


  IdeController(DBus<MessageDisk> &bus_disk, DBus<MessageIrqLines> &bus_irqlines,
		unsigned char irq, unsigned bdf, unsigned disknr, DiskParameter params, char *buffer, unsigned long baddr)
    : _bus_disk(bus_disk), _bus_irqlines(bus_irqlines),
      _irq(irq), _bdf(bdf), _disknr(disknr), _params(params), _buffer(buffer), _baddr(baddr), _bufferoffset(0)
  {
    PCI_reset();
    reset_device();
    Logging::printf("Instanciated IDE controller with bdf %#x for disk '%s' with %#Lx sectors\n",
                    bdf, params.name, params.sectors);
  }
};

PARAM_HANDLER(ide,
	      "ide:port0,port1,irq,bdf,disk - attach an IDE controller to a PCI bus.",
	      "Example: Use 'ide:0x1f0,0x3f6,14,0x38' to attach an IDE controller to 00:07.0 on legacy ports 0x1f0/0x3f6 with irq 14.",
	      "If no bdf is given, the first free one is searched.")
{
  DiskParameter params;
  MessageDisk msg(argv[4], &params);
  check0(!mb.bus_disk.send(msg), "could not find disk #%x", msg.disknr);

  MessageHostOp msg1(MessageHostOp::OP_ALLOC_FROM_GUEST, (unsigned long)IdeController::BUFFER_SIZE);
  MessageHostOp msg2(MessageHostOp::OP_GUEST_MEM, 0UL);
  if (!mb.bus_hostop.send(msg1) || !mb.bus_hostop.send(msg2))
    Logging::panic("%s failed to alloc %d from guest memory\n", __PRETTY_FUNCTION__, IdeController::BUFFER_SIZE);
  unsigned bdf = PciHelper::find_free_bdf(mb.bus_pcicfg, argv[3]);
  IdeController *dev = new IdeController(mb.bus_disk, mb.bus_irqlines, argv[2], bdf, msg.disknr, params, msg2.ptr + msg1.phys, msg1.phys);
  mb.bus_pcicfg.add(dev, IdeController::receive_static<MessagePciConfig>);
  mb.bus_ioin.  add(dev, IdeController::receive_static<MessageIOIn>);
  mb.bus_ioout. add(dev, IdeController::receive_static<MessageIOOut>);
  mb.bus_diskcommit.add(dev, IdeController::receive_static<MessageDiskCommit>);
  // set default state; this is normally done by the BIOS
  // set MMIO region and IRQ
   dev->PCI_write(IdeController::PCI_BAR0_offset, argv[0]);
   dev->PCI_write(IdeController::PCI_BAR1_offset, argv[1]);
   dev->PCI_write(IdeController::PCI_INTR_offset, argv[2]);
  // enable IRQ and IOPort access
   dev->PCI_write(IdeController::PCI_CMD_STS_offset, 0x401);
}
#else

VMM_REGSET(PCI,
       VMM_REG_RO(PCI_ID,        0x0, 0x275c8086)
       VMM_REG_RW(PCI_CMD_STS,   0x1, 0x100000, 0x0401,)
       VMM_REG_RO(PCI_RID_CC,    0x2, 0x01010102)
       VMM_REG_RW(PCI_BAR0,      0x4, 1, 0x0000fff8,)
       VMM_REG_RW(PCI_BAR1,      0x5, 1, 0x0000fffc,)
       VMM_REG_RO(PCI_SS,        0xb, 0x275c8086)
       VMM_REG_RO(PCI_CAP,       0xd, 0x00)
       VMM_REG_RW(PCI_INTR,      0xf, 0x0100, 0xff,));
#endif
