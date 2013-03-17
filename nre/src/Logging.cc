/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <service/logging.h>
#include <stream/Serial.h>
#include <util/Util.h>

using namespace nre;

void Logging::panic(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    nre::Util::vpanic(format, ap);
}

void Logging::printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    Serial::get().vwritef(format, ap);
    va_end(ap);
}

void Logging::vprintf(const char *format, va_list &ap) {
    Serial::get().vwritef(format, ap);
}
