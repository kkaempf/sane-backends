/*
 * File:   pieusb_buffer.c
 * Author: Jan Vleeshouwers
 *
 */

#define DEBUG_DECLARE_ONLY

#include "pieusb.h"
#include "pieusb_buffer.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

/*static void buffer_output_state(struct Pieusb_Read_Buffer* buffer);*/

/* Auxiliary */
static void buffer_update_read_index(struct Pieusb_Read_Buffer* buffer, int increment);
/*
SANE_Byte* buffer_pack(const SANE_Uint* data, SANE_Int size, SANE_Int depth, SANE_Int* pack_size);
SANE_Uint* buffer_unpack(const SANE_Byte* data, SANE_Int size, SANE_Int depth, SANE_Int* unpack_size);
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

#include "pieusb_specific.h"
#include "pieusb_buffer.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <endian.h>

/* When creating the release backend, make complains about unresolved external
 * le16toh, although it finds the include <endian.h> */
#if __BYTE_ORDER == __LITTLE_ENDIAN
 #define le16toh(x) (x)
#else
 #define le16toh(x) __bswap_16 (x)
#endif

/* READER */

/**
 * Initialize the buffer.
 * A scanner has a Pieusb_Read_Buffer struct as one of its members.
 *
 * @param buffer the buffer to initialize
 * @param width number of pixels on a line (row)
 * @param height number of lines in the buffer (pixels in a column)
 * @param colors bitmap specifying the colors in the scanned data (bitmap: 0000 IBGR)
 * @param depth number of bits of a color
 * @param bigendian how to store multi-byte values: bigendian if true
 * @param maximum_size maximum size of buffer (-1 = size of image)
 */
void
pieusb_buffer_create(struct Pieusb_Read_Buffer* buffer, SANE_Int width, SANE_Int height, SANE_Byte color_spec, SANE_Byte depth)
{
    int k, result;
    char buffer_name[L_tmpnam];
    unsigned int buffer_size_bytes;
    unsigned char g;

    /* Base parameters */
    buffer->width = width;
    buffer->height = height;
    buffer->colors = 0;
    if (color_spec & 0x01) { buffer->color_index_red = 0; buffer->colors++; } else { buffer->color_index_red = -1; }
    if (color_spec & 0x02) { buffer->color_index_green = 1; buffer->colors++; } else { buffer->color_index_green = -1; }
    if (color_spec & 0x04) { buffer->color_index_blue = 2; buffer->colors++; } else { buffer->color_index_blue = -1; }
    if (color_spec & 0x08) { buffer->color_index_infrared = 3; buffer->colors++; } else { buffer->color_index_infrared = -1; }
    if (buffer->colors == 0) {
        DBG(DBG_error, "pieusb_buffer_create(): no colors specified\n");
        return;
    }
    buffer->depth = depth;
    if (depth < 1 || depth > 16) {
        DBG(DBG_error, "pieusb_buffer_create(): unsupported depth %d\n", depth);
        return;
    }
    buffer->packing_density = (depth == 1) ? 8 : 1; /* These are all the situations we have */

    /* Derived*/
    buffer->packet_size_bytes = (buffer->depth * buffer->packing_density + 7) / 8;
    buffer->line_size_packets = (buffer->width + buffer->packing_density -1) / buffer->packing_density;
    buffer->line_size_bytes = buffer->line_size_packets * buffer->packet_size_bytes;
    buffer->image_size_bytes = buffer->colors * buffer->height * buffer->line_size_bytes;

    /* Create empty file */
    tmpnam(buffer_name); /* see man tmpnam => add O_EXCL */
    buffer->data_file = open(buffer_name, O_RDWR | O_CREAT | O_EXCL | O_TRUNC, (mode_t)0600);
    if (buffer->data_file == -1) {
        buffer->data = NULL;
	perror("pieusb_buffer_create(): error opening image buffer file");
        return;
    }
    /* Stretch the file size */
    buffer_size_bytes = buffer->width * buffer->height * buffer->colors * sizeof(SANE_Uint);
    result = lseek(buffer->data_file, buffer_size_bytes-1, SEEK_SET);
    if (result == -1) {
	close(buffer->data_file);
        buffer->data = NULL;
	perror("pieusb_buffer_create(): error calling fseek() to 'stretch' the file");
        return;
    }
    /* Write one byte at the end */
    g = 0x00;
    result = write(buffer->data_file, &g, 1);
    if (result < 0) {
	close(buffer->data_file);
        buffer->data = NULL;
	perror("pieusb_buffer_create(): error writing a byte at the end of the file");
        return;
    }
    /* Create memory map */
    buffer->data = mmap(NULL, buffer_size_bytes, PROT_WRITE | PROT_READ, MAP_SHARED, buffer->data_file, 0);
    if (buffer->data == MAP_FAILED) {
        close(buffer->data_file);
        buffer->data = NULL;
        perror("pieusb_buffer_create(): error mapping file");
        return;
    }

    /* Reading and writing */
    buffer->p_read = calloc(buffer->colors,sizeof(SANE_Uint*));
    buffer->p_write = calloc(buffer->colors,sizeof(SANE_Uint*));
    for (k = 0; k < buffer->colors; k++) {
        buffer->p_write[k] = buffer->data + k * buffer->height * buffer->width;
        buffer->p_read[k] = buffer->p_write[k];
    }
    buffer->read_index[0] = 0;
    buffer->read_index[1] = 0;
    buffer->read_index[2] = 0;
    buffer->read_index[3] = 0;

    /* Statistics */
    buffer->bytes_read = 0;
    buffer->bytes_written = 0;
    buffer->bytes_unread = 0;

    DBG(DBG_info,"pieusb: Read buffer created: w=%d h=%d ncol=%d depth=%d in file %s\n",
      buffer->width, buffer->height, buffer->colors, buffer->depth, buffer_name);
}

/**
 * Delete buffer and free its resources
 *
 * @param buffer
 */
void
pieusb_buffer_delete(struct Pieusb_Read_Buffer* buffer)
{
    munmap(buffer->data, buffer->image_size_bytes);
    /* ftruncate(buffer->data_file,0); Can we delete given a file-descriptor? */
    close(buffer->data_file);
    free(buffer->p_read);
    free(buffer->p_write);
    buffer->data = 0;
    buffer->width = 0;
    buffer->height = 0;
    buffer->depth = 0;
    buffer->colors = 0;
    buffer->packing_density = 0;

    DBG(DBG_info,"pieusb: Read buffer deleted\n");
}

/**
 * Add a line to the reader buffer, for the given color.
 * The buffer checks and decides how to interpret the data.
 *
 * @param buffer
 * @param color Color code for line
 * @param line
 * @param size Number of bytes in line
 * @return 1 if successful, 0 if not
 */
SANE_Int
pieusb_buffer_put_single_color_line(struct Pieusb_Read_Buffer* buffer, SANE_Byte color, void* line, SANE_Int size)
{

    SANE_Int c, k, m, n;

    DBG(DBG_info, "pieusb_buffer_put_single_color_line() entered\n");

    /* Check index code */
    c = -1;
    switch (color) {
        case 'R':
            c = buffer->color_index_red;
            break;
        case 'G':
            c = buffer->color_index_green;
            break;
        case 'B':
            c = buffer->color_index_blue;
            break;
        case 'I':
            c = buffer->color_index_infrared;
            break;
    }
    if (c == -1) {
        DBG(DBG_error, "pieusb_buffer_put_single_color_line(): color '%c' not specified when buffer was created\n", color);
        return 0;
    }
    DBG(DBG_info, "pieusb_buffer_put_single_color_line() line color = %d (0=R, 1=G, 2=B, 3=I)\n",c);

    /* Check line size (for a line with a single color) */
    if (buffer->line_size_bytes != size) {
        DBG(DBG_error, "pieusb_buffer_put_single_color_line(): incorrect line size, expecting %d, got %d\n", buffer->line_size_bytes, size);
        return 0;
    }

    /* The general approach for all densities and packet sizes
     * - process packet_size_bytes at a time
     * - use packing_density to decode the full packet into separate values
     * - now save these values as neighbouring pixels on the current line
     * Use custom code for the 1-byte and 2-byte single sample cases,
     * because the general approach is a bit overkill for them.
     */

    if (buffer->packet_size_bytes == 1 && buffer->packing_density == 1) {
        uint8_t* p_packet = (uint8_t*)line;
        n = 0;
        while (n < size) {
            /* Get next packet data & store in buffer */
            *buffer->p_write[c]++ = *p_packet++;
            n++;
        }
    } else if (buffer->packet_size_bytes == 2 && buffer->packing_density == 1) {
        uint16_t* p_packet = (uint16_t*)line;
        n = 0;
        while (n < size) {
            /* Get next packet data & store in buffer */
            *buffer->p_write[c]++ = le16toh (*p_packet++);
            n += 2;
        }
    } else {
        uint8_t* p_packet = (uint8_t*)line;
        uint8_t *packet = (uint8_t *)alloca(buffer->packet_size_bytes * sizeof(uint8_t));
        SANE_Uint val;
        uint8_t mask = ~(0xFF >> buffer->depth); /* byte with depth most significant bits set */
        n = 0;
        while (n < size) {
            /* Get next packet data */
            for (k = 0; k < buffer->packet_size_bytes; k++) packet[k] = *p_packet++;
            /* Unpack packing_density samples from packet. Of course,
             * buffer->depth * packing_density <= # bits in packet.
             * That is checked at buffer creation. */
            for (k = 0; k < buffer->packing_density; k++) {
                /* Take 1st depth bits and store in val */
                val = (packet[0] & mask) >> (8-buffer->depth);
                /* Now shift packet bytes depth bits left */
                for (m = 0; m < buffer->packet_size_bytes; m++) {
                    /* Shift left one sample */
                    packet[m] <<= buffer->depth;
                    if (m < buffer->packet_size_bytes-1) {
                        /* If there are more bytes, insert 1st depth bits of next byte */
                        packet[m] |= (packet[m+1] >> (8-buffer->depth));
                    }
                }
                /* Store in buffer */
                *buffer->p_write[c]++ = val;
            }
            n += buffer->packet_size_bytes;
        }
    }

    /* Update state & statistics */
    buffer->bytes_written += size;
    buffer->bytes_unread += size;

    /* Output current buffer state */
    /* buffer_output_state(buffer); */

    return 1;
}

/**
 * Write line of full color pixels to the buffer.
 *
 * @param buffer Read buffer
 * @param pixel array of full color pixel data
 * @param size Number of bytes in the line
 * @return 1 if successful, 0 if not
 */
/**
 *
 */
SANE_Int
pieusb_buffer_put_full_color_line(struct Pieusb_Read_Buffer* buffer, void* line, int size)
{
    int k, c, m, n;

    DBG(DBG_info,"pieusb_buffer_put_full_color_line() entered\n");

    /* Check line size */
    if (buffer->line_size_bytes * buffer->colors != size) {
        DBG(DBG_error, "pieusb_buffer_put_full_color_line(): incorrect line size, expecting %d, got %d\n", buffer->line_size_bytes * buffer->colors, size);
        return 0;
    }

    /* The general approach for all densities and packet sizes
     * - process packet_size_bytes at a time
     * - use packing_density to decode the full packet into separate values
     * - now save these values as neighbouring pixels on the current line
     * Use custom code for the 1-byte and 2-byte single sample cases,
     * because the general approach is a bit overkill for them.
     */

    if (buffer->packet_size_bytes == 1 && buffer->packing_density == 1) {
        uint8_t* p_packet = (uint8_t*)line;
        n = 0;
        while (n < size) {
            /* Get next packet data & store in buffer */
            for (c = 0; c < buffer->colors; c++) {
                *buffer->p_write[c]++ = *p_packet++;
                n++;
            }
        }
    } else if (buffer->packet_size_bytes == 2 && buffer->packing_density == 1) {
        uint16_t* p_packet = (uint16_t*)line;
        n = 0;
        while (n < size) {
            /* Get next packet data & store in buffer */
            for (c = 0; c < buffer->colors; c++) {
                *buffer->p_write[c]++ = le16toh (*p_packet++);
                n += 2;
            }
        }
    } else {
        uint8_t* p_packet = (uint8_t*)line;
        uint8_t *packet = (uint8_t *)alloca(buffer->packet_size_bytes * sizeof(uint8_t));
        SANE_Uint val;
        uint8_t mask = ~(0xFF >> buffer->depth); /* byte with depth most significant bits set */
        /* DBG(DBG_info,"buffer_put_full_color_line(): mask %02x\n",mask); */
        n = 0;
        while (n < size) {
            /* Get next packet data */
            for (c = 0; c < buffer->colors; c++) {
                for (k = 0; k < buffer->packet_size_bytes; k++) {
                    packet[k] = *p_packet++;
                    /* DBG(DBG_info,"buffer_put_full_color_line(): packet[%d] = %02x\n",k,packet[k]); */
                }
                /* Unpack packing_density samples from packet. Of course,
                 * buffer->depth * packing_density <= # bits in packet.
                 * That is checked at buffer creation. */
                for (k = 0; k < buffer->packing_density; k++) {
                    /* Take 1st depth bits and store in val */
                    val = (packet[0] & mask) >> (8-buffer->depth);
                    /* DBG(DBG_info,"buffer_put_full_color_line(): val[%d] = %02x\n",k,val); */
                    /* Now shift packet bytes depth bits left */
                    for (m = 0; m < buffer->packet_size_bytes; m++) {
                        /* Shift left one sample */
                        packet[m] <<= buffer->depth;
                        /* DBG(DBG_info,"buffer_put_full_color_line(): shift packet[%d] = %02x\n",m,packet[m]); */
                        if (m < buffer->packet_size_bytes-1) {
                            /* If there are more bytes, insert 1st depth bits of next byte */
                            packet[m] |= (packet[m+1] >> (8-buffer->depth));
                            /* DBG(DBG_info,"buffer_put_full_color_line(): shift packet[%d] = %02x\n",m,packet[m]); */
                        }
                    }
                    /* Store in buffer */
                    *buffer->p_write[c]++ = val;
                }
                n += buffer->packet_size_bytes;
            }
        }
    }

    /* Update state & statistics */
    buffer->bytes_written += size;
    buffer->bytes_unread += size;

    /* Output current buffer state */
    /* buffer_output_state(buffer); */

    return 1;
}

/**
 * Return bytes from the buffer. Do not mind pixel boundaries.
 * Since the image data is organized in color planes, return bytes from the
 * planes in the defined order. Take care to return multi-byte values after
 * each other. Pack unpacked values.
 *
 * @param buffer Buffer to return bytes from.
 * @param data Byte array to return bytes in
 * @param max_len Maximum number of bytes returned
 * @param len Actual number of bytes returned
 */
void
pieusb_buffer_get(struct Pieusb_Read_Buffer* buffer, SANE_Byte* data, SANE_Int max_len, SANE_Int* len)
{
    SANE_Byte *pdata;
    SANE_Int n, i, n_bits, N;

    DBG(DBG_info,"pieusb_buffer_get() entered\n");

    /* Read from the p_read locations */
    pdata = data;
    n = 0;
    N = buffer->width * buffer->height;
    /* Determine bytes to return */
    if (buffer->packet_size_bytes == 1 && buffer->packing_density == 1) {
        /* Single byte values in buffer */
        while (n < max_len && buffer->bytes_read < buffer->image_size_bytes) {
            /* Return byte*/
            *pdata++ = *(buffer->data + N*buffer->read_index[0] + buffer->width*buffer->read_index[1] + buffer->read_index[2]) & 0xFF;
            /* Update read indices */
            buffer_update_read_index(buffer,1);
            /* Update number of bytes read */
            buffer->bytes_read++;
            n++;
        }
    } else if (buffer->packet_size_bytes == 1 && buffer->packing_density == 8) {
        /* Unpacked bits in buffer: repack */
        while (n < max_len && buffer->bytes_read < buffer->image_size_bytes) {
            uint8_t val = 0;
            /* How many bits to pack? At the end of a line it may be less than 8 */
            n_bits = 8;
            if (buffer->width - buffer->read_index[2] < 8) {
                n_bits = buffer->width - buffer->read_index[2];
            }
            /* Pack n_bits samples from same color plane */
            for (i = 0; i < n_bits; i++) {
                if (*(buffer->data + N*buffer->read_index[0] + buffer->width*buffer->read_index[1] + buffer->read_index[2] + i) > 0) {
                    val |= (0x80 >> i);
                }
            }
            /* Return byte */
            *pdata++ = val;
            /* Update read indices */
            buffer_update_read_index(buffer,n_bits);
            /* Update number of bytes read */
            buffer->bytes_read++;
            n++;
        }
    } else if (buffer->packet_size_bytes == 2) {
        /* Two-byte values in buffer */
        while (n < max_len && buffer->bytes_read < buffer->image_size_bytes) {
            /* Pointer to byte to return */
            SANE_Uint val = *(buffer->data + N*buffer->read_index[0] + buffer->width*buffer->read_index[1] + buffer->read_index[2]);
            /* Return byte */
            if (buffer->read_index[3] == 0) {
                *pdata++ = *((SANE_Byte*)(&val));
            } else {
                *pdata++ = *((SANE_Byte*)(&val)+1);
            }
            /* Update read indices */
            buffer_update_read_index(buffer,1);
            /* Update number of bytes read */
            buffer->bytes_read++;
            n++;
        }
    } else {
        /* not implemented */
        DBG(DBG_error, "buffer_put(): paccket size & density of %d/%d not implementd\n", buffer->packet_size_bytes, buffer->packing_density);
        return;
    }
    *len = n;

    /* Update statistics */
    buffer->bytes_unread -= n;

    /* Output current buffer state */
    /* buffer_output_state(buffer); */
}

static void buffer_update_read_index(struct Pieusb_Read_Buffer* buffer, int increment)
{
    /* Update read indices
     * [3] = byte-index in 2-byte value: increased first, if we have 2-byte data
     * [2] = index of pixel on line: increased after color plane
     * [1] = index of line: increased after line is complete
     * [0] = color index: increased first since SANE requires full color pixels */
    if (buffer->read_index[3] == 0 && buffer->packet_size_bytes == 2) {
        buffer->read_index[3] = 1;
    } else {
        buffer->read_index[3] = 0;
        buffer->read_index[0]++;
        if (buffer->read_index[0] == buffer->colors) {
            buffer->read_index[0] = 0;
            buffer->read_index[2] += increment;
            if (buffer->read_index[2] >= buffer->width) {
                buffer->read_index[2] = 0;
                buffer->read_index[1]++;
            }
        }
    }
}

#if 0
/*
 * buffer_output_state
 *
 * debug function
 *
 */

static void buffer_output_state(struct Pieusb_Read_Buffer* buffer)
{
    SANE_Int line_size;
    SANE_Int N, k, loc[4];

    line_size = buffer->line_size_bytes * buffer->colors; /* Full line size in bytes */

    DBG(DBG_info,"Buffer data\n");
    DBG(DBG_info,"  width/height/colors/depth = %d %d %d %d (buffer size %d)\n",
        buffer->width, buffer->height, buffer->colors, buffer->depth, buffer->image_size_bytes);

    /* Summary */
    N = buffer->width * buffer->height;
    for (k = 0; k < buffer->colors; k++) {
        loc[k] = buffer->p_read[k] - buffer->data - k*N;
    }
    for (k = buffer->colors; k < 4; k++) {
        loc[k] = 0;
    }
    DBG(DBG_info,"  reading at: lines = %d:%d:%d:%d\n", loc[0], loc[1], loc[2], loc[3]);
    for (k = 0; k < buffer->colors; k++) {
        loc[k] = buffer->p_write[k] - buffer->data - k*N;
    }
    for (k = buffer->colors; k < 4; k++) {
        loc[k] = 0;
    }
    DBG(DBG_info,"  writing at: lines = %d:%d:%d:%d\n", loc[0], loc[1], loc[2], loc[3]);

    /* Progress */
    double fdata = (double)buffer->bytes_unread/buffer->image_size_bytes*100;
    double fread = (double)buffer->bytes_read/buffer->image_size_bytes*100;
    double fwritten = (double)buffer->bytes_written/buffer->image_size_bytes*100;
    DBG(DBG_info,"  byte counts: image = %d, data = %d (%.0f%%), read = %d (%.0f%%), written = %d (%.0f%%)\n",
        buffer->image_size_bytes, buffer->bytes_unread, fdata, buffer->bytes_read, fread, buffer->bytes_written, fwritten);
    DBG(DBG_info,"  line counts: image = %.1f, data = %.1f, read = %.1f, written = %.1f\n",
        (double)buffer->image_size_bytes/line_size, (double)buffer->bytes_unread/line_size, (double)buffer->bytes_read/line_size, (double)buffer->bytes_written/line_size);

}
#endif
