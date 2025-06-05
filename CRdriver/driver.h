// driver.h

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <devioctl.h> // For CTL_CODE

// IOCTL Definition
#define MYPCISCANNER_DEVICE_TYPE 0x8000
#define IOCTL_MYPCISCANNER_SCAN_BUS0 CTL_CODE( \
    MYPCISCANNER_DEVICE_TYPE, \
    0x800, \
    METHOD_BUFFERED, \
    FILE_ANY_ACCESS \
)

// PCI Vendor IDs
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_VENDOR_ID_AMD   0x1022
#define PCI_VENDOR_ID_ATI_AMD 0x1002 
#define PCI_MAX_DEVICES       32
#define PCI_MAX_FUNCTIONS     8
#define PCI_INVALID_VENDORID  0xFFFF

// NEW: Define a context structure for the WDFDRIVER object
typedef struct _DRIVER_CONTEXT {
    WDFDEVICE ControlDevice; // To store the handle of our control device
} DRIVER_CONTEXT, * PDRIVER_CONTEXT;

// NEW: Declare an accessor function for the driver context
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, WdfGetDriverContext)

// Forward declarations
DRIVER_INITIALIZE DriverEntry;
// EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;
NTSTATUS MyPciScannerScanBus0AndPrint(WDFDEVICE Device);