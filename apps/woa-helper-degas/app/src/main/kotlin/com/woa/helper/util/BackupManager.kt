package com.woa.helper.util

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object BackupManager {
    private const val BACKUP_DIR = "/sdcard/WOAHelper/Backups"

    fun winBackup(bootPartition: String): ShellResult {
        if (!Regex("/dev/block/[A-Za-z0-9_./-]+").matches(bootPartition)) return ShellResult.Error("Boot partition not found")
        val access = MountManager.requireWritable()
        if (access is ShellResult.Error) return access
        val winPath = MountManager.getWinPath()
        if (winPath.isEmpty()) return ShellResult.Error("Mount path is empty")
        return ShellManager.execResult("dd bs=8M if=$bootPartition of=$winPath/boot.img", master = true)
    }

    fun androidBackup(bootPartition: String): ShellResult {
        if (!Regex("/dev/block/[A-Za-z0-9_./-]+").matches(bootPartition)) return ShellResult.Error("Boot partition not found")
        val mkdirResult = ShellManager.execResult("mkdir -p $BACKUP_DIR")
        if (mkdirResult is ShellResult.Error) return mkdirResult
        return ShellManager.execResult("dd bs=8M if=$bootPartition of=$BACKUP_DIR/boot.img")
    }

    fun modemBackup(): ShellResult = ShellResult.Error("Modem backup/provisioning is unavailable in this Xiaomi 14T fork")

    fun updateDate(onUpdate: (String) -> Unit) {
        val date = SimpleDateFormat("dd-MM HH:mm", Locale.US).format(Date())
        onUpdate(date)
    }
}
