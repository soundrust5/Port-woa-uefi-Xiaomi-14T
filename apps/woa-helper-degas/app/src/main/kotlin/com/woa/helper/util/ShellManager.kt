package com.woa.helper.util

import android.util.Log
import android.os.Build
import com.topjohnwu.superuser.Shell
import com.woa.helper.BuildConfig
import java.io.File

sealed class ShellResult {
    data class Success(val output: String) : ShellResult()
    data class Error(val message: String) : ShellResult()
}

object ShellManager {
    private var rootShell: Shell? = null
    private var masterShell: Shell? = null
    private var currentDir: String? = null

    @Synchronized
    fun init(dir: File) {
        if (Build.DEVICE != "degas") return
        val path = dir.absolutePath
        if (rootShell != null && currentDir == path) return
        try {
            close()
            currentDir = path
            rootShell = Shell.Builder.create().build()
            masterShell = Shell.Builder.create().setFlags(Shell.FLAG_MOUNT_MASTER).build()
            listOf(rootShell, masterShell).forEach { shell ->
                shell?.let {
                    // KernelSU supports the normal Android root shell; no Magisk busybox assumption.
                    exec("cd $path", it)
                }
            }
        } catch (e: Exception) {
            Log.e("ShellManager", "init failed", e)
            close()
        }
    }

    private fun blocked(command: String): Boolean {
        // Defence in depth for old upstream flash entry points; backups remain reads.
        return Regex("""(?:of=|>\s*|\b(?:flash|erase|mkfs\S*|wipefs)\s+)["']?/dev/block/""").containsMatchIn(command) ||
            command.contains("/sys/fs/selinux/enforce") && command.contains(">")
    }

    fun close() {
        try {
            rootShell?.close()
            masterShell?.close()
        } catch (_: Exception) { }
        rootShell = null
        masterShell = null
        currentDir = null
    }

    fun exec(command: String, master: Boolean = false): String {
        val shell = if (master) masterShell else rootShell
        return exec(command, shell)
    }

    fun exec(command: String, shell: Shell?): String {
        if (blocked(command)) return ""
        if (shell == null) {
            Log.w("ShellManager", "exec called before init: $command")
            return ""
        }
        if (BuildConfig.DEBUG) Log.d("ShellManager", command)
        val out = ArrayList<String>()
        val err = ArrayList<String>()
        shell.newJob().add(command).to(out, err).exec()
        if (BuildConfig.DEBUG && out.isNotEmpty()) Log.d("ShellManager", out.toString())
        if (BuildConfig.DEBUG && err.isNotEmpty()) Log.w("ShellManager", err.toString())
        return out.lastOrNull() ?: ""
    }

    fun execResult(command: String, master: Boolean = false): ShellResult {
        val shell = if (master) masterShell else rootShell
        return execResult(command, shell)
    }

    fun execResult(command: String, shell: Shell?): ShellResult {
        if (blocked(command)) return ShellResult.Error("Unavailable in the Xiaomi 14T build: raw partition writes are disabled")
        if (shell == null) {
            Log.w("ShellManager", "execResult called before init: $command")
            return ShellResult.Error("Shell not initialized")
        }
        if (BuildConfig.DEBUG) Log.d("ShellManager", command)
        val out = ArrayList<String>()
        val err = ArrayList<String>()
        val result = shell.newJob().add(command).to(out, err).exec()
        if (BuildConfig.DEBUG && out.isNotEmpty()) Log.d("ShellManager", out.toString())
        if (BuildConfig.DEBUG && err.isNotEmpty()) Log.w("ShellManager", err.toString())
        if (!result.isSuccess) {
            val message = if (err.isNotEmpty()) err.joinToString("\n") else "Command failed (exit code ${result.code})"
            return ShellResult.Error(message)
        }
        return ShellResult.Success(out.lastOrNull() ?: "")
    }

    fun isRootGranted(): Boolean = Shell.isAppGrantedRoot() == true

    fun isEmpty(command: String, master: Boolean = false): Boolean = exec(command, master).isEmpty()
}
