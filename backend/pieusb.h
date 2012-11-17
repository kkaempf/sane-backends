/* 
 * File:   pieusb.h
 * Author: jan
 *
 * Created on July 22, 2012, 2:22 PM
 */

#ifndef PIEUSB_H
#define	PIEUSB_H

SANE_Status sane_pieusb_init (SANE_Int * version_code, SANE_Auth_Callback authorize);
void sane_pieusb_exit (void);
SANE_Status sane_pieusb_get_devices (const SANE_Device *** device_list, SANE_Bool local_only);
SANE_Status sane_pieusb_open (SANE_String_Const devicename, SANE_Handle * handle);
void sane_pieusb_close (SANE_Handle handle);
const SANE_Option_Descriptor *sane_pieusb_get_option_descriptor (SANE_Handle handle, SANE_Int option);
SANE_Status sane_pieusb_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action, void *value, SANE_Int * info);
SANE_Status sane_pieusb_get_parameters (SANE_Handle handle, SANE_Parameters * params);
SANE_Status sane_pieusb_start (SANE_Handle handle);
SANE_Status sane_pieusb_read (SANE_Handle handle, SANE_Byte * data, SANE_Int max_length, SANE_Int * length);
void sane_pieusb_cancel (SANE_Handle handle);
SANE_Status sane_pieusb_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking);
SANE_Status sane_pieusb_get_select_fd (SANE_Handle handle, SANE_Int * fd);

#endif	/* PIEUSB_H */

