#!/bin/sh
#sudo qemu -fda $sodex/src/bin/fsboot.bin -net nic -net user -net tap,ifname=mytap -usb -usbdevice host:0457:0151 &

# disk image (mass storage)
sudo qemu -fda $sodex/src/bin/fsboot.bin -net nic -net user -net tap,ifname=mytap -usb -usbdevice disk:disk.img
