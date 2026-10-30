package com.woa.helper.util

import android.content.Context
import android.os.Environment
import android.os.Build
import com.woa.helper.preference.Pref
import java.io.File

object MountManager {
    private var cachedWinPartition: String = ""
    private var cachedWinPath: String = ""
    private var cachedFilesDir: File? = null
    private var ntfsBinary: String = ""
    private var selinuxOnMount: Boolean = false

    fun init(filesDir: File, context: Context) {
        cachedFilesDir = filesDir
        ntfsBinary = "${context.applicationInfo.nativeLibraryDir}/libntfs3g_exec.so"
        cachedWinPath = if (Pref.getMountLocation(context)) {
            "/mnt/Windows"
        } else {
            "${Environment.getExternalStorageDirectory().path}/Windows"
        }
        selinuxOnMount = false
    }

    fun getWinPartition(): String {
        if (Build.DEVICE != "degas") return ""
        // Resolve only the installation's named partition, never raw partition numbers.
        val partition = ShellManager.exec("test -b /dev/block/by-name/Windows && realpath /dev/block/by-name/Windows")
        if (!Regex("/dev/block/[A-Za-z0-9_./-]+").matches(partition)) return ""
        cachedWinPartition = partition
        return cachedWinPartition
    }

    fun getWinPath(): String = cachedWinPath

    private fun mountEntry(): MountTable.Entry? {
        if (cachedWinPath.isEmpty()) return null
        // Ask for the exact target; source and access mode are checked separately.
        val line = ShellManager.exec("awk -v target='$cachedWinPath' '$2 == target { print; exit }' /proc/mounts", master = true)
        return MountTable.atTarget(line, cachedWinPath)
    }

    fun isMounted(): Boolean {
        val win = getWinPartition()
        return win.isNotEmpty() && mountEntry()?.source == win
    }

    private fun mountedMode(): String = mountEntry()?.options?.firstOrNull { it == "ro" || it == "rw" } ?: ""

    fun mount(writable: Boolean = false): ShellResult {
        val win = getWinPartition()
        if (win.isEmpty()) return ShellResult.Error("Windows partition not found")
        val requestedMode = if (writable) "rw" else "ro"
        val existing = mountEntry()
        if (existing != null && existing.source != win) return ShellResult.Error("Mount target is already occupied by another filesystem; no action taken.")
        if (isMounted()) return if (mountedMode() == requestedMode) ShellResult.Success("")
            else ShellResult.Error("Windows is already mounted in a different access mode. Unmount first.")
        val winPath = cachedWinPath
        if (winPath.isEmpty()) return ShellResult.Error("Mount path not configured")
        if (selinuxOnMount) {
            val selinuxResult = ShellManager.execResult("echo 0 > /sys/fs/selinux/enforce")
            if (selinuxResult is ShellResult.Error) return selinuxResult
        }
        val targetCheck = ShellManager.execResult("test ! -L '$winPath' && { test ! -e '$winPath' || { test -d '$winPath' && test -z \"\$(ls -A '$winPath')\"; }; }", master = true)
        if (targetCheck is ShellResult.Error) return ShellResult.Error("Mount directory must be empty and must not be a symbolic link.")
        val mkdirResult = ShellManager.execResult("mkdir -p '$winPath'", master = true)
        if (mkdirResult is ShellResult.Error) return mkdirResult
        val mountResult = ShellManager.execResult("test -x '$ntfsBinary' && '$ntfsBinary' '$win' '$winPath' -o ${if (writable) "rw" else "ro"},windows_names,norecover,allow_other,uid=1023,gid=1023,umask=000", master = true)
        if (mountResult is ShellResult.Error) return ShellResult.Error("NTFS mount refused. Fully shut down Windows; do not force dirty or hibernated NTFS.\n${mountResult.message}")
        if (!isMounted()) return ShellResult.Error("Mount not visible in the root mount namespace. KernelSU mount access needs validation on this ROM.")
        if (mountedMode() != requestedMode) {
            ShellManager.execResult("umount '$winPath'", master = true)
            return ShellResult.Error("NTFS did not grant the requested access mode. Windows may be hibernated or dirty; mount read-only instead.")
        }
        val osCheck = ShellManager.execResult("test -f '$winPath/Windows/System32/ntoskrnl.exe'", master = true)
        if (osCheck is ShellResult.Error) {
            ShellManager.execResult("umount '$winPath'", master = true)
            return ShellResult.Error("The named partition does not contain a Windows installation.")
        }
        return ShellResult.Success("")
    }

    fun requireWritable(): ShellResult =
        if (isMounted() && mountedMode() == "rw") ShellResult.Success("")
        else ShellResult.Error("Mount Windows using the Read/write choice first. File tools will not change read-only access automatically.")

    fun unmount(): ShellResult {
        val winPath = cachedWinPath
        if (winPath.isEmpty()) return ShellResult.Error("Mount path not configured")
        if (!isMounted()) return ShellResult.Error("No verified Windows mount at this path; no unmount performed")
        val umountResult = ShellManager.execResult("umount '$winPath'", master = true)
        if (umountResult is ShellResult.Error) {
            restoreSelinux()
            return umountResult
        }
        val rmdirResult = ShellManager.execResult("rmdir $winPath", master = true)
        if (rmdirResult is ShellResult.Error) {
            restoreSelinux()
            return ShellResult.Success("Windows unmounted; the existing mount directory was retained")
        }
        restoreSelinux()
        return ShellResult.Success("")
    }

    private fun restoreSelinux() {
        if (selinuxOnMount) {
            ShellManager.execResult("echo 1 > /sys/fs/selinux/enforce")
        }
    }

    fun resetCache() {
        cachedWinPartition = ""
        cachedWinPath = ""
        selinuxOnMount = false
    }

    fun isSelinuxPermissive(): Boolean {
        return ShellManager.exec("cat /sys/fs/selinux/enforce").trim() == "0"
    }
}
