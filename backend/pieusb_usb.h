/*
 * File:   pieusb_usb.h
 * Author: Jan Vleeshouwers
 *
 * Created on August 20, 2012, 7:47 PM
 */

#ifndef PIEUSB_USB_H
#define	PIEUSB_USB_H

#define PIEUSB_WAIT_BUSY 2 /* seconds to wait on busy condition */

/* Structures used by the USB functions */

struct Pieusb_Command_Status {
    SANE_Status sane_status;
    SANE_Byte senseKey; /* sense key: see Pieusb_Sense */
    SANE_Byte senseCode; /* sense code */
    SANE_Byte senseQualifier; /* sense code qualifier */
};

void commandScanner(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status);

#endif	/* PIEUSB_USB_H */

