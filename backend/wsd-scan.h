/* sane - Scanner Access Now Easy.

   wsd-scan.h

   Copyright (C) 2019 Klaus Kämpf

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

#ifndef WSD_SCAN_H
#define	WSD_SCAN_H

#include "../include/sane/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define BACKEND_NAME wsd_scan

#include "../include/sane/sane.h"
#include "../include/sane/sanei_debug.h"
#include <openwsd/openwsd.h>

/* Debug error levels */
#define DBG_error        1      /* errors */
#define DBG_warning      3      /* warnings */
#define DBG_info         5      /* information */
#define DBG_info_sane    7      /* information sane interface level */
#define DBG_inquiry      8      /* inquiry data */
#define DBG_info_proc    9      /* information wsd backend functions */
#define DBG_info_scan   11      /* information scanner commands */
#define DBG_info_usb    13      /* information usb level functions */


/* Options supported by the scanner
 * see Scan Service Definition Version 1.0 (Microsoft 2012)
 */

enum Wsd_Option
{
    OPT_NUM_OPTS = 0,
    /* ------------------------------------------- */
    OPT_SCAN_SOURCE,            /* platen, adf, film */
    /* ------------------------------------------- */
    OPT_FORMAT_GROUP,
    OPT_RESOLUTION,
    OPT_COLOR,
    /* ------------------------------------------- */
    OPT_GEOMETRY_GROUP,
    OPT_WIDTH,
    OPT_HEIGHT,
#if 0
    OPT_FORMAT,                 /* jpeg, tiff, pdf, ... */
    OPT_COMPRESSION_QUALITY_FACTOR, /* 0..100 */
    OPT_CONTENT_TYPE,           /* Auto, Text, Photo, Halftone, Mixed */
    OPT_SIZE_AUTO_DETECT,       /* true, false */
    OPT_AUTO_EXPOSURE,          /* unsupported, true, false */
    OPT_BRIGHTNESS,             /* unsupported, ... */
    OPT_CONTRAST,               /* unsupported, ... */
    OPT_SCALING_WIDTH,
    OPT_SCALING_HEIGHT,
    OPT_ROTATION,               /* 0, 90, 180, 270 */ 
    OPT_ADF_DUPLEX,
    OPT_FILM_SCAN_MODE,
    /* ------------------------------------------- */
    OPT_ENHANCEMENT_GROUP,
    /* ------------------------------------------- */
    OPT_ADVANCED_GROUP,
#endif
    /* must come last: */
    NUM_OPTIONS
};

/*-----------------------------------------------------------------*/

struct _WsdScanner {
    SANE_Device sane_device;
    struct _WsdScanner *next;
    char url[PATH_MAX];
    WsdClient *client;
    SANE_Int scanning; /* true if busy scanning */
    SANE_Int cancel_request; /* if true, scanner should terminate a scan */
    SANE_Parameters scan_parameters;
    char *job_id;
    char *job_token;
    /* SANE option descriptions and settings for this scanner instance */
    SANE_Option_Descriptor opt[NUM_OPTIONS];
    Option_Value val[NUM_OPTIONS];
};

typedef struct _WsdScanner WsdScanner;

#endif	/* WSD_SCAN_H */
