/**
 * Synthetic Testing Environment
 *
 * Copyright (C) 2013 Markus Partheymueller, Intel Corporation.
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

#include <iostream>

#ifdef PICTEST
#include "pic.h"
#endif

#ifdef IOAPICTEST
#include "ioapic.h"
#endif

#ifdef LAPICTEST
#include "lapic.h"
#endif

#ifdef SATATEST
#include "sata.h"
#endif

int main(int argc, char **argv) {
  std::cout << "Hello, this is Seoulcheck." << std::endl;

#ifdef PICTEST
  std::cout << "Running PIC test." << std::endl;
  runPicTest();
#endif

#ifdef IOAPICTEST
  std::cout << "Running I/O APIC test." << std::endl;
  runIOAPicTest();
#endif

#ifdef LAPICTEST
  std::cout << "Running LAPIC test." << std::endl;
  runLAPICTest();
#endif

#ifdef SATATEST
  std::cout << "Running SATA test." << std::endl;
  runSATATest();
#endif

}
