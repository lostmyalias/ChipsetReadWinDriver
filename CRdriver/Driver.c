// driver.c

#include <ntddk.h>
#include <wdf.h>
#include "driver.h"  // Ensure this has DRIVER_CONTEXT, IOCTLs, and all forward declarations

// Forward declarations (must match driver.h)
DRIVER_INITIALIZE DriverEntry;
// EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd; // No longer needed as a primary callback
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;
NTSTATUS MyPciScannerScanBus0AndPrint(WDFDEVICE Device);

// Minimal stub for EvtDriverDeviceAdd if WDF_DRIVER_CONFIG_INIT still needs a non-NULL function pointer.
// However, for WDF_NO_EVENT_CALLBACK, this might not even be referenced.
// If WDF_DRIVER_CONFIG_INIT allows NULL with WDF_NO_EVENT_CALLBACK, this can be removed.
// For now, keeping a safe stub.
NTSTATUS EvtDriverDeviceAdd_Stub(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit);


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDFDRIVER hDriver;
    WDF_OBJECT_ATTRIBUTES driverAttributes;
    PWDFDEVICE_INIT pDeviceInit = NULL;
    WDFDEVICE hControlDevice = NULL;
    PDRIVER_CONTEXT driverContext = NULL;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFQUEUE hQueue = NULL;

    DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\MyPciScanner");
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\MyPciScanner");
    DECLARE_CONST_UNICODE_STRING(sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)");

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DriverEntry - IN\n"));

    // Initialize driver config for a Non-PnP driver
    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK); // Use WDF_NO_EVENT_CALLBACK for EvtDriverDeviceAdd
    config.EvtDriverUnload = EvtDriverUnload;
    config.DriverInitFlags = WdfDriverInitNonPnpDriver; // Explicitly mark as non-PnP

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&driverAttributes, DRIVER_CONTEXT);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &driverAttributes, // For driver context
        &config,
        &hDriver
    );
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDriverCreate failed %!STATUS!\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: WdfDriverCreate succeeded\n"));

    driverContext = WdfGetDriverContext(hDriver);
    driverContext->ControlDevice = NULL;

    pDeviceInit = WdfControlDeviceInitAllocate(hDriver, &sddlString);
    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfControlDeviceInitAllocate failed %!STATUS!\n", status));
        return status; // WDF will call EvtDriverUnload for hDriver
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Allocated DeviceInit for control device\n"));

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

    // Note: WdfDeviceSetPnpCapabilities is typically for PnP devices or if you need to
    // fine-tune capabilities. For a basic non-PnP control device primarily identified
    // by WdfDriverInitNonPnpDriver, explicitly setting Removable=WdfTrue might not be
    // strictly necessary for unload if the non-PnP configuration is correct.
    // We can omit it for now to keep it simpler, as per ChatGPT's latest research finding.

    status = WdfDeviceCreate(&pDeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &hControlDevice);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreate failed %!STATUS!\n", status));
        // pDeviceInit is consumed
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Control device created\n"));

    driverContext->ControlDevice = hControlDevice;

    status = WdfDeviceCreateSymbolicLink(hControlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreateSymbolicLink failed %!STATUS!\n", status));
        // Let EvtDriverUnload (called by WDF if DriverEntry returns error now) handle deletion
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Symbolic link created\n"));

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    status = WdfIoQueueCreate(hControlDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &hQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfIoQueueCreate failed %!STATUS!\n", status));
        // Let EvtDriverUnload handle deletion
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: I/O queue created\n"));

    WdfControlFinishInitializing(hControlDevice);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Control device initialization finished\n"));

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: DriverEntry - OUT (SUCCESS)\n"));
    return STATUS_SUCCESS;
}

// Minimal stub for EvtDriverDeviceAdd, which WDF_DRIVER_CONFIG_INIT needs a placeholder for,
// even if WDF_NO_EVENT_CALLBACK is used for the PnP aspects.
// This will not be called if DriverInitFlags includes WdfDriverInitNonPnpDriver and
// WDF_NO_EVENT_CALLBACK was used for the EvtDriverDeviceAdd parameter.
NTSTATUS
EvtDriverDeviceAdd_Stub( // Renamed to avoid conflict if EvtDriverDeviceAdd is still declared elsewhere
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtDriverDeviceAdd_Stub called (should be unexpected)\n"));
    return STATUS_NOT_SUPPORTED; // Or STATUS_SUCCESS; driver isn't handling PnP adds.
}


VOID
EvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    PAGED_CODE();
    PDRIVER_CONTEXT driverContext = WdfGetDriverContext(Driver);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - IN\n"));

    if (driverContext != NULL && driverContext->ControlDevice != NULL) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "MyPciScannerDriver: EvtDriverUnload - Deleting control device (Handle: %p)\n", driverContext->ControlDevice));
        WdfObjectDelete(driverContext->ControlDevice);
        driverContext->ControlDevice = NULL;
    }
    else {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: EvtDriverUnload - ControlDevice handle in context is NULL or context is NULL (nothing to delete here).\n"));
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - OUT\n"));
}

// EvtIoDeviceControl and MyPciScannerScanBus0AndPrint remain the same as your last working version.
// Ensure they are present below.
VOID
EvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtIoDeviceControl - IN (IoControlCode=0x%X)\n", IoControlCode));

    if (IoControlCode == IOCTL_MYPCISCANNER_SCAN_BUS0) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "MyPciScannerDriver: Scan bus 0 IOCTL received.\n")); // Corrected string
        status = MyPciScannerScanBus0AndPrint(device);
        WdfRequestCompleteWithInformation(Request, status, 0);
    }
    else {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: Unknown IOCTL 0x%X\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestComplete(Request, status);
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtIoDeviceControl - OUT (Status=%!STATUS!)\n", status));
}

NTSTATUS
MyPciScannerScanBus0AndPrint(
    _In_ WDFDEVICE Device
)
{
    UNREFERENCED_PARAMETER(Device);

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

