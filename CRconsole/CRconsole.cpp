#include <windows.h>
#include <stdio.h>
#include <winioctl.h> // For IOCTL definitions and DeviceIoControl

// Define the IOCTL code exactly as it is in your driver.h
// It's best practice to have this in a shared header file,
// but for now, we can redefine it here.
// Ensure MYPCISCANNER_DEVICE_TYPE is also defined if it's not standard.
// For simplicity, if your driver.h defines MYPCISCANNER_DEVICE_TYPE as, say, 0x8000:
#define MYPCISCANNER_DEVICE_TYPE 0x8000
#define IOCTL_MYPCISCANNER_SCAN_BUS0 CTL_CODE( \
    MYPCISCANNER_DEVICE_TYPE, \
    0x800, \
    METHOD_BUFFERED, \
    FILE_ANY_ACCESS \
)

int main() {
    HANDLE hDevice;
    BOOL bResult;
    DWORD bytesReturned;

    printf("Attempting to open device \\\\.\\MyPciScanner...\n");

    // Step 1: Open a handle to the driver
    hDevice = CreateFile(
        L"\\\\.\\MyPciScanner",         // Symbolic link to our driver's device
        GENERIC_READ | GENERIC_WRITE,   // Desired access
        0,                              // Share mode (0 for exclusive access)
        NULL,                           // Security attributes
        OPEN_EXISTING,                  // Creation disposition
        0,                              // Flags and attributes (0 for synchronous I/O)
        NULL                            // Template file
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device. Error code: %lu\n", GetLastError());
        // Before running this, make sure your "CRdriver" service is started!
        // You can start it manually with: sc start CRdriver (from an admin prompt)
        return 1;
    }

    printf("Device opened successfully. Handle: %p\n", hDevice);

    // Step 2: Send the IOCTL
    printf("Sending IOCTL_MYPCISCANNER_SCAN_BUS0...\n");

    // For this IOCTL, we are not sending any input data and not expecting output data
    // in these buffers. The driver prints to DbgView.
    bResult = DeviceIoControl(
        hDevice,                       // Handle to the device
        IOCTL_MYPCISCANNER_SCAN_BUS0,  // IOCTL code
        NULL,                          // Input buffer (none for this IOCTL)
        0,                             // Input buffer size
        NULL,                          // Output buffer (none for this IOCTL)
        0,                             // Output buffer size
        &bytesReturned,                // Bytes returned
        NULL                           // Overlapped structure (NULL for synchronous)
    );

    if (!bResult) {
        printf("DeviceIoControl failed. Error code: %lu\n", GetLastError());
    }
    else {
        printf("IOCTL sent successfully.\n");
        // Check DbgView for your driver's output!
    }

    // Step 3: Close the handle
    printf("Closing device handle...\n");
    if (CloseHandle(hDevice)) {
        printf("Device handle closed successfully.\n");
    }
    else {
        printf("Failed to close device handle. Error code: %lu\n", GetLastError());
    }

    // We'll add service start/stop later.
    // For now, stop the service manually if you started it manually:
    // sc stop CRdriver (from an admin prompt)

    printf("Console app finished. Press Enter to exit.\n");
    getchar();

    return 0;
}