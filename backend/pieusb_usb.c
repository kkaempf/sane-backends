/* sane - Scanner Access Now Easy.

   pieusb_usb.c

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

#define DEBUG_DECLARE_ONLY
#include "pieusb.h"
#include "pieusb_scancmd.h"
#include "pieusb_usb.h"

#include <sane/sanei_usb.h>
#include <unistd.h> /* usleep */

/* USB functions */

static SANE_Status _ctrl_out_byte(SANE_Int device_number, SANE_Int port, SANE_Byte b);
static SANE_Status _ctrl_out_int(SANE_Int device_number, unsigned int size);
static SANE_Status _ctrl_in_byte(SANE_Int device_number, SANE_Byte* b);
static SANE_Status _bulk_in(SANE_Int device_number, SANE_Byte* data, unsigned int size);

/* Defines for use in USB functions */

#define REQUEST_TYPE_IN	(USB_TYPE_VENDOR | USB_DIR_IN)
#define REQUEST_TYPE_OUT (USB_TYPE_VENDOR | USB_DIR_OUT)
#define REQUEST_REGISTER 0x0c
#define REQUEST_BUFFER 0x04
#define ANYINDEX 0x00 /* wIndex value for USB control transfer - value is irrelevant */

/* from libieee1284 */
#define C1284_NSTROBE 0x01
#define C1284_NINIT   0x04

/* usb via ieee1284 */
#define IEEE1284_ADDR  0x00
#define IEEE1284_RESET 0x30
#define IEEE1284_SCSI  0xe0

#define PORT_SCSI_SIZE   0x0082
#define PORT_SCSI_STATUS 0x0084
#define PORT_SCSI_CMD    0x0085
#define PORT_PAR_CTRL    0x0087 /* IEEE1284 parallel control */
#define PORT_PAR_DATA    0x0088 /* IEEE1284 parallel data */

typedef enum {
  USB_STATUS_OK    = 0x00, /* ok */
  USB_STATUS_READ  = 0x01, /* read: send expected length, then read data */
  USB_STATUS_CHECK = 0x02, /* check condition */
  USB_STATUS_BUSY  = 0x03, /* wait on usb */
  USB_STATUS_AGAIN = 0x08, /* re-send scsi cmd */
  USB_STATUS_ERROR = 0xff  /* usb i/o error */
} PIEUSB_USB_Status;

static PIEUSB_USB_Status _pieusb_scsi_command(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size);


/* Standard SCSI Sense keys */
#define SCSI_NO_SENSE        0x00
#define SCSI_RECOVERED_ERROR 0x01
#define SCSI_NOT_READY       0x02
#define SCSI_MEDIUM_ERROR    0x03
#define SCSI_HARDWARE_ERROR  0x04
#define SCSI_ILLEGAL_REQUEST 0x05
#define SCSI_UNIT_ATTENTION  0x06
#define SCSI_DATA_PROTECT    0x07
#define SCSI_BLANK_CHECK     0x08
#define SCSI_VENDOR_SPECIFIC 0x09
#define SCSI_COPY_ABORTED    0x0A
#define SCSI_ABORTED_COMMAND 0x0B
#define SCSI_EQUAL           0x0C
#define SCSI_VOLUME_OVERFLOW 0x0D
#define SCSI_MISCOMPARE      0x0E
#define SCSI_RESERVED        0x0F

/* Standard SCSI Sense codes*/
#define SCSI_NO_ADDITIONAL_SENSE_INFORMATION 0x00

struct scsi_cmd_text_t { int cmd; char *text; };
static struct scsi_cmd_text_t scsi_cmd_text[] = {
  { 0x00, "Test Unit Ready" }
  ,{ 0x01, "Calibrate" }
  ,{ 0x03, "Request Sense" }
  ,{ 0x04, "Format" }
  ,{ 0x08, "Read" }
  ,{ 0x0a, "Write" }
  ,{ 0x0f, "Get Param" }
  ,{ 0x10, "Mark" }
  ,{ 0x11, "Space" }
  ,{ 0x12, "Inquiry" }
  ,{ 0x15, "Mode Select" }
  ,{ 0x16, "Reserve Unit" }
  ,{ 0x18, "Copy" }
  ,{ 0x1a, "Mode Sense" }
  ,{ 0x1b, "Scan" }
  ,{ 0x1d, "Diagnose" }
  ,{ 0xa8, "Read Extended" }
  ,{ 0xd1, "Slide" }
  ,{ 0xd2, "Set Scan Head" }
  ,{ 0xd7, "Read Gain Offset" }
  ,{ 0xdc, "Write Gain Offset" }
  ,{ 0xdd, "Read State" }
  ,{ -1, NULL }
};

static char *
scsi_cmd_to_text(int cmd)
{
  int i = 0;
  while (scsi_cmd_text[i].text) {
    if (scsi_cmd_text[i].cmd == cmd)
      return scsi_cmd_text[i].text;
    i++;
  }
  return "**unknown**";
}

/**
 * Convert PIEUSB_Status to SANE_Status
 */
SANE_Status
pieusb_convert_status(PIEUSB_Status status)
{
  switch (status) {
    case PIEUSB_STATUS_CHECK_CONDITION:
      return PIEUSB_STATUS_DEVICE_BUSY;
      break;
    case PIEUSB_MAX_SANE_STATUS:
    /*fallthru*/
    default:
      if (status < PIEUSB_MAX_SANE_STATUS) {
        return (SANE_Status)status;
      }
  }
  return SANE_STATUS_INVAL;
}


/**
 * hex dump 'size' bytes starting at 'ptr'
 */
static void
_hexdump(char *msg, unsigned char *ptr, int size)
{
  unsigned char *lptr = ptr;
  int count = 0;
  long start = 0;

  while (size-- > 0)
  {
    if ((count % 16) == 0)
      fprintf (stderr, "%s\t%08lx:", msg?msg:"", start);
      msg = NULL;
	fprintf (stderr, " %02x", *ptr++);
	count++;
	start++;
	if (size == 0)
	{
	    while ((count%16) != 0)
	    {
		fprintf (stderr, "   ");
		count++;
	    }
	}
	if ((count % 16) == 0)
	{
	    fprintf (stderr, " ");
	    while (lptr < ptr)
	    {
	        unsigned char c = ((*lptr&0x7f) < 32)?'.':(*lptr & 0x7f);
		fprintf (stderr, "%c", c);
		lptr++;
	    }
	    fprintf (stderr, "\n");
	}
    }
    if ((count % 16) != 0)
	fprintf (stderr, "\n");

    fflush(stderr);
    return;
}


/* =========================================================================
 *
 * USB functions
 *
 * ========================================================================= */

/**
 * Send a command to the device, retry 10 times if device is busy
 * and return SENSE data in the sense fields of status if there is a CHECK
 * CONDITION response from the command.
 * If the REQUEST SENSE command fails, the SANE status code is unequal to
 * PIEUSB_STATUS_GOOD and the sense fields are empty.
 *
 * @param device_number Device number
 * @param command Command array
 * @param data Input or output data buffer
 * @param size Size of the data buffer
 * @param status Pieusb_Command_Status
 */

PIEUSB_Status
pieusb_command(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size)
{
#define MAXTRIES 10
  int k = MAXTRIES;
  SANE_Status st;
  SANE_Byte usbstat;
  PIEUSB_USB_Status status = USB_STATUS_AGAIN;

  DBG (DBG_info_usb,"***\tpieusb_command(%02x:%s): size 0x%02x\n", command[0], scsi_cmd_to_text(command[0]), size);

  do {
    k--;
    if (status == USB_STATUS_AGAIN) {
      status = _pieusb_scsi_command (device_number, command, data, size);
    }
    DBG (DBG_info_usb, "pieusb_command(): try %d, status %d\n", MAXTRIES-k, status);

    switch (status) {
      case USB_STATUS_OK: /* 0x00 */
        return PIEUSB_STATUS_GOOD;
        break;
      case USB_STATUS_READ: /* 0x01 */
        st = _ctrl_in_byte (device_number, &usbstat);
        if (st != SANE_STATUS_GOOD) {
	  DBG (DBG_error, "pieusb_command() fails data in: %d\n", st);
	  return USB_STATUS_ERROR;
	}
        status = usbstat;
        break;
      case USB_STATUS_CHECK: /* check condition */
      {
	struct Pieusb_Sense sense;
	struct Pieusb_Command_Status senseStatus;
	SANE_Char* sd;

#define SCSI_REQUEST_SENSE      0x03

	if (command[0] == SCSI_REQUEST_SENSE) {
	  DBG (DBG_error, "pieusb_command() recursive SCSI_REQUEST_SENSE\n");
	  return USB_STATUS_ERROR;
	}

	/* A check sense may be a busy state in disguise
	 * It is also practical to execute a request sense command by
	 * default. The calling function should interpret
	 * PIEUSB_STATUS_CHECK_SENSE as 'sense data available'. */

	pieusb_cmd_get_sense (device_number, &sense, &senseStatus);
	sd = senseDescription (&sense);
	DBG (DBG_info_usb, "pieusb_command(): CHECK CONDITION: %s\n", sd);
	free(sd);

	if (senseStatus.pieusb_status == PIEUSB_STATUS_GOOD) {
	  if (sense.senseKey == SCSI_NOT_READY ||
	      sense.senseCode == 4 ||
	      sense.senseQualifier == 1) {
	    /* This is a busy condition.
	     * Decrement number of remaining retries and pause */
	    status = USB_STATUS_BUSY;
	    DBG (DBG_info_usb, "pieusb_command(): checked - busy - try %d\n", k);
	    if (k > 0) {
	      sleep(PIEUSB_WAIT_BUSY);
	    }
	  } else {
	    status = USB_STATUS_CHECK;
	    k = 0;
	  }
	} else {
	  DBG (DBG_error, "pieusb_command(): CHECK CONDITION, but REQUEST SENSE fails\n");
	  status = USB_STATUS_ERROR;
	  k = 0;
	}
	break;
      }
      case USB_STATUS_BUSY:  /* wait on usb */
        DBG (DBG_info_usb, "pieusb_command(): busy - try %d\n", k);
	st = _ctrl_in_byte (device_number, &usbstat);
	if (st != SANE_STATUS_GOOD) {
	  DBG (DBG_error, "pieusb_scsi_command() fails data out: %d\n", st);
	  return USB_STATUS_ERROR;
	}
        status = usbstat;
        break;
      case USB_STATUS_AGAIN: /* re-send scsi cmd */
	break;
      case USB_STATUS_ERROR:
        k = 0;
        break;
    }
  } while (k > 0);
  
  DBG (DBG_info_usb, "pieusb_command() finished with state %d\n", status);
  return status;
}

/**
 * Prepare IEEE1284 interface
 * Issue one of IEEE1284_ADDR, IEEE1284_RESET, or IEEE1284_SCSI
 * 
 * @param device_number Device number
 * @param command - IEEE1284 command
 * @param status Pieusb_Command_Status
 */

static SANE_Status
pieusb_ieee_command(SANE_Int device_number, SANE_Byte command)
{
    SANE_Status st;
  static int sequence[] = { 0xff, 0xaa, 0x55, 0x00, 0xff, 0x87, 0x78 };
#define SEQUENCE_LEN 7
  unsigned int i;
    /* 2 x 4 + 3 bytes preceding command, then SCSI_COMMAND_LEN bytes command */
    /* IEEE1284 command, see hpsj5s.c:cpp_daisy() */
  for (i = 0; i < SEQUENCE_LEN; ++i) {
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, sequence[i]);
    if (st != SANE_STATUS_GOOD)
      return st;
  }
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, command);
  if (st != SANE_STATUS_GOOD)
    return st;
    st = _ctrl_out_byte(device_number, PORT_PAR_CTRL, C1284_NINIT|C1284_NSTROBE); /* CTRL_VAL_FINAL */
  if (st != SANE_STATUS_GOOD)
    return st;
  usleep(3000); /* 3.000 usec -> 3 msec */
    st = _ctrl_out_byte(device_number, PORT_PAR_CTRL, C1284_NINIT);
  if (st != SANE_STATUS_GOOD)
    return st;
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0xff);
  if (st != SANE_STATUS_GOOD)
    return st;

  return st;
#undef SEQUENCE_LEN
}

/**
 * Send a command to the device.
 * The command is a SCSI_COMMAND_LEN-byte array. The data-array is used for input and output.
 * The sense-fields of Pieusb_Command_Status are cleared.
 *
 * @param device_number Device number
 * @param command Command array
 * @param data Input or output data buffer
 * @param size Size of the data buffer
 * @returns PIEUSB_SCSI_Status
 */
static PIEUSB_USB_Status
_pieusb_scsi_command(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size)
{
  SANE_Status st;
  SANE_Byte usbstat;
  int i;

  DBG (DBG_info_usb, "_pieusb_scsi_command(): %02x:%s\n", command[0], scsi_cmd_to_text (command[0]));

  if (pieusb_ieee_command (device_number, IEEE1284_SCSI) != SANE_STATUS_GOOD) {
    DBG (DBG_error, "_pieusb_scsi_command() can't prep scsi cmd\n");
    return USB_STATUS_ERROR;
  }
  
  /* output command */
  for (i = 0; i < SCSI_COMMAND_LEN; ++i) {
    SANE_Status st;
    st = _ctrl_out_byte (device_number, PORT_SCSI_CMD, command[i]);
    if (st != SANE_STATUS_GOOD) {
      DBG (DBG_error, "_pieusb_scsi_command() fails command out, after %d bytes: %d\n", i, st);
      return USB_STATUS_ERROR;
    }
  }
  
  /* Verify this sequence */
  st = _ctrl_in_byte (device_number, &usbstat);
  if (st != SANE_STATUS_GOOD) {
    DBG (DBG_error, "_pieusb_scsi_command() fails command out: %d\n", st);
    return USB_STATUS_ERROR;
  }
  /* Process rest of the data, if present; either input or output, possibly bulk */
  DBG (DBG_info_usb, "_pieusb_scsi_command(): usbstat 0x%02x\n", usbstat);
  if (usbstat == USB_STATUS_OK && size > 0) {
    /*
     * send additional data to usb
     */
    _hexdump ("Out", data, size);
    for (i = 0; i < size; ++i) {
      st = _ctrl_out_byte (device_number, PORT_SCSI_CMD, data[i]);
      if (st != SANE_STATUS_GOOD) {
	DBG (DBG_error, "_pieusb_scsi_command() fails data out after %d bytes: %d\n", i, st);
	return USB_STATUS_ERROR;
      }
    }
    usbstat = USB_STATUS_BUSY; /* force status re-read */
  }
  else if (usbstat == USB_STATUS_READ) {
    /* Intermediate status OK, device has made data available for reading */
    /* Read data
     must be done in parts if size is large; no verification inbetween
     max part size = 0xfff0 = 65520 */
    SANE_Int remsize;
    SANE_Int partsize = 0;
    remsize = size;
	
    DBG (DBG_info_usb, "pieusb_scsi_command(): data in\n");
    while (remsize > 0) {
      partsize = remsize > 65520 ? 65520 : remsize;
      /* send expected length */
      st = _ctrl_out_int (device_number, partsize);
      if (st != SANE_STATUS_GOOD) {
	DBG (DBG_error, "_pieusb_scsi_command() prepare read data failed for size %d: %d\n", partsize, st);
	return USB_STATUS_ERROR;
      }
      /* read expected length bytes */
      st = _bulk_in (device_number, data + size - remsize, partsize);
      if (st != SANE_STATUS_GOOD) {
	DBG (DBG_error, "_pieusb_scsi_command() read data failed for size %d: %d\n", partsize, st);
	return USB_STATUS_ERROR;
      }
      remsize -= partsize;
    }
    _hexdump ("In", data, size);
  }

  return usbstat;
}


/**
 * Simplified control transfer: one byte to given port
 *
 * @param device_number device number
 * @param b byte to send to device
 * @return SANE status
 */
static SANE_Status _ctrl_out_byte(SANE_Int device_number, SANE_Int port, SANE_Byte b) {
    /* int r = libusb_control_transfer(scannerHandle, CTRL_OUT, 0x0C, 0x0088, ANYINDEX, &b, 1, TIMEOUT); */
    return sanei_usb_control_msg(device_number, REQUEST_TYPE_OUT, REQUEST_REGISTER, port, ANYINDEX, 1, &b);
}


/**
 * Simplified control transfer for port/wValue = 0x82 - prepare bulk
 *
 * @param device_number device number
 * @param size Size of bulk transfer which follows (numer of bytes)
 * @return SANE status
 */
static SANE_Status _ctrl_out_int(SANE_Int device_number, unsigned int size) {
    SANE_Byte bulksize[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    bulksize[4] = size & 0xFF;
    bulksize[5] = (size & 0xFF00) >> 8;
    return sanei_usb_control_msg(device_number, REQUEST_TYPE_OUT, REQUEST_BUFFER, PORT_SCSI_SIZE, ANYINDEX, 8, bulksize);
}


/*
 * Ctrl inbound, single byte
 */
/**
 * Inbound control transfer
 *
 * @param device_number device number
 * @param b byte received from device
 * @return SANE status
 */
static SANE_Status _ctrl_in_byte(SANE_Int device_number, SANE_Byte* b) {
    /* int r = libusb_control_transfer(scannerHandle, CTRL_IN, 0x0C, 0x0084, ANYINDEX, &b, 1, TIMEOUT); */
    /* int r = libusb_control_transfer(scannerHandle, CTRL_IN, 0x0C, 0x0084, ANYINDEX, &b, 1, TIMEOUT); */
    return sanei_usb_control_msg(device_number, REQUEST_TYPE_IN, REQUEST_REGISTER, PORT_SCSI_STATUS, ANYINDEX, 1, b);
}


/**
 * Bulk in transfer for data, in parts of 0x4000 bytes max
 *
 * @param device_number device number
 * @param data array holding or receiving data (must be preallocated)
 * @param size size of the data array
 * @return SANE status
 */
static SANE_Status
_bulk_in(SANE_Int device_number, SANE_Byte data[], unsigned int size) {
    unsigned int total = 0;
    SANE_Status r = SANE_STATUS_GOOD;
    SANE_Byte * buffer = malloc(0x4000);
    while (total < size) {
        /* Determine bulk size */
        size_t part = ((size-total) >= 0x4000 ? 0x4000 : (size-total));
        /* Get bulk data */
        /* r = libusb_bulk_transfer(scannerHandle, BULK_ENDPOINT, buffer, part, &N, TIMEOUT); */
/*
        fprintf(stderr,"[pieusb] _bulk_in() calling sanei_usb_read_bulk(), size=%d, current total=%d, this part=%d\n",size,total,part);
*/
        r = sanei_usb_read_bulk(device_number, buffer, &part);
/*
        fprintf(stderr,"[pieusb] _bulk_in() called sanei_usb_read_bulk(), result=%d (expected 0)\n",r);
*/
        if (r == SANE_STATUS_GOOD) {
            /* Read data into buffer, part = # bytes actually read */
            unsigned int k;
            for (k = 0; k < part; k++) {
                *(data+total+k) = *(buffer+k);
            }
            total += part;
        } else {
            break;
        }
    }
    free(buffer);
    return r;
}


/**
 * Return a textual description of the given sense code.
 *
 * @param sense
 * @return description
 */
SANE_String senseDescription(struct Pieusb_Sense* sense)
{
    SANE_Char* desc = malloc(200);
    if (sense->senseKey == 0x02) {
        strcpy (desc,"NOT READY");
    } else if (sense->senseKey == 5) {
        strcpy (desc,"ILLEGAL REQUEST");
    } else if (sense->senseKey == 6) {
        strcpy (desc,"UNIT ATTENTION");
    } else if (sense->senseKey == 11) {
        strcpy (desc,"ABORTED COMMAND");
    } else {
        sprintf (desc,"senseKey %d", sense->senseKey);
    }

    if (sense->senseCode == 4 && sense->senseQualifier == 1) {
        strcat (desc,": Logical unit is in the process of becoming ready");
    } else if (sense->senseCode == 26 && sense->senseQualifier == 0) {
        strcat (desc,": Invalid field in parameter list");
    } else if (sense->senseCode == 32 && sense->senseQualifier == 0) {
        strcat (desc,": Invald command operation code");
    } else if (sense->senseCode == 130 && sense->senseQualifier == 0) {
        strcat (desc,": SCAN entering Calibration phase (vs)");
    } else if (sense->senseCode == 0 && sense->senseQualifier == 6) {
        strcat (desc,": I/O process terminated");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 130) {
        strcat (desc,": MODE SELECT value invalid: resolution too high (vs)");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 131) {
        strcat (desc,": MODE SELECT value invalid: select only one color (vs)");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 131) {
        strcat (desc,": MODE SELECT value invalid: unsupported bit depth (vs)");
    } else {
        sprintf (desc,": senseCode %d, senseQualifier %d", sense->senseCode, sense->senseQualifier);
    }
    return desc;
}

