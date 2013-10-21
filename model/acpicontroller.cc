/**
 * ACPI controller model
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


#include <nul/timer.h>
#include <service/time.h>

#include "nul/motherboard.h"
#include "executor/bios.h"

#define CMD_ACPI_ENABLE 0xab
#define CMD_ACPI_DISABLE 0xba

#define PORT_SMI_CMD        0xaeae

/* The pm1 event register group is somewhat complicated.
 * port numbers follow a partition rule of the register block.
 * see ACPI spec 4.7.3.1
 */
#define PM1_EVT_LEN         4
#define PORT_PM1A_EVENT_BLK     0xaea6
#define PORT_PM1B_EVENT_BLK     0xaeaa
#define PORT_PM1A_EVENT_STATUS  (PORT_PM1A_EVENT_BLK)
#define PORT_PM1A_EVENT_ENABLE  (PORT_PM1A_EVENT_BLK + (PM1_EVT_LEN) / 2) // 0xa6 + 4/2 = 0xa8
#define PORT_PM1B_EVENT_STATUS  (PORT_PM1B_EVENT_BLK)
#define PORT_PM1B_EVENT_ENABLE  (PORT_PM1B_EVENT_BLK + (PM1_EVT_LEN) / 2) // 0xaa + 4/2 = 0xac

#define PM1_CNT_LEN         2
#define PORT_PM1A_CONTROL   0xaeb0
#define PORT_PM1B_CONTROL   0xaeb2

#define PORT_GPE0_STATUS    0xaeb4
#define PORT_GPE1_STATUS    0xaeb5
#define PORT_GPE0_ENABLE    (PORT_GPE0_STATUS + 2)
#define PORT_GPE1_ENABLE    (PORT_GPE1_STATUS + 2)

#define PORT_PCIU   0xae00
#define PORT_PCID   0xae04
#define PORT_B0EJ   0xae08


class AcpiController : public StaticReceiver<AcpiController>, public BiosCommon
{
    private:
        unsigned short _pm1a_status;
        unsigned short _pm1a_enable;
        unsigned short _pm1a_control;

        unsigned short _pm1b_status;
        unsigned short _pm1b_enable;
        unsigned short _pm1b_control;

        unsigned char _gpe0_sts;
        unsigned char _gpe0_en;
        unsigned char _gpe1_sts;
        unsigned char _gpe1_en;

        unsigned _b0ej; // write-only register
        unsigned _pciu; // read-only, REFRESH register (card plugged in)
        unsigned _pcid; // read-only, DETACH register (card to be unplugged)

        bool _processed;

        StopWatch _watch;

    public:
        void trigger_gpe(unsigned event_nr)
        {

            // Activate this event in the appropriate register
            _gpe0_sts |=  0x00ff & (1 << event_nr);
            _gpe1_sts |= (0xff00 & (1 << event_nr)) >> 8;

            // If this event is masked by the guest, then just ignore it
            if ((0 == _gpe0_sts & _gpe0_en) || (0 == _gpe1_sts & _gpe1_en))
                return;

            // Send the guest an SCI
            MessageIrqLines msg(MessageIrq::ASSERT_IRQ, 9);
            _mb.bus_irqlines.send(msg);
        }

        bool  receive(MessageDiscovery &msg) {
            if (msg.type != MessageDiscovery::DISCOVERY) return false;

            /* The following FADT entries will tell the guest kernel
             * how to interact with the system when receiving
             * System Control Interrupts (SCI).
             * Only the GPE part is important for hot plugging, but
             * all the PM-stuff is mandatory for event management
             * to work.
             */
            discovery_write_dw("FACP", 56, PORT_PM1A_EVENT_BLK);
            discovery_write_dw("FACP", 60, PORT_PM1B_EVENT_BLK);
            discovery_write_dw("FACP", 64, PORT_PM1A_CONTROL);
            discovery_write_dw("FACP", 68, PORT_PM1B_CONTROL);
            discovery_write_dw("FACP", 88, PM1_EVT_LEN, 1);
            discovery_write_dw("FACP", 89, PM1_CNT_LEN, 1);

            discovery_write_dw("FACP", 80, PORT_GPE0_STATUS, 4); // GPE0_BLK
            discovery_write_dw("FACP", 84, PORT_GPE1_STATUS, 4); // GPE1_BLK

            discovery_write_dw("FACP", 92,  4, 1); // GPE0_BLK_LEN
            discovery_write_dw("FACP", 93,  4, 1); // GPE1_BLK_LEN
            discovery_write_dw("FACP", 94, 16, 1); // GPE1_BASE (offset)

            /* This is used at boot once. Linux will write
             * CMD_ACPI_ENABLE via system IO using port PORT_SMI_CMD
             * to tell the mainboard it wants to use ACPI.
             * If CMD_ACPI_ENABLE was defined as 0x00, the guest kernel
             * would think that ACPI was always on. Therefore, this is
             * optional and one could just erase the next three lines.
             */
            discovery_write_dw("FACP", 48, PORT_SMI_CMD);
            discovery_write_dw("FACP", 52, CMD_ACPI_ENABLE, 1);
            discovery_write_dw("FACP", 53, CMD_ACPI_DISABLE, 1);

            return true;
        }

        bool  receive(MessageIOIn &msg) {
            switch (msg.port) {
                case PORT_PM1A_EVENT_STATUS:
                    //Logging::printf("In on port pm1a EVENT STATUS: %x len %u\n", _pm1a_status, msg.type);
                    msg.value = _pm1a_status;
                    return true;
                case PORT_PM1A_EVENT_ENABLE:
                    //Logging::printf("In on port pm1a EVENT ENABLE: %x len %u\n", _pm1a_enable, msg.type);
                    msg.value = _pm1a_enable;
                    return true;
                case PORT_PM1A_CONTROL:
                    //Logging::printf("In on port pm1a CONTROL %x len %u\n", _pm1a_control, msg.type);
                    msg.value = _pm1a_control;
                    return true;

                case PORT_PM1B_EVENT_STATUS:
                    //Logging::printf("In on port pm1b EVENT STATUS: %x len %u\n", _pm1b_status, msg.type);
                    msg.value = _pm1b_status;
                    return true;
                case PORT_PM1B_EVENT_ENABLE:
                    //Logging::printf("In on port pm1b EVENT ENABLE: %x len %u\n", _pm1b_enable, msg.type);
                    msg.value = _pm1b_enable;
                    return true;
                case PORT_PM1B_CONTROL:
                    //Logging::printf("In on port pm1b CONTROL %x len %u\n", _pm1b_control, msg.type);
                    msg.value = _pm1b_control;
                    return true;


                case PORT_GPE0_STATUS:
                    //Logging::printf("In on port GPE0 STS: %x\n", _gpe0_sts);
                    msg.value = _gpe0_sts;
                    return true;
                case PORT_GPE0_ENABLE:
                    //Logging::printf("In on port GPE0 EN %x\n", _gpe0_en);
                    msg.value = _gpe0_en;
                    return true;
                case PORT_GPE1_STATUS:
                    //Logging::printf("In on port GPE1 STS: %x\n", _gpe1_sts);
                    msg.value = _gpe1_sts;
                    return true;
                case PORT_GPE1_ENABLE:
                    //Logging::printf("In on port GPE1 EN %x\n", _gpe1_en);
                    msg.value = _gpe1_en;
                    return true;

                case PORT_PCIU:
                    //Logging::printf("--- In on PCIU\n");
                    msg.value = _pciu;
                    return true;
                case PORT_PCID:
                    //Logging::printf("--- In on PCID\n");
                    msg.value = _pcid;
                    return true;
                default:;
            }
            return false;
        }

        bool  receive(MessageIOOut &msg) {
            switch (msg.port) {
                case PORT_SMI_CMD:
                    /* During boot the guest kernel checks PORT_SMI_CMD
                     * in the ACPI FADT table. If SCI_EN is not set,
                     * the system is in legacy mode. Hence it sends the
                     * CMD_ACPI_ENABLE cmd it got from the FADT again to
                     * this port and then polls for SCI_EN until it is set.
                     * ACPI is then officially active. */
                    if (msg.value == CMD_ACPI_ENABLE) {
                        Logging::printf("Enabling ACPI for guest.\n");
                        _pm1a_control |= 1; // Setting SCI_EN bit
                    }
                    else if (msg.value == CMD_ACPI_DISABLE) {
                        Logging::printf("Disabling ACPI for guest.\n");
                        _pm1a_control &= ~1U;
                    }
                    return true;

                case PORT_PM1A_EVENT_STATUS:
                    //Logging::printf("Out on port pm1a EVENT STATUS: %x len %u\n", msg.value, msg.type);
                    return true;
                case PORT_PM1A_EVENT_ENABLE:
                    //Logging::printf("Out on port pm1a EVENT ENABLE: %x len %u\n", msg.value, msg.type);
                    _pm1a_enable = static_cast<unsigned short>(msg.value);
                    return true;
                case PORT_PM1A_CONTROL:
                    //Logging::printf("Out on port pm1a CONTROL %x len %u\n", msg.value, msg.type);
                    return true;


                case PORT_PM1B_EVENT_STATUS:
                    //Logging::printf("Out on port pm1b EVENT STATUS: %x len %u\n", msg.value, msg.type);
                    return true;
                case PORT_PM1B_EVENT_ENABLE:
                    //Logging::printf("Out on port pm1b EVENT ENABLE: %x len %u\n", msg.value, msg.type);
                    _pm1a_enable = static_cast<unsigned short>(msg.value);
                    return true;
                case PORT_PM1B_CONTROL:
                    //Logging::printf("Out on port pm1b CONTROL %x len %u\n", msg.value, msg.type);
                    return true;

                case PORT_GPE0_STATUS:
                    //Logging::printf("Out on port GPE0 STS: %x len %u\n", msg.value, msg.type);
                    _gpe0_sts &= ~ static_cast<unsigned char>(msg.value);
                    return true;
                case PORT_GPE0_ENABLE:
                    //Logging::printf("Out on port GPE0 EN %x len %u\n", msg.value, msg.type);
                    _gpe0_en = static_cast<unsigned char>(msg.value);
                    return true;
                case PORT_GPE1_STATUS:
                    //Logging::printf("Out on port GPE1 STS: %x\n", msg.value);
                    _gpe1_sts &= ~ static_cast<unsigned char>(msg.value);
                    return true;
                case PORT_GPE1_ENABLE:
                    //Logging::printf("Out on port GPE1 EN %x\n", msg.value);
                    _gpe1_en = static_cast<unsigned char>(msg.value);
                    return true;

                case PORT_B0EJ:
                    _watch.stop();
                    Logging::printf("PCI hot-unplug confirmed by guest "
                            "(Output on B0EJ: %x) after %llu ms\n",
                            msg.value, _watch.delta());
                    _pcid &= ~msg.value;
                    //Logging::printf("PCIU: %x, PCID: %x\n", _pciu, _pcid);
                    return true;
                default:;
            }

            /* Deassert this IRQ if all enabled events were cleared by the guest.
             * This interrupt is thrown again otherwise. */
            if (!(_pm1a_status & _pm1a_enable) &&
                !(_pm1b_status & _pm1b_enable) &&
                !(_gpe0_sts & _gpe0_en) &&
                !(_gpe1_sts & _gpe1_en)) {
                MessageIrqLines msg(MessageIrq::DEASSERT_IRQ, 9);
                _mb.bus_irqlines.send(msg);
            }

            return false;
        }

        bool receive(MessageRestore &msg)
        {
            const mword bytes = reinterpret_cast<mword>(&_processed)
                -reinterpret_cast<mword>(&_pm1a_status);

            if (msg.devtype == MessageRestore::RESTORE_RESTART) {
                _processed = false;
                msg.bytes += bytes + sizeof(msg);
                return false;
            }

            if (msg.devtype != MessageRestore::RESTORE_ACPI || _processed) return false;

            if (msg.write) {
                msg.bytes = bytes;
                memcpy(msg.space, reinterpret_cast<void*>(&_pm1a_status), bytes);
            }
            else {
                memcpy(reinterpret_cast<void*>(&_pm1a_status), msg.space, bytes);
            }

            Logging::printf("%s ACPI controller\n", msg.write?"Saved":"Restored");

            _processed = true;
            return true;
        }

        AcpiController(Motherboard &mb)
            : BiosCommon(mb),
            _pm1a_status(0), _pm1a_enable(0), _pm1a_control(0),
            _pm1b_status(0), _pm1b_enable(0), _pm1b_control(0),
            _gpe0_sts(0), _gpe0_en(0), _gpe1_sts(0), _gpe1_en(0),
            _b0ej(0), _pciu(0), _pcid(0),
            _processed(false), _watch(mb.clock())
        { }
};

PARAM_HANDLER(acpimodel,
        "acpimodel - Capable of issuing ACPI events to the guest.")
{
    AcpiController * dev = new AcpiController(mb);
    mb.bus_discovery .add(dev, AcpiController::receive_static<MessageDiscovery>);
    mb.bus_ioin      .add(dev, AcpiController::receive_static<MessageIOIn>);
    mb.bus_ioout     .add(dev, AcpiController::receive_static<MessageIOOut>);
    mb.bus_restore   .add(dev, AcpiController::receive_static<MessageRestore>);
}
