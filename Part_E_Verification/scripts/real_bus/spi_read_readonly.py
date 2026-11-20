#!/usr/bin/env python3
# READ-ONLY probe of the MCP2518FD on SPI0 CE<n> (argv[1] = 0 or 1) through /dev/spidev0.<n>: only the READ instruction (0b0011 | 12-bit address) is sent, never WRITE/RESET.
import ctypes, fcntl, struct, sys
SPI_IOC_WR_MODE = 0x40016b01
def xfer(fd, tx, speed):
    n=len(tx); txb=ctypes.create_string_buffer(bytes(tx),n); rxb=ctypes.create_string_buffer(n)
    t=struct.pack("=QQIIHBBBBBB", ctypes.addressof(txb), ctypes.addressof(rxb), n, speed, 0, 8, 0, 0, 0, 0, 0)
    fcntl.ioctl(fd, 0x40206b00, t)          # SPI_IOC_MESSAGE(1)
    return rxb.raw
regs=[("OSC     0xE00",0xE00),("IOCON   0xE04",0xE04),("CRC     0xE08",0xE08),("ECCCON  0xE0C",0xE0C),("DEVID   0xE14",0xE14),("C1CON   0x000",0x000)]
for mode in (0,3):
  for speed in (500000,2000000,10000000):
    fd=open("/dev/spidev0.%s" % sys.argv[1],"r+b",buffering=0)
    fcntl.ioctl(fd, SPI_IOC_WR_MODE, struct.pack("B",mode))
    out=[]
    for name,a in regs:
        cmd=0x3000|a                         # READ
        rx=xfer(fd.fileno(),[cmd>>8,cmd&0xFF,0,0,0,0],speed)
        out.append("%s=%s"%(name.split()[0],rx[2:6].hex()))
    print("mode %d %8d Hz: %s"%(mode,speed," ".join(out)))
    fd.close()
