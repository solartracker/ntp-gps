#ifndef UBX_CLASS_H
#define UBX_CLASS_H
/*******************************************************************************
 ubx_class.h

 Copyright (C) 2025 Richard Elwell

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

*******************************************************************************/

// UBX Message Classes
// (u-blox 7 through 10, compatible with most GNSS receivers)

// Navigation Results Messages (Position, Velocity, Time, Satellite Info)
#define UBX_CLS_NAV   0x01    // NAV-* : Navigation output messages

// Receiver Manager Messages (Satellite data, raw measurements)
#define UBX_CLS_RXM   0x02    // RXM-* : Receiver management and raw data

// Information Messages (Debug / Notifications / GPTXT)
#define UBX_CLS_INF   0x04    // INF-* : Information (e.g., debug text, errors)

// Acknowledge Messages
#define UBX_CLS_ACK   0x05    // ACK-* : Command acknowledgments

// Configuration Input Messages
#define UBX_CLS_CFG   0x06    // CFG-* : Receiver configuration

// Update / Flash Memory Access Messages
#define UBX_CLS_UPD   0x09    // UPD-* : Firmware and memory update

// Monitoring Messages
#define UBX_CLS_MON   0x0A    // MON-* : System monitoring and diagnostics

// AssistNow / Aiding Messages (Legacy)
#define UBX_CLS_AID   0x0B    // AID-* : AssistNow aiding data

// Timing Messages
#define UBX_CLS_TIM   0x0D    // TIM-* : Timing data (timepulse, time mark, etc.)

// External Sensor Fusion Messages
#define UBX_CLS_ESF   0x10    // ESF-* : External Sensor Fusion (IMU, etc.)

// Multi-GNSS Assistance Messages
#define UBX_CLS_MGA   0x13    // MGA-* : Multi-GNSS assistance

// Logging Configuration and Data Messages
#define UBX_CLS_LOG   0x21    // LOG-* : Data logging

// Security Features and Authentication (u-blox 9+)
#define UBX_CLS_SEC   0x27    // SEC-* : Security-related messages

// High-Rate Navigation Output (u-blox 9+)
#define UBX_CLS_HNR   0x28    // HNR-* : High rate navigation data

// Experimental / Engineering Test Class (u-blox internal use)
#define UBX_CLS_TRK   0x03    // TRK-* : Tracking / debugging (not public)

// ============================================================================
// Optional: quick short aliases (useful for macros)
// ============================================================================
#define CLS_NAV  UBX_CLS_NAV
#define CLS_RXM  UBX_CLS_RXM
#define CLS_INF  UBX_CLS_INF
#define CLS_ACK  UBX_CLS_ACK
#define CLS_CFG  UBX_CLS_CFG
#define CLS_UPD  UBX_CLS_UPD
#define CLS_MON  UBX_CLS_MON
#define CLS_AID  UBX_CLS_AID
#define CLS_TIM  UBX_CLS_TIM
#define CLS_ESF  UBX_CLS_ESF
#define CLS_MGA  UBX_CLS_MGA
#define CLS_LOG  UBX_CLS_LOG
#define CLS_SEC  UBX_CLS_SEC
#define CLS_HNR  UBX_CLS_HNR
#define CLS_TRK  UBX_CLS_TRK

#endif // UBX_CLASS_H
