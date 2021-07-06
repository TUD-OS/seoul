/*
 * IpHelper class
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
 *
 * This was previously used for network communication in the NUL userland
 * when virtualizing with the NOVA microhypervisor.  Functionality was not
 * ported and rather the interface is described here to ease porting to the
 * UNIX socket interface.
 */

#ifndef __IPHELPER_H
#define __IPHELPER_H

#include <nul/timer.h>

#define IP_AS_UL(a, b, c, d) ((((d) & 0xff) << 24) | (((c) & 0xff) << 16) | (((b) & 0xff) << 8) | ((a) & 0xff))

class IpHelper;

class TcpSocket
{
    friend IpHelper;

    private:

    bool              _outgoing;
    unsigned short    _local_port;
    unsigned short    _remote_port;

    // Indicates if we are connected.
    bool            _connected;
    // A socket can still be "connected" although closed, if there is still data to be sent.
    // After sending this data, the socket will finally be marked as "closed"
    bool            _closed;

    /* ... semaphores used to be initialized here */
    /* ... buffers ... */

    /* Only to be called by IpHelper */
    TcpSocket(unsigned caps)
        : _remote_port(0), _connected(false), _closed(true)
    { /* ... */ }

    /* Forbidden and hence not implemented: */
    TcpSocket(TcpSocket const&);
    void operator=(TcpSocket const&);

    public:
    /*
     * Methods for the end user!
     */

    bool block_until_connected() { return false; }

    /* Close this socket. */
    void close() {}

    /* Blocking receive function. Difference to BSD sockets:
     * Does _not_ return before it received the expected number of bytes. */
    bool receive(void *data, unsigned bytes) { return false; }

    /* Blocking send function. Difference to BSD sockets:
     * Does _not_ return before the user ACKed all bytes. */
    bool send(void *data, unsigned bytes) { return false; }

    /* Nonblocking send function. Returns immediately.
     * Call wait_complete after you pushed multiple send_nonblocking() calls. */
    bool send_nonblocking(void *data, unsigned bytes) { return false; }

    /* Wait until the receiver ACKed all packets sent from this socket. */
    bool wait_complete() { return false; }
};

class IpHelper
{
    private:
        /* ... */

        unsigned long long _mac;

        mword _ip;
        mword _netmask;
        mword _gateway;

        TcpSocket *_sockets;

        IpHelper() : _mac(0), _ip(0), _netmask(0), _gateway(0), _sockets(NULL)
        {};


        /* Forbidden, hence not implemented: */
        IpHelper(IpHelper const&);
        void operator=(IpHelper const&);

    public:
        /* This is a singleton */
        static IpHelper & instance()
        {
            static IpHelper instance;
            return instance;
        }

        /* === These methods are to be used from the network thread === */

        /* Attach a KernelSemaphore to this and get notified on timeout events.
         * You will better attach this to network events, too. */
        unsigned timer_sm() { return 0; /* This used to return a network timer semaphor capability */ }

        /* Call this after the semaphore let you through to reprogram for the next timeout */
        void check_timeout() {}

        /* Call this regularly to let sockets send */
        void sockets_send() {}

        /* Feed this method regularly with new incoming packets from the network. */
        void do_tcpip(unsigned char* data, unsigned size) {}

        /* === These methods are to be used by the actual end user === */

        /* Call this once at the beginning to initialize everything. */
        bool init(/* ... */) { return false;}

        /* Block-wait until IpHelper gets an IP and return its value. */
        mword get_ip() { return 0; }

        /* Connect to port at given IP and return a working socket. */
        TcpSocket * connect(unsigned addr, unsigned port) { return NULL; }

        /* Make a socket listen on port and return a TcpSocket object when a connection
         * was established */
        TcpSocket * listen(unsigned port) { return NULL; }
};

#endif /* __IPHELPER_H */
