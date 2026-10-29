# Failed specialize recovery and UFS0.2 maintenance

User authorized UFS0.2 update and OOBE recovery/bypass with Xiaomi blankpassword.
Phone already fastboot. Stock boot_b restored to access Android NTFS.
Full offline SYSTEM/SOFTWARE/SAM and logs captured read-only under
evidence/raw/2026-10-27-degas-oobe-stop. No reinstall or formatting.

Captured Panther setuperr.log gives the concrete failed action:
Sysprep_Online_Specialize_Bcd / spbcd.dll reports BCD not a system store,
status0xC0000098, error0x3ee. SYSTEM GeneralizationState=6 and ChildCompletion
setup.exe=1 match the previously handled OP12 failed-specialize state.

Chosen correction preserves normal setup processing rather than clearing all
setup flags or editing SAM offline:

- Add Description/System DWORD1 to a copied BCD. Existing host helper proves
  the semantic change only; GPT/partition GUIDs and Windows loader preserved.
- Dedicated PE maintenance uses the already qualified oldUFS driver. It mounts
  only exact Windows/ESP GUIDs and ranges, then DISM/Add-Driver installs signed
  version0.2 performance package into full Windows and checks boot Start/path.
- Native repair guard requires exactly GeneralizationState6 / setup.exe1,
  resets them to4 /0, flushes and unloads offline SYSTEM.
- Existing unattend local Xiaomi/no-password configuration retained, adding
  one automatic login and HideEULAPage. No SkipMachineOOBE shortcut.
- Publish system-marked BCD with fullWindows default after all steps succeed.
- Backup SYSTEM/SOFTWARE/DRIVERS and BCD before maintenance. Source UFS0.1
  package remains installed/retained for rollback. No raw journal in PE.

Sources: tools/degas_setup_maintenance.cmd, degas_install_bootstrap.c with
DEGAS_MAINTENANCE=1, degas_full_windows_verify.c RepairSetup.
Maintenance PE ARM64 compile, WIM verify and executable readback passed.
BCD default-PE candidate semantic diff verified. Device file hashes and GPT
readback passed: artifacts/degas/setup-maintenance/staged.json.

At staging completion, UFS0.2 was NOT YET installed: user must select Windows
Boot Manager to execute maintenance. Await SUCCESS before claiming completed.
The service script restores full Windows BCD for the following boot. Existing
ESP installer WIM preserved as sources/boot.wim.pre-maintenance.

No broad setup-complete native account helper is deployed; its draft was
discarded once the precise system-store failure was identified.

## Maintenance success confirmed

User reported SUCCESS and returnedfastboot. Android read-only verification
captured evidence/raw/2026-10-27-degas-maintenance-success. Installed boot
service Start0 now points to degasufs.inf_arm64_91fef7ec9d8552a8/degasufs.sys,
matching signed UFS0.2 SHA d1d41db322bbc47bb7554f5d8a589d4708e21f5082c697e0d89f18d73d9b2d88.
GeneralizationState4 and ChildCompletion setup.exe0 verified. ESP BCD equals
the reviewed BCD.system byte-for-byte. Filesystems unmounted; GPT unchanged.
Next fullWindows boot remains untested after this servicing.

## Second stop: missing setup queue; explicit direct completion

Next user boot reported installation process could not start. Captured
evidence/raw/2026-10-27-degas-oobe-stop-2: setupact says saved
MainQueueOnline absent in Panther/SetupInfo; windeploy exits0x80070006.
Thus the two-state retry cannot reconstruct the lost setup work queue.

User's existing direct-bypass authorization is now implemented by a reviewed
online helper tools/degas_finish_setup.c. LocalSystem/fullWindows/ACPI DGS0001
and authorization-file checks precede changes. Native NetUser APIs create or
validate Xiaomi, set authorized blankpassword, resolve Administrators bySID,
and verify group membership. Bounded retry120sec ifSAM notready. Only after
account success configure explicit empty-password autologon and complete setup.
No offline SAM edit, no other account removal. Error logs and stops; wrapper
reboots only on exact exitcode0 (also treats negative exception codes aserror).

Offline staging changes only SYSTEM Setup/CmdLine and SetupType2, proven by
semantic snapshot equality. Original hive is clean (equal sequence numbers).
SYSTEM/SOFTWARE/SAM and transaction logs backed up on NTFS before staging;
old SYSTEM logs moved beside backups to prevent stale replay over new hive.
Helper, script and committed hive verified again after unmount/remountRO.
Records artifacts/degas/setup-finish/applied.json. UFS0.2 unchanged.
Helper not yet executed at staging completion; next Windows boot should run
completion then reboot once, followed by normal Xiaomi login on next boot.

## Direct completion verified

User saw CMD followed by Please wait, then automatic reboot. Read-only capture
evidence/raw/2026-10-27-degas-finish-result-1 contains DegasFinish.done and log:
NetUserAdd=0, Admin membership=0, finish result=0. SYSTEM SystemSetupInProgress,
OOBEInProgress, SetupType, SetupPhase all0; CmdLine empty. SOFTWARE ImageState
IMAGE_STATE_COMPLETE, DefaultUserName Xiaomi and AutoAdminLogon1. SAM has local
Xiaomi account. No additional repair edits made. Next normal desktop login
still awaits observation; automatic restart is consistent with wrapper's
success-only shutdown request, not evidence of a spontaneous crash.
