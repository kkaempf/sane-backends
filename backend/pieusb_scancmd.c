/*
 * File:   pieusb__scancmd.c
 * Author: Jan Vleeshouwers
 *
 */

#define DEBUG_DECLARE_ONLY

#include "pieusb.h"
#include "pieusb_scancmd.h"
#include "pieusb_usb.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

/* =========================================================================
 *
 * Pieusb utility functions
 *
 * ========================================================================= */

/*
 * Get the unsigned char value in the array at given offset
 */
static SANE_Byte
_get_byte(SANE_Byte* array, SANE_Byte offset) {
    return *(array+offset);
}

/*
 * Set the array at given offset to the given unsigned char value
 */
static void
_set_byte(SANE_Byte val, SANE_Byte* array, SANE_Byte offset) {
    *(array+offset) = val;
}


/*
 * Get the unsigned short value in the array at given offset.
 * All data in structures is little-endian, so the LSB comes first
 * SANE_Int is 4 bytes, but that is not a problem.
 */
static SANE_Int
_get_short(SANE_Byte* array, SANE_Byte offset) {
    SANE_Int i = *(array+offset+1);
    i <<= 8;
    i += *(array+offset);
    return i;
}


/*
 * Put the bytes of a short int value into an unsigned char array
 * All data in structures is little-endian, so start with LSB
 */
static void
_set_short(SANE_Word val, SANE_Byte* array, SANE_Byte offset) {
    *(array+offset) = val & 0xFF;
    *(array+offset+1) = (val>>8) & 0xFF;
}


/*
 * Get the signed int value in the array at given offset.
 * All data in structures is little-endian, so the LSB comes first
 */
static SANE_Int
_get_int(SANE_Byte* array, SANE_Byte offset) {
    SANE_Int i = *(array+offset+3);
    i <<= 8;
    i += *(array+offset+2);
    i <<= 8;
    i += *(array+offset+1);
    i <<= 8;
    i += *(array+offset);
    return i;
}

#if 0 /* unused */
/*
 * Put the bytes of a signed int value into an unsigned char array
 * All data in structures is little-endian, so start with LSB
 */
static void
_set_int(SANE_Word val, SANE_Byte* array, SANE_Byte offset) {
    *(array+offset) = val & 0xFF;
    *(array+offset+1) = (val>>8) & 0xFF;
    *(array+offset+2) = (val>>16) & 0xFF;
    *(array+offset+3) = (val>>24) & 0xFF;
}
#endif

/*
 * Copy count unsigned char values from src to dst
 */
static void
_copy_bytes(SANE_Byte* dst, SANE_Byte* src, SANE_Byte count) {
    SANE_Byte k;
    for (k=0; k<count; k++) {
        *dst++ = *src++;
    }
}


/*
 * Get count unsigned short values in the array at given offset.
 * All data in structures is little-endian, so the MSB comes first.
 * SANE_Word is 4 bytes, but that is not a problem.
 */
static void
_get_shorts(SANE_Word* dst, SANE_Byte* src, SANE_Byte count) {
    SANE_Byte k;
    for (k=0; k<count; k++) {
        *dst = *(src+2*k+1);
        *dst <<= 8;
        *dst++ += *(src+2*k);
    }
}

/*
 * Copy an unsigned short array of given size
 * All data in structures is little-endian, so start with MSB of each short.
 * SANE_Word is 4 bytes, but that is not a problem. All MSB 2 bytes are ignored.
 */
static void
_set_shorts(SANE_Word* src, SANE_Byte* dst, SANE_Byte count) {
    SANE_Byte k;
    for (k=0; k<count; k++) {
        *(dst+2*k) = *src & 0xFF;
        *(dst+2*k+1) = ((*src++)>>8) & 0xFF;
    }
}


/* =========================================================================
 *
 * Pieusb scanner commands
 *
 * ========================================================================= */

#define SCSI_COMMAND_LEN 6

/* Standard SCSI command codes */
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_READ               0x08
#define SCSI_WRITE              0x0A
#define SCSI_PARAM              0x0F
#define SCSI_INQUIRY            0x12
#define SCSI_MODE_SELECT        0x15
#define SCSI_COPY               0x18
#define SCSI_MODE_SENSE         0x1A
#define SCSI_SCAN               0x1B

/* Non-standard SCSI command codes */
#define SCSI_SET_SCAN_HEAD      0xD2
#define SCSI_READ_GAIN_OFFSET   0xD7
#define SCSI_WRITE_GAIN_OFFSET  0xDC
#define SCSI_READ_STATE         0xDD

/* Additional SCSI READ/WRITE codes */
#define SCSI_HALFTONE_PATTERN   0x11
#define SCSI_SCAN_FRAME         0x12
#define SCSI_CALIBRATION_INFO   0x15

/**
 * Perform a TEST UNIT READY (SCSI command code 0x00)
 * Returns status->pieusb_status:
 * - PIEUSB_STATUS_GOOD if device is ready
 * - PIEUSB_STATUS_DEVICE_BUSY if device is still busy after timeout
 * - PIEUSB_STATUS_CHECK_CONDITION with accompanying sense codes if command
 *   returned a CHECK CONDITION
 * - other SANE status code if TEST UNIT READY failed or if it returned
 *   CHECK CONDITION and REQUEST SENSE failed
 *
 * @param device_number Device number
 * @return Pieusb_Command_Status SANE_STATUS_GOOD if ready, SANE_STATUS_DEVICE_BUSY if not
 */
void
cmdIsUnitReady(SANE_Int device_number, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];

    DBG (DBG_info_scan, "cmdIsUnitReady()\n");

    setCommand(command, SCSI_TEST_UNIT_READY, 0);

    commandScannerRepeat(device_number, command, NULL, 0, status);

    DBG (DBG_info_scan, "cmdIsUnitReady() return status = %s\n", sane_strstatus(status->pieusb_status));
}

/**
 * Perform a REQUEST SENSE (SCSI command code 0x03)
 * Returns status->pieusb_status:
 * - PIEUSB_STATUS_GOOD is the command executes OK
 * - other SANE status code if REQUEST SENSE fails
 * The sense fields in status are always 0. A REQUEST SENSE is not repeated if
 * the device returns PIEUSB_STATUS_DEVICE_BUSY.
 *
 * @param device_number Device number
 * @param sense Sense data
 * @param status Command result status
 * @see struc Pieusb_Sense
 */
void
cmdGetSense(SANE_Int device_number, struct Pieusb_Sense* sense, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define DATA_SIZE 14
    SANE_Int size = DATA_SIZE;
    SANE_Byte data[DATA_SIZE];
    PIEUSB_SCSI_Status sst;

    DBG (DBG_info_scan, "cmdGetSense()\n");

    setCommand(command, SCSI_REQUEST_SENSE, size);

    memset(data, '\0', size);
    sst = pieusb_scsi_command(device_number, command, data, size);
    if (sst != SCSI_STATUS_OK) {
      /*FIXME*/
        return;
    }

    /* Decode data recieved */
    sense->errorCode = _get_byte(data, 0);
    sense->segment = _get_byte(data, 1);
    sense->senseKey = _get_byte(data, 2);
    _copy_bytes(sense->info, data+3, 4);
    sense->addLength = _get_byte(data, 7);
    _copy_bytes(sense->cmdInfo, data+8, 4);
    sense->senseCode = _get_byte(data, 12);
    sense->senseQualifier = _get_byte(data, 13);
    status->pieusb_status = PIEUSB_STATUS_GOOD;
#undef DATA_SIZE
}

/**
 * Read the halftone pattern with the specified index. This requires two
 * commands, one to ask the device to prepare the pattern, and one to read it.
 *
 * @param device_number Device number
 * @param index index of halftone pattern
 * @param pattern Halftone pattern (not implemented)
 * @return Pieusb_Command_Status
 * @see Pieusb_Halftone_Pattern
 */
void
cmdGetHalftonePattern(SANE_Int device_number, SANE_Int index, struct Pieusb_Halftone_Pattern* pattern, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define PATTERN_SIZE 256 /* Assumed maximum pattern size */
    SANE_Int size = PATTERN_SIZE;
    SANE_Byte data[PATTERN_SIZE];
    int psize;
    SANE_Char* desc;
    PIEUSB_SCSI_Status sst;

    DBG (DBG_info_scan, "cmdGetHalftonePattern()\n");

    /* Ask scanner to prepare the pattern with the given index. Only SCSI_COMMAND_LEN bytes of data. */
    setCommand(command, SCSI_WRITE, SCSI_COMMAND_LEN);
    memset(data, '\0', SCSI_COMMAND_LEN);
    data[0] = SCSI_HALFTONE_PATTERN | 0x80; /* set bit 7 means prepare read */
    data[4] = index;

    sst = pieusb_scsi_command(device_number, command, data, SCSI_COMMAND_LEN);
    if (sst != SCSI_STATUS_OK) {
      /* FIXME */
        return;
    }

    /* Read pattern */
    setCommand(command, SCSI_READ, size);

    memset(data, '\0', size);
    sst = pieusb_scsi_command(device_number, command, data, size);

    /*FIXME: analyse */
    fprintf(stderr, "Halftone pattern %d:\n", index);
    psize = (data[3]<<8) + data[2];
    desc = (SANE_Char*)(data + 4 + psize);
    data[4 + psize + 16] = '\0';
    fprintf(stderr,"Descr. offset from byte 4 = %d, %16s, index = %d, size = %dx%d\n", psize, desc, data[4]&0x7F, data[6], data[7]);
#undef PATTERN_SIZE
}

/**
 * Read the scan frame with the specified index. This requires two
 * commands, one to ask the device to prepare the pattern, and one to read it.
 *
 * @param device_number Device number
 * @param frame Scan frame
 * @return Pieusb_Command_Status
 * @see Pieusb_Scan_Frame
 */
void
cmdGetScanFrame(SANE_Int device_number, SANE_Int index, struct Pieusb_Scan_Frame* frame, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define FRAME_SIZE 256 /* Assumed maximum frame size */
    SANE_Int size = FRAME_SIZE;
    SANE_Byte data[FRAME_SIZE];
    PIEUSB_SCSI_Status sst;

    DBG (DBG_info_scan, "cmdGetScanFrame()\n");

    /* Ask scanner to prepare the scan frame with the given index. Only SCSI_COMMAND_LEN bytes of data. */
    setCommand(command, SCSI_WRITE, SCSI_COMMAND_LEN);
    memset(data, '\0', SCSI_COMMAND_LEN);
    data[0] = SCSI_SCAN_FRAME | 0x80; /* set bit 7 means prepare read */
    data[4] = index;

    sst = pieusb_scsi_command(device_number, command, data, SCSI_COMMAND_LEN);
    if (sst != SCSI_STATUS_OK) {
      /* FIXME */
        return;
    }

    /* Read scan frame */
    setCommand(command, SCSI_READ, size);

    memset(data, '\0', size);
    sst = pieusb_scsi_command(device_number, command, data, size);
    /* FIXME */
    /* Decode data */
    frame->code = _get_byte(data, 0);
    frame->size = _get_short(data, 2);
    frame->index = _get_byte(data, 4);
    frame->x0 = _get_short(data, 6);
    frame->y0 = _get_short(data, 8);
    frame->x1 = _get_short(data, 10);
    frame->y1 = _get_short(data, 12);

    DBG (DBG_info_scan, "cmdGetScanFrame() set:\n");
    DBG (DBG_info_scan, " x0,y0 = %d,%d\n", frame->x0, frame->y0);
    DBG (DBG_info_scan, " x1,y1 = %d,%d\n", frame->x1, frame->y1);
    DBG (DBG_info_scan, " code = %d\n", frame->code);
    DBG (DBG_info_scan, " index = %d\n", frame->index);
    DBG (DBG_info_scan, " size = %d\n", frame->size);
#undef FRAME_SIZE
}

/**
 * Read the relative exposure time for the specified colorbits. This requires two
 * commands, one to ask the device to prepare the value, and one to read it.
 *
 * @param device_number Device number
 * @param time Relative exposure time(s)
 * @return Pieusb_Command_Status
 * @see Pieusb_Exposure_Time
 */
void
cmdGetRelativeExposureTime(SANE_Int device_number, SANE_Int colorbits, struct Pieusb_Exposure_Time* time, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdGetRelativeExposureTime(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/**
 * Read the highlight and shadow levels with the specified colorbits. This requires two
 * commands, one to ask the device to prepare the value, and one to read it.
 *
 * @param device_number Device number
 * @param hgltshdw Highlight and shadow level(s)
 * @return Pieusb_Command_Status
 * @see Pieusb_Highlight_Shadow
 */
void
cmdGetHighlightShadow(SANE_Int device_number, SANE_Int colorbits, struct Pieusb_Highlight_Shadow* hgltshdw, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdGetHighlightShadow(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/**
 * Read the shading data parameters. This requires two
 * commands, one to ask the device to prepare the value, and one to read it.
 *
 * @param device_number Device number
 * @param shading Shading data parameters
 * @return Pieusb_Command_Status
 * @see Pieusb_Shading_Parameters
 */
void
cmdGetShadingParameters(SANE_Int device_number, struct Pieusb_Shading_Parameters_Info* shading, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define SHADING_SIZE 32
    SANE_Int size = SHADING_SIZE;
    SANE_Byte data[SHADING_SIZE];
    int k;
    PIEUSB_SCSI_Status sst;

    DBG (DBG_info_scan, "cmdGetShadingParameters()\n");

    /* Ask scanner to prepare the scan frame with the given index. Only SCSI_COMMAND_LEN bytes of data. */
    setCommand(command, SCSI_WRITE, SCSI_COMMAND_LEN);
    memset(data, '\0', SCSI_COMMAND_LEN);
    data[0] = SCSI_CALIBRATION_INFO | 0x80; /* set bit 7 means prepare read */

    sst = pieusb_scsi_command(device_number, command, data, SCSI_COMMAND_LEN);
    if (sst != SCSI_STATUS_OK) {
      /* FIXME */
        return;
    }

    /* Read shading parameters */
    setCommand(command, SCSI_READ, size);

    memset(data, '\0', size);
    sst = pieusb_scsi_command(device_number, command, data, size);
    /* FIXME */
    /* Decode data */
    for (k=0; k<data[4]; k++) {
        shading[k].type = _get_byte(data, 8+6*k);
        shading[k].sendBits = _get_byte(data, 9+6*k);
        shading[k].recieveBits = _get_byte(data, 10+6*k);
        shading[k].nLines = _get_byte(data, 11+6*k);
        shading[k].pixelsPerLine = _get_short(data, 12+6*k);
    }
#undef SHADING_SIZE
}

/**
 * Read scanned data from the scanner memory into a byte array. The lines
 * argument specifies how many lines will be read, the size argument specifies
 * the total amount of bytes in these lines. Use cmdGetScanParameters() to
 * determine the current line size and the number of available lines.\n
 * If there is scanned data available, it should be read. Waiting too long
 * causes the scan to stop, probably because a buffer is filled to its limits
 * (if so, it is approximately 2Mb in size). I haven't tried what happens if you
 * start reading after a stop. Reading to fast causes the scanner to return
 * a busy status, which is not a problem.
 * This is a SCSI READ command (code 0x08). It is distinguished from the other
 * READ commands by the context in which it is issued: see cmdStartScan().
 *
 * @param device_number
 * @param data
 * @param lines
 * @param size
 * @return Pieusb_Command_Status
 */
void
cmdGetScannedLines(SANE_Int device_number, SANE_Byte* data, SANE_Int lines, SANE_Int size, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];

    DBG (DBG_info_scan, "cmdGetScannedLines(): %d (%d bytes)\n", lines, size);

    setCommand(command, SCSI_READ, lines);
    memset(data, '\0', size);

    commandScannerRepeat(device_number, command, data, size, status);
}

/**
 * Set the halftone pattern with the given index to the specified pattern. The
 * command is a SCSI WRITE command (code 0x0A, write code 0x11).
 *
 * @param device_number Device number
 * @param index Pattern index (0-7)
 * @param pattern Halftone pattern (not implemented)
 * @return Pieusb_Command_Status
 * @see Pieusb_Halftone_Pattern
 */
void
cmdSetHalftonePattern(SANE_Int device_number, SANE_Int index, struct Pieusb_Halftone_Pattern* pattern, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdSetHalftonePattern(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/**
 * Set the scan frame with the given index to the frame. The command is a SCSI
 * WRITE command (code 0x0A, write code 0x12).
 *
 * @param device_number Device number
 * @param index Frame index (0-7)
 * @param frame Scan frame
 * @return Pieusb_Command_Status
 * @see Pieusb_Scan_Frame
 */
void
cmdSetScanFrame(SANE_Int device_number, SANE_Int index, struct Pieusb_Scan_Frame* frame, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define FRAME_SIZE 14
    SANE_Int size = FRAME_SIZE;
    SANE_Byte data[FRAME_SIZE];

    DBG (DBG_info_scan, "cmdSetScanFrame()\n");

    setCommand(command, SCSI_WRITE, size);

    DBG (DBG_info_scan, "cmdSetScanFrame() set:\n");
    DBG (DBG_info_scan, " x0,y0 = %d,%d\n",frame->x0,frame->y0);
    DBG (DBG_info_scan, " x1,y1 = %d,%d\n",frame->x1,frame->y1);
    DBG (DBG_info_scan, " code = %d\n",frame->code);
    DBG (DBG_info_scan, " index = %d\n",frame->index);
    DBG (DBG_info_scan, " size = %d\n",frame->size);

    /* Code data */
    memset(data, '\0', size);
    _set_short(SCSI_SCAN_FRAME, data, 0);
    _set_short(size-4, data, 2); /* size: one frame, 5 shorts */
    _set_short(index, data, 4);
    _set_short(frame->x0, data, 6);
    _set_short(frame->y0, data, 8);
    _set_short(frame->x1, data, 10);
    _set_short(frame->y1, data, 12);

    commandScannerRepeat(device_number, command, data, size, status);
#undef FRAME_SIZE
}

/**
 * Set the relative exposure time to the given values. Only the first
 * Pieusb_Exposure_Time_Color is used. The command is a SCSI
 * WRITE command (code 0x0A, write code 0x13).
 *
 * @param device_number Device number
 * @param time Relative exposure time
 * @return Pieusb_Command_Status
 * @see Pieusb_Exposure_Time
 */
void
cmdSetRelativeExposureTime(SANE_Int device_number, struct Pieusb_Exposure_Time* time, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdSetRelativeExposureTime(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/**
 * Set the highlight and shadow levels to the given values. Only the first
 * Pieusb_Highlight_Shadow_Color is used. The command is a SCSI
 * WRITE command (code 0x0A, write code 0x14).
 *
 * @param device_number Device number
 * @param hgltshdw highlight and shadow level
 * @return Pieusb_Command_Status
 * @see Pieusb_Highlight_Shadow
 */
void
cmdSetHighlightShadow(SANE_Int device_number, struct Pieusb_Highlight_Shadow* hgltshdw, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdSetHighlightShadow(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/**
 * Set the CCD-mask for the colors set in the given color bit mask. The mask
 * array must contain 2x5340 = 10680 bytes. The command is a SCSI WRITE command
 * (code 0x0A, write code 0x16).
 * (The command is able to handle more masks at once, but that is not implemented.)
 *
 * @param device_number Device number
 * @param colorbits 0000RGB0 color bit mask; at least one color bit must be set
 * @param mask CCD mask to use
 * @return Pieusb_Command_Status
 */
void
cmdSetCCDMask(SANE_Int device_number, SANE_Byte colorbits, SANE_Byte* mask, struct Pieusb_Command_Status *status)
{
    DBG (DBG_info_scan, "cmdSetCCDMask(): not implemented\n");
    status->pieusb_status = PIEUSB_STATUS_INVAL;
}

/* SCSI PARAM, code 0x0F */
/**
 * Get the parameters of an executed scan, such as width, lines and bytes, which
 * are needed to calculate the parameters of the READ-commands which read the
 * actual scan data.
 *
 * @param device_number Device number
 * @param parameters Scan parameters
 * @return Pieusb_Command_Status
 * @see Pieusb_Scan_Parameters
 */
void
cmdGetScanParameters(SANE_Int device_number, struct Pieusb_Scan_Parameters* parameters, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define PARAMETER_SIZE 18
    SANE_Int size = PARAMETER_SIZE;
    SANE_Byte data[PARAMETER_SIZE];

    DBG (DBG_info_scan, "cmdGetScanParameters()\n");

    setCommand(command, SCSI_PARAM, size);
    memset(data, '\0', size);

    commandScannerRepeat(device_number, command, data, size, status);
    if (status->pieusb_status != PIEUSB_STATUS_GOOD) {
        return;
    }

    /* Decode data recieved */
    parameters->width = _get_short(data,0);
    parameters->lines = _get_short(data,2);
    parameters->bytes = _get_short(data,4);
    parameters->filterOffset1 = _get_byte(data,6);
    parameters->filterOffset2 = _get_byte(data,7);
    parameters->period = _get_int(data,8);
    parameters->scsiTransferRate = _get_short(data,12);
    parameters->availableLines = _get_short(data,14);

    DBG (DBG_info_scan, "cmdGetScanParameters() read:\n");
    DBG (DBG_info_scan, " width = %d\n",parameters->width);
    DBG (DBG_info_scan, " lines = %d\n",parameters->lines);
    DBG (DBG_info_scan, " bytes = %d\n",parameters->bytes);
    DBG (DBG_info_scan, " offset1 = %d\n",parameters->filterOffset1);
    DBG (DBG_info_scan, " offset2 = %d\n",parameters->filterOffset2);
    DBG (DBG_info_scan, " available lines = %d\n",parameters->availableLines);
#undef PARAMETER_SIZE
}

/**
 * Read INQUIRY block from device (SCSI command code 0x12). This block contains
 * information about the properties of the scanner.
 * Returns status->pieusb_status:
 * - PIEUSB_STATUS_GOOD if the INQUIRY command succeeded
 * - PIEUSB_STATUS_DEVICE_BUSY if device is busy after repeat retries
 * - PIEUSB_STATUS_CHECK_CONDITION with accompanying sense codes if the command
 *   returned a CHECK CONDITION
 * - other SANE status code if INQUIRY failed or if it returned CHECK CONDITION
 *   and REQUEST SENSE failed
 *
 * @param device_number Device number
 * @param data Input or output data buffer
 * @param size Size of the data buffer
 * @return Pieusb_Command_Status
 * @see Pieusb_Scanner_Properties
 */
void
cmdDoInquiry(SANE_Int device_number, struct Pieusb_Scanner_Properties* inq, SANE_Byte size, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define INQUIRY_SIZE 256
    SANE_Byte data[INQUIRY_SIZE];
    int k;

    DBG (DBG_info_scan, "cmdGetScannerProperties()\n");

    setCommand(command, SCSI_INQUIRY, size);
    memset(data, '\0', INQUIRY_SIZE); /* size may be less than INQUIRY_SIZE, so prevent returning noise */

    commandScannerRepeat(device_number, command, data, size, status);
    if (status->pieusb_status != PIEUSB_STATUS_GOOD) {
        return;
    }

    /* Decode data recieved */
    inq->deviceType = _get_byte(data, 0);
    inq->additionalLength = _get_byte(data, 4);
    _copy_bytes((SANE_Byte*)(inq->vendor), data+8, 8); /* Note: not 0-terminated */
    _copy_bytes((SANE_Byte*)(inq->product), data+16, 16); /* Note: not 0-terminated */
    _copy_bytes((SANE_Byte*)(inq->productRevision), data+32, 4); /* Note: not 0-terminated */
    /* 1st Vendor-specific block, 20 bytes, see pie_get_inquiry_values(), partially: */
    inq->maxResolutionX = _get_short(data, 36);
    inq->maxResolutionY = _get_short(data, 38);
    inq->maxScanWidth = _get_short(data, 40);
    inq->maxScanHeight = _get_short(data, 42);
    inq->filters = _get_byte(data, 44);
    inq->colorDepths = _get_byte(data, 45);
    inq->colorFormat = _get_byte(data, 46);
    inq->imageFormat = _get_byte(data, 48);
    inq->scanCapability = _get_byte(data, 49);
    inq->optionalDevices = _get_byte(data, 50);
    inq->enhancements = _get_byte(data, 51);
    inq->gammaBits = _get_byte(data, 52);
    inq->lastFilter = _get_byte(data, 53);
    inq->previewScanResolution = _get_short(data, 54);
    /* 2nd vendor specific block (36 bytes at offset 96) */
    _copy_bytes((SANE_Byte*)(inq->firmwareVersion), data+96, 4); inq->firmwareVersion[4]=0x00;
    inq->halftones = _get_byte(data, 100);
    inq->minumumHighlight = _get_byte(data, 101);
    inq->maximumShadow = _get_byte(data, 102);
    inq->calibrationEquation = _get_byte(data, 103);
    inq->maximumExposure = _get_short(data ,104);
    inq->minimumExposure = _get_short(data ,106);
    inq->x0 = _get_short(data, 108);
    inq->y0 = _get_short(data, 110);
    inq->x1 = _get_short(data, 112);
    inq->y1 = _get_short(data, 114);
    inq->model = _get_short(data, 116);
    _copy_bytes((SANE_Byte*)(inq->production), data+120, 24);
    _copy_bytes((SANE_Byte*)(inq->signature), data+144, 40);
    /* remove newline in signature */
    for (k=0; k<40; k++) if (inq->signature[k]=='\n') inq->signature[k]=' ';
#undef INQUIRY_SIZE
}

/**
 * Set scan mode parameters, such as resolution, colors to scan, color depth,
 * color format, and a couple of scan quality settings (sharpen, skip
 * calibration, fast infrared). It performs the SCSI-command MODE SELECT,
 * code 0x15.
 *
 * @param device_number Device number
 * @param mode Mode parameters
 * @return Pieusb_Command_Status
 * @see Pieusb_Mode
 */
void
cmdSetMode(SANE_Int device_number, struct Pieusb_Mode* mode, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define MODE_SIZE 16
    SANE_Int size = MODE_SIZE;
    SANE_Byte data[MODE_SIZE];
    SANE_Byte quality;

    DBG (DBG_info_scan, "cmdSetMode()\n");

    setCommand(command, SCSI_MODE_SELECT, size);

    DBG (DBG_info_scan, "cmdSetMode() set:\n");
    DBG (DBG_info_scan, " resolution = %d\n", mode->resolution);
    DBG (DBG_info_scan, " passes = %02x\n", mode->passes);
    DBG (DBG_info_scan, " depth = %02x\n", mode->colorDepth);
    DBG (DBG_info_scan, " color format = %02x\n", mode->colorFormat);
    DBG (DBG_info_scan, " sharpen = %d\n", mode->sharpen);
    DBG (DBG_info_scan, " skip calibration = %d\n", mode->skipShadingAnalysis);
    DBG (DBG_info_scan, " fast infrared = %d\n", mode->fastInfrared);
    DBG (DBG_info_scan, " halftone pattern = %d\n", mode->halftonePattern);
    DBG (DBG_info_scan, " line threshold = %d\n", mode->lineThreshold);

    /* Code data */
    memset(data, '\0', size);
    _set_byte(size-1, data, 1);
    _set_short(mode->resolution, data, 2);
    _set_byte(mode->passes, data, 4);
    _set_byte(mode->colorDepth, data, 5);
    _set_byte(mode->colorFormat, data, 6);
    _set_byte(mode->byteOrder, data, 8);
    quality = 0x00;
    if (mode->sharpen) quality |= 0x02;
    if (mode->skipShadingAnalysis) quality |= 0x08;
    if (mode->fastInfrared) quality |= 0x80;
    _set_byte(quality, data, 9);
    _set_byte(mode->halftonePattern, data, 12);
    _set_byte(mode->lineThreshold, data, 13);

    commandScannerRepeat(device_number, command, data, size, status);
#undef MODE_SIZE
}

/* SCSI COPY, code 0x18 */
/**
 * Get the currently used CCD-mask, which defines which pixels have been used in
 * the scan, and which allows to relate scan data to shading data. A mask is a
 * 5340 byte array which consists only contains the values 0x00 and 0x70. A
 * value of 0x00 indicates the pixel is used, a value of 0x70 that it is not.\n
 * The number of 0x00 bytes equals the number of pixels on a line.\n
 * The mask begins with a number of 0x70 bytes equal to the scan frame x0-value
 * divided by 2.\n
 * The SCSI-command COPY (code 0x18) is used for function.
 *
 * @param device_number Device number
 * @param mask
 * @return Pieusb_Command_Status
 */
void
cmdGetCCDMask(SANE_Int device_number, SANE_Byte* mask, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
    SANE_Int size = 5340;

    DBG (DBG_info_scan, "cmdGetCCDMask()\n");

    setCommand(command, SCSI_COPY, size);

    memset(mask, '\0', size);
    commandScannerRepeat(device_number, command, mask, size, status);

/*
    int k;
    fprintf(stderr,"CCD mask:\n");
    for (k = 0; k < size; k++) {
        fprintf(stderr,"%02x ",mask[k]);
        if ((k+1) % 50 == 0) fprintf(stderr,"\n");
    }
*/

}

/**
 * Get scan mode parameters, such as resolution, colors to scan, color depth,
 * color format, and a couple of scan quality settings (sharpen, skip
 * calibration, fast infrared). It performs the SCSI-command MODE SELECT,
 * code 0x1A.
 *
 * @param device_number Device number
 * @param mode Mode parameters
 * @return Pieusb_Command_Status
 * @see Pieusb_Mode
 */
void
cmdGetMode(SANE_Int device_number, struct Pieusb_Mode* mode, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define MODE_SIZE 16
    SANE_Int size = MODE_SIZE;
    SANE_Byte data[MODE_SIZE];
    SANE_Byte quality;

    DBG (DBG_info_scan, "cmdGetMode()\n");

    setCommand(command, SCSI_MODE_SENSE, size);
    memset(data, '\0', size);

    commandScannerRepeat(device_number, command, data, size, status);
    if (status->pieusb_status != PIEUSB_STATUS_GOOD) {
        return;
    }

    /* Decode data recieved */
    mode->resolution = _get_short(data, 2);
    mode->passes = _get_byte(data, 4);
    mode->colorDepth = _get_byte(data, 5);
    mode->colorFormat = _get_byte(data, 6);
    mode->byteOrder = _get_byte(data, 8);
    quality = _get_byte(data, 9);
    mode->sharpen = (quality |= 0x02) ? SANE_TRUE : SANE_FALSE;
    mode->skipShadingAnalysis = (quality |= 0x08) ? SANE_TRUE : SANE_FALSE;
    mode->fastInfrared = (quality |= 0x80) ? SANE_TRUE : SANE_FALSE;
    mode->halftonePattern = _get_byte(data, 12);
    mode->lineThreshold = _get_byte(data, 13);

    DBG (DBG_info_scan, "cmdGetMode():\n");
    DBG (DBG_info_scan, " resolution = %d\n", mode->resolution);
    DBG (DBG_info_scan, " passes = %02x\n", mode->passes);
    DBG (DBG_info_scan, " depth = %02x\n", mode->colorDepth);
    DBG (DBG_info_scan, " color format = %02x\n", mode->colorFormat);
    DBG (DBG_info_scan, " sharpen = %d\n", mode->sharpen);
    DBG (DBG_info_scan, " skip calibration = %d\n", mode->skipShadingAnalysis);
    DBG (DBG_info_scan, " fast infrared = %d\n", mode->fastInfrared);
    DBG (DBG_info_scan, " halftone pattern = %d\n", mode->halftonePattern);
    DBG (DBG_info_scan, " line threshold = %d\n", mode->lineThreshold);
#undef MODE_SIZE
}

/**
 * Start a scan (SCSI SCAN command, code 0x1B, size byte = 0x01).\n
 * There are four phases in a scan process. During each phase a limited number of
 * commands is available. The phases are:\n
 * 1. Calibration phase: make previously collected shading correction data available\n
 * 2. Line-by-line scan & read phase\n
 * 3. Output CCD-mask phase\n
 * 4. Scan and output scan data phase\n
 *
 * The calibration phase is skipped if Pieusb_Mode.skipCalibration is set. If
 * the scanner determines a calibration is necessary, a CHECK CONDIDITION response
 * is returned. Available command during this phase:\n
 * 1. cmdIsUnitReady()\n
 * 2. cmdGetScannedLines(): read shading correction lines\n
 * 3. cmdStopScan: abort scanning process\n
 * 4. cmdGetOptimizedSettings() : the settings are generated during the initialisation of this phase, so they are current\n
 * 5. cmdSetSettings(): settings take effect in the next scan phase\n\n
 * The line-by-line phase is only entered if Pieusb_Mode.div_10[0] bit 5 is
 * set. It is not implemented.\n\n
 * In the CCD-mask output phase the CCD-mask is read. Available command during this phase:\n
 * 1. cmdIsUnitReady()\n
 * 2. cmdGetCCDMask()\n
 * 3. cmdStopScan: abort scanning process\n\n
 * In the 'scan and output scan data' phase, the slide is scanned while data is
 * read in the mean time. Available command during this phase:\n
 * 1. cmdIsUnitReady()\n
 * 2. cmdGetScannedLines()\n
 * 2. cmdGetScanParameters()\n
 * 4. cmdStopScan: abort scanning process\n
 *
 * @param device_number Device number
 * @return Pieusb_Command_Status
 */
void
cmdStartScan(SANE_Int device_number, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];

    DBG (DBG_info_scan, "cmdStartScan()\n");

    setCommand(command, SCSI_SCAN, 1);

    commandScannerRepeat(device_number, command, NULL, 0, status);
}

/**
 * Stop a scan started with cmdStartScan(). It issues a SCSI SCAN command,
 * code 0x1B, with size byte = 0x00.
 *
 * @param device_number Device number
 * @return Pieusb_Command_Status
 */
void
cmdStopScan(SANE_Int device_number, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];

    DBG (DBG_info_scan, "cmdStopScan()\n");

    setCommand(command, SCSI_SCAN, 0);

    commandScannerRepeat(device_number, command, NULL, 0, status);
}

/**
 * Set scan head to a specific position, depending on the value for mode:\n
 * mode = 1: Returns the scan head to the resting position, after a short move
 * forward. If this command is left out between two scans, the second scan is
 * up-down-mirrored, and scanning starts where the proevious scan stopped.\n
 * mode = 2: Resets the scan head an then moves it forward depending on 'size',
 * but it is a bit unpredictable to what position. The scanner may attempt to
 * move the head past its physical end position. The mode is not implemented.\n
 * mode = 3: This command positions the scan head to the start of the slide.\n
 * mode = 4 or 5: The command forwards (4) or retreats (5) the scan head the
 * given amount of steps (in size).\n
 * The SCSI code is 0xD2, there is no related command name.
 *
 * @param device_number Device number
 * @param mode
 * @param size
 * @return Pieusb_Command_Status
 */
void
cmdSetScanHead(SANE_Int device_number, SANE_Int mode, SANE_Int steps, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define SCAN_HEAD_SIZE 4
    SANE_Int size = SCAN_HEAD_SIZE;
    SANE_Byte data[SCAN_HEAD_SIZE];

    DBG (DBG_info_scan, "cmdSetScanHead()\n");

    setCommand(command, SCSI_SET_SCAN_HEAD, size);

    /* Code data */
    memset(data, '\0', size);
    switch (mode) {
        case 1:
            data[0] = 2;
            break;
        case 2:
            DBG (DBG_error, "cmdSetScanHead() mode 2 unreliable, possibly dangerous\n");
            status->pieusb_status = PIEUSB_STATUS_INVAL;
            return;
        case 3:
            data[0] = 8;
            break;
        case 4:
            data[0] = 0; /* forward */
            data[2] = (steps>>8) & 0xFF;
            data[3] = steps & 0xFF;
            break;
        case 5:
            data[0] = 1; /* backward */
            data[2] = (steps>>8) & 0xFF;
            data[3] = steps & 0xFF;
            break;
    }

    commandScannerRepeat(device_number, command, data, size, status);
#undef SCAN_HEAD_SIZE
}

/**
 * Get internal scanner settings which have resulted from an auto-calibration
 * procedure. This procedure only runs when calibrating (Scan phase 1), so the
 * data returned are relatively static.\n
 * The SCSI code is 0xD7, there is no related command name.
 *
 * @param device_number Device number
 * @param settings Settings for gain and offset for the four colors RGBI
 * @return Pieusb_Command_Status
 * @see Pieusb_Settings
 */
void
cmdGetGainOffset(SANE_Int device_number, struct Pieusb_Settings* settings, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define GAIN_OFFSET_SIZE 103
    SANE_Int size = GAIN_OFFSET_SIZE;
    SANE_Byte data[GAIN_OFFSET_SIZE];
    int k;
    SANE_Byte val[3];

    DBG (DBG_info_scan, "cmdGetOptimizedSettings()\n");

    setCommand(command, SCSI_READ_GAIN_OFFSET, size);

    memset(data, '\0', size);
    commandScannerRepeat(device_number, command, data, size, status);
    if (status->pieusb_status != PIEUSB_STATUS_GOOD) {
        return;
    }

    /* Decode data received */
    _get_shorts(settings->saturationLevel, data+54, 3);
    _get_shorts(settings->exposureTime, data+60, 3);
    _copy_bytes(val, data+66, 3);
    for (k=0; k<3; k++) settings->offset[k] = val[k];
    _copy_bytes(val, data+72, 3);
    for (k=0; k<3; k++) settings->gain[k] = val[k];
    settings->light = _get_byte(data, 75);
    settings->exposureTime[3] = _get_short(data, 98);
    settings->offset[3] = _get_byte(data, 100);
    settings->gain[3] = _get_byte(data, 102);

    DBG (DBG_info_scan, "cmdGetGainOffset() set:\n");
    DBG (DBG_info_scan, " saturationlevels = %d-%d-%d\n", settings->saturationLevel[0], settings->saturationLevel[1], settings->saturationLevel[2]);
    DBG (DBG_info_scan, " minimumExposureTime = %d\n", settings->minimumExposureTime);
    DBG (DBG_info_scan, " ---\n");
    DBG (DBG_info_scan, " exposure times = %d-%d-%d-%d\n", settings->exposureTime[0], settings->exposureTime[1], settings->exposureTime[2], settings->exposureTime[3]);
    DBG (DBG_info_scan, " gain = %d-%d-%d-%d\n", settings->gain[0], settings->gain[1], settings->gain[2], settings->gain[3]);
    DBG (DBG_info_scan, " offset = %d-%d-%d-%d\n", settings->offset[0], settings->offset[1], settings->offset[2], settings->offset[3]);
    DBG (DBG_info_scan, " light = %02x\n", settings->light);
    DBG (DBG_info_scan, " double times = %02x\n", settings->doubleTimes);
    DBG (DBG_info_scan, " extra entries = %02x\n", settings->extraEntries);
#undef GAIN_OFFSET_SIZE
}


/**
 * Set internal scanner settings such as gain and offset.\n
 * There are two effective moments for this command:\n
 * 1. For a scan without calibration phase: before the cmdStartScan() command;
 * 2. For a sccan with calibration phase: before (or during) reading the shading reference data.
 * The SCSI code is 0xDC, there is no related command name.
 *
 * @param device_number Device number
 * @param settings Settings for gain and offset for the four colors RGBI
 * @return Pieusb_Command_Status
 * @see Pieusb_Settings
 */
void
cmdSetGainOffset(SANE_Int device_number, struct Pieusb_Settings* settings, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define GAIN_OFFSET_SIZE 23
    SANE_Int size = GAIN_OFFSET_SIZE;
    SANE_Byte data[GAIN_OFFSET_SIZE];
    int k;
    SANE_Byte val[3];

    DBG (DBG_info_scan, "cmdSetGainOffset()\n");

    setCommand(command, SCSI_WRITE_GAIN_OFFSET, size);

    DBG (DBG_info_scan, "cmdSetGainOffset() set:\n");
    DBG (DBG_info_scan, " exposure times = %d-%d-%d-%d\n", settings->exposureTime[0], settings->exposureTime[1], settings->exposureTime[2], settings->exposureTime[3]);
    DBG (DBG_info_scan, " gain = %d-%d-%d-%d\n", settings->gain[0], settings->gain[1], settings->gain[2], settings->gain[3]);
    DBG (DBG_info_scan, " offset = %d-%d-%d-%d\n", settings->offset[0], settings->offset[1], settings->offset[2], settings->offset[3]);
    DBG (DBG_info_scan, " light = %02x\n", settings->light);
    DBG (DBG_info_scan, " double times = %02x\n", settings->doubleTimes);
    DBG (DBG_info_scan, " extra entries = %02x\n", settings->extraEntries);

    /* Code data */
    memset(data, '\0', size);
    _set_shorts(settings->exposureTime, data, 3);
    for (k=0; k<3; k++) val[k] = settings->offset[k];
    _copy_bytes(data+6, val, 3);
    for (k=0; k<3; k++) val[k] = settings->gain[k];
    _copy_bytes(data+12, val, 3);
    _set_byte(settings->light, data, 15);
    _set_byte(settings->extraEntries, data, 16);
    _set_byte(settings->doubleTimes, data, 17);
    _set_short(settings->exposureTime[3], data, 18);
    _set_byte(settings->offset[3], data, 20);
    _set_byte(settings->gain[3], data, 22);

    commandScannerRepeat(device_number, command, data, size, status);
#undef GAIN_OFFSET_SIZE
}

/**
 * Get scanner state information: button pushed,
 * warming up, scanning.
 * The SCSI code is 0xDD, there is no related command name.
 *
 * @param device_number Device number
 * @param state State information
 * @return Pieusb_Command_Status
 */
void
cmdGetState(SANE_Int device_number, struct Pieusb_Scanner_State* state, struct Pieusb_Command_Status *status)
{
    SANE_Byte command[SCSI_COMMAND_LEN];
#define GET_STATE_SIZE 11
    SANE_Byte data[GET_STATE_SIZE];
    SANE_Int size = GET_STATE_SIZE;

    /* Execute READ STATUS command */
    DBG (DBG_info_scan, "cmdGetState()\n");

    setCommand(command, SCSI_READ_STATE, size);

    memset(data, '\0', size);
    commandScannerRepeat(device_number, command, data, size, status);
    if (status->pieusb_status != PIEUSB_STATUS_GOOD) {
        return;
    }

    /* Decode data recieved */
    state->buttonPushed = _get_byte(data, 0);
    state->warmingUp = _get_byte(data, 5);
    state->scanning = _get_byte(data, 6);
#undef GET_STATE_SIZE
}

/**
 * Prepare SCSI_COMMAND_LEN-byte command array with command code and size value
 *
 * @param command
 * @param code
 * @param size
 */
void
setCommand(SANE_Byte* command, SANE_Byte code, SANE_Word size)
{
    memset(command, '\0', SCSI_COMMAND_LEN);
    command[0] = code;
    command[3] = (size>>8) & 0xFF;
    command[4] = size & 0xFF;
}
