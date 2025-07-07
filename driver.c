#include <ntddk.h>
#include <intrin.h>
#include <windef.h>

#define LOG_COMPONENT DPFLTR_IHVDRIVER_ID

#define PAGELK 0x4B4C45474150ULL
#define MZ_SIGNATURE 0x5A4D

// Offsets from ntoskrnl.exe File version 10.0.26100.4343
#define OFFSET_TO_PSPNOTIFYENABLEMASK 0xFD8A98
#define OFFSET_TO_PSPBAMEXTENSIONHOST 0xFC5CB8  
#define OFFSET_TO_EXGETEXTENSIONTABLE 0x441094
#define OFFSET_TO_EXRELEASEEXTENSIONTABLE 0x470860

typedef PVOID(*pExGetExtensionTable)(PVOID Extension);
typedef VOID(*pExReleaseExtensionTable)(PVOID Extension);

PCHAR g_NtoskrnlBase = NULL;
PCHAR g_nt_PspNotifyEnableMask = NULL;
PCHAR g_nt_PspBamExtensionHost = NULL;
pExGetExtensionTable g_ExGetExtensionTable = NULL;
pExReleaseExtensionTable g_ExReleaseExtensionTable = NULL;
PVOID g_Orignal_Bam_BampCreateProcessCallback = NULL;
DWORD g_Orignal_nt_PspNotifyEnableMask_val = 0;
BOOLEAN g_unhook = FALSE;

VOID ProcessNotifyCallbackEx2(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
    if (CreateInfo) {
        DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "Hello From ProcessNotifyCallbackEx2(), process created: PID=%u\n", (ULONG)(ULONG_PTR)ProcessId);
    }
}

// taken from https://www.unknowncheats.me/forum/3041967-post4.html
PVOID get_ntoskrnl_base() {
    ULONG_PTR entry = __readmsr(0xC0000082) & ~0xFFFULL;

    while (TRUE) {
        if (*(USHORT*)entry == MZ_SIGNATURE) {
            ULONG_PTR x;
            for (x = entry; x < entry + 0x400; x += sizeof(ULONG64)) {
                if (*(ULONG64*)x == PAGELK) {
                    return (PVOID)entry;
                }
            }
        }

        entry -= 0x1000;
    }

    return NULL;
}

BOOLEAN find_nt_globals() {
    g_NtoskrnlBase = get_ntoskrnl_base();
    PVOID BamExtensionTable = NULL;

    if (!g_NtoskrnlBase) {
        DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[-] Cant find ntoskrnl base\n");
        return FALSE;
    }

    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] ntoskrnl base @ 0x%llx\n", (unsigned long long)g_NtoskrnlBase);

    g_nt_PspNotifyEnableMask = g_NtoskrnlBase + OFFSET_TO_PSPNOTIFYENABLEMASK;

    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!PspNotifyEnableMask @ 0x%llx\n", (unsigned long long)g_nt_PspNotifyEnableMask);

    g_nt_PspBamExtensionHost = g_NtoskrnlBase + OFFSET_TO_PSPBAMEXTENSIONHOST;

    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!PspBamExtensionHost @ 0x%llx\n", (unsigned long long)g_nt_PspBamExtensionHost);

    g_ExGetExtensionTable = (pExGetExtensionTable)(g_NtoskrnlBase + OFFSET_TO_EXGETEXTENSIONTABLE);
    g_ExReleaseExtensionTable = (pExReleaseExtensionTable)(g_NtoskrnlBase + OFFSET_TO_EXRELEASEEXTENSIONTABLE);

    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!ExGetExtensionTable @ 0x%llx\n", (unsigned long long)g_ExGetExtensionTable);
    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!ExReleaseExtensionTable @ 0x%llx\n", (unsigned long long)g_ExReleaseExtensionTable);

    return TRUE;
}

BOOLEAN HookBamPsCallback(BOOLEAN hook) {
    BOOLEAN ret = FALSE;
    PVOID BamExtensionTable = NULL;

    if (hook) {
        // temporary disable process callbacks
        g_Orignal_nt_PspNotifyEnableMask_val = InterlockedExchange(g_nt_PspNotifyEnableMask, 0);

        BamExtensionTable = g_ExGetExtensionTable(*(ULONGLONG*)g_nt_PspBamExtensionHost);

        if (!BamExtensionTable) {
            DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[-] nt!ExGetExtensionTable(nt!PspBamExtensionHost) return: 0x%llx\n", (unsigned long long)BamExtensionTable);
            goto end;
        }

        DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!ExGetExtensionTable(nt!PspBamExtensionHost) return: 0x%llx\n", (unsigned long long)BamExtensionTable);

        g_Orignal_Bam_BampCreateProcessCallback = *(ULONGLONG*)BamExtensionTable;

        if (!g_Orignal_Bam_BampCreateProcessCallback) {
            DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[-] Bam!BampCreateProcessCallback @ 0x%llx\n", (unsigned long long)g_Orignal_Bam_BampCreateProcessCallback);
            goto end;
        }

        DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] Bam!BampCreateProcessCallback @ 0x%llx\n", (unsigned long long)g_Orignal_Bam_BampCreateProcessCallback);

        InterlockedExchangePointer(BamExtensionTable, &ProcessNotifyCallbackEx2);
        ret = TRUE;
    }

    else {
        // temporary disable process callbacks
        g_Orignal_nt_PspNotifyEnableMask_val = InterlockedExchange(g_nt_PspNotifyEnableMask, 0);

        BamExtensionTable = g_ExGetExtensionTable(*(ULONGLONG*)g_nt_PspBamExtensionHost);

        if (!BamExtensionTable) {
            DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[-] nt!ExGetExtensionTable(nt!PspBamExtensionHost) return: 0x%llx\n", (unsigned long long)BamExtensionTable);
            goto end;
        }

        DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "[+] nt!ExGetExtensionTable(nt!PspBamExtensionHost) return: 0x%llx\n", (unsigned long long)BamExtensionTable);
        InterlockedExchangePointer(BamExtensionTable, g_Orignal_Bam_BampCreateProcessCallback);
    }

    end:
    // restore process callbacks
     InterlockedExchange(g_nt_PspNotifyEnableMask, g_Orignal_nt_PspNotifyEnableMask_val);
     return ret;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    if (g_unhook) {
        HookBamPsCallback(FALSE); // unhook
    }

    DbgPrintEx(LOG_COMPONENT, DPFLTR_INFO_LEVEL, "Driver unloaded\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = DriverUnload;

    if (find_nt_globals()) {
        g_unhook = HookBamPsCallback(TRUE);
    }

	return STATUS_SUCCESS;
}