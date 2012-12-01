/* sane - Scanner Access Now Easy.

   pieusb_usb.h

   Copyright (C) 2012 Jan Vleeshouwers, Michael Rickmann, Klaus Kaempf

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.  */

#ifndef PIEUSB_USB_H
#define	PIEUSB_USB_H

/* Structures used by the USB functions */

struct Pieusb_Command_Status {
    SANE_Status sane_status;
    SANE_Byte senseKey; /* sense key: see Pieusb_Sense */
    SANE_Byte senseCode; /* sense code */
    SANE_Byte senseQualifier; /* sense code qualifier */
};

/* USB functions */

static SANE_Status ctrloutbyte(SANE_Int device_number, SANE_Int port, SANE_Byte b);
static SANE_Status ctrloutint(SANE_Int device_number, unsigned int size);
static SANE_Status ctrlinbyte(SANE_Int device_number, SANE_Byte* b);
static SANE_Status bulkin(SANE_Int device_number, SANE_Byte* data, unsigned int size);
static void commandScanner(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status);
static void commandScannerRepeat(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status, int repeat);
static SANE_Status interpretStatus(SANE_Byte status[]);

static SANE_Byte getByte(SANE_Byte* array, SANE_Byte offset);
static void setByte(SANE_Byte val, SANE_Byte* array, SANE_Byte offset);
static SANE_Int getShort(SANE_Byte* array, SANE_Byte offset);
static void setShort(SANE_Word val, SANE_Byte* array, SANE_Byte offset);
static SANE_Int getInt(SANE_Byte* array, SANE_Byte offset);
static void setInt(SANE_Word val, SANE_Byte* array, SANE_Byte offset);
static void getBytes(SANE_Byte* val, SANE_Byte* array, SANE_Byte offset, SANE_Byte count);
static void setBytes(SANE_Byte* val, SANE_Byte* array, SANE_Byte offset, SANE_Byte count);
static void getShorts(SANE_Word* val, SANE_Byte* array, SANE_Byte offset, SANE_Byte count);
static void setShorts(SANE_Word* val, SANE_Byte* array, SANE_Byte offset, SANE_Byte count);

#endif	/* PIEUSB_USB_H */

