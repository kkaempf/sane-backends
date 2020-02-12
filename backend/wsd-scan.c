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
#include <sys/param.h> /* for MIN and MAX */

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

static WsdScanner *wsd_scanner_list = NULL;

/* --------------------------------------------------------------------------
 *
 * wsd_scan internals
 *
 * --------------------------------------------------------------------------*/

/**
 * Determine maximum lengt of a set of strings.
 *
 * @param strings Set of strings
 * @return maximum length
 */
static size_t
_max_string_size (SANE_String_Const const strings[])
{
    size_t size, max_size = 0;
    int i;
    DBG (DBG_info_proc, "_max_string_size\n");

    for (i = 0; strings[i]; ++i) {
        size = strlen (strings[i]) + 1;
        DBG (DBG_info_proc, "_max_string_size(%s:%ld)\n", strings[i], size);
        if (size > max_size) {
            max_size = size;
        }
    }

    return max_size;
}


/*
 * create request options
 *
 */
static request_opt_t *
_create_request_options()
{
    request_opt_t *options = wsd_options_create();
    if (DBG_LEVEL > 127) {
        wsd_set_options_flags(options, FLAG_DUMP_REQUEST);
        wsd_set_options_flags(options, FLAG_DUMP_RESPONSE);
    }
    return options;
}


static int
_color_mode_to_depth(char *text)
{
    if (!strcmp(text, WSD_COLOR_ENTRY_BW1)) {
        return 1;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_GS4)) {
        return 4;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_GS8)) {
        return 8;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_GS16)) {
        return 16;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_RGB24)) {
        return 24;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_RGB48)) {
        return 48;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_RGBA32)) {
        return 32;
    } else if (!strcmp(text, WSD_COLOR_ENTRY_RGBA64)) {
        return 64;
    }
    DBG (DBG_error, "Unknown color mode '%s'\n", text);
    return 0;
}


static char *
_depth_to_color_mode(int depth)
{
    switch (depth) {
        case 1:
            return WSD_COLOR_ENTRY_BW1;
        case 4:
            return WSD_COLOR_ENTRY_GS4;
        case 8:
            return WSD_COLOR_ENTRY_GS8;
        case 16:
            return WSD_COLOR_ENTRY_GS16;
        case 24:
            return WSD_COLOR_ENTRY_RGB24;
        case 48:
            return WSD_COLOR_ENTRY_RGB48;
        case 32:
            return WSD_COLOR_ENTRY_RGBA32;
        case 64:
            return WSD_COLOR_ENTRY_RGBA64;
        default:
            DBG (DBG_error, "Unknown color depth '%d'\n", depth);
            break;
    }
    return WSD_COLOR_ENTRY_BW1;
}



/*
 * extract options from xml to string list
 */
SANE_String_Const *
_build_options_list(WsXmlNodeH parent, const char *initial, const char *ns, const char *outer, const char *inner)
{
    int count = (initial)?1:0;
    SANE_String_Const *options;
    const char *parent_name;
    if (!parent) {
        DBG (DBG_error, "_build_options_list() - no parent given\n");
        return NULL;
    }
    parent_name = ws_xml_get_node_local_name(parent);
    DBG (DBG_info_sane, "_build_options_list(parent %s, ns %s, outer %s, inner %s\n", parent_name, ns, outer, inner);

    WsXmlNodeH outer_node = ws_xml_find_in_tree(parent, ns, outer, 1);
    if (outer_node) {
        count += ws_xml_get_child_count_by_qname(outer_node, ns, inner);
        DBG (DBG_info_sane, "Found %d %s entries\n", count, inner);
    }
    options = calloc(count+1, sizeof(SANE_String_Const)); /* +1 for trailing NULL */
    if (!options) {
        DBG (DBG_error, "_build_options_list() - calloc() returned NULL\n");
        return NULL;
    }
    int o = 0;
    if (initial) {
      options[o++] = strdup(initial);
    }
    WsXmlNodeH inner_node;
    int i = 0;
    while ((inner_node = ws_xml_get_child(outer_node, i++, ns, inner))) {
        options[o++] = strdup(ws_xml_get_node_text(inner_node));
    }
    DBG (DBG_error, "_build_options_list() - created %d of %d options\n", o, count);
  
    return options;
}

/**
 * Initiaize scanner options from the device definition.
 * The function is called by sane_open(), when no
 * optimized settings are available yet. The scanner object is fully
 * initialized in sane_start().
 *
 * @param scanner Scanner to initialize
 * @return SANE_STATUS_GOOD
 */
SANE_Status
_init_options (WsdScanner* scanner, WsXmlNodeH scanner_configuration)
{
    int i;

    DBG (DBG_info_proc, "_init_options\n");

    memset (scanner->opt, 0, sizeof (scanner->opt));
    memset (scanner->val, 0, sizeof (scanner->val));

    for (i = 0; i < NUM_OPTIONS; ++i) {
        scanner->opt[i].size = sizeof (SANE_Word);
        scanner->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }

    /* Number of options (a pseudo-option) */
    scanner->opt[OPT_NUM_OPTS].name = SANE_NAME_NUM_OPTIONS;
    scanner->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
    scanner->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
    scanner->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
    scanner->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
    scanner->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

    /* "Source" group: */

    /* retrieve input sources */
    SANE_String_Const *scan_sources = calloc(4, sizeof(SANE_String_Const));
    if (!scan_sources) {
        return SANE_STATUS_NO_MEM;
    }
    i = 0;
    if (ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN, 1)) {
      scan_sources[i++] = WSD_PLATEN;
      DBG (DBG_info_sane, "%s found\n", WSD_PLATEN);
    }
    if (ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_ADF, 1)) {
      scan_sources[i++] = WSD_ADF;
      DBG (DBG_info_sane, "%s found\n", WSD_ADF);
    }
    if (ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_FILM, 1)) {
      scan_sources[i++] = WSD_FILM;
      DBG (DBG_info_sane, "%s found\n", WSD_FILM);
    }
    scanner->opt[OPT_SCAN_SOURCE].name = "Source";
    scanner->opt[OPT_SCAN_SOURCE].title = "Scan source";
    scanner->opt[OPT_SCAN_SOURCE].desc = "Scan input selector";
    scanner->opt[OPT_SCAN_SOURCE].type = SANE_TYPE_STRING;
    scanner->opt[OPT_SCAN_SOURCE].unit = SANE_UNIT_NONE;
    scanner->opt[OPT_SCAN_SOURCE].size = _max_string_size(scan_sources);
//    scanner->opt[OPT_SCAN_SOURCE].cap = 0;
    scanner->opt[OPT_SCAN_SOURCE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    scanner->opt[OPT_SCAN_SOURCE].constraint.string_list = scan_sources;
    scanner->val[OPT_SCAN_SOURCE].s = strdup(scan_sources[0]);

    /* Format Group */
    scanner->opt[OPT_FORMAT_GROUP].name = "Format";
    scanner->opt[OPT_FORMAT_GROUP].title = "Scan format";
    scanner->opt[OPT_FORMAT_GROUP].desc = "";
    scanner->opt[OPT_FORMAT_GROUP].type = SANE_TYPE_GROUP;
    scanner->opt[OPT_FORMAT_GROUP].unit = 0;
    scanner->opt[OPT_FORMAT_GROUP].size = 0;
    scanner->opt[OPT_FORMAT_GROUP].cap = 0;
    scanner->opt[OPT_FORMAT_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    /* resolutions */
  
    /* first - optical resolution */
    WsXmlNodeH optical_resolution = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN_OPTICAL_RESOLUTION, 1);
    if (!optical_resolution) {
        optical_resolution = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_ADF_OPTICAL_RESOLUTION, 1);
        if (!optical_resolution) {
            optical_resolution = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_FILM_OPTICAL_RESOLUTION, 1);
            if (!optical_resolution) {
                DBG (DBG_error, "No OpticalResolution found in %s\n", ws_xml_get_node_local_name(scanner_configuration));
                return SANE_STATUS_INVAL;
            }
        }
    }
    WsXmlNodeH width = ws_xml_find_in_tree(optical_resolution, XML_NS_WDP_SCAN, WSD_WIDTH, 1);
    WsXmlNodeH height = ws_xml_find_in_tree(optical_resolution, XML_NS_WDP_SCAN, WSD_HEIGHT, 1);
    if (!width && !height) {
        DBG (DBG_error, "No %s or %s found in %s\n", WSD_WIDTH, WSD_HEIGHT, ws_xml_get_node_local_name(optical_resolution));
        return SANE_STATUS_INVAL;
    }
    SANE_String max_width = ws_xml_get_node_text(width);
    SANE_String max_height = ws_xml_get_node_text(height);

    /* then - other resolutions */
    WsXmlNodeH platen_resolutions = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN_RESOLUTIONS, 1);
    if (!platen_resolutions) {
        platen_resolutions = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_ADF_RESOLUTIONS, 1);
        if (!platen_resolutions) {
            platen_resolutions = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_FILM_RESOLUTIONS, 1);
            if (!platen_resolutions) {
                DBG (DBG_error, "No resolutions found in scanner configuration\n");
                return SANE_STATUS_INVAL;
            }
        }
    }
    SANE_String_Const *x_resolutions = _build_options_list(platen_resolutions, max_width, XML_NS_WDP_SCAN, WSD_WIDTHS, WSD_WIDTH);
//    SANE_String_Const *y_resolutions = _build_options_list(platen_resolutions, max_height, XML_NS_WDP_SCAN, WSD_HEIGHTS, WSD_HEIGHT);

    scanner->opt[OPT_RESOLUTION].name = "Resolution";
    scanner->opt[OPT_RESOLUTION].title = "Scan resolution";
    scanner->opt[OPT_RESOLUTION].desc = "Resolution in dots per inch";
    scanner->opt[OPT_RESOLUTION].type = SANE_TYPE_STRING;
    scanner->opt[OPT_RESOLUTION].unit = SANE_UNIT_NONE;
    scanner->opt[OPT_RESOLUTION].size = _max_string_size(x_resolutions);
//    scanner->opt[OPT_RESOLUTION].cap = 0;
    scanner->opt[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    scanner->opt[OPT_RESOLUTION].constraint.string_list = x_resolutions;
    scanner->val[OPT_RESOLUTION].s = strdup(x_resolutions[0]);
    DBG (DBG_info_sane, "scanner->val[%d].s = %s\n", OPT_RESOLUTION, scanner->val[OPT_RESOLUTION].s);

    /* colors */

    WsXmlNodeH color = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN_COLOR, 1);
    if (!color) {
       color = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_ADF_COLOR, 1);
       if (!color) {
           color = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_FILM_COLOR, 1);
           if (!color) {
               DBG (DBG_error, "No Color found in %s\n", ws_xml_get_node_local_name(scanner_configuration));
           }
       }
    }
    i = ws_xml_get_child_count_by_qname(color, XML_NS_WDP_SCAN, WSD_COLOR_ENTRY);
    if (i == 0) {
        DBG (DBG_error, "%s has no %s\n", ws_xml_get_node_local_name(color), WSD_COLOR_ENTRY);
        return SANE_STATUS_INVAL;
    }
    SANE_Word *bpp_list = calloc(i+1, sizeof(SANE_Word));
    if (!bpp_list) {
        return SANE_STATUS_NO_MEM;
    }
    bpp_list[0] = i; /* count */
    int j;
    for (j = 1; j <= i; j++) {
        WsXmlNodeH color_entry = ws_xml_get_child(color, j-1, XML_NS_WDP_SCAN, WSD_COLOR_ENTRY);
        char *text;
        if (!color_entry) {
            DBG (DBG_error, "%s has no %s as child #%d\n", ws_xml_get_node_local_name(color), WSD_COLOR_ENTRY, j-1);
            return SANE_STATUS_INVAL;
        }
        text = ws_xml_get_node_text(color_entry);
        bpp_list[j] = _color_mode_to_depth(text);
        if (bpp_list[j] == 0) {
            DBG (DBG_error, "Unknown %s:%s in %s, ignoring\n", WSD_COLOR_ENTRY, text, ws_xml_get_node_local_name(color));
        } else {
            DBG (DBG_info_sane, "color depth[%d] = %d\n", j, bpp_list[j]);
        }
    }
    scanner->opt[OPT_COLOR].name = "Color";
    scanner->opt[OPT_COLOR].title = "Color depth";
    scanner->opt[OPT_COLOR].desc = "Bits per pixel";
    scanner->opt[OPT_COLOR].type = SANE_TYPE_INT;
    scanner->opt[OPT_COLOR].unit = SANE_UNIT_BIT;
    scanner->opt[OPT_COLOR].size = sizeof(SANE_Word);
//    scanner->opt[OPT_COLOR].cap = 0;
    scanner->opt[OPT_COLOR].constraint_type = SANE_CONSTRAINT_WORD_LIST;
    scanner->opt[OPT_COLOR].constraint.word_list = bpp_list;
    scanner->val[OPT_COLOR].w = bpp_list[i]; /* highest mode */

    /* Geometry Group */
    scanner->opt[OPT_GEOMETRY_GROUP].name = "Size";
    scanner->opt[OPT_GEOMETRY_GROUP].title = "Scan size";
    scanner->opt[OPT_GEOMETRY_GROUP].desc = "";
    scanner->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
    scanner->opt[OPT_GEOMETRY_GROUP].cap = 0;
    scanner->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    /* width, height */
    WsXmlNodeH platen_minimum_size = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN_MINIMUM_SIZE, 1);
    if (!platen_minimum_size) {
        DBG (DBG_error, "No %s found\n", WSD_PLATEN_MINIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    WsXmlNodeH platen_maximum_size = ws_xml_find_in_tree(scanner_configuration, XML_NS_WDP_SCAN, WSD_PLATEN_MAXIMUM_SIZE, 1);
    if (!platen_minimum_size) {
        DBG (DBG_error, "No %s found\n", WSD_PLATEN_MAXIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    int max_w, min_w; /* in 1/1000th of an inch */
    width = ws_xml_find_in_tree(platen_minimum_size, XML_NS_WDP_SCAN, WSD_WIDTH, 1);
    if (!width) {
        DBG (DBG_error, "No %s found in %s\n", WSD_WIDTH, WSD_PLATEN_MINIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    min_w = atoi(ws_xml_get_node_text(width));
    width = ws_xml_find_in_tree(platen_maximum_size, XML_NS_WDP_SCAN, WSD_WIDTH, 1);
    if (!width) {
        DBG (DBG_error, "No %s found in %s\n", WSD_WIDTH, WSD_PLATEN_MAXIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    max_w = atoi(ws_xml_get_node_text(width));
    SANE_Range *range = calloc(1, sizeof(SANE_Range));
    if (!range) {
        return SANE_STATUS_NO_MEM;
    }
    DBG (DBG_info_sane, "width:  %d - %d mm\n", min_w, max_w);
    scanner->opt[OPT_WIDTH].name = "Width";
    scanner->opt[OPT_WIDTH].title = "Scan width";
    scanner->opt[OPT_WIDTH].desc = "Width of scan area";
    scanner->opt[OPT_WIDTH].type = SANE_TYPE_INT;
    scanner->opt[OPT_WIDTH].unit = SANE_UNIT_MM;
    scanner->opt[OPT_WIDTH].size = sizeof(SANE_Word);
//    scanner->opt[OPT_WIDTH].cap = 0;
    scanner->opt[OPT_WIDTH].constraint_type = SANE_CONSTRAINT_RANGE;
    scanner->opt[OPT_WIDTH].constraint.range = range;
    scanner->opt[OPT_WIDTH].constraint_type = SANE_CONSTRAINT_RANGE;
    range->min = min_w;
    range->max = max_w;
    range->quant = 0;
    scanner->opt[OPT_WIDTH].constraint.range = range;
    scanner->val[OPT_WIDTH].w = range->min;

    /* height */
    int max_h, min_h; /* in 1/1000th of an inch */
    height = ws_xml_find_in_tree(platen_minimum_size, XML_NS_WDP_SCAN, WSD_HEIGHT, 1);
    if (!height) {
        DBG (DBG_error, "No %s found in %s\n", WSD_HEIGHT, WSD_PLATEN_MINIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    min_h = atoi(ws_xml_get_node_text(height));
    height = ws_xml_find_in_tree(platen_maximum_size, XML_NS_WDP_SCAN, WSD_HEIGHT, 1);
    if (!height) {
        DBG (DBG_error, "No %s found in %s\n", WSD_HEIGHT, WSD_PLATEN_MAXIMUM_SIZE);
        return SANE_STATUS_INVAL;
    }
    max_h = atoi(ws_xml_get_node_text(height));
    DBG (DBG_info_sane, "height:  %d - %d mm\n", min_h, max_h);

    range = calloc(1, sizeof(SANE_Range));
    if (!range) {
        return SANE_STATUS_NO_MEM;
    }
    range->min = MIN(min_w, min_h);
    range->max = MAX(max_w, max_h);
    range->quant = 0;
    scanner->opt[OPT_HEIGHT].name = "Height";
    scanner->opt[OPT_HEIGHT].title = "Scan height";
    scanner->opt[OPT_HEIGHT].desc = "Height of scan area";
    scanner->opt[OPT_HEIGHT].type = SANE_TYPE_INT;
    scanner->opt[OPT_HEIGHT].unit = SANE_UNIT_MM;
    scanner->opt[OPT_HEIGHT].size = sizeof(SANE_Word);
//    scanner->opt[OPT_HEIGHT].cap = 0;
    scanner->opt[OPT_HEIGHT].constraint_type = SANE_CONSTRAINT_RANGE;
    scanner->opt[OPT_HEIGHT].constraint.range = range;
    scanner->opt[OPT_HEIGHT].constraint_type = SANE_CONSTRAINT_RANGE;
    range->min = min_h;
    range->max = max_h;
    range->quant = 0;
    scanner->opt[OPT_HEIGHT].constraint.range = range;
    scanner->val[OPT_HEIGHT].w = range->min;

#if 0
    scanner->opt[OPT_].name = "Source";
    scanner->opt[OPT_].title = "Scan source";
    scanner->opt[OPT_].desc = "Scan input selector";
    scanner->opt[OPT_].type = SANE_TYPE_STRING;
    scanner->opt[OPT_].unit = SANE_UNIT_NONE;
    scanner->opt[OPT_].size = _max_string_size(scan_sources);
//    scanner->opt[OPT_].cap = 0;
    scanner->opt[OPT_].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    scanner->opt[OPT_].constraint.string_list = scan_sources;
    scanner->val[OPT_].s = strdup(scan_sources[0]);
#endif
    return SANE_STATUS_GOOD;
}


SANE_Status
_get_status (WsdScanner *scanner)
{
    SANE_Status status = SANE_STATUS_GOOD;

    request_opt_t *options = _create_request_options();

    DBG (DBG_info_sane, "_get_status()\n");

    WsdRequest *request = wsd_action_get_scanner_status(scanner->client, options);
    if (!request) {
        DBG (DBG_error, "get_scanner_status failed\n");
        status = SANE_STATUS_IO_ERROR;
        goto _get_status_done;
    }
    WsXmlNodeH node = wsd_response_node(request);
    WsXmlNodeH scanner_status = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_SCANNER_STATUS, 1);
    if (!request) {
        DBG (DBG_error, "No %s in status response\n", WSD_SCANNER_STATUS);
        status = SANE_STATUS_IO_ERROR;
        goto _get_status_done;
    }
    WsXmlNodeH scanner_state = ws_xml_find_in_tree(scanner_status, XML_NS_WDP_SCAN, WSD_SCANNER_STATE, 1);
    if (!scanner_state) {
        DBG (DBG_error, "No %s in %s\n", WSD_SCANNER_STATE, WSD_SCANNER_STATUS);
        status = SANE_STATUS_IO_ERROR;
        goto _get_status_done;
    }
    char *text = ws_xml_get_node_text(scanner_state);
    if (!strcmp(text, "Idle")) {
        status = SANE_STATUS_GOOD;
    } else if (!strcmp(text, "Processing")) {
        status = SANE_STATUS_DEVICE_BUSY;
    } else {
        DBG (DBG_error, "Status is %s\n", text);
        status = SANE_STATUS_IO_ERROR;
        goto _get_status_done;
    }
_get_status_done:
    if (request)
        wsd_request_destroy(request);
    wsd_options_destroy(options);
    return status;
}

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
    debug_add_handler(wsd_debug_message_handler, DBG_LEVEL, NULL);

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
        WsdScanner **pivot = &wsd_scanner_list; 
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
                strncpy(scanner->url, url, PATH_MAX-1);
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

    while (wsd_scanner_list) {
        WsdScanner *scanner;
        if (wsd_scanner_list->sane_device.name) free((SANE_String)wsd_scanner_list->sane_device.name);
        if (wsd_scanner_list->sane_device.vendor) free((SANE_String)wsd_scanner_list->sane_device.vendor);
        if (wsd_scanner_list->sane_device.model) free((SANE_String)wsd_scanner_list->sane_device.model);
        if (wsd_scanner_list->sane_device.type) free((SANE_String)wsd_scanner_list->sane_device.type);
        scanner = wsd_scanner_list->next;
        free (wsd_scanner_list);
        wsd_scanner_list = scanner;
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
    const SANE_Device **devlist;
    int i;
    request_opt_t *options;

    /* Create SANE_DEVICE list from device list created in sane_init() */
    i = 0;
    for (scanner = wsd_scanner_list; scanner; scanner = scanner->next) {
        i++;
    }

    DBG (DBG_info_sane, "sane_get_devices: found %d scanner\n", i);
    devlist = calloc ((i + 1), sizeof (WsdScanner *));
    if (!devlist) {
        return SANE_STATUS_NO_MEM;
    }
    i = 0;
    options = _create_request_options();
    for (scanner = wsd_scanner_list; scanner; scanner = scanner->next) {
        WsdRequest *request;
        DBG (DBG_info_sane, "sane_get_devices: create scanner '%s'\n", scanner->url);
        scanner->client = wsd_client_create_from_url(scanner->url);
        if (!scanner->client) {
          DBG (DBG_error, "Can't access %s\n", scanner->url);
          continue;
        }
        request = wsd_action_get_scanner_description(scanner->client, options);
        if (!request) {
            DBG (DBG_error, "Description request creation failed\n");
            continue;
        }
        else {
            WS_LASTERR_Code error;
            error = wsd_get_last_error(request);
            if (error != WS_LASTERR_OK) {
                char *error_str = wsd_transport_get_last_error_string(error);
                DBG (DBG_error, "No reponse: error %d:%s\n", error, error_str);
            } else {
                // set name, vendor, model, type
                WsXmlNodeH node = wsd_response_node(request);
                WsXmlNodeH scanner_name_node = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_SCANNER_NAME, 1);
                WsXmlNodeH scanner_info_node = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_SCANNER_INFO, 1);
//                WsXmlNodeH scanner_location_node = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_ELEMENT_SCANNER_LOCATION, 1);
                scanner->sane_device.name = strdup(scanner->url);
                scanner->sane_device.vendor = strdup("unknown");
                if (scanner_name_node) {
                    scanner->sane_device.model = strdup(ws_xml_get_node_text(scanner_name_node));
                    DBG (DBG_info_sane, "sane_get_devices: %s:%s\n", WSD_SCANNER_NAME, scanner->sane_device.model);
                } else {
                    DBG (DBG_error, "No %s found\n", WSD_SCANNER_NAME);
                }
                if (scanner_info_node) {
                    scanner->sane_device.type = strdup(ws_xml_get_node_text(scanner_info_node));
                    DBG (DBG_info_sane, "sane_get_devices: %s:%s\n", WSD_SCANNER_INFO, scanner->sane_device.type);
                } else {
                    DBG (DBG_error, "No %s found\n", WSD_SCANNER_INFO);
                }
//                WsXmlDocH doc = wsd_response_doc(request);
//                xml_parser_element_dump(stderr, doc, node);
            }
        }
        wsd_request_destroy(request);
        wsd_client_destroy(scanner->client);
        scanner->client = NULL;
        devlist[i++] = &(scanner->sane_device);
    }
    devlist[i] = NULL;
    DBG (DBG_info_sane, "sane_get_devices: returning %p\n", (void *)devlist);
    *device_list = devlist;

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
    WsdScanner *scanner;
    WsdRequest *request;
    SANE_Status status = SANE_STATUS_GOOD;
    request_opt_t *options;

    DBG (DBG_info_sane, "sane_open(%s)\n", devicename);

    for (scanner = wsd_scanner_list; scanner; scanner = scanner->next) {
        if (strcmp(devicename, scanner->url)) {
            continue;
        }
        if (scanner->client) {
            // reopen
            return SANE_STATUS_DEVICE_BUSY;
        }
        scanner->client = wsd_client_create_from_url(scanner->url);
        break;
    }
    if (!scanner) {
        DBG (DBG_error, "No scanner matches '%s'\n", devicename);
        // no device found
        return SANE_STATUS_INVAL;
    }
    if (!scanner->client) {
        DBG (DBG_error, "Client creation for '%s' failed\n", scanner->url);
        // open failed
        return SANE_STATUS_INVAL;
    }
    scanner->scanning = 0;
    scanner->cancel_request = 0;

    options = _create_request_options();

    request = wsd_action_get_scanner_configuration(scanner->client, options);
    if (!request) {
        DBG (DBG_error, "Configuration request creation failed\n");
        status = SANE_STATUS_IO_ERROR;
        goto exit_from_open;
    }
    WsXmlNodeH node = wsd_response_node(request);
    _init_options(scanner, node);

//  WsXmlDocH doc = wsd_response_doc(request);
//                xml_parser_element_dump(stderr, doc, node);

    *handle = (SANE_Handle)scanner;
exit_from_open:
    wsd_options_destroy(options);
    return status;
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
    WsdScanner *scanner = (WsdScanner *)handle;

    DBG (DBG_info_proc, "sane_get_option_descriptor() option=%d\n", option);

    if ((unsigned) option >= NUM_OPTIONS)
    {
      return NULL;
    }

    return scanner->opt + option;
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
		     void *val, SANE_Int *info)
{
    WsdScanner *scanner = handle;
    SANE_Status status = SANE_STATUS_GOOD;
    SANE_Word cap;
    SANE_String_Const name;

    DBG(DBG_info_sane,"sane_control_option(%s:%d)\n", (action == SANE_ACTION_GET_VALUE)?"Get":((action == SANE_ACTION_SET_VALUE)?"Set":"?"), option);
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
                case OPT_NUM_OPTS:
                /* word options: */
                case OPT_COLOR:
                case OPT_WIDTH:
                case OPT_HEIGHT:
                    *(SANE_Word *) val = scanner->val[option].w;
                    DBG (DBG_info_sane, "get %s [#%d] val=%d\n", name, option,scanner->val[option].w);
                    break;

#if 0
                /* word-array options: */
                    memcpy (val, scanner->val[option].wa, scanner->opt[option].size);
                    break;
#endif
                /* string options */
                case OPT_SCAN_SOURCE:
                case OPT_RESOLUTION:
                    strcpy (val, scanner->val[option].s);
                    DBG (DBG_info_sane, "get %s [#%d] val=%s\n", name, option,scanner->val[option].s);
                    break;
                default:
                    DBG(DBG_error,"SANE_ACTION_GET_VALUE(%d) - not implemented\n", option);
                    status = SANE_STATUS_INVAL;
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
                case OPT_COLOR:
                /* (mostly) side-effect-free word options: */
                    if (info) {
                        *info |= SANE_INFO_RELOAD_PARAMS;
                    }
                /* fall through */
                    scanner->val[option].w = *(SANE_Word *) val;
                    break;
#if 0
                /* side-effect-free word-array options: */
                case OPT_CROP_IMAGE:
                    memcpy (scanner->val[option].wa, val, scanner->opt[option].size);
                    break;
#endif
                /* options with side-effects: */
                case OPT_SCAN_SOURCE:
                case OPT_RESOLUTION:
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
#if 0
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
#endif
                default:
                    DBG(DBG_error,"SANE_ACTION_SET_VALUE(%d) - not implemented\n", option);
                    break;
            }

            /* Check the whole set
            if (sanei_pieusb_analyse_options(scanner)) {
                return SANE_STATUS_GOOD;
            } else {
                return SANE_STATUS_INVAL;
            }
              */
            break;
        case SANE_ACTION_SET_AUTO:
            break;
        default:
            status = SANE_STATUS_INVAL;
            break;
    }
    return status;
}


/**
 * Initiates aquisition of an image from the scanner.

 * @param handle Scanner handle
 * @return status
 */

SANE_Status
sane_start (SANE_Handle handle)
{
    SANE_Status status = SANE_STATUS_GOOD;
    WsdScanOptions scan_options;
    
    WsdScanner *scanner = (WsdScanner *)handle;
    WsdRequest *request = NULL;
    request_opt_t *options = _create_request_options();

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

    status = _get_status(scanner);
    if (status != SANE_STATUS_GOOD)
        goto start_done;

    scan_options.jobname = "scanjob";
    scan_options.username = "sane",
      scan_options.format = "jfif";
      scan_options.images_to_transfer = 1,
      scan_options.input_source = scanner->val[OPT_SCAN_SOURCE].s;
      scan_options.content_type = "Auto";
      scan_options.front_color_mode = _depth_to_color_mode(scanner->val[OPT_COLOR].w);
      scan_options.back_color_mode = NULL;
      scan_options.x_resolution = scanner->val[OPT_RESOLUTION].w;
      scan_options.y_resolution = scanner->val[OPT_RESOLUTION].w;
    
    request = wsd_action_create_scan_job(scanner->client, options, &scan_options);
    if (!request) {
        DBG (DBG_error, "sane_start(): create_scan_job failed\n");
        status = SANE_STATUS_IO_ERROR;
        goto start_done;
    }
    WsXmlNodeH node = wsd_response_node(request);
    WsXmlNodeH job_id = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_JOB_ID, 1);
    if (!job_id) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_JOB_ID);
        goto start_done;
    }
    scanner->job_id = strdup(ws_xml_get_node_text(job_id));
    WsXmlNodeH job_token = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_JOB_TOKEN, 1);
    if (!job_token) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_JOB_TOKEN);
        goto start_done;
    }
    scanner->job_token = strdup(ws_xml_get_node_text(job_token));
    DBG (DBG_info_sane, "Job token '%s'\n", scanner->job_token);
    WsXmlNodeH media_front_image_info = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_MEDIA_FRONT_IMAGE_INFO, 1);
    if (!media_front_image_info) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_MEDIA_FRONT_IMAGE_INFO);
        goto start_done;
    }
    WsXmlNodeH color_processing = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_COLOR_PROCESSING, 1);
    if (!color_processing) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_COLOR_PROCESSING);
        goto start_done;
    }
    int bits_per_pixel = _color_mode_to_depth(ws_xml_get_node_text(color_processing));
    switch (bits_per_pixel) {
    case 1:
      scanner->scan_parameters.format = SANE_FRAME_GRAY;
      scanner->scan_parameters.depth = 1;
      break;
    case 4:
    case 8:
      scanner->scan_parameters.format = SANE_FRAME_GRAY;
      scanner->scan_parameters.depth = 8;
      break;
    case 16:
      scanner->scan_parameters.format = SANE_FRAME_GRAY;
      scanner->scan_parameters.depth = 16;
      break;
    case 24:
    case 32:
      scanner->scan_parameters.format = SANE_FRAME_RGB;
      scanner->scan_parameters.depth = 8;
      break;
    case 48:
    case 64:
      scanner->scan_parameters.format = SANE_FRAME_RGB;
      scanner->scan_parameters.depth = 16;
      break;
    default:
        DBG (DBG_error, "Strange bits_per_pixel %d\n", bits_per_pixel);
        goto start_done;
    }
    WsXmlNodeH pixels_per_line = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_PIXELS_PER_LINE, 1);
    if (!pixels_per_line) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_PIXELS_PER_LINE);
        goto start_done;
    }
    scanner->scan_parameters.pixels_per_line = atoi(ws_xml_get_node_text(pixels_per_line));
    WsXmlNodeH number_of_lines = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_NUMBER_OF_LINES, 1);
    if (!number_of_lines) {
        DBG (DBG_error, "No %s in create_scan_job response\n", WSD_NUMBER_OF_LINES);
        goto start_done;
    }
    scanner->scan_parameters.lines = atoi(ws_xml_get_node_text(number_of_lines));
    scanner->scan_parameters.bytes_per_line = (scanner->scan_parameters.pixels_per_line * scanner->scan_parameters.depth) / 8;

    scanner->scanning = 1;
    scanner->cancel_request = 0;

#if 0
    scanner->scan_parameters.bytes_per_line;
    scanner->scan_parameters.depth;
    scanner->scan_parameters.format;
    scanner->scan_parameters.last_frame;
    scanner->scan_parameters.lines;
    scanner->scan_parameters.pixels_per_line;
    /* Handle cancel request */
    if (scanner->cancel_request) {
        return sanei_wsdscan_on_cancel (scanner);
    }
#endif

start_done:
    if (request)
        wsd_request_destroy(request);
    wsd_options_destroy(options);
    return status;
}


/**
 * Obtain the current scan parameters. The returned parameters are guaranteed
 * to be accurate between the time a scan has been started (sane_start() has
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
    DBG (DBG_info_sane, "sane_get_parameters()\n");

    WsdScanner *scanner = (WsdScanner *)handle;
    SANE_Int resolution;

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
            params->last_frame = SANE_TRUE;
        } else {

            /* Calculate appropriate values from option settings */
            DBG (DBG_info_sane, "sane_get_parameters from option values\n");
            resolution = atoi(scanner->val[OPT_RESOLUTION].s);
            DBG (DBG_info_sane, "  resolution %d\n", resolution);
            SANE_Int colors = scanner->val[OPT_COLOR].w;
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
        DBG(DBG_info_sane," format = %d\n", params->format);
        DBG(DBG_info_sane," last_frame = %d\n", params->last_frame);
        DBG(DBG_info_sane," bytes_per_line = %d\n", params->bytes_per_line);
        DBG(DBG_info_sane," pixels_per_line = %d\n", params->pixels_per_line);
        DBG(DBG_info_sane," lines = %d\n", params->lines);
        DBG(DBG_info_sane," depth = %d\n", params->depth);

    } else {

        DBG(DBG_error,"sane_get_parameters() no params argument, no values returned\n");
        return SANE_STATUS_INVAL;

    }

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
    static u_buf_t *image_buf = NULL;
    static char *image_data = NULL;
    static int image_size = 0;
    WsdScanner *scanner = (WsdScanner *)handle;
    WsdRequest *request = NULL;
    SANE_Status status = SANE_STATUS_GOOD;
    request_opt_t *options = _create_request_options();

    DBG(DBG_info_sane, "sane_read(): requested %d bytes\n", max_len);

    if (image_data) {
        int size = MIN(max_len, image_size);
        DBG (DBG_info_sane, "sane_read(): have image_data, copying %d bytes\n", size);
        if (size == 0) {
            u_buf_free(image_buf);
            image_buf = NULL;
            image_data = NULL;
            image_size = 0;
            *len = 0;
            scanner->scanning = SANE_FALSE;
            DBG( DBG_info_sane, "sane_read(): EOF\n");
            return SANE_STATUS_EOF;
        }
        memcpy(buf, image_data, size);
        *len = size;
        image_size -= size;
        image_data += size;
        DBG( DBG_info_sane, "sane_read(): return %d bytes\n", *len);
        return status;
    }
    /* No reading if not scanning */
    if (!scanner->scanning) {
        *len = 0;
        status = SANE_STATUS_IO_ERROR; /* SANE standard does not allow a SANE_STATUS_INVAL return */
        goto sane_read_done;
    }
    DBG (DBG_info_sane, "sane_read(): no image_data, checking status\n");

    status = _get_status(scanner);
    if (status != SANE_STATUS_DEVICE_BUSY) {
        DBG( DBG_error, "Scanner is not busy in sane_read()\n");
        goto sane_read_done;
    }
    WsdScanJob scan_job = {
        .id = scanner->job_id,
        .token = scanner->job_token,
        .document_name = "document.name"
    };
    DBG (DBG_info_sane, "sane_read(): RetrieveImage\n");
    request = wsd_action_retrieve_image(scanner->client, options, &scan_job, &image_buf);
    if (!request) {
        DBG (DBG_error, "sane_start(): create_scan_job failed\n");
        status = SANE_STATUS_IO_ERROR;
        goto sane_read_done;
    }
    WsXmlNodeH node = wsd_response_node(request);
    WsXmlNodeH scan_data = ws_xml_find_in_tree(node, XML_NS_WDP_SCAN, WSD_SCAN_DATA, 1);
    if (!scan_data) {
        DBG (DBG_error, "No %s in RetrieveImageResponse\n", WSD_SCAN_DATA);
        status = SANE_STATUS_INVAL;
        goto sane_read_done;
    }
    /* <xop:Include href="cid:id6"/> */
    WsXmlNodeH xop_include = ws_xml_find_in_tree(scan_data, XML_NS_XOP, WSD_XOP_INCLUDE, 1);
    if (!xop_include) {
        DBG (DBG_error, "No xop:%s in %s\n", WSD_XOP_INCLUDE, WSD_SCAN_DATA);
        status = SANE_STATUS_INVAL;
        goto sane_read_done;
    }
    WsXmlAttrH xop_href = ws_xml_find_node_attr(xop_include, NULL, WSD_XOP_HREF);
    if (!xop_href) {
        DBG (DBG_error, "No %s attribute in %s\n", WSD_XOP_HREF, WSD_XOP_INCLUDE);
        status = SANE_STATUS_INVAL;
        goto sane_read_done;
    }
    char *mtom_href = ws_xml_get_attr_value(xop_href);
    if (strncmp(mtom_href, WSD_XOP_CID, WSD_XOP_CID_LEN)) {
        DBG (DBG_error, "No %s prefix attribute in '%s'\n", WSD_XOP_CID, mtom_href);
        status = SANE_STATUS_INVAL;
        goto sane_read_done;
    }
    DBG (DBG_info_sane, "sane_read(): have %d bytes of image_data\n", image_size);
    image_data = u_buf_ptr(image_buf);
    image_size = u_buf_len(image_buf);
    FILE *output = fopen("output.jpeg", "w+");
    fwrite(image_data, image_size, 1, output);
    fclose(output);
    status = SANE_STATUS_GOOD;
sane_read_done:
    if (request)
        wsd_request_destroy(request);
    if (image_buf && (image_data == NULL)) {
        u_buf_free(image_buf);
        image_buf = NULL;
    }
    wsd_options_destroy(options);

    return status;
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
