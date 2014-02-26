/*
 * File:   pieusb_usb.c
 * Author: Jan Vleeshouwers
 *
 * Created on August 20, 2012, 7:47 PM
 */

#define DEBUG_DECLARE_ONLY
#include "pieusb.h"
#include "pieusb_scancmd.h"
#include "pieusb_usb.h"

#include <sane/sanei_usb.h>

/* USB functions */

static SANE_Status _ctrl_out_byte(SANE_Int device_number, SANE_Int port, SANE_Byte b);
static SANE_Status _ctrl_out_int(SANE_Int device_number, unsigned int size);
static SANE_Status _ctrl_in_byte(SANE_Int device_number, SANE_Byte* b);
static SANE_Status _bulk_in(SANE_Int device_number, SANE_Byte* data, unsigned int size);
SANE_Status _interprete_status(SANE_Byte status[]);

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

#define PORT_SCSI_SIZE 0x0082
#define PORT_SCSI_STATUS 0x0084
#define PORT_SCSI_CMD 0x0085
#define PORT_PAR_CTRL 0x0087 /* IEEE1284 parallel control */
#define PORT_PAR_DATA 0x0088 /* IEEE1284 parallel data */

/* SCSI status codes */

#define SCSI_STATUS_OK 0x00
#define SCSI_STATUS_SENSE 0x02
#define SCSI_STATUS_BUSY 0x08
#define SCSI_STATUS_WRITE_ERROR 0x0A
#define SCSI_STATUS_READ_ERROR 0x0B

#define USB_STATUS_READY_TO_ACCEPT_DATA 0x00
#define USB_STATUS_DATA_AVAILABLE 0x01
#define USB_STATUS_COMMAND_COMPLETE 0x03

/* Standard SCSI Sense keys */
#define SCSI_NO_SENSE 0x00
#define SCSI_RECOVERED_ERROR 0x01
#define SCSI_NOT_READY 0x02
#define SCSI_MEDIUM_ERROR 0x03
#define SCSI_HARDWARE_ERROR 0x04
#define SCSI_ILLEGAL_REQUEST 0x05
#define SCSI_UNIT_ATTENTION 0x06
#define SCSI_DATA_PROTECT 0x07
#define SCSI_BLANK_CHECK 0x08
#define SCSI_VENDOR_SPECIFIC 0x09
#define SCSI_COPY_ABORTED 0x0A
#define SCSI_ABORTED_COMMAND 0x0B
#define SCSI_EQUAL 0x0C
#define SCSI_VOLUME_OVERFLOW 0x0D
#define SCSI_MISCOMPARE 0x0E
#define SCSI_RESERVED 0x0F

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

/* =========================================================================
 *
 * USB functions
 *
 * ========================================================================= */

/**
 * Send a command to the device, retry at most repeat times if device is busy
 * and return SENSE data in the sense fields of status if there is a CHECK
 * CONDITION response from the command.
 * If the REQUEST SENSE command fails, the SANE status code is unequal to
 * PIEUSB_STATUS_GOOD and the sense fields are empty.
 *
 * If repeat == 0, commandScannerRepeat() equals commandScanner() with an
 * included sense check in case of a check sense return.
 *
 * @param device_number Device number
 * @param command Command array
 * @param data Input or output data buffer
 * @param size Size of the data buffer
 * @param status Pieusb_Command_Status
 * @param repeat Maximum number of tries after a busy state
 */
void
commandScannerRepeat(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status, int repeat)
{
    int k = repeat;
    int tries = 0;
    SANE_Char* sd;
    struct Pieusb_Sense sense;
    struct Pieusb_Command_Status senseStatus;

    DBG(DBG_info_usb,"commandScannerRepeat(%02x:%s): enter, repeat=%d\n", command[0], scsi_cmd_to_text(command[0]), repeat);
    do {

        commandScanner(device_number, command, data, size, status);
        tries++;

        switch (status->pieusb_status) {

            case PIEUSB_STATUS_GOOD:
                /* Command executed succesfully */
                k = 0;
                break;

            case PIEUSB_STATUS_DEVICE_BUSY:
                /* Decrement number of remaining retries and pause */
                k--;
                DBG(DBG_info_usb,"commandScannerRepeat(): busy - repeat %d\n",k);
                if (k>0) sleep(PIEUSB_WAIT_BUSY);
                break;

            case PIEUSB_STATUS_IO_ERROR:
            case PIEUSB_STATUS_INVAL:
                /* Unexpected data returned by device */
	        DBG(DBG_info_usb,"commandScannerRepeat(): error/invalid - exit: status %d\n", status->pieusb_status);
                k = 0;
                break;

            case PIEUSB_STATUS_CHECK_CONDITION:
                /* A check sense may be a busy state in disguise
                 * It is also practical to execute a request sense command by
                 * default. The calling function should interpret
                 * PIEUSB_STATUS_CHECK_SENSE as 'sense data available'. */
                cmdGetSense(device_number, &sense, &senseStatus);
                if (senseStatus.pieusb_status == PIEUSB_STATUS_GOOD) {
                    if (sense.senseKey == SCSI_NOT_READY ||
                        sense.senseCode == 4 ||
                        sense.senseQualifier == 1) {
                        /* This is a busy condition.
                         * Decrement number of remaining retries and pause */
                        status->pieusb_status = PIEUSB_STATUS_DEVICE_BUSY;
                        k--;
                        DBG(DBG_info_usb,"commandScannerRepeat(): checked - busy - repeat %d\n",k);
                        if (k>0) sleep(PIEUSB_WAIT_BUSY);
                    } else {
                        status->pieusb_status = PIEUSB_STATUS_CHECK_CONDITION;
                        status->senseKey = sense.senseKey;
                        status->senseCode = sense.senseCode;
                        status->senseQualifier = sense.senseQualifier;
                        sd = senseDescription(&sense);
                        DBG(DBG_info_usb,"commandScannerRepeat(): CHECK CONDITION: %s\n",sd);
                        free(sd);
                        k = 0;
                    }
                } else {
                    DBG(DBG_error,"commandScannerRepeat(): CHECK CONDITION, but REQUEST SENSE fails\n");
                    status->pieusb_status = PIEUSB_STATUS_INVAL;
                    k = 0;
                }
                break;

            default:
                /* Keep current status */
                break;
        }

     } while (k>0);
     DBG(DBG_info_usb,"commandScannerRepeat(): ready, tries=%d\n",tries);
}

/**
 * Send a command to the device.
 * The command is a 6-byte array. The data-array is used for input and output.
 * The sense-fields of Pieusb_Command_Status are cleared.
 *
 * @param device_number Device number
 * @param command Command array
 * @param data Input or output data buffer
 * @param size Size of the data buffer
 * @param status Pieusb_Command_Status
 */
void
commandScanner(SANE_Int device_number, SANE_Byte command[], SANE_Byte data[], SANE_Int size, struct Pieusb_Command_Status *status)
{

    SANE_Status st;
    /* Clear 2-byte status-array which contains 1 or 2 byte code returned from device */
    SANE_Byte usbstat[] = {0x00, 0x00};
    /* Clear status structure to be returned */
    status->pieusb_status = PIEUSB_STATUS_GOOD;
    status->senseKey = SCSI_NO_SENSE;
    status->senseCode = SCSI_NO_ADDITIONAL_SENSE_INFORMATION;
    status->senseQualifier = 0x00;

    /* 2 x 4 + 3 bytes preceding command, then 6 bytes command */
    /* IEEE1284 command, see hpsj5s.c:cpp_daisy() */
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0xff); /* CTRL_VAL_INIT */
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0xaa);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0x55);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0x00);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0xff);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0x87);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0x78);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, IEEE1284_SCSI);
    st = _ctrl_out_byte(device_number, PORT_PAR_CTRL, C1284_NINIT|C1284_NSTROBE); /* CTRL_VAL_FINAL */
    st = _ctrl_out_byte(device_number, PORT_PAR_CTRL, C1284_NINIT);
    st = _ctrl_out_byte(device_number, PORT_PAR_DATA, 0xff);
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[0]); /* CTRL_VAL_CMD */
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[1]);
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[2]);
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[3]);
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[4]);
    st = _ctrl_out_byte(device_number, PORT_SCSI_CMD, command[5]);
    /* Verify this sequence */
    st = _ctrl_in_byte(device_number, &usbstat[0]);
    if (st != SANE_STATUS_GOOD) {
        DBG(DBG_error, "commandScanner() fails 1st verification, 1st byte\n");
        status->pieusb_status = st;
        return;
    }
    /* Process rest of the data, if present; either input or output, possibly bulk */
    switch (usbstat[0]) {
        case USB_STATUS_READY_TO_ACCEPT_DATA:
            /* Intermediate status OK, device is ready to accept additional command data */
            /* Write data */
            {
                SANE_Int k = 0;
                for (k=0; k<size; k++) _ctrl_out_byte(device_number, PORT_SCSI_CMD, data[k]);
                /* Verify again */
                st = _ctrl_in_byte(device_number, &usbstat[0]);
                if (st != SANE_STATUS_GOOD) {
                    DBG(DBG_error, "commandScanner() fails 2nd verification after write, 1st byte\n");
                    status->pieusb_status = st;
                    return;
                }
                switch (usbstat[0]) {
                    case USB_STATUS_COMMAND_COMPLETE:
                        st = _ctrl_in_byte(device_number,&usbstat[1]);
                        if (st != SANE_STATUS_GOOD) {
                            DBG(DBG_error, "commandScanner() fails 2nd verification after write, 2nd byte\n");
                            status->pieusb_status = st;
                            return;
                        }
                        break;
                    default:
                        /* Error, use special code for 2nd status byte */
                        usbstat[1] = SCSI_STATUS_WRITE_ERROR;
                        break;
                }
            }
            break;
        case USB_STATUS_DATA_AVAILABLE:
            /* Intermediate status OK, device has made data available for reading */
            /* Read data
               must be done in parts if size is large; no verification inbetween
               max part size = 0xfff0 = 65520 */
            {
                SANE_Int remsize = 0;
                SANE_Int partsize = 0;
                remsize = size;

                while (remsize>0) {
                    partsize = remsize > 65520 ? 65520 : remsize;
                    _ctrl_out_int(device_number, partsize);
                    _bulk_in(device_number, data+size-remsize, partsize);
                    remsize -= partsize;
                }
                /* Verify again */
                st = _ctrl_in_byte(device_number,&usbstat[0]);
                if (st != SANE_STATUS_GOOD) {
                    DBG(DBG_error, "commandScanner() fails 2nd verification after read, 1st byte\n");
                    status->pieusb_status = st;
                    return;
                }
                switch (usbstat[0]) {
                    case USB_STATUS_COMMAND_COMPLETE:
                        st = _ctrl_in_byte(device_number,&usbstat[1]);
                        if (st != SANE_STATUS_GOOD) {
                            DBG(DBG_error, "commandScanner() fails 2nd verification after read, 2nd byte\n");
                            status->pieusb_status = st;
                            return;
                        }
                        break;
                    default:
                        /* Error, use special code */
                        usbstat[1] = SCSI_STATUS_READ_ERROR;
                        break;
                }
            }
            break;
        case USB_STATUS_COMMAND_COMPLETE: /* Next byte needed */
            {
                st = _ctrl_in_byte(device_number,&usbstat[1]);
                if (st != SANE_STATUS_GOOD) {
                    DBG(DBG_error, "commandScanner() fails 1st verification, 2nd byte\n");
                    status->pieusb_status = st;
                    return;
                }
                break;
            }
    }

    status->pieusb_status = usbstat[0];
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
static SANE_Status _bulk_in(SANE_Int device_number, SANE_Byte data[], unsigned int size) {
    unsigned int total = 0;
    SANE_Status r = SANE_STATUS_GOOD;
    SANE_Byte * buffer = malloc(0x4000);
    while (total<size) {
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
        if (r==0) {
            /* Read data into buffer, part = # bytes actually read */
            unsigned int k;
            for (k=0; k<part; k++) {
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
 * Interpret the 2-byte status returned from the device as a SANE status.
 *
 * @param status Pieusb status
 * @return SANE status
 */
SANE_Status _interprete_status(SANE_Byte status[])
{
    SANE_Status s = SANE_STATUS_INVAL;
    switch (status[0]) {
        case USB_STATUS_COMMAND_COMPLETE:
            /* Command completed */
            switch (status[1]) {
                case SCSI_STATUS_OK:
                    /* OK*/
                    s = SANE_STATUS_GOOD;
                    break;
                case SCSI_STATUS_SENSE:
                    /* Check condition */
                    s = SANE_STATUS_CHECK_CONDITION;
                    break;
                case SCSI_STATUS_BUSY:
                    /* Busy*/
                    s = SANE_STATUS_DEVICE_BUSY;
                    break;
                default:
                    /* Unexpected code */
                    s = SANE_STATUS_INVAL;
                    break;
            }
            break;
        default:
            /* IO-erro or unexpected code */
            switch (status[1]) {
                case SCSI_STATUS_WRITE_ERROR:
                case SCSI_STATUS_READ_ERROR:
                    s = SANE_STATUS_IO_ERROR;
                default:
                    s = SANE_STATUS_INVAL;
            }
            break;
    }
    return s;
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
        strcpy(desc,"NOT READY");
    } else if (sense->senseKey == 5) {
        strcpy(desc,"ILLEGAL REQUEST");
    } else if (sense->senseKey == 6) {
        strcpy(desc,"UNIT ATTENTION");
    } else if (sense->senseKey == 11) {
        strcpy(desc,"ABORTED COMMAND");
    } else {
        strcpy(desc,"?");
    }

    if (sense->senseCode == 4 && sense->senseQualifier == 1) {
        strcat(desc,": Logical unit is in the process of becoming ready");
    } else if (sense->senseCode == 26 && sense->senseQualifier == 0) {
        strcat(desc,": Invalid field in parameter list");
    } else if (sense->senseCode == 32 && sense->senseQualifier == 0) {
        strcat(desc,": Invald command operation code");
    } else if (sense->senseCode == 130 && sense->senseQualifier == 0) {
        strcat(desc,": SCAN entering Calibration phase (vs)");
    } else if (sense->senseCode == 0 && sense->senseQualifier == 6) {
        strcat(desc,": I/O process terminated");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 130) {
        strcat(desc,": MODE SELECT value invalid: resolution too high (vs)");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 131) {
        strcat(desc,": MODE SELECT value invalid: select only one color (vs)");
    } else if (sense->senseCode == 38 && sense->senseQualifier == 131) {
        strcat(desc,": MODE SELECT value invalid: unsupported bit depth (vs)");
    } else {
        strcat(desc,": ?");
    }
    return desc;
}

