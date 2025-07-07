# BamExtensionTableHook

## Introduction

Windows allows kernel-mode drivers to receive notifications about process creation and termination via the [`nt!PsSetCreateProcessNotifyRoutine`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-pssetcreateprocessnotifyroutine), [`nt!PsSetCreateProcessNotifyRoutineEx`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-pssetcreateprocessnotifyroutineex), and [`nt!PsSetCreateProcessNotifyRoutineEx2`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-pssetcreateprocessnotifyroutineex2) APIs. Internally, these registered callbacks are stored in a kernel-managed array called `nt!PspCreateProcessNotifyRoutine`.

This array holds all active process notification routines. Whenever a process is created or exits, the kernel iterates over this array and invokes each registered callback.

## **Common Bypass Technique**

Attackers with arbitrary kernel R/W primitive can locate the `nt!PspCreateProcessNotifyRoutine` array in memory and modify its contents. A common technique involves zeroing out specific entries in the array, either targeting callbacks registered by specific drivers or clearing the entire array. This effectively disables the affected callbacks and prevents security products from receiving process-related events.

## **Extension Table**

While attackers often focus on clearing the `nt!PspCreateProcessNotifyRoutine` array, Windows maintains a separate internal callback mechanism known as the [Extension Table](https://medium.com/yarden-shafir/yes-more-callbacks-the-kernel-extension-mechanism-c7300119a37a).

Inside the kernel function `nt!PspCallProcessNotifyRoutines`, which is responsible for invoking all registered process notify callbacks, there is a special check for certain system callbacks that are not registered through the standard API path (e.g., via `nt!PsSetCreateProcessNotifyRoutineEx2`). One notable example is `bam.sys` (the Background Activity Moderator driver, which was introduced in [Windows 10 version 1709 (RS3)](https://en.wikipedia.org/wiki/Windows_10,_version_1709)).

**NOTE**: Similar logic exists for `dam.sys` within `nt!PspCreateProcessNotifyRoutine`, but this driver does not appear to register an active callback.

The `bam.sys` callback is registered through a different mechanism entirely. Instead of using the standard (`nt!PsSetCreateProcessNotifyRoutine(Ex|Ex2)`) API, it registers its callback via the undocumented `nt!ExRegisterExtension` function, which maintains callbacks in a separate "Extension Table".

## **Proof of Concept**

The [Extension Table](https://medium.com/yarden-shafir/yes-more-callbacks-the-kernel-extension-mechanism-c7300119a37a) mechanism was already documented in 2019 (Thanks to [Yarden Shafir](https://x.com/yarden_shafir)), and the [idea of hooking it was discussed in other research that was published](http://publications.alex-ionescu.com/Infiltrate/Infiltrate%202019%20-%20DKOM%2030%20-%20Hiding%20and%20Hooking%20with%20Windows%20Extension%20Hosts.pdf). Despite this documentation, I am not aware of any practical use of this mechanism in real-world scenarios: attacks seen in the wild do not appear to target these callbacks, EDR/AV products do not seem to utilize this mechanism, and it is not publicly utilized by open-source offensive or defensive projects.

To demonstrate the persistence of the extension table mechanism, I developed a driver that targets the `nt!PspBamExtensionHost` data structure. The driver locates this structure and overwrites the pointer to `bam!BampCreateProcessCallback`, redirecting it to our custom callback function [ProcessNotifyCallbackEx2](https://github.com/Dor00tkit/BamExtensionTableHook/blob/2c244598a0051d5050239990395bffc82ea09e01/driver.c#L28).

Unlike the standard callbacks that can be disabled by clearing the `nt!PspCreateProcessNotifyRoutine` array, this approach targets the extension table mechanism itself. When process creation or termination events occur, instead of executing the original `bam.sys` callback (`bam!BampCreateProcessCallback`), the system executes our custom callback function. This effectively preserves process monitoring capabilities even when attackers believe they have disabled all process callbacks.

**Note:** Based on current observations, Patch Guard does not seem to monitor modifications to the extension table mechanism, leaving this technique undetected.

From a defensive perspective, this mechanism provides an additional layer of process monitoring that remains active even when standard callbacks are disabled. Defenders can leverage the extension table mechanism to maintain visibility into process creation and termination events, as attackers focusing solely on clearing the `nt!PspCreateProcessNotifyRoutine` array will leave these callbacks intact.

Conversely, attackers seeking complete process callback evasion must consider both the documented callback mechanisms and the undocumented extension table mechanism. Additionally, beyond clearing specific callback pointers, they can achieve comprehensive disabling by writing 0 to `nt!PspNotifyEnableMask`, which disables all process notification callbacks including those registered through the extension table mechanism.

Tested on:
* Windows 11 Version 24H2 (OS Build 26100.4349)

## **Further Reading and References**
1. [Yes, More Callbacks — The Kernel Extension Mechanism](https://medium.com/yarden-shafir/yes-more-callbacks-the-kernel-extension-mechanism-c7300119a37a)
2. [DKOM 3.0 Hiding and Hooking with Windows Extension Hosts](http://publications.alex-ionescu.com/Infiltrate/Infiltrate%202019%20-%20DKOM%2030%20-%20Hiding%20and%20Hooking%20with%20Windows%20Extension%20Hosts.pdf)
3. [BAM internals](https://dfir.ru/2020/04/08/bam-internals/)
4. [Lazarus Group's Rootkit Attack Using BYOVD](https://asec.ahnlab.com/wp-content/uploads/2022/09/Analysis-Report-on-Lazarus-Groups-Rootkit-Attack-Using-BYOVD_Sep-22-2022.pdf)
5. [Removing Kernel Callbacks Using Signed Drivers](https://br-sn.github.io/Removing-Kernel-Callbacks-Using-Signed-Drivers/)
6. [CheekyBlinder](https://github.com/br-sn/CheekyBlinder)
7. [Finding the Base of the Windows Kernel](https://wumb0.in/finding-the-base-of-the-windows-kernel.html).
8. [[unknowncheats] get ntoskrnl base address](https://www.unknowncheats.me/forum/3041967-post4.html)
9. [NtDoc](https://ntdoc.m417z.com/)
10. [Vergilius Project](https://www.vergiliusproject.com/)

## **Thanks**

[Yarden Shafir](https://x.com/yarden_shafir), [Alex Ionescu](https://x.com/aionescu), [Gabrielle Viala](https://x.com/pwissenlit), [lena151](https://archive.org/details/lena151), [OpenSecurityTraining2 (OST2)](https://ost2.fyi/).