package com.woa.helper.main

import android.content.Context
import android.os.Build
import android.view.View
import com.woa.helper.R

data class DeviceConfig(
    val guideLink: String, val groupLink: String, val image: Int,
    val panel: Int = View.GONE, val dbkp: Int = View.GONE,
    val dbkpLink: String = "", val dbkpCodename: String = "",
    val unsupported: Boolean = false
)
object Device {
    var codename: String = ""
    var uefiPath: String = ""
    fun init(context: Context) { codename = Build.DEVICE }
    fun getVars() = DeviceConfig(
        guideLink = "https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T",
        groupLink = "https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/issues",
        image = R.drawable.degas,
        unsupported = Build.DEVICE != "degas"
    )
}
