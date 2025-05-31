// driver.c

#include <ntddk.h>
#include <wdf.h>
#include "driver.h"

// Forward declaration for our EvtDriverDeviceAdd callback
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDFDRIVER hDriver; // Handle to the WDFDRIVER object

    PWDFDEVICE_INIT pDeviceInit = NULL; // Pointer to device initialization structure
    WDFDEVICE hControlDevice = NULL;    // Handle to our control device

    // Strings for our device name, SDDL, and symbolic link
    DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\MyPciScanner");
    DECLARE_CONST_UNICODE_STRING(sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)"); // System, Admin, Authenticated Users
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\MyPciScanner");

    WDF_IO_QUEUE_CONFIG ioQueueConfig; // Structure for configuring the I/O queue
    WDFQUEUE hQueue;                   // Handle to the created I/O queue

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DriverEntry - IN\n"));

    // Initialize driver config structure, specifying EvtDriverUnload
    // EvtDriverDeviceAdd is still required by WDF_DRIVER_CONFIG_INIT,
    // but it will be a minimal stub as PnP won't use it to create our control device.
    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);
    config.EvtDriverUnload = EvtDriverUnload;

    // Create the WDFDRIVER object
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES, // Optional driver attributes
        &config,                  // Driver Config Info
        &hDriver                  // Store the WDFDRIVER handle
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfDriverCreate failed %!STATUS!\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: WDFDRIVER created successfully.\n"));

    // ---- Create Control Device Object directly in DriverEntry ----

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Allocating DeviceInit for Control Device...\n"));
    // For a control device, we allocate a WDFDEVICE_INIT structure.
    // WdfControlDeviceInitAllocate takes the WDFDRIVER handle.
    pDeviceInit = WdfControlDeviceInitAllocate(
        hDriver, // Use the WDFDRIVER handle we just got
        &sddlString
    );

    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfControlDeviceInitAllocate failed %!STATUS!\n", status));
        // No WDF objects to cleanup here other than WDFDRIVER which framework handles on DriverEntry failure.
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DeviceInit allocated.\n"));

    // Assign a name to the device object.
    status = WdfDeviceInitAssignName(pDeviceInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfDeviceInitAssignName failed %!STATUS!\n", status));
        WdfDeviceInitFree(pDeviceInit); // Free the allocated pDeviceInit
        return status;
    }

    // Set device characteristics (optional but good practice for control devices)
    WdfDeviceInitSetCharacteristics(pDeviceInit, FILE_DEVICE_SECURE_OPEN, TRUE);
    WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_UNKNOWN); // Common for control devices

    // Create the WDFDEVICE object for our control device.
    status = WdfDeviceCreate(&pDeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &hControlDevice);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfDeviceCreate for control device failed %!STATUS!\n", status));
        // pDeviceInit is freed by WdfDeviceCreate on failure if it was successfully used by it.
        // If WdfDeviceCreate itself fails before consuming pDeviceInit, pDeviceInit might still need freeing.
        // However, WDF framework typically handles pDeviceInit if WdfDeviceCreate is called with it.
        // For safety, if WdfDeviceCreate fails, pDeviceInit is usually considered consumed or invalid.
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Control Device %wZ created successfully.\n", &ntDeviceName));

    // Create the symbolic link for user-mode access.
    status = WdfDeviceCreateSymbolicLink(hControlDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfDeviceCreateSymbolicLink failed %!STATUS!\n", status));
        WdfObjectDelete(hControlDevice); // Clean up the created WDFDEVICE
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Symbolic link %wZ created successfully.\n", &symbolicLinkName));

    // Configure and create a default I/O queue for the control device.
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Configuring I/O Queue for Control Device...\n"));
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &ioQueueConfig,
        WdfIoQueueDispatchSequential
    );
    ioQueueConfig.EvtIoDeviceControl = EvtIoDeviceControl; // Your existing callback

    status = WdfIoQueueCreate(
        hControlDevice,
        &ioQueueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &hQueue // Store queue handle if needed, or WDF_NO_HANDLE
    );
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: WdfIoQueueCreate failed %!STATUS!\n", status));
        // Symbolic link is cleaned up when hControlDevice is deleted.
        WdfObjectDelete(hControlDevice); // This will also delete associated symbolic link and default queue.
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: I/O Queue created successfully.\n"));

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: DriverEntry - Control Device Setup Complete - OUT\n"));
    return status; // STATUS_SUCCESS from WdfDriverCreate if everything else succeeded
}

NTSTATUS
EvtDriverDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    // This driver creates its control device in DriverEntry.
    // EvtDriverDeviceAdd is provided to satisfy WDF_DRIVER_CONFIG_INIT requirements
    // but is not expected to be called for creating our primary control device
    // with the DefaultInstall INF approach.
    // If it were called for some other unexpected PnP device, 
    // we simply do nothing and return success or an appropriate status.

    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverDeviceAdd - Called (unexpectedly for control device creation path)\n"));

    // Returning STATUS_SUCCESS means we "handled" it, but created no device.
    // Returning STATUS_NOT_SUPPORTED might also be appropriate if this driver
    // strictly should not handle any PnP devices.
    return STATUS_SUCCESS;
}

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
    WDFDEVICE device = WdfIoQueueGetDevice(Queue); // Get the WDFDEVICE from the queue

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtIoDeviceControl - IN\n"));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Received IOCTL code 0x%X\n", IoControlCode));

    switch (IoControlCode)
    {
    case IOCTL_MYPCISCANNER_SCAN_BUS0:
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: IOCTL_MYPCISCANNER_SCAN_BUS0 received!\n"));

        // Call our PCI scanning function
        status = MyPciScannerScanBus0AndPrint(device); // Pass the WDFDEVICE along

        WdfRequestCompleteWithInformation(Request, status, 0); // BytesWritten = 0
        break;
    }

    default:
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: Received unknown IOCTL code 0x%X\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestComplete(Request, status);
        break;
    }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtIoDeviceControl - OUT (request completed with %!STATUS!)\n", status));
}

NTSTATUS
MyPciScannerScanBus0AndPrint(
    _In_ WDFDEVICE Device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PCI_COMMON_CONFIG pciConfig; // Structure to hold PCI configuration data
    ULONG busNumber = 0;         // We are scanning bus 0
    ULONG deviceNumber;
    ULONG functionNumber;
    ULONG bytesRead; // To store how many bytes GetBusData read

    UNREFERENCED_PARAMETER(Device);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: MyPciScannerScanBus0AndPrint - IN\n"));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: Starting scan of PCI Bus %d\n", busNumber));

    //
    // TODO LATER: Obtain BUS_INTERFACE_STANDARD here.
    // For now, the GetBusData call below will be a conceptual placeholder.
    // Example:
    // BUS_INTERFACE_STANDARD PciBusInterface;
    // status = GetPciBusInterface(Device, &PciBusInterface); // A function we'd have to write
    // if (!NT_SUCCESS(status)) {
    //     KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MyPciScannerDriver: Failed to get PCI bus interface: %!STATUS!\n", status));
    //     return status;
    // }
    //

    for (deviceNumber = 0; deviceNumber < PCI_MAX_DEVICES; deviceNumber++)
    {
        for (functionNumber = 0; functionNumber < PCI_MAX_FUNCTIONS; functionNumber++)
        {
            // Initialize pciConfig for safety, especially VendorID to check if a device exists
            RtlZeroMemory(&pciConfig, sizeof(PCI_COMMON_CONFIG));
            pciConfig.VendorID = PCI_INVALID_VENDORID; // Assume no device initially

            // --- Placeholder for GetBusData ---
            // This is where we would call the GetBusData function from the BUS_INTERFACE_STANDARD
            // to read the PCI configuration header for the current bus, device, and function.
            //
            // Conceptual call (cannot be compiled directly without the interface):
            // bytesRead = PciBusInterface.GetBusData(
            // PciBusInterface.Context, // Context from the interface
            // PCI_WHICHSPACE_CONFIG, // Indicate we're reading PCI config space
            // &pciConfig,            // Buffer to store the data
            // 0,                     // Offset in config space (0 for header)
            // sizeof(PCI_COMMON_CONFIG) // Number of bytes to read
            // );
            //
            // if (bytesRead == 0 || bytesRead == PCI_INVALID_VENDORID) { // Or other ways to check if read failed for non-existent device
            //    // No device at this slot/function, or error reading
            //    if (functionNumber == 0) break; // Optimization: if func 0 doesn't exist, higher funcs likely won't either for this device
            //    continue;
            // }
            //
            // --- End Placeholder for GetBusData ---

             // ** SIMULATION / TESTING HOOK (Remove after GetBusData is implemented) **
            // To test the logic without actual hardware reads yet, you could manually fill pciConfig
            // for a known device, e.g., if you know you have an Intel device at 0:2:0
            if (busNumber == 0 && deviceNumber == 2 && functionNumber == 0) {
                pciConfig.VendorID = PCI_VENDOR_ID_INTEL;
                pciConfig.DeviceID = 0x1234; // Example Device ID
                pciConfig.HeaderType = 0x00; // Simulate Type 0, single function for simplicity
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: SIMULATING Intel device at 0:%d:%d\n", deviceNumber, functionNumber));
                bytesRead = sizeof(PCI_COMMON_CONFIG); // Simulate a successful read for the whole structure
            }
            else if (busNumber == 0 && deviceNumber == 3 && functionNumber == 0) {
                pciConfig.VendorID = PCI_VENDOR_ID_AMD;
                pciConfig.DeviceID = 0x5678; // Example Device ID
                pciConfig.HeaderType = 0x00; // Simulate Type 0, single function
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: SIMULATING AMD device at 0:%d:%d\n", deviceNumber, functionNumber));
                bytesRead = sizeof(PCI_COMMON_CONFIG); // Simulate a successful read
            }
            else {
                // For all other slots in this simulation, act as if no device is present
                // or GetBusData returned nothing.
                bytesRead = 0; // Simulate no device or failed read
                pciConfig.VendorID = PCI_INVALID_VENDORID; // Ensure VendorID reflects no device
            }
            // ** END SIMULATION / TESTING HOOK **


            // If no data was read OR VendorID is 0xFFFF, it means no device is present at that slot/function.
            if (bytesRead == 0 || pciConfig.VendorID == PCI_INVALID_VENDORID) { // Corrected check
                if (functionNumber == 0) {
                    // Optimization: if function 0 of a device does not exist,
                    // then no other functions for that device number exist.
                    break; // Break from functions loop, continue to next deviceNumber
                }
                continue; // Continue to the next functionNumber
            }

            // Check if it's an Intel or AMD device
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
                // You could print more info from pciConfig here, e.g., pciConfig.HeaderType, pciConfig.ClassCode, etc.
            }

            // If it's not a multi-function device (check HeaderType bit 7),
            // we don't need to check functions 1-7 for this deviceNumber.
            // The PCI_COMMON_CONFIG contains HeaderType. Bit 7 (0x80) indicates multi-function.
            // This check should also be after a successful GetBusData.
            if (functionNumber == 0 && !(pciConfig.HeaderType & 0x80)) {
                break; // Break from functions loop, continue to next deviceNumber
            }
        } // End of functions loop
    } // End of devices loop

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: PCI Bus %d scan complete.\n", busNumber));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: MyPciScannerScanBus0AndPrint - OUT\n"));
    return status;
}

VOID
EvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - IN\n"));

    // If you initialized WPP Tracing, you would clean it up here:
    // PDRIVER_OBJECT driverObject = WdfDriverWdmGetDriverObject(Driver);
    // WPP_CLEANUP(driverObject);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "MyPciScannerDriver: EvtDriverUnload - OUT\n"));
    return;
}