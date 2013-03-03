/*
 * File:   pieusb.h
 * Author: Jan Vleeshouwers
 *
 * Created on July 22, 2012, 2:22 PM
 */

#ifndef PIEUSB_H
#define	PIEUSB_H

#include <sane/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define BACKEND_NAME pieusb

#include <sane/sane.h>
#include <sane/sanei_usb.h>
#include <sane/sanei_debug.h>


#include "pieusb_usb.h"

/* Additional SANE status code */
#define SANE_STATUS_CHECK_CONDITION 14 /* add to SANE_status enum */

/* --------------------------------------------------------------------------
 *
 * SUPPORTED DEVICES SPECIFICS
 *
 * --------------------------------------------------------------------------*/

/* List of default supported scanners by vendor-id, product-id and model number.
 * A default list will be created in sane_init(), and entries in the config file
 *  will be added to it. */

struct Pieusb_USB_Device_Entry
{
    SANE_Word vendor;		/* USB vendor identifier */
    SANE_Word product;		/* USB product identifier */
    SANE_Word model;		/* USB model number */
    SANE_Int device_number;     /* USB device number if the device is present */
};

extern struct Pieusb_USB_Device_Entry* pieusb_supported_usb_device_list;
extern struct Pieusb_USB_Device_Entry pieusb_supported_usb_device; /* for searching */

struct Pieusb_Device_Definition;
extern struct Pieusb_Device_Definition *definition_list_head;

void commandScannerRepeat(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status, int repeat);

/* Debug error levels */
#define DBG_error        1      /* errors */
#define DBG_warning      3      /* warnings */
#define DBG_info         5      /* information */
#define DBG_info_sane    7      /* information sane interface level */
#define DBG_inquiry      8      /* inquiry data */
#define DBG_info_proc    9      /* information pieusb backend functions */
#define DBG_info_scan   11      /* information scanner commands */
#define DBG_info_usb    13      /* information usb level functions */

#endif	/* PIEUSB_H */

