package com.woa.helper.main

import android.os.Handler
import android.os.Looper
import android.service.quicksettings.TileService
import android.widget.Toast
import com.woa.helper.R
import com.woa.helper.preference.Pref
import com.woa.helper.util.BackupManager
import com.woa.helper.util.MountManager
import com.woa.helper.util.ShellManager
import com.woa.helper.util.ShellResult
import com.woa.helper.util.DevcfgManager
import com.woa.helper.widget.MountWidget
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

abstract class CommonTileService : TileService() {
    protected fun mount(): String? {
        MountManager.init(filesDir, this)
        return when (val result = MountManager.mount()) {
            is ShellResult.Success -> null
            is ShellResult.Error -> result.message
        }
    }

    protected fun unmount(): String? {
        return when (val result = MountManager.unmount()) {
            is ShellResult.Success -> null
            is ShellResult.Error -> result.message
        }
    }

    protected fun updateTileState(state: Int, subtitleText: String? = null) {
        Handler(Looper.getMainLooper()).post {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q && subtitleText != null) {
                qsTile.subtitle = subtitleText
            }
            qsTile.state = state
            qsTile.updateTile()
        }
    }
}

class QuickBootTile : CommonTileService() {
    override fun onStartListening() { super.onStartListening(); updateTileState(0, getString(R.string.degas_unavailable)) }
    override fun onClick() { super.onClick(); updateTileState(0, getString(R.string.degas_unavailable)) }
}

class MountTile : CommonTileService() {

    override fun onStartListening() {
        super.onStartListening()
        Thread {
            ShellManager.init(filesDir)
            update()
        }.start()
    }

    override fun onClick() {
        super.onClick()
        Thread {
            ShellManager.init(filesDir)
            val error = if (MountManager.isMounted()) unmount() else mount()
            if (error != null) {
                Toast.makeText(this, "${getString(R.string.wrong)}\n$error", Toast.LENGTH_LONG).show()
            } else {
                MountWidget.requestUpdate(this)
                MainActivity.instance?.get()?.updateMountText()
            }
            update()
        }.start()
    }

    private fun update() {
        if (isSecure && !Pref.getSecure(this)) {
            updateTileState(0)
            return
        }
        if (MountManager.getWinPartition().isEmpty()) {
            updateTileState(0)
            return
        }
        MountManager.init(filesDir, this)
        updateTileState(if (MountManager.isMounted()) 2 else 1)
    }
}
