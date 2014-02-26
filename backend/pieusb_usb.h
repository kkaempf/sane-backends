/*
 * File:   pieusb_usb.h
 * Author: Jan Vleeshouwers
 *
 * Created on August 20, 2012, 7:47 PM
 */

#ifndef PIEUSB_USB_H
#define	PIEUSB_USB_H

#define PIEUSB_WAIT_BUSY 2 /* seconds to wait on busy condition */

typedef enum
  {
    PIEUSB_STATUS_GOOD = 0,		/* everything A-OK */
    PIEUSB_STATUS_UNSUPPORTED,		/* operation is not supported */
    PIEUSB_STATUS_CANCELLED,		/* operation was cancelled */
    PIEUSB_STATUS_DEVICE_BUSY,		/* device is busy; try again later */
    PIEUSB_STATUS_INVAL,		/* data is invalid (includes no dev at open) */
    PIEUSB_STATUS_EOF,			/* no more data available (end-of-file) */
    PIEUSB_STATUS_JAMMED,		/* document feeder jammed */
    PIEUSB_STATUS_NO_DOCS,		/* document feeder out of documents */
    PIEUSB_STATUS_COVER_OPEN,		/* scanner cover is open */
    PIEUSB_STATUS_IO_ERROR,		/* error during device I/O */
    PIEUSB_STATUS_NO_MEM,		/* out of memory */
    PIEUSB_STATUS_ACCESS_DENIED,	/* access to resource has been denied */
    PIEUSB_MAX_SANE_STATUS,             /* -- separator -- */
    PIEUSB_STATUS_CHECK_CONDITION
  }
PIEUSB_Status;

SANE_Status pieusb_convert_status(PIEUSB_Status status);

/* Structures used by the USB functions */

struct Pieusb_Command_Status {
    PIEUSB_Status pieusb_status;
    SANE_Byte senseKey; /* sense key: see Pieusb_Sense */
    SANE_Byte senseCode; /* sense code */
    SANE_Byte senseQualifier; /* sense code qualifier */
};

void pieusb_scsi_command(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status);

#endif	/* PIEUSB_USB_H */

