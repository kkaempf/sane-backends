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

#define PIEUSB_WAIT_BUSY 1 /* seconds to wait on busy condition */

#define SCSI_COMMAND_LEN 6

typedef enum {
  PIEUSB_STATUS_GOOD = 0,	/* everything A-OK */
  PIEUSB_STATUS_UNSUPPORTED,	/* operation is not supported */
  PIEUSB_STATUS_CANCELLED,	/* operation was cancelled */
  PIEUSB_STATUS_DEVICE_BUSY,	/* device is busy; try again later */
  PIEUSB_STATUS_INVAL,		/* data is invalid (includes no dev at open) */
  PIEUSB_STATUS_EOF,		/* no more data available (end-of-file) */
  PIEUSB_STATUS_JAMMED,		/* document feeder jammed */
  PIEUSB_STATUS_NO_DOCS,	/* document feeder out of documents */
  PIEUSB_STATUS_COVER_OPEN,	/* scanner cover is open */
  PIEUSB_STATUS_IO_ERROR,	/* error during device I/O */
  PIEUSB_STATUS_NO_MEM,		/* out of memory */
  PIEUSB_STATUS_ACCESS_DENIED,	/* access to resource has been denied */
  PIEUSB_MAX_SANE_STATUS,       /* -- separator -- */
  PIEUSB_STATUS_CHECK_CONDITION
} PIEUSB_Status;

SANE_Status pieusb_convert_status(PIEUSB_Status status);

/* Structures used by the USB functions */

struct Pieusb_Command_Status {
    PIEUSB_Status pieusb_status;
    SANE_Byte senseKey; /* sense key: see Pieusb_Sense */
    SANE_Byte senseCode; /* sense code */
    SANE_Byte senseQualifier; /* sense code qualifier */
};

PIEUSB_Status pieusb_command(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size);

#endif	/* PIEUSB_USB_H */

