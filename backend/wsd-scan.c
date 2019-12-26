/* sane - Scanner Access Now Easy.

   wsd-scan.c

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

#define DEBUG_NOT_STATIC
/* --------------------------------------------------------------------------
 *
 * INCLUDES
 *
 * --------------------------------------------------------------------------*/

#include "../include/sane/config.h"
/* Standard includes for various utiliy functions */
#include <ctype.h> /* for isblank */
#include <stdio.h> /* for FILE */
#include <string.h> /* for strlen */
#include <stdlib.h> /* for NULL */
#include <stdint.h>
#include <math.h>

/* Configuration defines */
#include "../include/sane/config.h"

/* SANE includes */
#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"

/* Backend includes */
#include "../include/sane/sanei_backend.h"
#include "wsd-scan.h"

/* --------------------------------------------------------------------------
 *
 * DEFINES
 *
 * --------------------------------------------------------------------------*/

/* Build number of this backend */
#define BUILD 1

/* Configuration filename */
#define WSDSCAN_CONFIG_FILE "wsd-scan.conf"

/* Debug error levels */
#define DBG_error        1      /* errors */
#define DBG_warning      3      /* warnings */
#define DBG_info         5      /* information */
#define DBG_info_sane    7      /* information sane interface level */
#define DBG_inquiry      8      /* inquiry data */
#define DBG_info_proc    9      /* information wsd backend functions */
#define DBG_info_scan   11      /* information scanner commands */
#define DBG_info_usb    13      /* information usb level functions */

/* device flags */

#define FLAG_SLIDE_TRANSPORT 0x01

static WsdScanner *devlist = NULL;

/* --------------------------------------------------------------------------
 *
 * SANE INTERFACE
 *
 * --------------------------------------------------------------------------*/

/**
 * Initializes the debugging system, the USB system, the version code and
 * 'attaches' available scanners, i.e. creates device definitions for all
 * scanner devices found.
 *
 * @param version_code
 * @param authorize
 * @return SANE_STATUS_GOOD
 */
SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback __sane_unused__ authorize)
{
    FILE *fp;
    char config_line[PATH_MAX];
    /* Initialize debug logging */
    DBG_INIT ();
    debug_add_handler(wsd_debug_message_handler, DEBUG_LEVEL_ALWAYS, NULL);

    DBG (DBG_info_sane, "sane_init() build %d\n", BUILD);

    /* Set version code to current major, minor and build number */
    /* TODO: use V_MINOR instead or SANE_CURRENT_MINOR? If so, why?  */
    if (version_code)
        *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);

    /* Add entries from config file */
    fp = sanei_config_open (WSDSCAN_CONFIG_FILE);
    if (!fp) {
        DBG (DBG_info_sane, "sane_init() did not find a config file, using default list of supported devices\n");
    } else {
        DBG (DBG_info_sane, "sane_init() found config file: %s\n", WSDSCAN_CONFIG_FILE);
        WsdScanner **pivot = &devlist; 
        while (sanei_config_read (config_line, sizeof (config_line), fp)) {
            char *s, *url;
            /* Ignore line comments and empty lines */
            if (config_line[0] == '#') continue;
            if (strlen (config_line) == 0) continue;
            /* Ignore lines which do not begin with 'usb ' */
            if (strncmp (config_line, "url ", 4) != 0) continue;
            s = config_line + 4;
            /* Parse vendor-id, product-id and model number and add to list */
            DBG (DBG_info_sane, "sane_init() config file parsing '%s'\n", config_line);
            while (*s && isblank(*s)) s++;
            url = s;
            while (*s && !isblank(*s)) s++;
            *s = '\0';
            if (url < s) {
                WsdScanner *scanner = calloc (1, sizeof(WsdScanner));
                if (*pivot == NULL) {
                    *pivot = scanner;
                } else {
                    (*pivot)->next = scanner;
                }
                strncpy(scanner->url, url, PATH_MAX);
                pivot = &scanner;
                DBG (DBG_info_sane, "sane_init() wsd-scan device '%s'\n", url);
                
            } else {
                DBG (DBG_info_sane, "sane_init() config file parsing %s: error\n", config_line);
            }
	}
        fclose (fp);
    }

    return SANE_STATUS_GOOD;
}

/**
 * Backend exit.
 * Clean up allocated memory.
 */
void
sane_exit (void)
{
    DBG (DBG_info_sane, "sane_exit()\n");

    while (devlist) {
        WsdScanner *scanner;
        scanner = devlist->next;
        free (devlist);
        devlist = scanner;
    }
    return;
}

/**
 * Create a SANE device list from the device list generated by sane_init().
 *
 * @param device_list List of SANE_Device elements
 * @param local_only If true, disregard network scanners. Not applicable for USB scanners.
 * @return SANE_STATUS_GOOD, or SANE_STATUS_NO_MEM if the list cannot be allocated
 */
SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool __sane_unused__ local_only)
{

    DBG (DBG_info_sane, "sane_get_devices()\n");

    WsdScanner *scanner;
    WsdRequest *request;
    int i;
    request_opt_t *options = wsd_options_create();
    wsd_set_options_flags(options, FLAG_DUMP_REQUEST);

    /* Create SANE_DEVICE list from device list created in sane_init() */
    i = 0;
    for (scanner = devlist; scanner; scanner = scanner->next) {
        i++;
    }

    DBG (DBG_info_sane, "sane_get_devices: found %d scanner\n", i);
    device_list = calloc ((i + 1), sizeof (WsdScanner *));
    if (!device_list) {
        return SANE_STATUS_NO_MEM;
    }
    i = 0;
    for (scanner = devlist; scanner; scanner = scanner->next) {
        DBG (DBG_info_sane, "sane_get_devices: create scanner '%s'\n", scanner->url);
        scanner->client = wsd_client_create_from_url(scanner->url);
        if (!scanner->client) {
          DBG (DBG_error, "Can't access %s\n", scanner->url);
          continue;
        }
        request = wsd_action_get_scanner_description(scanner->client, options);
        if (!request) {
            DBG (DBG_error, "Request creation failed\n");
            continue;
        }
        else {
            WS_LASTERR_Code error;
            error = wsd_get_last_error(request);
            if (error != WS_LASTERR_OK) {
                char *error_str = wsd_transport_get_last_error_string(error);
                DBG (DBG_error, "No reponse: error %d:%s\n", error, error_str);
            } else {
                WsXmlDocH doc = wsd_response_doc(request);
                WsXmlNodeH node = wsd_response_node(request);
                xml_parser_element_dump(stderr, doc, node);
            }
        }
        wsd_request_destroy(request);
        wsd_client_destroy(scanner->client);
        device_list[i++] = &(scanner->sane_device);
    }
    device_list[i] = NULL;
    wsd_options_destroy(options);

    return SANE_STATUS_GOOD;
}

/**
 * Open the scanner with the given devicename and return a handle to it, which
 * is a pointer to a WsdScan_Scanner struct. The handle will be an input to
 * a couple of other functions of the SANE interface.
 *
 * @param devicename Name of the device, corresponds to SANE_Device.name
 * @param handle handle to scanner (pointer to a WsdScanner struct)
 * @return SANE_STATUS_GOOD if the device has been opened
 */
SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{

    DBG (DBG_info_sane, "sane_open(%s)\n", devicename);
    handle = handle;
#if 0
    SANE_Status status;
    WsdScanner *scanner, *s;
    /* If no device found, return error */
    if (!dev) {
        return SANE_STATUS_INVAL;
    }

    /* Now create a scanner structure to return */

    /* Check if we are not opening the same scanner again. */
    for (s = first_handle; s; s = s->next) {
        if (s->device->sane.name == devicename) {
            *handle = s;
            return SANE_STATUS_GOOD;
        }
    }

    /* Create a new scanner instance */
    scanner = malloc (sizeof (*scanner));
    if (!scanner) {
        return SANE_STATUS_NO_MEM;
    }
    memset (scanner, 0, sizeof (*scanner));
    scanner->device = dev;
    scanner->cancel_request = 0;

    /* First time settings */
    /* ? */
    /* Insert newly opened handle into list of open handles: */
    scanner->next = first_handle;
    first_handle = scanner;

    *handle = scanner;
#endif
    return SANE_STATUS_GOOD;
}

/**
 * Close the scanner and remove the scanner from the list of active scanners.
 *
 * @param handle Scanner handle
 */
void
sane_close (SANE_Handle handle)
{
    DBG (DBG_info_sane, "sane_close()\n");
    handle = handle;
#if 0
    /* Remove handle from list */
    if (prev) {
        prev->next = scanner->next;
    } else {
        first_handle = scanner->next;
    }
    free (scanner);
#endif
}


/**
 * Get option descriptor. Return the option descriptor with the given index
 *
 * @param handle Scanner handle
 * @param option Index of option descriptor to return
 * @return The option descriptor
 */
const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
//    WsdScanner *scanner = handle;

    DBG (DBG_info_proc, "sane_get_option_descriptor() option=%d\n", option);

    if ((unsigned) option >= NUM_OPTIONS)
    {
      return NULL;
    }
#if 0
    return scanner->opt + option;
#else
    handle = handle;
    return NULL;
#endif
}


/**
 * Set or inquire the current value of option number 'option' of the device
 * represented by the given handle.
 *
 * @param handle Scanner handle
 * @param option Index of option to set or get
 * @param action Determines if the option value is read or set
 * @param val Pointer to value to set or get
 * @param info About set result. May be NULL.
 * @return SANE_STATUS_GOOD, or SANE_STATUS_INVAL if a parameter cannot be set
 */
SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action,
		     void *val, SANE_Int * info)
{
    WsdScanner *scanner = handle;
//    SANE_Status status;
  action = action;
  val = val;
  info = info;
    DBG(DBG_info_sane,"sane_control_option()\n");
    if (info) {
        *info = 0;
    }

    /* Don't set or get options while the scanner is busy */
    if (scanner->scanning) {
        DBG(DBG_error,"Device busy scanning, no option returned\n");
        return SANE_STATUS_DEVICE_BUSY;
    }

    /* Check if option index is between bounds */
    if ((unsigned) option >= NUM_OPTIONS) {
        DBG(DBG_error,"Index too large, no option returned\n");
        return SANE_STATUS_INVAL;
    }
#if 0
    /* Check if option is switched on */
    cap = scanner->opt[option].cap;
    if (!SANE_OPTION_IS_ACTIVE (cap))
    {
        DBG(DBG_error,"Option inactive (%s)\n", scanner->opt[option].name);
        return SANE_STATUS_INVAL;
    }

    /* Get name of option */
    name = scanner->opt[option].name;
    if (!name)
    {
      name = "(no name)";
    }

    /* */
    switch (action) {
        case SANE_ACTION_GET_VALUE:

            DBG (DBG_info_sane, "get %s [#%d]\n", name, option);

            switch (option) {

                /* word options: */
                case OPT_NUM_OPTS:
                case OPT_BIT_DEPTH:
                case OPT_RESOLUTION:
                case OPT_TL_X:
                case OPT_TL_Y:
                case OPT_BR_X:
                case OPT_BR_Y:
                case OPT_THRESHOLD:
                case OPT_SHARPEN:
                case OPT_SHADING_ANALYSIS:
                case OPT_FAST_INFRARED:
	        case OPT_ADVANCE_SLIDE:
                case OPT_CORRECT_SHADING:
                case OPT_CORRECT_INFRARED:
                case OPT_CLEAN_IMAGE:
                case OPT_SMOOTH_IMAGE:
                case OPT_TRANSFORM_TO_SRGB:
                case OPT_INVERT_IMAGE:
                case OPT_PREVIEW:
                case OPT_SAVE_SHADINGDATA:
                case OPT_SAVE_CCDMASK:
	        case OPT_LIGHT:
	        case OPT_DOUBLE_TIMES:
                case OPT_SET_EXPOSURE_R:
                case OPT_SET_EXPOSURE_G:
                case OPT_SET_EXPOSURE_B:
                case OPT_SET_EXPOSURE_I:
                case OPT_SET_GAIN_R:
                case OPT_SET_GAIN_G:
                case OPT_SET_GAIN_B:
                case OPT_SET_GAIN_I:
                case OPT_SET_OFFSET_R:
                case OPT_SET_OFFSET_G:
                case OPT_SET_OFFSET_B:
                case OPT_SET_OFFSET_I:
                    *(SANE_Word *) val = scanner->val[option].w;
                    DBG (DBG_info_sane, "get %s [#%d] val=%d\n", name, option,scanner->val[option].w);
                    return SANE_STATUS_GOOD;

                /* word-array options: => for exposure gain offset? */
                case OPT_CROP_IMAGE:
                    memcpy (val, scanner->val[option].wa, scanner->opt[option].size);
                    return SANE_STATUS_GOOD;

                /* string options */
                case OPT_MODE:
                case OPT_CALIBRATION_MODE:
                case OPT_GAIN_ADJUST:
                case OPT_HALFTONE_PATTERN:
                    strcpy (val, scanner->val[option].s);
                    DBG (DBG_info_sane, "get %s [#%d] val=%s\n", name, option,scanner->val[option].s);
                    return SANE_STATUS_GOOD;
            }
            break;

        case SANE_ACTION_SET_VALUE:

            switch (scanner->opt[option].type) {
                case SANE_TYPE_INT:
                    DBG (DBG_info_sane, "set %s [#%d] to %d, size=%d\n", name, option, *(SANE_Word *) val, scanner->opt[option].size);
                    break;
                case SANE_TYPE_FIXED:
                    DBG (DBG_info_sane, "set %s [#%d] to %f\n", name, option, SANE_UNFIX (*(SANE_Word *) val));
                    break;
                case SANE_TYPE_STRING:
                    DBG (DBG_info_sane, "set %s [#%d] to %s\n", name, option, (char *) val);
                    break;
                case SANE_TYPE_BOOL:
                    DBG (DBG_info_sane, "set %s [#%d] to %d\n", name, option, *(SANE_Word *) val);
                    break;
                default:
                    DBG (DBG_info_sane, "set %s [#%d]\n", name, option);
            }
            /* Check if option can be set */
            if (!SANE_OPTION_IS_SETTABLE (cap)) {
              return SANE_STATUS_INVAL;
            }
            /* Check if new value within bounds */
            status = sanei_constrain_value (scanner->opt + option, val, info);
            if (status != SANE_STATUS_GOOD) {
              return status;
            }
            /* Set option and handle info return */
            switch (option)
            {
                /* (mostly) side-effect-free word options: */
                case OPT_BIT_DEPTH:
                case OPT_RESOLUTION:
                case OPT_TL_X:
                case OPT_TL_Y:
                case OPT_BR_X:
                case OPT_BR_Y:
                case OPT_SHARPEN:
                case OPT_SHADING_ANALYSIS:
                case OPT_FAST_INFRARED:
                    if (info) {
                        *info |= SANE_INFO_RELOAD_PARAMS;
                    }
                  /* fall through */
                case OPT_NUM_OPTS:
                case OPT_PREVIEW:
	        case OPT_ADVANCE_SLIDE:
                case OPT_CORRECT_SHADING:
                case OPT_CORRECT_INFRARED:
                case OPT_CLEAN_IMAGE:
                case OPT_SMOOTH_IMAGE:
                case OPT_TRANSFORM_TO_SRGB:
                case OPT_INVERT_IMAGE:
                case OPT_SAVE_SHADINGDATA:
                case OPT_SAVE_CCDMASK:
                case OPT_THRESHOLD:
	        case OPT_LIGHT:
	        case OPT_DOUBLE_TIMES:
                case OPT_SET_GAIN_R:
                case OPT_SET_GAIN_G:
                case OPT_SET_GAIN_B:
                case OPT_SET_GAIN_I:
                case OPT_SET_OFFSET_R:
                case OPT_SET_OFFSET_G:
                case OPT_SET_OFFSET_B:
                case OPT_SET_OFFSET_I:
                case OPT_SET_EXPOSURE_R:
                case OPT_SET_EXPOSURE_G:
                case OPT_SET_EXPOSURE_B:
                case OPT_SET_EXPOSURE_I:
                    scanner->val[option].w = *(SANE_Word *) val;
                    break;

                /* side-effect-free word-array options: */
                case OPT_CROP_IMAGE:
                    memcpy (scanner->val[option].wa, val, scanner->opt[option].size);
                    break;

                /* options with side-effects: */
                case OPT_MODE:
                {
                    /* Free current setting */
                    if (scanner->val[option].s) {
                        free (scanner->val[option].s);
                    }
                    /* New setting */
                    scanner->val[option].s = (SANE_Char *) strdup (val);
                    /* Info */
                    if (info) {
                        *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;
                    }
                    break;
                }

                case OPT_CALIBRATION_MODE:
                case OPT_GAIN_ADJUST:
                case OPT_HALFTONE_PATTERN:
                {
                     /* Free current setting */
                    if (scanner->val[option].s) {
                        free (scanner->val[option].s);
                    }
                    /* New setting */
                    scanner->val[option].s = (SANE_Char *) strdup (val);
                    break;
                }

            }

            /* Check the whole set
            if (sanei_pieusb_analyse_options(scanner)) {
                return SANE_STATUS_GOOD;
            } else {
                return SANE_STATUS_INVAL;
            }
              */
            break;
        default:
            return SANE_STATUS_INVAL;
            break;
    }
#endif
    return SANE_STATUS_INVAL;
}

/**
 * Obtain the current scan parameters. The returned parameters are guaranteed
 * to be accurate between the time a scan has been started (sane start() has
 * been called) and the completion of that request. Outside of that window, the
 * returned values are best-effort estimates of what the parameters will be when
 * sane start() gets invoked. - says the SANE standard.
 *
 * @param handle Scanner handle
 * @param params Scan parameters
 * @return SANE_STATUS_GOOD
 */
SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  handle = handle;
  params = params;
    DBG (DBG_info_sane, "sane_get_parameters\n");
#if 0
    WsdScanner *scanner = handle;
    const char *mode;
    double resolution, width, height;
    SANE_Int colors;
    if (params) {

        if (scanner->scanning) {
            /* sane_start() initialized a SANE_Parameters struct in the scanner */
            DBG (DBG_info_sane, "sane_get_parameters from scanner values\n");
            params->bytes_per_line = scanner->scan_parameters.bytes_per_line;
            params->depth = scanner->scan_parameters.depth;
            params->format = scanner->scan_parameters.format;
            params->last_frame = scanner->scan_parameters.last_frame;
            params->lines = scanner->scan_parameters.lines;
            params->pixels_per_line = scanner->scan_parameters.pixels_per_line;
        } else {
            /* Calculate appropriate values from option settings */
            DBG (DBG_info_sane, "sane_get_parameters from option values\n");
            if (scanner->val[OPT_PREVIEW].b) {
                resolution = scanner->device->fast_preview_resolution;
            } else {
                resolution = SANE_UNFIX(scanner->val[OPT_RESOLUTION].w);
            }
            DBG (DBG_info_sane, "  resolution %f\n", resolution);
            width = SANE_UNFIX(scanner->val[OPT_BR_X].w)-SANE_UNFIX(scanner->val[OPT_TL_X].w);
            height = SANE_UNFIX(scanner->val[OPT_BR_Y].w)-SANE_UNFIX(scanner->val[OPT_TL_Y].w);
            DBG (DBG_info_sane, "  width x height: %f x %f\n", width, height);
            params->lines = height / MM_PER_INCH * resolution;
            params->pixels_per_line = width / MM_PER_INCH * resolution;
            mode = scanner->val[OPT_MODE].s;
            if (strcmp(mode, SANE_VALUE_SCAN_MODE_LINEART) == 0) {
                params->format = SANE_FRAME_GRAY;
                params->depth = 1;
                colors = 1;
            } else if(strcmp(mode, SANE_VALUE_SCAN_MODE_HALFTONE) == 0) {
                params->format = SANE_FRAME_GRAY;
                params->depth = 1;
                colors = 1;
            } else if(strcmp(mode, SANE_VALUE_SCAN_MODE_GRAY) == 0) {
                params->format = SANE_FRAME_GRAY;
                params->depth = scanner->val[OPT_BIT_DEPTH].w;
                colors = 1;
            } else if(strcmp(mode, SANE_VALUE_SCAN_MODE_RGBI) == 0) {
                params->format = SANE_FRAME_RGB; /* was: SANE_FRAME_RGBI */
                params->depth = scanner->val[OPT_BIT_DEPTH].w;
                colors = 4;
            } else { /* SANE_VALUE_SCAN_MODE_COLOR */
                params->format = SANE_FRAME_RGB;
                params->depth = scanner->val[OPT_BIT_DEPTH].w;
                colors = 3;
            }
            DBG (DBG_info_sane, "  colors: %d\n", colors);
            if (params->depth == 1) {
                params->bytes_per_line = colors * (params->pixels_per_line + 7)/8;
            } else if (params->depth <= 8) {
                params->bytes_per_line = colors * params->pixels_per_line;
            } else if (params->depth <= 16) {
                params->bytes_per_line = 2 * colors * params->pixels_per_line;
            }
            params->last_frame = SANE_TRUE;
        }

        DBG(DBG_info_sane,"sane_get_parameters(): SANE parameters\n");
        DBG(DBG_info_sane," format = %d\n",params->format);
        DBG(DBG_info_sane," last_frame = %d\n",params->last_frame);
        DBG(DBG_info_sane," bytes_per_line = %d\n",params->bytes_per_line);
        DBG(DBG_info_sane," pixels_per_line = %d\n",params->pixels_per_line);
        DBG(DBG_info_sane," lines = %d\n",params->lines);
        DBG(DBG_info_sane," depth = %d\n",params->depth);

    } else {

        DBG(DBG_info_sane," no params argument, no values returned\n");

    }
#endif
    return SANE_STATUS_GOOD;
}

/**
 * Initiates aquisition of an image from the scanner.
 * SCAN Phase 1: initialization and calibration
 * (SCAN Phase 2: line-by-line scan & read is not implemented)
 * SCAN Phase 3: get CCD-mask
 * SCAN phase 4: scan slide and save data in scanner buffer

 * @param handle Scanner handle
 * @return
 */
SANE_Status
sane_start (SANE_Handle handle)
{
    WsdScanner *scanner = handle;


    DBG (DBG_info_sane, "sane_start()\n");

    /* ----------------------------------------------------------------------
     *
     * Exit if currently scanning
     *
     * ---------------------------------------------------------------------- */
    if (scanner->scanning) {
        DBG (DBG_error, "sane_start(): scanner is already scanning, exiting\n");
        return SANE_STATUS_DEVICE_BUSY;
    }

    scanner->scanning = SANE_TRUE;
    scanner->cancel_request = SANE_FALSE;
#if 0
    /* Handle cancel request */
    if (scanner->cancel_request) {
        return sanei_wsdscan_on_cancel (scanner);
    }
#endif

    return SANE_STATUS_GOOD;

}

/**
 * Read image data from the scanner buffer.
 *
 * @param handle
 * @param buf
 * @param max_len
 * @param len
 * @return
 */
SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len, SANE_Int * len)
{
  buf = buf;
  len = len;
    WsdScanner *scanner = handle;
//    SANE_Int return_size;

    DBG(DBG_info_sane, "sane_read(): requested %d bytes\n", max_len);

    /* No reading if not scanning */
    if (!scanner->scanning) {
        *len = 0;
        return SANE_STATUS_IO_ERROR; /* SANE standard does not allow a SANE_STATUS_INVAL return */
    }

    /* Handle cancel request */
    if (scanner->cancel_request) {
        /* cancel */
    }
    return SANE_STATUS_GOOD;
}


/**
 * Request cancellation of current scanning process.
 *
 * @param handle Scanner handle
 */
void
sane_cancel (SANE_Handle handle)
{
    WsdScanner *scanner = handle;

    DBG (DBG_info_sane, "sane_cancel\n");

    if (scanner->scanning) {
        scanner->cancel_request = 1;
    }
}


/**
 * Set the I/O mode of handle h. The I/O mode can be either blocking or
 * non-blocking, but for USB devices, only blocking mode is supported.
 *
 * @param handle Scanner handle
 * @param non_blocking
 * @return SANE_STATUS_UNSUPPORTED;
 */
SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
    /* WsdScanner *scanner = handle; */

    DBG (DBG_info_sane, "sane_set_io_mode: handle = %p, non_blocking = %s\n", handle, non_blocking == SANE_TRUE ? "true" : "false");

    if (non_blocking) {
	return SANE_STATUS_UNSUPPORTED;
    }

    return SANE_STATUS_GOOD;
}


/**
 * Obtain a file-descriptor for the scanner that is readable if image data is
 * available. The select file-descriptor is returned in *fd.
 * The function has not been implemented since USB-device only operate in
 * blocking mode.
 *
 * @param handle Scanner handle
 * @param fd File descriptor with imae data
 * @return SANE_STATUS_INVAL
 */
SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
    DBG(DBG_info_sane,"sane_get_select_fd(): not supported (only for non-blocking IO)\n");
    handle = handle;
    fd = fd;
    return SANE_STATUS_UNSUPPORTED;
}
