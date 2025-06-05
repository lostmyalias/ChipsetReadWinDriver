// driver.c
#include <ntddk.h>
#include <wdf.h>
#include "driver.h" // Ensure this has DRIVER_CONTEXT, IOCTLs, and all forward declarations

// Forward declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;
NTSTATUS MyPciScannerScanBus0AndPrint(WDFDEVICE Device);


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDFDRIVER hDriver;
    PWDFDEVICE_INIT pDeviceInit = NULL;
    WDFDEVICE hControlDevice = NULL;       // Handle for the control device
    PDRIVER_CONTEXT driverContext = NULL;  // Pointer to our driver context

    WDF_OBJECT_ATTRIBUTES driverAttributes; // For driver context
    WDF_DEVICE_PNP_CAPABILITIES pnpCaps;    // For setting PnP capabilities on the control device

    DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\MyPciScanner");
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\MyPciScanner");
    DECLARE_CONST_UNICODE_STRING(sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)");
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFQUEUE hQueue = NULL; // Initialize

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DriverEntry - IN\n"));

    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd); // EvtDriverDeviceAdd is a stub
    config.EvtDriverUnload = EvtDriverUnload;
    config.DriverInitFlags = WdfDriverInitNonPnpDriver;   // Mark driver as Non-PnP

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&driverAttributes, DRIVER_CONTEXT);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &driverAttributes,   // Driver context attributes
        &config,
        &hDriver
    );
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDriverCreate failed %!STATUS!\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: WDFDRIVER created successfully.\n"));

    driverContext = WdfGetDriverContext(hDriver);
    driverContext->ControlDevice = NULL;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Allocating DeviceInit for Control Device...\n"));
    pDeviceInit = WdfControlDeviceInitAllocate(hDriver, &sddlString);
    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfControlDeviceInitAllocate failed %!STATUS!\n", status));
        return status; // WDF calls EvtDriverUnload for hDriver if DriverEntry returns error after WdfDriverCreate success
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DeviceInit allocated.\n"));

    status = WdfDeviceInitAssignName(pDeviceInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceInitAssignName failed %!STATUS!\n", status));
        WdfDeviceInitFree(pDeviceInit);
        pDeviceInit = NULL;
        return status;
    }

    WdfDeviceInitSetCharacteristics(pDeviceInit, FILE_DEVICE_SECURE_OPEN, TRUE);
    WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_UNKNOWN);

    status = WdfDeviceCreate(&pDeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &hControlDevice);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreate for control device failed %!STATUS!\n", status));
        // pDeviceInit is consumed/freed by WdfDeviceCreate
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Control Device %wZ created successfully.\n", &ntDeviceName));

    driverContext->ControlDevice = hControlDevice;

    // Set PnP capabilities on the created WDFDEVICE handle (hControlDevice)
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
    pnpCaps.Removable = WdfTrue;      // Helps SCM understand it can be "stopped"
    pnpCaps.NoDisplayInUI = WdfTrue;  // Optionally hide from Device Manager
    WdfDeviceSetPnpCapabilities(hControlDevice, &pnpCaps); // Uses hControlDevice
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: PnP Capabilities set for control device.\n"));


    status = WdfDeviceCreateSymbolicLink(hControlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreateSymbolicLink failed %!STATUS!\n", status));
        // EvtDriverUnload will handle deleting hControlDevice from context
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Symbolic link %wZ created successfully.\n", &symbolicLinkName));

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Configuring I/O Queue for Control Device...\n"));
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    status = WdfIoQueueCreate(hControlDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &hQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfIoQueueCreate failed %!STATUS!\n", status));
        // EvtDriverUnload will handle deleting hControlDevice from context
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: I/O Queue created successfully.\n"));

    WdfControlFinishInitializing(hControlDevice);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Control device initialization finished.\n"));

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DriverEntry - OUT (SUCCESS)\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
EvtDriverDeviceAdd( // Minimal stub
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtDriverDeviceAdd called (unexpected for this driver's configuration)\n"));
    return STATUS_SUCCESS;
}

VOID
EvtIoDeviceControl( // Should be unchanged
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtIoDeviceControl - IN, Ioctl=0x%X\n", IoControlCode));

    switch (IoControlCode)
    {
    case IOCTL_MYPCISCANNER_SCAN_BUS0:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: IOCTL_SCAN_BUS0 received.\n"));
        status = MyPciScannerScanBus0AndPrint(device);
        WdfRequestCompleteWithInformation(Request, status, 0);
        break;

    default:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: Unknown IOCTL 0x%X\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestComplete(Request, status);
        break;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtIoDeviceControl - OUT (status %!STATUS!)\n", status));
}

NTSTATUS
MyPciScannerScanBus0AndPrint( // Added UNREFERENCED_PARAMETER
    _In_ WDFDEVICE Device
)
{
    UNREFERENCED_PARAMETER(Device); // Fix for C4100

    NTSTATUS status = STATUS_SUCCESS;
    PCI_COMMON_CONFIG pciConfig;
    ULONG busNumber = 0;
    ULONG deviceNumber;
    ULONG functionNumber;
    ULONG bytesRead = 0;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: MyPciScannerScanBus0AndPrint - IN\n"));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Starting scan of PCI Bus %d\n", busNumber));

    for (deviceNumber = 0; deviceNumber < PCI_MAX_DEVICES; deviceNumber++)
    {
        for (functionNumber = 0; functionNumber < PCI_MAX_FUNCTIONS; functionNumber++)
        {
            RtlZeroMemory(&pciConfig, sizeof(PCI_COMMON_CONFIG));
            pciConfig.VendorID = PCI_INVALID_VENDORID;
            bytesRead = 0;

            // ** SIMULATION / TESTING HOOK **
            if (busNumber == 0 && deviceNumber == 2 && functionNumber == 0) {
                pciConfig.VendorID = PCI_VENDOR_ID_INTEL;
                pciConfig.DeviceID = 0x1234;
                pciConfig.HeaderType = 0x00;
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: SIMULATING Intel device at 0:%d:%d\n", deviceNumber, functionNumber));
                bytesRead = sizeof(PCI_COMMON_CONFIG);
            }
            else if (busNumber == 0 && deviceNumber == 3 && functionNumber == 0) {
                pciConfig.VendorID = PCI_VENDOR_ID_AMD;
                pciConfig.DeviceID = 0x5678;
                pciConfig.HeaderType = 0x00;
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: SIMULATING AMD device at 0:%d:%d\n", deviceNumber, functionNumber));
                bytesRead = sizeof(PCI_COMMON_CONFIG);
            }
            // ** END SIMULATION / TESTING HOOK **

            if (bytesRead == 0 || pciConfig.VendorID == PCI_INVALID_VENDORID) {
                if (functionNumber == 0) {
                    break;
                }
                continue;
            }

            if (pciConfig.VendorID == PCI_VENDOR_ID_INTEL ||
                pciConfig.VendorID == PCI_VENDOR_ID_AMD ||
                pciConfig.VendorID == PCI_VENDOR_ID_ATI_AMD)
            {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "MyPciScannerDriver: Found Device on Bus %d, Device %d, Function %d\n",
                    busNumber, deviceNumber, functionNumber));
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "  VendorID: 0x%04X, DeviceID: 0x%04X\n",
                    pciConfig.VendorID, pciConfig.DeviceID));
            }

            if (functionNumber == 0 && !(pciConfig.HeaderType & 0x80)) {
                break;
            }
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: PCI Bus %d scan complete.\n", busNumber));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: MyPciScannerScanBus0AndPrint - OUT\n"));
    return status;
}

VOID
EvtDriverUnload( // With context cleanup
    _In_ WDFDRIVER Driver
)
{
    PAGED_CODE();

    PDRIVER_CONTEXT driverContext = NULL;
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - IN\n"));

    driverContext = WdfGetDriverContext(Driver);
    if (driverContext != NULL && driverContext->ControlDevice != NULL) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "MyPciScannerDriver: EvtDriverUnload - Deleting control device (Handle: %p)\n", driverContext->ControlDevice));
        WdfObjectDelete(driverContext->ControlDevice);
        driverContext->ControlDevice = NULL;
    }
    else {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, // Changed to ERROR_LEVEL for more visibility if this happens
            "MyPciScannerDriver: EvtDriverUnload - ControlDevice handle in context is NULL or context is NULL (nothing to delete here).\n"));
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - OUT\n"));
}
