/*
 * File:   pieusb_buffer.h
 * Author: Jan Vleeshouwers
 *
 * Created on September 1, 2012, 3:30 PM
 */

#ifndef PIEUSB_BUFFER_H
#define	PIEUSB_BUFFER_H

#include "pieusb.h"
#include <sane/sanei_ir.h>

struct Pieusb_Read_Buffer
{
    SANE_Uint* data; /* image data - always store as 16 bit values */
    SANE_Int data_file; /* associated file if memory mapped */

    /* Buffer parameters */
    SANE_Int width; /* number of pixels on a line */
    SANE_Int height; /* number of lines in buffer */
    SANE_Int colors; /* number of colors in a pixel */
    SANE_Int depth; /* number of bits of a color */
    SANE_Int packing_density; /* number of single color samples packed together */

    /* Derived quantities
     * All derived quantities pertain to the image, not to the buffer */
    SANE_Int packet_size_bytes; /* number of bytes of a packet of samples = round_up(depth*packing_density/8) */
    SANE_Int line_size_packets; /* number of packets on a single color line = round-down((width+packing_density-1)/packing_density) */
    SANE_Int line_size_bytes; /* number of bytes on a single color line =  line_size_packets*packet_size_bytes */
    SANE_Int image_size_bytes; /* total number of bytes in the buffer (= colors * height * line_size_packets* packet_size_bytes) */
    SANE_Int color_index_red; /* color index of the red color plane (-1 if not used) */
    SANE_Int color_index_green; /* color index of the green color plane (-1 if not used) */
    SANE_Int color_index_blue; /* color index of the blue color plane (-1 if not used) */
    SANE_Int color_index_infrared; /* color index of the infrared color plane (-1 if not used) */

    /* Reading - byte oriented */
    SANE_Uint** p_read; /* array of pointers to next sample to read for each color plane */
    SANE_Int read_index[4]; /* location where to read next (color-index, height-index, width-index, byte-index) */
    SANE_Int bytes_read; /* number of bytes read from the buffer */
    SANE_Int bytes_unread; /* number of bytes not yet read from the buffer */
    SANE_Int bytes_written; /* number of bytes written to the buffer */

    /* Writing */
    SANE_Uint** p_write; /* array of pointers to next byte to write for each color plane */
};

void pieusb_buffer_get(struct Pieusb_Read_Buffer* buffer, SANE_Byte* data, SANE_Int max_len, SANE_Int* len);
void pieusb_buffer_create(struct Pieusb_Read_Buffer* buffer, SANE_Int width, SANE_Int height, SANE_Byte colors, SANE_Byte depth);
void pieusb_buffer_delete(struct Pieusb_Read_Buffer* buffer);
SANE_Int pieusb_buffer_put_full_color_line(struct Pieusb_Read_Buffer* buffer, void* line, int size);
SANE_Int pieusb_buffer_put_single_color_line(struct Pieusb_Read_Buffer* buffer, SANE_Byte color, void* line, SANE_Int size);

#endif	/* PIEUSB_BUFFER_H */

