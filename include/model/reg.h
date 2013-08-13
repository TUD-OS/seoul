/** @file
 * Generic hardware register definition.
 *
 * Copyright (C) 2009, Bernhard Kauer <bk@vmmon.org>
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
#define VMM_DEFINE_REG(NAME, OFFSET, VALUE, MASK) private: unsigned NAME; public: static const unsigned NAME##_offset = OFFSET; static const unsigned NAME##_mask   = MASK; static const unsigned NAME##_reset  = VALUE;
#define VMM_REG_RO(NAME, OFFSET, VALUE) VMM_REG(NAME, OFFSET, static const unsigned NAME = VALUE;, value = VALUE; , break; , )
#define VMM_REG_RW(NAME, OFFSET, VALUE, MASK, WRITE_CALLBACK) VMM_REG(NAME, OFFSET, VMM_DEFINE_REG(NAME, OFFSET, VALUE, MASK) , value = NAME; , if (!MASK) return false; if (strict && value & ~MASK) return false; NAME = (NAME & ~MASK) | (value & MASK); WRITE_CALLBACK; , NAME=VALUE;)
#define VMM_REG_WR(NAME, OFFSET, VALUE, MASK, RW1S, RW1C, WRITE_CALLBACK) VMM_REG(NAME, OFFSET, VMM_DEFINE_REG(NAME, OFFSET, VALUE, MASK), value = NAME; ,  if (!MASK) return false; unsigned oldvalue = NAME; value = value & ~RW1S | ( value | oldvalue) & RW1S; value = value & ~RW1C | (~value & oldvalue) & RW1C; NAME = (NAME & ~MASK) | (value & MASK); WRITE_CALLBACK; , NAME = VALUE;)
#define VMM_REGSET(NAME, ...) private: __VA_ARGS__
#define VMM_REG(NAME, OFFSET, MEMBER, READ, WRITE, RESET) MEMBER
#include VMM_REGBASE
#undef  VMM_REG
#undef  VMM_REGSET
#define VMM_REGSET(NAME, ...)  bool NAME##_read(unsigned offset, unsigned &value) { switch (offset) { __VA_ARGS__ default: break; } return false; }
#define VMM_REG(NAME, OFFSET, MEMBER, READ, WRITE, RESET) case OFFSET:  { READ }; return true;
#include VMM_REGBASE
#undef  VMM_REG
#undef  VMM_REGSET
#define VMM_REGSET(NAME, ...)  bool NAME##_write(unsigned offset, unsigned value, bool strict=false) { switch (offset) { __VA_ARGS__ default: break; } return 0; }
#define VMM_REG(NAME, OFFSET, MEMBER, READ, WRITE, RESET) case OFFSET:  { WRITE }; return true;
#include VMM_REGBASE
#undef  VMM_REG
#undef  VMM_REGSET
#define VMM_REGSET(NAME, ...)  void NAME##_reset() { __VA_ARGS__ }; private:
#define VMM_REG(NAME, OFFSET, MEMBER, READ, WRITE, RESET) RESET
#include VMM_REGBASE
#undef  VMM_REG
#undef  VMM_REGSET
#undef  VMM_REG_WR
#undef  VMM_REG_RW
#undef  VMM_REG_RO
#undef  VMM_DEFINE_REG
#undef VMM_REGBASE

/**
 * \def VMM_REG_RO(NAME, OFFSET, VALUE)
 *
 * Defines a read-only register.
 */

/**
 * \def VMM_REG_RW(NAME, OFFSET, VALUE, MASK, WRITE_CALLBACK)
 *
 * Defines a read/write register.
 *
 * \param NAME Name of the register
 * \param OFFSET Offset of the register
 * \param VALUE Reset value of the register.
 * \param MASK Defines which bits of the register are writable - the corresponding bit is set to 1
 * \param WRITE_CALLBACK Code that is executed whenever the register gets a new value.
 */

/**
 * \def VMM_REG_WR(NAME, OFFSET, VALUE, MASK, RW1S, RW1C, WRITE_CALLBACK)
 *
 * Defines a read/write register with set/clear bits.
 *
 * \param NAME Name of the register
 * \param OFFSET Offset of the register
 * \param VALUE Reset value of the register.
 * \param MASK Defines which bits of the register are writable - the corresponding bit is set to 1
 * \param RW1S Defines which bits of the register are set when 1 is written to the bit. Writing 0 to that bit does nothing.
 * \param RW1C Defines which bits of the register are cleared when 1 is written to the bit. Writing 0 to that bit does nothing.
 * \param WRITE_CALLBACK Code that is executed whenever the register gets a new value.
 */

/**
 * \def VMM_REGSET(NAME, ...)
 *
 * Defines a set of registers.
 */
