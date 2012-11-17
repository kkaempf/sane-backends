/* 
 * File:   pieusb_buffer.h
 * Author: jan
 *
 * Created on September 1, 2012, 3:30 PM
 */

/*
 * Read buffer
 * 
 * Data obtained from the scanner cannot be presented to the frontend immediately.
 * The scanner returns data in the 'index' color format, which means it returns
 * data in batches which contain a single color of a scan line.
 * 
 * These must finally be converted into the SANE data format (data for a single
 * pixel in consecutive bytes). Apart from that, sane_read() must be able to
 * return any amount of data bytes.
 * 
 * In between, data processing may be necessary, usually requiring the whole
 * image to be available.
 * 
 * To accommodate all this, the buffer stores all samples as 16-bit values, even
 * if the original values are 8-bit or even 1 bit. This is a waste of space, but
 * makes processing much easier, and it is only temporary.
 * 
 * The read buffer is constructed by a call to buffer_create(), which initializes
 * the buffer based on width, height, number of colors and depth. The buffer
 * contains bytes organized in lines, where each line consists of a fixed number
 * of pixels, each pixel of a fixed number of colors, and each color of a fixed
 * number of bits (or bytes). 
 * 
 * Reading from the buffer only requires incrementing a byte pointer. Reading
 * should check that data is returned from complete lines. The buffer maintains
 * a read pointer and a current read line index.
 * 
 * Writing data into the buffer is somewhat more complex since the data must be
 * converted. The buffer maintains current write line indices for each color in
 * the buffer, and derives a free line index and a incomplete line index from
 * these. The free line index indicates the first line which contains no data
 * yet, the incomplete line index indicates the first line which data is incomplete
 * (at least one color has been written).
 * 
 * Multi-color data with a bit depth of 1 are packed in single color bytes, so
 * the data obtained from the scanner does not need conversion.
 */

#ifndef PIEUSB_BUFFER_H
#define	PIEUSB_BUFFER_H

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

static void buffer_create(struct Pieusb_Read_Buffer* buffer, SANE_Int width, SANE_Int height, SANE_Byte colors, SANE_Byte depth);
static void buffer_delete(struct Pieusb_Read_Buffer* buffer);
static SANE_Int buffer_put_single_color_line(struct Pieusb_Read_Buffer* buffer, SANE_Byte color, void* line, SANE_Int size);
static SANE_Int buffer_put_full_color_line(struct Pieusb_Read_Buffer* buffer, void* line, int size);
static void buffer_get(struct Pieusb_Read_Buffer* buffer, SANE_Byte* data, SANE_Int max_len, SANE_Int* len);
static void buffer_output_state(struct Pieusb_Read_Buffer* buffer);

/* Auxiliary */
static void buffer_update_read_index(struct Pieusb_Read_Buffer* buffer, int increment);
/*
SANE_Byte* buffer_pack(const SANE_Uint* data, SANE_Int size, SANE_Int depth, SANE_Int* pack_size);
SANE_Uint* buffer_unpack(const SANE_Byte* data, SANE_Int size, SANE_Int depth, SANE_Int* unpack_size);
*/

#endif	/* PIEUSB_BUFFER_H */

