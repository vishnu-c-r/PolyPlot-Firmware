/*
 * Copyright 2001-2010 Georges Menie (www.menie.org)
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* this code needs standard functions memcpy() and memset()
   and input/output functions _inbyte() and _outbyte().
   the prototypes of the input/output functions are:
     int _inbyte(uint16_t timeout); // msec timeout
     void _outbyte(int c);
 */

#include "xmodem.h"

static Channel* serialPort;
static Print*   file;

static int _inbyte(uint16_t timeout) {
    uint8_t data;
    auto    res = serialPort->timedReadBytes(&data, 1, timeout);
    return res != 1 ? -1 : data;
}
static void _outbyte(int c) {
    serialPort->write((uint8_t)c);
}
static void _outbytes(uint8_t* buf, size_t len) {
    serialPort->write(buf, len);
}

/* CRC16 implementation acording to CCITT standards */

static const uint16_t crc16tab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

uint16_t crc16_ccitt(const uint8_t* buf, size_t len) {
    int      counter;
    uint16_t crc = 0;
    for (counter = 0; counter < len; counter++)
        crc = (crc << 8) ^ crc16tab[((crc >> 8) ^ *buf++) & 0x00FF];
    return crc;
}

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CTRLZ 0x1A

#define DLY_1S 1000
#define MAXRETRANS 25
#define TRANSMIT_XMODEM_1K

static int check(int crc, const uint8_t* buf, int sz) {
    if (crc) {
        uint16_t crc  = crc16_ccitt(buf, sz);
        uint16_t tcrc = (buf[sz] << 8) + buf[sz + 1];
        if (crc == tcrc)
            return 1;
    } else {
        int     i;
        uint8_t cks = 0;
        for (i = 0; i < sz; ++i) {
            cks += buf[i];
        }
        if (cks == buf[sz])
            return 1;
    }

    return 0;
}

static void flushinput(void) {
    while (_inbyte(((DLY_1S) * 3) >> 1) >= 0)
        ;
}

// We delay writing each packet until the next one arrives
// so that we can remove trailing control-Z's in only the
// last one.  The Xmodem protocol has no good way to denote
// the actual size of the file in bytes as opposed to packets.
// Instead it pads the final packet with control-Z.  By removing
// those trailing control-Z's before writing to the file, it
// is possible to handle files of any length.  This heuristic
// fails with binary files that are supposed to have trailing
// control-Z's.  Doing the control-Z removal only on the final
// packet avoids removing interior control-Z's that happen to
// land at the end of a packet.
static uint8_t held_packet[1024];
static size_t  held_packet_len;
static void    flush_packet(size_t packet_len, size_t& total_len) {
    if (held_packet_len > 0) {
        // Remove trailing ctrl-z's on the final packet
        size_t count;
        for (count = held_packet_len; count > 0; --count) {
            if (held_packet[count - 1] != CTRLZ) {
                break;
            }
        }
        file->write(held_packet, count);
        total_len += count;
        held_packet_len = 0;
    }
}
static void write_packet(uint8_t* buf, size_t packet_len, size_t& total_len) {
    if (held_packet_len > 0) {
        file->write(held_packet, held_packet_len);
        total_len += held_packet_len;
        held_packet_len = 0;
    }
    memcpy(held_packet, buf, packet_len);
    held_packet_len = packet_len;
}
int xmodemReceive(Channel* serial, FileStream* out) {
    serialPort      = serial;
    file            = out;
    held_packet_len = 0;

    uint8_t  xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
    uint8_t* p;
    int      bufsz = 0, crc = 0;
    uint8_t  trychar  = 'C';
    uint8_t  packetno = 1;
    int      i, c           = 0;
    int      retry, retrans = MAXRETRANS;

    size_t len = 0;

    for (;;) {
        for (retry = 0; retry < 16; ++retry) {
            if (trychar)
                _outbyte(trychar);
            if ((c = _inbyte((DLY_1S) << 1)) >= 0) {
                switch (c) {
                    case SOH:
                        bufsz = 128;
                        goto start_recv;
                    case STX:
                        bufsz = 1024;
                        goto start_recv;
                    case EOT:
                        flush_packet(bufsz, len);
                        _outbyte(ACK);
                        flushinput();
                        return len; /* normal end */
                    case CAN:
                        if ((c = _inbyte(DLY_1S)) == CAN) {
                            flushinput();
                            _outbyte(ACK);
                            return -1; /* canceled by remote */
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        if (trychar == 'C') {
            trychar = NAK;
            continue;
        }
        flushinput();
        _outbyte(CAN);
        _outbyte(CAN);
        _outbyte(CAN);
        return -2; /* sync error */

    start_recv:
        if (trychar == 'C')
            crc = 1;
        trychar = 0;
        p       = xbuff;
        *p++    = c;
        for (i = 0; i < (bufsz + (crc ? 1 : 0) + 3); ++i) {
            if ((c = _inbyte(DLY_1S)) < 0)
                goto reject;
            *p++ = c;
        }

        if (xbuff[1] == (uint8_t)(~xbuff[2]) && (xbuff[1] == packetno || xbuff[1] == packetno - 1) && check(crc, &xbuff[3], bufsz)) {
            if (xbuff[1] == packetno) {
                write_packet(xbuff + 3, bufsz, len);
                ++packetno;
                retrans = MAXRETRANS + 1;
            }
            if (--retrans <= 0) {
                flushinput();
                _outbyte(CAN);
                _outbyte(CAN);
                _outbyte(CAN);
                return -3; /* too many retry error */
            }
            _outbyte(ACK);
            continue;
        }
    reject:
        flushinput();
        _outbyte(NAK);
    }
}

int xmodemTransmit(Channel* serial, FileStream* infile) {
    serialPort = serial;

    uint8_t xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
    int     bufsz, crc = -1;
    uint8_t packetno = 1;
    int     i, c = 0;
    size_t  len = 0;
    int     retry;

    for (;;) {
        for (retry = 0; retry < 16; ++retry) {
            if ((c = _inbyte((DLY_1S) << 1)) >= 0) {
                switch (c) {
                    case 'C':
                        crc = 1;
                        goto start_trans;
                    case NAK:
                        crc = 0;
                        goto start_trans;
                    case CAN:
                        if ((c = _inbyte(DLY_1S)) == CAN) {
                            _outbyte(ACK);
                            flushinput();
                            return -1; /* canceled by remote */
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        _outbyte(CAN);
        _outbyte(CAN);
        _outbyte(CAN);
        flushinput();
        return -2; /* no sync */

        for (;;) {
        start_trans:
#ifdef TRANSMIT_XMODEM_1K
            xbuff[0] = STX;
            bufsz    = 1024;
#else
            xbuff[0] = SOH;
            bufsz    = 128;
#endif
            xbuff[1] = packetno;
            xbuff[2] = ~packetno;

            auto nbytes = infile->read(&xbuff[3], bufsz);
            if (nbytes > 0) {
                while (nbytes < bufsz) {
                    xbuff[3 + nbytes] = CTRLZ;
                    nbytes++;
                }
                if (crc) {
                    uint16_t ccrc    = crc16_ccitt(&xbuff[3], bufsz);
                    xbuff[bufsz + 3] = (ccrc >> 8) & 0xFF;
                    xbuff[bufsz + 4] = ccrc & 0xFF;
                } else {
                    uint8_t ccks = 0;
                    for (i = 3; i < bufsz + 3; ++i) {
                        ccks += xbuff[i];
                    }
                    xbuff[bufsz + 3] = ccks;
                }
                for (retry = 0; retry < MAXRETRANS; ++retry) {
                    _outbytes(xbuff, bufsz + 4 + (crc ? 1 : 0));
                    if ((c = _inbyte(DLY_1S)) >= 0) {
                        switch (c) {
                            case ACK:
                                ++packetno;
                                len += bufsz;
                                goto start_trans;
                            case CAN:
                                if ((c = _inbyte(DLY_1S)) == CAN) {
                                    _outbyte(ACK);
                                    flushinput();
                                    return -1; /* canceled by remote */
                                }
                                break;
                            case NAK:
                            default:
                                break;
                        }
                    }
                }
                _outbyte(CAN);
                _outbyte(CAN);
                _outbyte(CAN);
                flushinput();
                return -4; /* xmit error */
            } else {
                for (retry = 0; retry < 10; ++retry) {
                    _outbyte(EOT);
                    if ((c = _inbyte((DLY_1S) << 1)) == ACK)
                        break;
                }
                flushinput();
                return (c == ACK) ? len : -5;
            }
        }
    }
}

#ifdef TEST_XMODEM_RECEIVE
int main(void) {
    int st;

    printf("Send data using the xmodem protocol from your terminal emulator now...\n");
    /* the following should be changed for your environment:
	   0x30000 is the download address,
	   65536 is the maximum size to be written at this address
	 */
    st = xmodemReceive((char*)0x30000, 65536);
    if (st < 0) {
        printf("Xmodem receive error: status: %d\n", st);
    } else {
        printf("Xmodem successfully received %d bytes\n", st);
    }

    return 0;
}
#endif
#ifdef TEST_XMODEM_SEND
int main(void) {
    int st;

    printf("Prepare your terminal emulator to receive data now...\n");
    /* the following should be changed for your environment:
	   0x30000 is the download address,
	   12000 is the maximum size to be send from this address
	 */
    st = xmodemTransmit((char*)0x30000, 12000);
    if (st < 0) {
        printf("Xmodem transmit error: status: %d\n", st);
    } else {
        printf("Xmodem successfully transmitted %d bytes\n", st);
    }

    return 0;
}
#endif
