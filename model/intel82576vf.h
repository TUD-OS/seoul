// -*- Mode: C++ -*-
/** @file
 * Intel 82576 VF device model.
 *
 * Copyright (C) 2010, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2013 Jacek Galowicz, Intel Corporation.
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

#include <service/net.h>

class Mta {
  uint32 _bits[128];

public:
  
  static uint16 hash(EthernetAddr const &addr)
  {
    return 0xFFF & (((addr.byte[4] >> 4)
		     | static_cast<uint16>(addr.byte[5]) << 4));
  }

  bool includes(EthernetAddr const &addr) const
  {
    uint16 h = hash(addr);
    return (_bits[(h >> 5) & 0x7F] & (1 << (h & 0x1F))) != 0;
  }

  void set(uint16 hash) { _bits[(hash >> 5) & 0x7F] |= 1 << (hash&0x1F); }
  void clear() { memset(_bits, 0, sizeof(_bits)); }

  Mta() : _bits() { }
};

struct arp_packet {
    unsigned char destination[6];
    unsigned char source[6];
    unsigned short eth_type;
    unsigned short hw_type;
    unsigned short protocol_type;
    unsigned char hwaddr_len;
    unsigned char protocoladdr_len;
    unsigned short operation;
    unsigned char sender_hwaddr[6];
    unsigned sender_ip;
    unsigned char target_hwaddr[6];
    unsigned target_ip;

    arp_packet(EthernetAddr src, EthernetAddr dst, unsigned ip_addr,
            unsigned short _operation)
        :
            eth_type(0x608), hw_type(0x100), protocol_type(0x8), hwaddr_len(6),
            protocoladdr_len(4), operation(_operation),
            sender_ip(ip_addr), target_ip(ip_addr)
    {
        memcpy(destination, dst.byte, 6);
        memset(target_hwaddr, 0, 6);
        memcpy(source, src.byte, 6);
        memcpy(sender_hwaddr, src.byte, 6);
    }

    bool source_is(const EthernetAddr &a) const
    {
        EthernetAddr my_addr(*reinterpret_cast<const uint64*>(destination));
        return my_addr == a;
    }
} __attribute__((packed));

// EOF
