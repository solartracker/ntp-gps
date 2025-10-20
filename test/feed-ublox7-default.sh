#!/bin/bash
################################################################################
# feed-ublox7-default.sh
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################
set -x
set -e
./feed.py nmea-ublox7-default-cold-x.txt 0.2
