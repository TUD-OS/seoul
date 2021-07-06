// ASL Example
DefinitionBlock (
        "dsdt.aml", // Output Filename
        "DSDT",     // Signature
        0x00,       // DSDT Compliance Revision
        "BAMM",     // OEMID
        "JONGE",    // TABLE ID
        0x1         // OEM Revision
        )
{
    Scope(\_SB) {
        Device(PCI0) {
            // The following magic code stands for "PCI Host Bridge"
            Name(_HID, EisaId("PNP0A03"))
            Name(_ADR, 0)
            Name(_UID, 0)

            // Hot Plug Parameters. Optional.
            // Linux will complain and use standard parameters,
            // if not given.
            Name(_HPP, Package(){
                0x08,  // Cache line size in dwords
                0x40,  // Latency timer in PCI clocks
                0x01,  // Enable SERR line
                0x00   // Enable PERR line
            })

            // PCI Routing Table
            // When defining as much ACPI information as
            // needed for hotplug, we also have to define
            // stuff like the following.
            // Otherwise, Linux would complain.
            Name(_PRT, Package() {
                Package() { 0x1ffff, 0, LNKA, 0 },
                Package() { 0x1ffff, 1, LNKB, 0 },
                Package() { 0x1ffff, 2, LNKC, 0 },
                Package() { 0x1ffff, 3, LNKD, 0 },

                Package() { 0x2ffff, 0, LNKA, 0 },
                Package() { 0x2ffff, 1, LNKB, 0 },
                Package() { 0x2ffff, 2, LNKC, 0 },
                Package() { 0x2ffff, 3, LNKD, 0 },

                Package() { 0x3ffff, 0, LNKA, 0 },
                Package() { 0x3ffff, 1, LNKB, 0 },
                Package() { 0x3ffff, 2, LNKC, 0 },
                Package() { 0x3ffff, 3, LNKD, 0 },

                Package() { 0x4ffff, 0, LNKA, 0 },
                Package() { 0x4ffff, 1, LNKB, 0 },
                Package() { 0x4ffff, 2, LNKC, 0 },
                Package() { 0x4ffff, 3, LNKD, 0 },
            })

            // At boot, Linux will either scan the system for
            // possible resources used by PCI cards or read
            // ACPI tables to obtain this information.
            // When providing as much ACPI data as needed
            // for hotplugging, then this is not optional any longer.
            // Linux would complain if all this was not provided here.
            Name (_CRS, ResourceTemplate () {
                // Bus enumeration from _MIN to _MAX
                WordBusNumber (
                    ResourceProducer,
                    MinFixed,     // _MIF
                    MaxFixed,     // _MAF
                    ,
                    0x00,         // _GRA
                    0x00,         // _MIN
                    0xFF,         // _MAX
                    0x00,         // _TRA
                    0x100)        // _LEN
                // IO ports usable by PCI from _MIN to _MAX
                WordIO (
                    ResourceProducer,
                    MinFixed,     // _MIF
                    MaxFixed,     // _MAF
                    PosDecode,
                    EntireRange,
                    0x0000,       // _GRA
                    0x0000,       // _MIN
                    0x7FFF,       // _MAX
                    0x00,         // _TRA
                    0x8000)       // _LEN
                // System memory for mapping BAR areas from _MIN to _MAX
                // BAR = Base Address Register, every PCI card will
                // usually have 2 of those.
                DWordMemory (
                    ResourceProducer,
                    PosDecode,
                    MinFixed,     // _MIF
                    MaxFixed,     // _MAF
                    NonCacheable, // _MEM
                    ReadWrite,    // _RW
                    0x00000000,   // _GRA
                    0xE0000000,   // _MIN
                    0xE0FFFFFF,   // _MAX
                    0x00,         // _TRA
                    0x01000000)   // _LEN
            })

            // This introduced three names dword fields in IO space.
            // The hotplug controller knows these IO port.
            // During hot plug/unplug, guest and the hosts hotplug-
            // controller will communicate over these.
            OperationRegion(PCST, SystemIO, 0xae00, 12)
            Field (PCST, DWordAcc, NoLock, WriteAsZeros)
            {
                PCIU, 32, // IO port 0xae00
                PCID, 32, // IO port 0xae04
                B0EJ, 32, // IO port 0xae08
            }

            // Status method. Statically returns "Everything is up and working"
            // because the PCI root bus will always be there.
            Method (_STA, 0) { Return (0xf) }
        }

        // All this interrupt routing information is necessary.
        // This defines the interrupts A, B, C, D, considered legacy
        // nowadays.
        // Hotplugging etc. will work without this anyway if the PCI device uses
        // MSI for interrupting, but the kernel would complain with
        // ugly error messages.
        // This device definitions are kept as minimal as possible.
        Device(LNKA){
                Name(_HID, EISAID("PNP0C0F")) // PCI interrupt link
                Name(_UID, 1)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0B)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (BUFF, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared) {5}
                    })
                    Return (BUFF)
                }
                Method (_PRS, 0, NotSerialized)
			    {
				    Name (BUFF, ResourceTemplate ()
                    {
					IRQ (Level, ActiveLow, Shared) {5,9,10}
                    })
                    Return (BUFF)
                }
                Method (_SRS, 1, NotSerialized) {}
                Method (_DIS, 0, NotSerialized) {}
        }
        Device(LNKB){
                Name(_HID, EISAID("PNP0C0F")) // PCI interrupt link
                Name(_UID, 2)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0B)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (BUFF, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared) {10}
                    })
                    Return (BUFF)
                }
                Method (_PRS, 0, NotSerialized)
			    {
				    Name (BUFF, ResourceTemplate ()
                    {
					IRQ (Level, ActiveLow, Shared) {5,9,10}
                    })
                    Return (BUFF)
                }
                Method (_SRS, 1, NotSerialized) {}
                Method (_DIS, 0, NotSerialized) {}
        }
        Device(LNKC){
                Name(_HID, EISAID("PNP0C0F")) // PCI interrupt link
                Name(_UID, 3)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0B)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (BUFF, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared) {9}
                    })
                    Return (BUFF)
                }
                Method (_PRS, 0, NotSerialized)
			    {
				    Name (BUFF, ResourceTemplate ()
                    {
					IRQ (Level, ActiveLow, Shared) {5,9,10}
                    })
                    Return (BUFF)
                }
                Method (_SRS, 1, NotSerialized) {}
                Method (_DIS, 0, NotSerialized) {}
        }
        Device(LNKD){
                Name(_HID, EISAID("PNP0C0F")) // PCI interrupt link
                Name(_UID, 4)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0B)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (BUFF, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared) {5}
                    })
                    Return (BUFF)
                }
                Method (_PRS, 0, NotSerialized)
			    {
				    Name (BUFF, ResourceTemplate ()
                    {
					IRQ (Level, ActiveLow, Shared) {5,9,10}
                    })
                    Return (BUFF)
                }
                Method (_SRS, 1, NotSerialized) {}
                Method (_DIS, 0, NotSerialized) {}
        }

    }

    Scope(\_SB.PCI0) {
        // These are PCI slot definitions.
        // They are necessary because every PCI card
        // which shall be ejectable, needs an _EJ0 method.
        Device (S01) {
           Name (_ADR, 0x10000)
           Name (_SUN, 0x01) // SUN: Slot User Number

           // This method is called by the operating system
           // after unloading the device driver etc.
           // _EJ0 = eject callback
           Method (_EJ0, 1) { PCEJ(0x01) }
        }

        Device (S02) {
           Name (_ADR, 0x20000)
           Name (_SUN, 0x02)
           Method (_EJ0, 1) { PCEJ(0x02) }
        }

        Device (S03) {
           Name (_ADR, 0x30000)
           Name (_SUN, 0x03)
           Method (_EJ0, 1) { PCEJ(0x03) }
        }

        Device (S04) {
           Name (_ADR, 0x40000)
           Name (_SUN, 0x04)
           Method (_EJ0, 1) { PCEJ(0x04) }
        }

        // Called by some PCI card's _EJ0 method,
        // This tells the hypervisor to turn off the
        // PCI device by writing (1 << PCI_ID) to the
        // IO port associated with the B0EJ symbol.
        Method (PCEJ, 1, NotSerialized) {
            Store(ShiftLeft(1, Arg0), B0EJ)
            Return (0x0)
        }

        // PCNT = PCi NoTify
        // PCNT(<device>, <1 = check for inserted device / 3 = eject requested>)
        // The values 1 and 3 are defined in the ACPI spec
        Method(PCNT, 2) {
            If (LEqual(Arg0, 0x01)) { Notify(S01, Arg1) }
            If (LEqual(Arg0, 0x02)) { Notify(S02, Arg1) }
            If (LEqual(Arg0, 0x03)) { Notify(S03, Arg1) }
            If (LEqual(Arg0, 0x04)) { Notify(S04, Arg1) }
        }

        /* PCI hotplug notify method */
        Method(PCNF, 0) {
            // Local0 = iterator
            Store (Zero, Local0)

            // These two fields contain bits mapped
            // to PCI devices, like in the GPE bitmap.

            // bit (1 << N) set here --> Device N was inserted
            Store (PCIU, Local1)
            // bit (1 << N) set here --> Device N has to be removed
            Store (PCID, Local2)

            While (LLess(Local0, 4)) {
                Increment(Local0)
                If (And(Local1, ShiftLeft(1, Local0))) {
                    PCNT(Local0, 1) // 1 => DEVICE CHECK
                }
                If (And(Local2, ShiftLeft(1, Local0))) {
                    PCNT(Local0, 3) // 3 => EJECT REQUEST
                }
            }
            Return(One)
        }
    }

    Scope (\_GPE)
    {
        Name(_HID, "ACPI0006")

        // These methods are wired to the according bits in the GPE bitmap.
        // The hypervisor will raise bits and then send an interrupt 9.
        // The ACPI code in the guest kernel will then dispatch one of these methods.
        Method(_E01) {
            \_SB.PCI0.PCNF() // PCI hotplug event
        }
    }

} // end of definition block
