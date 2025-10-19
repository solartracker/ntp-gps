#!/usr/bin/env python3
################################################################################
# feed.py — Simple GPS data feeder for SHM writer testing
#
# This script emulates a live GPS device by feeding NMEA sentences from
# a text file into a pseudo-terminal (PTY). The PTY slave device path is
# printed to stderr, allowing other programs (such as the SHM writer)
# to read GPS data as if it were coming from a serial port.
#
# Usage:
#     ./feed.py nmea.txt [interval]
#
# Arguments:
#     nmea.txt   Path to a text file containing NMEA sentences.
#     interval   Optional delay (in seconds) between lines. Defaults to 0.1.
#
# Example:
#     ./feed.py nmea.txt 0.2
#
# Notes:
#     - Each line from the input file is written to the PTY master end.
#     - The slave device path printed to stderr can be used as /dev/tty input.
#     - Useful for replaying recorded GPS data or testing
#       software that expects serial GPS input without actual hardware.
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#
################################################################################
import os, pty, time, sys

if len(sys.argv) < 2:
    print("Usage: feed.py nmea.txt [interval]", file=sys.stderr)
    sys.exit(1)

nmea_file = sys.argv[1]
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1

master, slave = pty.openpty()
slave_name = os.ttyname(slave)
print("Slave device:", slave_name, file=sys.stderr)

with open(nmea_file, "r") as f, os.fdopen(master, "wb", buffering=0) as m:
    for line in f:
        m.write(line.encode())
        time.sleep(interval)   # just sleep, no tcdrain()

