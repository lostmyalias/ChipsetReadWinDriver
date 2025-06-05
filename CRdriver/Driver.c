// Driver.c

#include <ntddk.h>
#include <wdf.h>
#include "driver.h"  // contains DRIVER_CONTEXT and IOCTL definitions

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_DRIVER_UNLOAD     EvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;

//--------------------------------------------------
// DriverEntry: Initialize WDF driver and create control device
//--------------------------------------------------
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
    // Grant system, admin, and users read/write to the device:
    DECLARE_CONST_UNICODE_STRING(sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)");

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: DriverEntry - IN\n"));

    // Initialize driver config; no PnP DeviceAdd needed, but WDF_DRIVER_CONFIG_INIT requires something.
    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);
    config.EvtDriverUnload = EvtDriverUnload;
    config.DriverInitFlags = WdfDriverInitNonPnpDriver; // Mark as non-PnP

    // Initialize driver context for storing our control device handle
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&driverAttributes, DRIVER_CONTEXT);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &driverAttributes,
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

    // Retrieve and initialize our driver context
    driverContext = WdfGetDriverContext(hDriver);
    driverContext->ControlDevice = NULL;

    // ---- Create the control device object ----
    pDeviceInit = WdfControlDeviceInitAllocate(hDriver, &sddlString);
    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfControlDeviceInitAllocate failed %!STATUS!\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Allocated DeviceInit for control device\n"));

    // Assign a name to the device
    status = WdfDeviceInitAssignName(pDeviceInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceInitAssignName failed %!STATUS!\n", status));
        WdfDeviceInitFree(pDeviceInit);
        return status;
    }

    // Optionally set characteristics
    WdfDeviceInitSetCharacteristics(pDeviceInit, FILE_DEVICE_SECURE_OPEN, TRUE);
    WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_UNKNOWN);

    // Create the framework device object for the control device
    status = WdfDeviceCreate(&pDeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &hControlDevice);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreate failed %!STATUS!\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Control device created\n"));

    // Store the handle in our context so we can delete it on unload
    driverContext->ControlDevice = hControlDevice;

    // Create symbolic link for user-mode access
    status = WdfDeviceCreateSymbolicLink(hControlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfDeviceCreateSymbolicLink failed %!STATUS!\n", status));
        WdfObjectDelete(hControlDevice);
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Symbolic link created\n"));

    // Configure and create a default I/O queue for IOCTLs
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    status = WdfIoQueueCreate(hControlDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &hQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "MyPciScannerDriver: WdfIoQueueCreate failed %!STATUS!\n", status));
        WdfObjectDelete(hControlDevice);
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: I/O queue created\n"));

    // Inform WDF that the control device is fully initialized (no more config calls allowed)
    WdfControlFinishInitializing(hControlDevice);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: Control device initialization finished\n"));

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: DriverEntry - OUT\n"));
    return STATUS_SUCCESS;
}

//--------------------------------------------------
// EvtDriverDeviceAdd: Not used for non-PnP control device
//--------------------------------------------------
NTSTATUS
EvtDriverDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);
    // For a strictly non-PnP driver we should not actually get here.
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtDriverDeviceAdd called unexpectedly\n"));
    return STATUS_SUCCESS;
}

//--------------------------------------------------
// EvtIoDeviceControl: Handle IOCTL_MYPCISCANNER_SCAN_BUS0
//--------------------------------------------------
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
            "MyPciScannerDriver: Scan bus 0 IOCTL\n"));
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

//--------------------------------------------------
// MyPciScannerScanBus0AndPrint: (unchanged)
//--------------------------------------------------
NTSTATUS
MyPciScannerScanBus0AndPrint(
    _In_ WDFDEVICE Device
)
{
    // [Scanning code as in original sample]
    // (Omitted here for brevity; it prints PCI devices.)
    UNREFERENCED_PARAMETER(Device);
    // ... [PCI simulation code] ...
    return STATUS_SUCCESS;
}

//--------------------------------------------------
// EvtDriverUnload: Clean up control device
//--------------------------------------------------
VOID
EvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    PAGED_CODE();

    PDRIVER_CONTEXT driverContext = WdfGetDriverContext(Driver);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtDriverUnload - IN\n"));

    if (driverContext != NULL && driverContext->ControlDevice != NULL) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "MyPciScannerDriver: Deleting control device\n"));
        WdfObjectDelete(driverContext->ControlDevice);
        driverContext->ControlDevice = NULL;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "MyPciScannerDriver: EvtDriverUnload - OUT\n"));
}
