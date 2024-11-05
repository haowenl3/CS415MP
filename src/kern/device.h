//           device.h - Device manager
//          

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "io.h"

extern void devmgr_init(void);
extern char devmgr_initialized;

//           int device_register (
//               const char * name,
//               int (*openfn)(struct io_intf ** ioptr, void * aux),
//               void * aux);
//          
//           Registers a device with the device manager. Argument /name/ is the name of
//           the device. Argument /openfn/ is a function to be used to open the device.
//           The /ioptr/ argument to /openfn/ is a pointer to an io_intf struct pointer,
//           to be filled in by /openfn/ with a pointer to a valid struct io_intf. The
//           /aux/ argument to device_register is passed as the second argument to
//           /openfn/ when it is called.
//           The device_register should be called by a device driver's attach function to
//           register the device with the system.
//           The device_register function returns a non-negative instance number on
//           success and a negative error number if an error occurs.


extern int device_register (
    const char * name,
    int (*openfn)(struct io_intf ** ioptr, void * aux),
    void * aux);

//           int device_open (
//               struct io_intf ** ioptr,
//               const char * name,
//               int instno);
//          
//           Open a device. The /ioptr/ argument is a pointer to a struct io_intf pointer,
//           which will be filled in by device_open if successful. Argument /name/ is a
//           device name as provided by the driver when the device was registered using
//           device_register. Argument /instno/ is the specific instance of the device.
//           Devices of the same type are assigned an instance number, in order of
//           registration, starting at 0.
//           Returns 0 on success, in which case *ioptr points to a valid struct io_intf.
//           Return a negative error number on error.

extern int device_open (
    struct io_intf ** ioptr,
    const char * name,
    int instno);

#endif