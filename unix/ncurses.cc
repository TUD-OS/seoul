/**
 * VGA text mode display
 *
 * Copyright (C) 2013, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
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
#include <nul/vcpu.h>
#include <host/screen.h>
#include <vector>
#include <curses.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include <seoul/unix.h>

class NcursesDisplay : public StaticReceiver<NcursesDisplay> {
  struct View {
    const char *name;
    const char *ptr;
    size_t      size;
    VgaRegs    *regs;

    View(const char *name, const char *ptr, size_t size, VgaRegs *regs)
      : name(name), ptr(ptr), size(size), regs(regs)
    {}

  };

  Motherboard          &mb;
  std::vector<View>     views;
  unsigned              current_view;
  double                boot_time;

  double now()
  {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return double(tv.tv_sec) + double(tv.tv_usec)/1000000;
  }


  void render_bar()
  {
    color_set(0x70, 0);
    mvprintw(25, 0, "%s: VM running %lus. Navigate using arrow keys. Quit with q. ",
             (views.size() and current_view < views.size()) ?
             views[current_view].name : "???",
             static_cast<unsigned long>(now() - boot_time));
    clrtobot();
  }

  void render_line(int y)
  {
    if (current_view < views.size()) {
      View           &view = views[current_view];
      uint16_t const *base = reinterpret_cast<uint16_t const *>(view.ptr + (view.regs->offset << 1));
      for (unsigned x = 0; x < 80; x ++) {
        uint16_t c = base[y*80 + x];
        int nc = c & 0xFF;
        if (nc == 0) nc = ' ';
        if (c & 0x8000) nc |= A_BLINK;

        color_set((c >> 8) & 0x7F, 0);
        mvaddch(y, x, nc);
      }
    }
    else {
      move(y, 0);
      clrtoeol();
    }
  }

  void display_loop()
  {
    initscr();
    raw();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    timeout(100);
    curs_set(0);
    start_color();

    int cols[] = { COLOR_BLACK,
                   COLOR_BLUE,
                   COLOR_GREEN,
                   COLOR_CYAN,
                   COLOR_RED,
                   COLOR_MAGENTA,
                   COLOR_YELLOW,
                   COLOR_WHITE,
    };

    for (unsigned n = 0; n < (1 << 7); n++) {
      unsigned fg = (n & 0xF)  % 8;
      unsigned bg = (n >> 4)   % 8;
      init_pair(n, cols[fg], cols[bg]);
    }

    clear();
    while (true) {
      for (unsigned y = 0; y < 25; y ++)
        render_line(y);
      render_bar();
      refresh();
      switch (getch()) {
      case 'q':
        goto done;
      case KEY_HOME: {
        MessageConsole msg(MessageConsole::TYPE_RESET);
        pthread_mutex_lock(&irq_mtx);
        mb.bus_console.send(msg);
        pthread_mutex_unlock(&irq_mtx);
      }
        break;

      case KEY_F(12): {
        pthread_mutex_lock(&irq_mtx);
        CpuEvent msg(VCpu::EVENT_DEBUG);
        for (VCpu *vcpu = mb.last_vcpu; vcpu; vcpu=vcpu->get_last())
          vcpu->bus_event.send(msg);
        pthread_mutex_unlock(&irq_mtx);
      }
        break;

      case KEY_LEFT:
      case KEY_UP:
        if (current_view) current_view --;
        break;
      case KEY_RIGHT:
      case KEY_DOWN:
        if (views.size())
          if (current_view < views.size() - 1)
            current_view ++;
        break;

      case KEY_BACKSPACE: {
        /* Migration example start event. As soon as the user hits this event,
         * the VM will be migrated to the hard coded destination host. */
        MessageHostOp msg(MessageHostOp::OP_MIGRATION_START,
                /* destination ip, address: 192.168.0.1 */ 0xC0A80001ul);
        mb.bus_hostop.send(msg);
      }
      case ERR:
      default:
        break;
      }
    }
  done:
    endwin();

    // XXX Not the nice way...
    exit(EXIT_SUCCESS);
  }

public:

  static void *display_loop(void *arg)
  {
    reinterpret_cast<NcursesDisplay *>(arg)->display_loop();
    return nullptr;
  }

  bool receive(MessageConsole &msg)
  {
    switch (msg.type)
      {
      case MessageConsole::TYPE_ALLOC_CLIENT:
        Logging::panic("console: ALLOC_CLIENT not supported.\n");
      case MessageConsole::TYPE_ALLOC_VIEW:
        assert(msg.ptr and msg.regs);
        current_view = msg.view = views.size();;
        views.push_back(View(msg.name, msg.ptr, msg.size, msg.regs));
        return true;
      case MessageConsole::TYPE_SWITCH_VIEW:
        current_view = msg.view;
        return true;
      case MessageConsole::TYPE_GET_MODEINFO:
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

  NcursesDisplay(Motherboard &mb)
    : mb(mb), current_view(0) {
    boot_time = now();
  }
};


PARAM_HANDLER(ncurses,
              "ncurses - ncurses display")
{
  NcursesDisplay *d = new NcursesDisplay(mb);;

  mb.bus_console.add(d, NcursesDisplay::receive_static<MessageConsole>);

  pthread_t p;
  pthread_create(&p, NULL, NcursesDisplay::display_loop, d);
}

// EOF
