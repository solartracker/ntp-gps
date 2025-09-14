#!/bin/bash
################################################################################
# bundle.sh
#
# Purpose:
#   Recursively inlines all `source` dependencies in a Bash script to produce
#   a single, self-contained output script. Removes original `source` lines.
#
# Usage:
#   bundle_script <input_script> <output_script> [verbose] [VAR1=value1 VAR2=value2 ...]
#
#   <input_script>   : Path to the script to bundle
#   <output_script>  : Path for the bundled output script
#   [verbose]        : Optional, 'true' or 'false' (default: false)
#   [VAR=value ...]  : Optional variable assignments to expand in source paths
#
# Example:
#   bundle_script uninstall.sh /usr/local/bin/uninstall.sh true "SCRIPT_DIR=/home/pi/ntp-gps"
#
# Notes:
#   • All `source` lines are removed.
#   • Dependencies are expanded in place.
#   • Script is now fully standalone and executable.
#   • The function preserves the structure of inlined files, marking the
#     beginning and end of each inlined section with comments:
#         # >>> begin inlined: filename
#         # <<< end inlined: filename
#   • This makes the output script portable and independent of external
#     shared scripts.
#
# Copyright (C) 2025 Richard Elwell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
################################################################################
#finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
#enter() { echo "[ENTERING] $(basename "$0")"; }
#enter
#set -x #debug switch
set -e

SCRIPT_DIR="$(cd "$(dirname -- "$0")" && pwd)"
source "$SCRIPT_DIR/shared-utils.sh"

bundle_script "$@"

