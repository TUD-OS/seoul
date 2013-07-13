/* Logging support.
 *
 * Copyright (C) 2012, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
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

#include <service/logging.h>
#include <nul/motherboard.h>
#include <host/screen.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

class LoggingView : public StaticReceiver<LoggingView> {

  unsigned              _view;
  DBus<MessageConsole> &_bus_console;
  VgaRegs               _regs;
  unsigned short       *_base;

public:

  void putchar(int c)
  {
    Screen::vga_putc(0x0700 | c, _base, _regs.cursor_pos);
  }

  void vprintf(const char *format, va_list &ap)
  {
    char buf[512];
    vsnprintf(buf, sizeof(buf), format, ap);

    char *cur = buf;
    while (*cur)
      putchar(*(cur++));
  }

  void panic(const char *format, va_list &ap)
  {
    MessageConsole msg(MessageConsole::TYPE_SWITCH_VIEW);
    msg.view = _view;
    _bus_console.send(msg);

    vprintf(format, ap);
    putchar('\n');

    // XXX
    sleep(1);
  }

  LoggingView(DBus<MessageConsole> &bus_console, char *mem, size_t size)
    : _bus_console(bus_console), _regs(), _base(reinterpret_cast<unsigned short *>(mem))
  {
    _regs.offset     = 0;
    _regs.cursor_pos = 0;
    memset(mem, 0, size);

    // alloc console
    MessageConsole msg("VMM", mem, size, &_regs);
    if (!bus_console.send(msg))
      Logging::panic("could not alloc a VGA backend");
    _view = msg.view;
  }

};

static LoggingView *view;

void Logging::panic(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);

  if (view) {
    view->panic(format, ap);
  } else {
    Logging::vprintf(format, ap);
    Logging::printf("\n");
  }

  va_end(ap);
  abort();
}

void Logging::printf(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  Logging::vprintf(format, ap);
  va_end(ap);
}


void Logging::vprintf(const char *format, va_list &ap)
{
  if (view)
    view->vprintf(format, ap);
  else
    ::vfprintf(stderr, format, ap);
}

PARAM_HANDLER(logging,
              "")
{
  size_t fbsize = 128 << 10;
  MessageHostOp msg(MessageHostOp::OP_ALLOC_FROM_GUEST, fbsize);
  MessageHostOp msg2(MessageHostOp::OP_GUEST_MEM, 0UL);
  if (!mb.bus_hostop.send(msg) || !mb.bus_hostop.send(msg2))
    Logging::panic("%s failed to alloc %zu from guest memory\n", __PRETTY_FUNCTION__, fbsize);

  LoggingView *v = new LoggingView(mb.bus_console, msg2.ptr + msg.phys, fbsize);
  view = v;

}

// EOF
