//           device.c - Device manager
//          

#include "device.h"
#include "console.h"
#include "halt.h"
#include "string.h"
#include "error.h"

//           COMPILE-TIME PARAMETER DEFAULTS
//          

#ifndef NDEV
#define NDEV 16
#endif

//           EXPORTED GLOBAL VARIABLES
//          

char devmgr_initialized;

//           INTERNAL TYPE DEFINITIONS
//          

struct device {
    const char * name;
    int (*openfn)(struct io_intf ** ioptr, void * aux);
    void * aux;
};

//           INTERNAL GLOBAL VARIABLES
//          

struct device devtab[NDEV];

//           EXPORTED FUNCTION DEFINITIONS
//          

void devmgr_init(void) {
    devmgr_initialized = 1;
}

int device_register (
    const char * name,
    int (*openfn)(struct io_intf ** ioptr, void * aux),
    void * aux)
{    
    int devno = 0;
    int instno = 0;

    assert (name != NULL);
    assert (openfn != NULL);

	//           Find empty slot in devtab

	while (devno < NDEV) {
        if (devtab[devno].name == NULL)
            break;
        
        if (strcmp(devtab[devno].name, name) == 0)              //major num and minor num
            instno += 1;

		devno += 1;
    }

	if (devno == NDEV)
		panic("Too many devices (increase NDEV)");
	
    devtab[devno].name = name;
	devtab[devno].openfn = openfn;
	devtab[devno].aux = aux;

    debug("%s%d registered (openfn=%p,aux=%p)", name, instno, openfn, aux);

	return instno;
}

int device_open (
    struct io_intf ** ioptr,
    const char * name,
    int instno)
{
	int devno = 0;
	int k = 0;

	trace("%s(name=%s,instno=%d)", __func__, name, instno);

	//           Find instno-th instance of device in devtab

	while (devno < NDEV) {
		if (devtab[devno].openfn != NULL &&
			strcmp(devtab[devno].name, name) == 0)
		{
			if (k == instno)
				break;
			else
				k += 1;
		}

		devno += 1;
	}

	if (devno == NDEV) {
		debug("Device %s%d not found", name, instno);
		return -ENODEV;
	} else
		debug("%s%d is device %d", name, instno, devno);

	//           Call driver's open function

	return devtab[devno].openfn(ioptr, devtab[devno].aux);
}
