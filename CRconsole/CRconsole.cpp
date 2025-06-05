// CRconsole.cpp

#include <windows.h>
#include <stdio.h>    // For printf, wprintf, getchar
#include <winioctl.h> // For IOCTL definitions and DeviceIoControl
#include <winsvc.h>   // For Service Control Manager APIs
#include <tchar.h>    // For _TCHAR, _tprintf (though we'll use wprintf for consistency here)

// Define the IOCTL code exactly as it is in your driver.h
#define MYPCISCANNER_DEVICE_TYPE 0x8000
#define IOCTL_MYPCISCANNER_SCAN_BUS0 CTL_CODE( \
    MYPCISCANNER_DEVICE_TYPE, \
    0x800, \
    METHOD_BUFFERED, \
    FILE_ANY_ACCESS \
)

// Helper function to print error messages
void PrintError(const wchar_t* prefix, DWORD dwError) {
    LPVOID lpMsgBuf;
    FormatMessageW( // Use FormatMessageW for wide strings
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&lpMsgBuf, // Cast to LPWSTR
        0, NULL);
    wprintf(L"%s: %s (Error Code: %lu)\n", prefix, (LPWSTR)lpMsgBuf, dwError);
    LocalFree(lpMsgBuf);
}

// Function to wait for a service to reach a specific state
BOOL WaitForServiceState(SC_HANDLE hService, const wchar_t* serviceName, DWORD dwDesiredState, DWORD dwTimeoutMs = 30000) {
    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwBytesNeeded;
    DWORD dwStartTime = GetTickCount();
    DWORD dwWaitTime;

    wprintf(L"Waiting for service '%s' to reach state %lu...\n", serviceName, dwDesiredState);

    while (TRUE) {
        if (!QueryServiceStatusEx(
            hService,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssStatus,
            sizeof(SERVICE_STATUS_PROCESS),
            &dwBytesNeeded))
        {
            PrintError(L"Error: QueryServiceStatusEx failed while waiting", GetLastError());
            return FALSE;
        }

        if (ssStatus.dwCurrentState == dwDesiredState) {
            wprintf(L"Service '%s' reached state %lu.\n", serviceName, dwDesiredState);
            return TRUE;
        }

        dwWaitTime = GetTickCount() - dwStartTime;
        if (dwWaitTime >= dwTimeoutMs) {
            wprintf(L"Error: Timeout waiting for service '%s' to reach state %lu (current state %lu)\n", serviceName, dwDesiredState, ssStatus.dwCurrentState);
            return FALSE;
        }

        // If it's in a pending state, wait a bit using the hint or a default
        // Ensure dwWaitHint is treated as a suggestion and capped to avoid overly long sleeps if the hint is huge.
        DWORD sleepInterval = 250; // Default sleep interval
        if (ssStatus.dwCurrentState == SERVICE_START_PENDING ||
            ssStatus.dwCurrentState == SERVICE_STOP_PENDING ||
            ssStatus.dwCurrentState == SERVICE_CONTINUE_PENDING ||
            ssStatus.dwCurrentState == SERVICE_PAUSE_PENDING) {
            if (ssStatus.dwWaitHint > 0) {
                sleepInterval = min(ssStatus.dwWaitHint, 3000); // Use wait hint, but cap it
            }
        }
        else {
            // Service is in a stable non-desired state, probably won't change without intervention
            wprintf(L"Service '%s' is in stable state %lu, not the desired state %lu. Will not reach desired state without intervention.\n", serviceName, ssStatus.dwCurrentState, dwDesiredState);
            return FALSE;
        }
        Sleep(sleepInterval);
    }
}

// Function to start the CRdriver service
BOOL StartDriverService() {
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    BOOL bSuccess = FALSE;
    const wchar_t* serviceName = L"CRdriver";

    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (hSCManager == NULL) {
        PrintError(L"Error: OpenSCManager failed", GetLastError());
        return FALSE;
    }

    hService = OpenService(hSCManager, serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (hService == NULL) {
        PrintError(L"Error: OpenService failed for CRdriver", GetLastError());
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwBytesNeeded;
    if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        PrintError(L"Error: QueryServiceStatusEx failed for CRdriver", GetLastError());
    }
    else {
        if (ssStatus.dwCurrentState == SERVICE_RUNNING) {
            wprintf(L"%s service is already running.\n", serviceName);
            bSuccess = TRUE;
        }
        else if (ssStatus.dwCurrentState == SERVICE_STOPPED || ssStatus.dwCurrentState == SERVICE_STOP_PENDING) {
            wprintf(L"Attempting to start %s service...\n", serviceName);
            if (!StartService(hService, 0, NULL)) {
                PrintError(L"Error: StartService failed for CRdriver", GetLastError());
            }
            else {
                wprintf(L"Service start initiated for %s. Waiting for service to run...\n", serviceName);
                if (WaitForServiceState(hService, serviceName, SERVICE_RUNNING)) {
                    wprintf(L"%s service started successfully.\n", serviceName);
                    bSuccess = TRUE;
                }
                else {
                    wprintf(L"%s service did not reach running state.\n", serviceName);
                }
            }
        }
        else {
            wprintf(L"%s service is in state %lu, not attempting to start.\n", serviceName, ssStatus.dwCurrentState);
        }
    }

    if (hService) CloseServiceHandle(hService);
    if (hSCManager) CloseServiceHandle(hSCManager);
    return bSuccess;
}

// Function to stop the CRdriver service
BOOL StopDriverService() {
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    BOOL bSuccess = FALSE;
    SERVICE_STATUS_PROCESS ssStatus; // Used for sending control and for waiting
    const wchar_t* serviceName = L"CRdriver";

    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (hSCManager == NULL) {
        PrintError(L"Error: OpenSCManager failed (for stop)", GetLastError());
        return FALSE;
    }

    hService = OpenService(hSCManager, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (hService == NULL) {
        PrintError(L"Error: OpenService failed for CRdriver (for stop)", GetLastError());
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    DWORD dwBytesNeeded;
    if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        PrintError(L"Error: QueryServiceStatusEx failed before stop for CRdriver", GetLastError());
    }
    else {
        if (ssStatus.dwCurrentState == SERVICE_STOPPED) {
            wprintf(L"%s service is already stopped.\n", serviceName);
            bSuccess = TRUE;
        }
        else if (ssStatus.dwCurrentState == SERVICE_RUNNING || ssStatus.dwCurrentState == SERVICE_PAUSE_PENDING || ssStatus.dwCurrentState == SERVICE_PAUSED) { // More states from which we can stop
            wprintf(L"Attempting to stop %s service (current state: %lu)...\n", serviceName, ssStatus.dwCurrentState);
            if (!ControlService(hService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssStatus)) { // Use LPSERVICE_STATUS for 2nd param
                PrintError(L"Error: ControlService (stop) failed for CRdriver", GetLastError());
            }
            else {
                wprintf(L"Service stop request sent for %s. Waiting for service to stop...\n", serviceName);
                if (WaitForServiceState(hService, serviceName, SERVICE_STOPPED)) {
                    wprintf(L"%s service stopped successfully.\n", serviceName);
                    bSuccess = TRUE;
                }
                else {
                    wprintf(L"%s service did not reach stopped state.\n", serviceName);
                }
            }
        }
        else {
            wprintf(L"%s service is in state %lu (e.g. STOP_PENDING), not attempting to send another stop signal.\n", serviceName, ssStatus.dwCurrentState);
            // If it's STOP_PENDING, we might just want to wait for it to become STOPPED
            if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                if (WaitForServiceState(hService, serviceName, SERVICE_STOPPED, 5000)) { // Shorter timeout for stop_pending
                    wprintf(L"%s service reached stopped state from pending.\n", serviceName);
                    bSuccess = TRUE;
                }
                else {
                    wprintf(L"%s service did not reach stopped state from pending.\n", serviceName);
                }
            }
        }
    }

    if (hService) CloseServiceHandle(hService);
    if (hSCManager) CloseServiceHandle(hSCManager);
    return bSuccess;
}


int main() {
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    BOOL bResult;
    DWORD bytesReturned;

    wprintf(L"--- Managing CRdriver Service ---\n");
    if (!StartDriverService()) {
        wprintf(L"Warning: Could not ensure CRdriver service is running. Attempting to open device anyway...\n");
        // For robust operation, you might exit here:
        // wprintf(L"CRITICAL: Could not start CRdriver service. Exiting.\n");
        // getchar(); // Keep console open
        // return 1; 
    }
    else {
        wprintf(L"CRdriver service management indicates service should be running.\n");
    }
    wprintf(L"---------------------------------\n\n");


    wprintf(L"Attempting to open device \\\\.\\MyPciScanner...\n");
    hDevice = CreateFile(
        L"\\\\.\\MyPciScanner",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, // Changed share mode slightly
        NULL, OPEN_EXISTING, 0, NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        PrintError(L"Failed to open device", GetLastError());
    }
    else {
        wprintf(L"Device opened successfully. Handle: %p\n", hDevice);
        wprintf(L"Sending IOCTL_MYPCISCANNER_SCAN_BUS0...\n");

        bResult = DeviceIoControl(
            hDevice, IOCTL_MYPCISCANNER_SCAN_BUS0,
            NULL, 0, NULL, 0,
            &bytesReturned, NULL
        );

        if (!bResult) {
            PrintError(L"DeviceIoControl failed", GetLastError());
        }
        else {
            wprintf(L"IOCTL sent successfully.\n");
        }

        wprintf(L"Closing device handle...\n");
        if (!CloseHandle(hDevice)) {
            PrintError(L"Failed to close device handle", GetLastError());
        }
        else {
            wprintf(L"Device handle closed successfully.\n");
        }
    }

    wprintf(L"\n--- Managing CRdriver Service for Shutdown ---\n");
    StopDriverService();
    wprintf(L"------------------------------------------\n");

    wprintf(L"Console app finished. Press Enter to exit.\n");
    getchar();
    return 0;
}