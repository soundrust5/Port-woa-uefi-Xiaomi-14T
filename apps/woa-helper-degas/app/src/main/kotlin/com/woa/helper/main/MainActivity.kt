package com.woa.helper.main

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.content.res.Resources
import android.graphics.RenderEffect
import android.graphics.Shader
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.View
import android.window.OnBackInvokedDispatcher
import android.view.animation.AnimationUtils
import android.widget.AdapterView
import android.widget.ArrayAdapter
import com.woa.helper.BuildConfig
import com.woa.helper.R
import com.woa.helper.databinding.ActivityMainBinding
import com.woa.helper.databinding.SetPanelBinding
import com.woa.helper.databinding.ToolboxBinding
import com.woa.helper.preference.Pref
import com.woa.helper.util.BackupManager
import com.woa.helper.util.DevcfgManager
import com.woa.helper.util.KernelManager
import com.woa.helper.util.MountManager
import com.woa.helper.util.ShellManager
import com.woa.helper.util.ShellResult
import com.woa.helper.util.ToolboxDeployer
import com.woa.helper.util.UpdateChecker
import com.woa.helper.util.UpdateResult
import com.woa.helper.util.RAM
import com.woa.helper.widget.MountWidget
import java.io.File
import java.io.IOException
import java.util.Locale
import java.lang.ref.WeakReference

@SuppressLint("StaticFieldLeak")
class MainActivity : Activity() {
    private lateinit var mainBinding: ActivityMainBinding
    private lateinit var settingsBinding: SetPanelBinding
    private lateinit var toolboxBinding: ToolboxBinding

    private val views: MutableList<View> = ArrayList()
    private var blurCount = 0

    private fun postUi(action: () -> Unit) {
        if (!isDestroyed) runOnUiThread(action)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        instance = WeakReference(this)
        applySavedLocale()
        setupEdgeToEdge()
        super.onCreate(savedInstanceState)
        initBase()
        setupNavigation()
        inflateLayouts()
        setupWindowInsets()
        setupLanguageSpinner()
        setupToolbar()
        initDeviceData()
        setupClickListeners()
        checkUpdatesAndModels()
        applyDeviceLimits()
    }

    private fun initBase() {
        Pref.setFilesDir(this, filesDir.toString())
        ShellManager.init(filesDir)
    }

    private fun applySavedLocale() {
        val tag = Pref.getLocale(this)
        val locales = if (tag == "und") {
            Resources.getSystem().configuration.locales
        } else {
            android.os.LocaleList.forLanguageTags(tag)
        }
        val config = Configuration(resources.configuration)
        config.setLocales(locales)
        @Suppress("DEPRECATION")
        resources.updateConfiguration(config, resources.displayMetrics)
    }

    private fun restartApp() {
        val intent = Intent(this, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        }
        startActivity(intent)
        finishAffinity()
    }

    private var backInvokedCallback: android.window.OnBackInvokedCallback? = null

    private fun setupNavigation() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = android.window.OnBackInvokedCallback { onBackHandled() }
            backInvokedCallback = callback
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback
            )
        }
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        onBackHandled()
    }

    private fun onBackHandled() {
        if (views.size <= 1) {
            moveTaskToBack(true)
            finish()
        } else {
            handleBackNavigation()
        }
    }

    private fun handleBackNavigation() {
        val currentView = views.last()
        if (BuildConfig.DEBUG && currentView === settingsBinding.root) {
            saveCodenameIfChanged()
        }
        currentView.startAnimation(AnimationUtils.loadAnimation(this, R.anim.slide_back_out))
        views.removeAt(views.lastIndex)
        val previousView = views.last()
        setContentView(previousView)
        previousView.startAnimation(AnimationUtils.loadAnimation(this, R.anim.slide_back_in))
    }

    private fun saveCodenameIfChanged() {
        val textbox = settingsBinding.codename
        val currentCodename = textbox.text.toString()
        val savedCodename = Pref.codenameChanger(false, this, "")
        if (currentCodename.isNotBlank() && currentCodename != savedCodename) {
            Pref.codenameChanger(true, this, currentCodename)
        }
    }

    @Suppress("DEPRECATION")
    private fun setupEdgeToEdge() {
        val isLight = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK != Configuration.UI_MODE_NIGHT_YES
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.decorView.windowInsetsController?.setSystemBarsAppearance(
                if (isLight) android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS else 0,
                android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
            )
        } else {
            window.decorView.systemUiVisibility = window.decorView.systemUiVisibility or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                (if (isLight) View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR else 0)
        }
    }

    private fun inflateLayouts() {
        copyAssets()
        mainBinding = ActivityMainBinding.inflate(layoutInflater)
        settingsBinding = SetPanelBinding.inflate(layoutInflater)
        toolboxBinding = ToolboxBinding.inflate(layoutInflater)
        Download.permission(this)
        setContentView(mainBinding.root)
        views.clear()
        views.add(mainBinding.root)
    }

    @Suppress("DEPRECATION")
    private fun setupWindowInsets() {
        findViewById<View>(android.R.id.content).setOnApplyWindowInsetsListener { _, insets ->
            val left = insets.systemWindowInsetLeft
            val top = insets.systemWindowInsetTop
            val right = insets.systemWindowInsetRight
            val bottom = insets.systemWindowInsetBottom
            listOf(mainBinding.app, toolboxBinding.app, settingsBinding.app).forEach {
                it.setPadding(0, 0, 0, bottom)
            }
            listOf(mainBinding.linearLayout, toolboxBinding.linearLayout, settingsBinding.linearLayout).forEach {
                it.setPadding(left, top, right, 0)
            }
            insets
        }
    }

    private var lastSpinnerPosition = -1

    private fun setupLanguageSpinner() {
        val languages = mutableListOf(getString(R.string.default1))
        val locales = mutableListOf("und")
        for (tag in BuildConfig.LOCALES) {
            locales.add(tag!!.lowercase(Locale.getDefault()))
            val locale = checkNotNull(android.os.LocaleList.forLanguageTags(tag).get(0))
            val country = locale.getDisplayCountry(locale)
            val lang = locale.getDisplayLanguage(locale) + if (country.isNotEmpty()) " ($country)" else ""
            languages.add(lang)
        }
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, languages)
        settingsBinding.languages.adapter = adapter
        val currentLocale = resources.configuration.locales[0]
        if (currentLocale != null) {
            val index = locales.indexOf(currentLocale.toLanguageTag().lowercase())
            if (index != -1) settingsBinding.languages.setSelection(index)
        }
        lastSpinnerPosition = settingsBinding.languages.selectedItemPosition
        settingsBinding.languages.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position == lastSpinnerPosition) return
                lastSpinnerPosition = position
                Pref.setLocale(this@MainActivity, locales[position])
                restartApp()
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
    }

    private fun setupToolbar() {
        val toolbarBindings = listOf(mainBinding.toolbarlayout, settingsBinding.toolbarlayout, toolboxBinding.toolbarlayout)
        toolbarBindings.forEach { binding ->
            binding.toolbar.navigationIcon = getDrawable(R.drawable.ic_launcher_foreground)
            binding.settings.setColorFilter(getColor(R.color.md_theme_primary))
        }
        mainBinding.toolbarlayout.toolbar.setTitle(R.string.app_name)
        mainBinding.toolbarlayout.toolbar.subtitle = "v${BuildConfig.VERSION_NAME}${if (BuildConfig.DEBUG) " (Debug)" else ""}"
        settingsBinding.toolbarlayout.toolbar.setTitle(R.string.preferences)
    }

    private fun initDeviceData() {
        MountManager.resetCache()
        MountManager.init(filesDir, this)
        MountManager.getWinPartition()
        Device.init(this)
        updateMountText()
        checkWin()
        checkUefi()
        Thread {
            val selinuxPermissive = MountManager.isSelinuxPermissive() && !Pref.getSelinux(this)
            postUi {
                if (selinuxPermissive) {
                    settingsBinding.selinux.visibility = View.GONE
                }
            }
        }.start()
        mainBinding.tvDate.text = getString(R.string.last, Pref.getDate(this))
        val slot = ShellManager.exec("getprop ro.boot.slot_suffix")
        if (slot.isEmpty()) {
            mainBinding.tvSlot.visibility = View.GONE
        } else {
            mainBinding.tvSlot.text = getString(R.string.slot, slot.drop(1)).uppercase(Locale.getDefault())
        }
        mainBinding.deviceName.text = "${Build.MODEL} (${Device.codename})"
        val props = Device.getVars()
        mainBinding.DeviceImage?.setImageResource(props.image)
        mainBinding.tvPanel.visibility = props.panel
        toolboxBinding.dbkp.visibility = props.dbkp
        toolboxBinding.flashUefi.visibility = if (props.dbkp == View.VISIBLE) View.GONE else View.VISIBLE
        if (props.unsupported && !Pref.getAGREE(this)) {
            Dlg.show(this, R.string.unsupported)
            Dlg.setYes(R.string.sure) {
                Pref.setAGREE(this, true)
                Dlg.close()
            }
        }
        val panel = detectPanelType()
        if (!Pref.getAGREE(this) && (panel.contains("f1p2_2") || panel.contains("f1_cmd"))) {
            showPanelWarning()
        }
        mainBinding.tvRamvalue.text = getString(R.string.ramvalue, RAM.getMemory(this).toDouble())
        mainBinding.tvPanel.text = getString(R.string.paneltype, panel)
    }

    private fun detectPanelType(): String {
        val cmdline = ShellManager.exec("cat /proc/cmdline")
        return when {
            cmdline.contains("tianmamd_dv2") -> "Tianma DV2"
            cmdline.contains("tianmamd_pp1") -> "Tianma PP1"
            cmdline.contains("tianmamd_pv") -> "Tianma PV"
            cmdline.contains("j20s_42") || cmdline.contains("k82_42") || cmdline.contains("k9d_42") || cmdline.contains("huaxing") -> "Huaxing"
            cmdline.contains("j20s_36") || cmdline.contains("tianma") || cmdline.contains("k9d_36") || cmdline.contains("k82_36") -> "Tianma"
            cmdline.contains("ebbg") -> "EBBG"
            cmdline.contains("samsung") || cmdline.contains("ea8076_f1mp") || cmdline.contains("ea8076_f1p2") ||
                cmdline.contains("ea8076_global") || cmdline.contains("S6E3FC3") || cmdline.contains("AMS646YD01") -> "Samsung"
            else -> ShellManager.exec("cat /proc/cmdline | tr ' :=' '\n' | grep dsi | tr ' _' '\n' | tail -3 | head -1")
        }
    }

    private fun showPanelWarning() {
        Dlg.show(this, R.string.upanel)
        Dlg.setYes(R.string.chat) {
            openLink(this, Device.getVars().groupLink)
            Pref.setAGREE(this, true)
            Dlg.close()
        }
        Dlg.setDismiss(R.string.nah) {
            Pref.setAGREE(this, true)
            Dlg.close()
        }
        Dlg.setNo(R.string.later) { Dlg.close() }
    }

    private fun setupClickListeners() {
        mainBinding.guide.setOnClickListener { openLink(this, Device.getVars().guideLink) }
        mainBinding.group.setOnClickListener { openLink(this, Device.getVars().groupLink) }
        mainBinding.cvInfo.setOnClickListener { checkUpdate(true) }
        mainBinding.mnt.setOnClickListener { mountUI(filesDir) }
        mainBinding.quickBoot.setOnClickListener { quickbootUI(filesDir) }
        setupBackupListener()
        setupToolboxListeners()
        setupSettingsListeners()
    }

    private fun applyDeviceLimits() {
        val reason = getString(R.string.degas_unavailable)
        listOf(toolboxBinding.flashUefi, toolboxBinding.sta,
            toolboxBinding.dumpModem, toolboxBinding.dbkp, toolboxBinding.devcfg,
            toolboxBinding.usbhost).forEach {
            it.visibility = View.VISIBLE
            it.isEnabled = false
            it.setSubtitle(reason)
        }
        mainBinding.backup.setTitle(R.string.degas_backup)
        mainBinding.backup.setSubtitle(R.string.degas_backup_subtitle)
        mainBinding.quickBoot.isEnabled = true
        mainBinding.quickBoot.setTitle(R.string.degas_reboot)
        mainBinding.quickBoot.setSubtitle(R.string.degas_reboot_subtitle)
        settingsBinding.codename.visibility = View.GONE
        settingsBinding.selinux.isEnabled = false
        settingsBinding.appUpdate.isEnabled = false
    }

    private fun setupBackupListener() {
        mainBinding.backup.setOnClickListener {
            Dlg.show(this, R.string.degas_backup_subtitle, R.drawable.ic_disk)
            Dlg.setDismiss(R.string.no) { Dlg.close() }
            Dlg.setNo(R.string.android) {
                performBackup(isAndroid = true)
            }
            Dlg.setYes(R.string.windows) {
                performBackup(isAndroid = false)
            }
        }
    }

    private fun performBackup(isAndroid: Boolean) {
        Dlg.dialogLoading()
        updateLastBackupDate()
        Thread {
            val bootPartition = getBoot()
            val result = if (isAndroid) {
                val backupResult = BackupManager.androidBackup(bootPartition)
                backupResult
            } else {
                val mountResult = MountManager.requireWritable()
                if (mountResult is ShellResult.Error) mountResult
                else BackupManager.winBackup(bootPartition)
            }
            postUi {
                when (result) {
                    is ShellResult.Success -> {
                        Dlg.setText(R.string.backuped)
                        Dlg.dismissButton()
                    }
                        is ShellResult.Error -> Dlg.showError(result)
                }
            }
        }.start()
    }

    private fun setupToolboxListeners() {
        mainBinding.toolbox.setOnClickListener {
            navigateToView(toolboxBinding.root, R.string.toolbox_title)
        }
        toolboxBinding.sta.setOnClickListener { setupSta() }
        toolboxBinding.dumpModem.setOnClickListener { setupDumpModem() }
        toolboxBinding.flashUefi.setOnClickListener { setupFlashUefi() }
        toolboxBinding.dbkp.setOnClickListener { setupDbkp() }
        toolboxBinding.devcfg.setOnClickListener { setupDevcfg() }
        toolboxBinding.software.setOnClickListener { setupSoftware() }
        toolboxBinding.atlasos.setOnClickListener { setupAtlasOS() }
        toolboxBinding.usbhost.setOnClickListener { setupUsbHost() }
        toolboxBinding.rotation.setOnClickListener { setupRotation() }
        toolboxBinding.tablet.setOnClickListener { setupTabletMode() }
        toolboxBinding.setup.setOnClickListener { setupFrameworks() }
        toolboxBinding.defender.setOnClickListener { setupDefenderEdge() }
    }

    private fun navigateToView(targetView: View, titleRes: Int) {
        views.add(targetView)
        views[views.size - 2].startAnimation(AnimationUtils.loadAnimation(this, R.anim.slide_out))
        setContentView(targetView)
        targetView.startAnimation(AnimationUtils.loadAnimation(this, R.anim.slide_in))
        val toolbar = when (targetView) {
            toolboxBinding.root -> toolboxBinding.toolbarlayout.toolbar
            settingsBinding.root -> settingsBinding.toolbarlayout.toolbar
            else -> mainBinding.toolbarlayout.toolbar
        }
        toolbar.title = getString(titleRes)
        toolbar.navigationIcon = getDrawable(R.drawable.ic_launcher_foreground)
    }

    private fun setupSettingsListeners() {
        val settingsClick = View.OnClickListener {
            navigateToView(settingsBinding.root, R.string.preferences)
            updateSettingsCheckboxes()
            settingsBinding.toolbarlayout.settings.visibility = View.GONE
        }
        listOf(mainBinding.toolbarlayout.settings, toolboxBinding.toolbarlayout.settings).forEach {
            it.setOnClickListener(settingsClick)
        }
        settingsBinding.confirmation.setOnChangeListener { b -> Pref.setConfirm(this, b) }
        settingsBinding.automount.setOnChangeListener { b -> Pref.setAutoMount(this, b) }
        settingsBinding.securelock.setOnChangeListener { b -> Pref.setSecure(this, !b) }
        settingsBinding.mountLocation.setOnChangeListener { b ->
            Pref.setMountLocation(this, b)
            MountManager.init(filesDir, this)
        }
        settingsBinding.selinux.setOnChangeListener { b ->
            Pref.setSelinux(this, b)
            MountManager.init(filesDir, this)
        }
        settingsBinding.appUpdate.setOnChangeListener { b -> Pref.setAppUpdate(this, b) }
        settingsBinding.chatButton.setOnClickListener { openLink(this, Device.getVars().groupLink) }
        settingsBinding.githubButton.setOnClickListener { openLink(this, "https://github.com/n00b69/woa-helper") }
        setupQuickBootDropdown()
        setupDevcfgSettings()
    }

    private fun updateSettingsCheckboxes() {
        val pairs = listOf(
            settingsBinding.confirmation to Pref.getConfirm(this),
            settingsBinding.automount to Pref.getAutoMount(this),
            settingsBinding.securelock to !Pref.getSecure(this),
            settingsBinding.mountLocation to Pref.getMountLocation(this),
            settingsBinding.selinux to Pref.getSelinux(this),
            settingsBinding.appUpdate to Pref.getAppUpdate(this),
            settingsBinding.devcfg1 to (Pref.getDevcfg1(this) && settingsBinding.devcfg1.visibility == View.VISIBLE),
            settingsBinding.devcfg2 to Pref.getDevcfg2(this)
        )
        pairs.forEach { it.first.isChecked = it.second }
        val isMounted = MountManager.isMounted()
        settingsBinding.mountLocation.isEnabled = !isMounted
        settingsBinding.selinux.isEnabled = !isMounted
        updateQuickBootDropdown()
    }

    private fun setupQuickBootDropdown() {
        settingsBinding.quickbootBackupHeader.setOnClickListener {
            val isVisible = settingsBinding.quickbootBackupContent.visibility == View.VISIBLE
            settingsBinding.quickbootBackupContent.visibility = if (isVisible) View.GONE else View.VISIBLE
            settingsBinding.quickbootBackupChevron.rotation = if (isVisible) 0f else 180f
        }
        settingsBinding.backupWindows.setOnChangeListener { b ->
            if (b) {
                Pref.setBackupIfNoneWindows(this, true)
                settingsBinding.forceWindowsSwitch.isEnabled = true
            } else {
                Pref.setBackupIfNoneWindows(this, false)
                Pref.setForceBackupWindows(this, false)
                settingsBinding.forceWindowsSwitch.isChecked = false
                settingsBinding.forceWindowsSwitch.isEnabled = false
            }
        }
        settingsBinding.backupAndroid.setOnChangeListener { b ->
            if (b) {
                Pref.setBackupIfNoneAndroid(this, true)
                settingsBinding.forceAndroidSwitch.isEnabled = true
            } else {
                Pref.setBackupIfNoneAndroid(this, false)
                Pref.setForceBackupAndroid(this, false)
                settingsBinding.forceAndroidSwitch.isChecked = false
                settingsBinding.forceAndroidSwitch.isEnabled = false
            }
        }
        settingsBinding.forceWindowsSwitch.setOnClickListener {
            if (settingsBinding.forceWindowsSwitch.isChecked) {
                Dlg.showBackupWarning(this) {
                    Pref.setForceBackupWindows(this, true)
                    Pref.setBackupIfNoneWindows(this, false)
                    settingsBinding.forceWindowsSwitch.isChecked = true
                }
                settingsBinding.forceWindowsSwitch.isChecked = false
            } else {
                Pref.setForceBackupWindows(this, false)
                Pref.setBackupIfNoneWindows(this, true)
            }
        }
        settingsBinding.forceAndroidSwitch.setOnClickListener {
            if (settingsBinding.forceAndroidSwitch.isChecked) {
                Dlg.showBackupWarning(this) {
                    Pref.setForceBackupAndroid(this, true)
                    Pref.setBackupIfNoneAndroid(this, false)
                    settingsBinding.forceAndroidSwitch.isChecked = true
                }
                settingsBinding.forceAndroidSwitch.isChecked = false
            } else {
                Pref.setForceBackupAndroid(this, false)
                Pref.setBackupIfNoneAndroid(this, true)
            }
        }
    }

    private fun updateQuickBootDropdown() {
        val windowsEnabled = Pref.getBackupIfNoneWindows(this) || Pref.getForceBackupWindows(this)
        val androidEnabled = Pref.getBackupIfNoneAndroid(this) || Pref.getForceBackupAndroid(this)
        settingsBinding.backupWindows.isChecked = windowsEnabled
        settingsBinding.backupAndroid.isChecked = androidEnabled
        settingsBinding.forceWindowsSwitch.isChecked = Pref.getForceBackupWindows(this)
        settingsBinding.forceAndroidSwitch.isChecked = Pref.getForceBackupAndroid(this)
        settingsBinding.forceWindowsSwitch.isEnabled = windowsEnabled
        settingsBinding.forceAndroidSwitch.isEnabled = androidEnabled
    }

    private fun setupDevcfgSettings() {
        Thread {
            val op7funny = ShellManager.exec("cat /proc/cmdline | grep oplus")
            val isOP7Variant = setOf("guacamole", "guacamolet", "OnePlus7Pro", "OnePlus7Pro4G", "OnePlus7ProTMO", "hotdog", "OnePlus7TPro", "OnePlus7TPro4G").contains(Device.codename)
            postUi {
                if (isOP7Variant && op7funny.isNotEmpty()) {
                    settingsBinding.devcfg1.setOnChangeListener { b ->
                        Pref.setDevcfg1(this@MainActivity, b)
                        settingsBinding.devcfg2.visibility = if (b) View.VISIBLE else View.GONE
                        Pref.setDevcfg2(this@MainActivity, false)
                    }
                    settingsBinding.devcfg2.setOnChangeListener { b -> Pref.setDevcfg2(this@MainActivity, b) }
                    toolboxBinding.devcfg.visibility = View.VISIBLE
                } else {
                    settingsBinding.devcfg1.visibility = View.GONE
                    settingsBinding.devcfg2.visibility = View.GONE
                    Pref.setDevcfg1(this@MainActivity, false)
                    Pref.setDevcfg2(this@MainActivity, false)
                }
            }
        }.start()
    }

    private fun checkUpdatesAndModels() {
        if (!autoUpdateChecked) { checkUpdate(); autoUpdateChecked = true }
        if (!BuildConfig.DEBUG) {
            settingsBinding.codename.visibility = View.GONE
        }
    }

    private fun setupToolboxAction(question: Int, icon: Int, deploy: (String) -> ShellResult) {
        Dlg.show(this, question, icon)
        Dlg.setNo(R.string.no) { Dlg.close() }
        Dlg.setYes(R.string.yes) {
            Dlg.dialogLoading()
            Thread {
                val mountResult = MountManager.requireWritable()
                if (mountResult is ShellResult.Error) {
                    postUi { Dlg.showMountError(mountResult.message) }
                    return@Thread
                }
                val result = deploy(filesDir.absolutePath)
                postUi {
                    when (result) {
                        is ShellResult.Success -> {
                            Dlg.setText(R.string.done)
                            Dlg.dismissButton()
                        }
                        is ShellResult.Error -> Dlg.showError(result)
                    }
                }
            }.start()
        }
    }

    private fun unsupportedAction() { Dlg.show(this, R.string.degas_unavailable); Dlg.dismissButton() }
    private fun setupSta() = unsupportedAction()
    private fun setupDumpModem() = unsupportedAction()
    private fun setupFlashUefi() = unsupportedAction()
    private fun setupDbkp() = unsupportedAction()
    private fun setupDevcfg() = unsupportedAction()

    private fun setupSoftware() {
        setupToolboxAction(R.string.software_question, R.drawable.ic_sensor, ToolboxDeployer::deploySoftware)
    }

    private fun setupAtlasOS() {
        if (!isNetworkConnected(this)) { noInternet(); return }
        Dlg.show(this, R.string.atlasos_question, R.drawable.ic_ar)
        Dlg.dismissButton()
        Dlg.setCancelable(true)
        val downloadPlaybook = { _: String, url: String, targetName: String ->
            Dlg.dialogLoading()
            Dlg.setBar(0)
            Dlg.setIcon(R.drawable.ic_download)
            Thread {
                val mountResult = MountManager.requireWritable()
                if (mountResult is ShellResult.Error) {
                    postUi { Dlg.showMountError(mountResult.message) }
                    return@Thread
                }
                val result = ToolboxDeployer.deployAtlasOS(url, targetName)
                postUi {
                    when (result) {
                        is ShellResult.Success -> {
                            Dlg.setIcon(R.drawable.ic_ar)
                            Dlg.hideBar()
                            Dlg.setText(R.string.done)
                            Dlg.dismissButton()
                        }
                        is ShellResult.Error -> Dlg.showError(result)
                    }
                }
            }.start()
        }
        Dlg.setNo(R.string.revios) {
            downloadPlaybook("ReviOS", "https://github.com/n00b69/modified-playbooks/releases/download/ReviOS/ReviPlaybook.apbx", "ReviPlaybook.apbx")
        }
        Dlg.setYes(R.string.atlasos) {
            downloadPlaybook("AtlasOS", "https://github.com/n00b69/modified-playbooks/releases/download/AtlasOS/AtlasPlaybook.apbx", "AtlasPlaybook.apbx")
        }
    }

    private fun setupUsbHost() {
        setupToolboxAction(R.string.usbhost_question, R.drawable.ic_mnt, ToolboxDeployer::deployUsbHost)
    }

    private fun setupRotation() {
        setupToolboxAction(R.string.rotation_question, R.drawable.ic_disk, ToolboxDeployer::deployRotation)
    }

    private fun setupTabletMode() {
        setupToolboxAction(R.string.tablet_question, R.drawable.ic_sensor, ToolboxDeployer::deployTabletMode)
    }

    private fun setupFrameworks() {
        if (!isNetworkConnected(this)) { noInternet(); return }
        Dlg.show(this, R.string.setup_question, R.drawable.ic_mnt)
        Dlg.setNo(R.string.no) { Dlg.close() }
        Dlg.setYes(R.string.yes) {
            Dlg.dialogLoading()
            Dlg.setIcon(R.drawable.ic_download)
            Dlg.setBar(0)
            Thread {
                val mountResult = MountManager.requireWritable()
                if (mountResult is ShellResult.Error) {
                    postUi { Dlg.showMountError(mountResult.message) }
                    return@Thread
                }
                val result = ToolboxDeployer.deployFrameworks(filesDir.absolutePath)
                postUi {
                    when (result) {
                        is ShellResult.Success -> {
                            Dlg.setIcon(R.drawable.ic_mnt)
                            Dlg.hideBar()
                            Dlg.setText(R.string.done)
                            Dlg.dismissButton()
                        }
                        is ShellResult.Error -> Dlg.showError(result)
                    }
                }
            }.start()
        }
    }

    private fun setupDefenderEdge() {
        Dlg.show(this, R.string.defender_question, R.drawable.edge2)
        Dlg.setNo(R.string.no) { Dlg.close() }
        Dlg.setYes(R.string.yes) {
            Dlg.dialogLoading()
            Dlg.setBar(0)
            Thread {
                val mountResult = MountManager.requireWritable()
                if (mountResult is ShellResult.Error) {
                    postUi { Dlg.showMountError(mountResult.message) }
                    return@Thread
                }
                val result = ToolboxDeployer.deployDefenderEdge(filesDir.absolutePath, isNetworkConnected(this@MainActivity))
                postUi {
                    when (result) {
                        is ShellResult.Success -> {
                            Dlg.setText(R.string.done)
                            Dlg.dismissButton()
                        }
                        is ShellResult.Error -> Dlg.showError(result)
                    }
                }
            }.start()
        }
    }

    override fun onResume() {
        super.onResume()
        MountManager.init(filesDir, this)
        updateMountText()
        updateSettingsCheckboxes()
        if (!ShellManager.isRootGranted()) {
            Dlg.show(this, R.string.nonroot)
            Dlg.setCancelable(false)
        }
        checkUefi()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            backInvokedCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        }
        if (instance?.get() == this) {
            instance?.clear()
            instance = null
        }
    }

    private fun copyAssets() {
        assets.list("")?.forEach { filename ->
            try {
                assets.open(filename).use { input ->
                    File(filesDir, filename).outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (_: IOException) {}
        }
        ShellManager.exec("chmod 644 ${filesDir.absolutePath}/libfuse-lite.so && chown root:root ${filesDir.absolutePath}/libfuse-lite.so")
        ShellManager.exec("chmod 644 ${filesDir.absolutePath}/libntfs-3g.so && chown root:root ${filesDir.absolutePath}/libntfs-3g.so")
        ShellManager.exec("chmod 755 ${filesDir.absolutePath}/mount.ntfs && chown root:root ${filesDir.absolutePath}/mount.ntfs")
    }

    private fun checkUefi() {
        // Permanent Mu boot image: no file discovery or boot rewrite is needed.
    }

    private fun checkWin() {
        Thread {
            if (!ShellManager.isRootGranted()) return@Thread
            val hasPartition = MountManager.getWinPartition().isNotEmpty()
            postUi {
                if (!hasPartition && !BuildConfig.DEBUG) {
                    Dlg.show(this@MainActivity, R.string.partition)
                    Dlg.setCancelable(false)
                    Dlg.setYes(R.string.guide) { openLink(this@MainActivity, Device.getVars().guideLink) }
                    listOf(mainBinding.mnt, mainBinding.toolbox, mainBinding.quickBoot, toolboxBinding.flashUefi).forEach { it.isEnabled = false }
                }
            }
        }.start()
    }

    private fun checkUpdate() = Unit
    private fun checkUpdate(manual: Boolean) {
        if (manual) { Dlg.show(this, R.string.degas_update); Dlg.dismissButton() }
    }

    private fun noInternet() {
        Dlg.show(this, R.string.internet)
        Dlg.dismissButton()
    }

    fun updateLastBackupDate() {
        BackupManager.updateDate { date ->
            Pref.setDate(this, date)
            mainBinding.tvDate.text = getString(R.string.last, date)
        }
    }

    fun updateMountText() {
        runOnUiThread {
            val mounted = getString(if (MountManager.isMounted()) R.string.unmountt else R.string.mountt)
            mainBinding.mnt.setTitle(getString(R.string.mnt_title, mounted))
            updateSettingsCheckboxes()
        }
    }

    fun quickbootUI(filesDir: File) {
        Dlg.show(this, R.string.degas_reboot_subtitle)
        Dlg.setNo(R.string.no) { Dlg.close() }
        Dlg.setYes(R.string.reboot) {
            Thread {
                MountManager.init(filesDir, this)
                if (MountManager.isMounted()) {
                    val unmount = MountManager.unmount()
                    if (unmount is ShellResult.Error) {
                        postUi { Dlg.showError(unmount) }
                        return@Thread
                    }
                }
                val result = ShellManager.execResult("/system/bin/svc power reboot")
                if (result is ShellResult.Error) postUi { Dlg.showError(result) }
            }.start()
        }
    }

    fun mountUI(filesDir: File) {
        MountManager.init(filesDir, this)
        if (!MountManager.isMounted()) {
            Dlg.show(this, R.string.degas_mount_question)
            Dlg.setDismiss(R.string.no) { Dlg.close() }
            Dlg.setNo(R.string.degas_mount_ro) { finishMount(false) }
            Dlg.setYes(R.string.degas_mount_rw) { finishMount(true) }
            return
        }

        MountManager.init(filesDir, this)
        val wasMounted = MountManager.isMounted()
        val question = if (wasMounted) R.string.unmount_question else R.string.mount_question
        Dlg.show(this, if (wasMounted) getString(question) else getString(question, MountManager.getWinPath()), R.drawable.ic_mnt)
        Dlg.setNo(R.string.no) { Dlg.close() }
        Dlg.setYes(R.string.yes) {
            Dlg.dialogLoading()
            Thread {
                val result = if (wasMounted) MountManager.unmount() else MountManager.mount()
                val isNowMounted = MountManager.isMounted()
                postUi {
                    updateMountText()
                    if (result is ShellResult.Success) MountWidget.requestUpdate(this)
                    if (wasMounted) {
                        when (result) {
                            is ShellResult.Success -> { Dlg.setText(R.string.unmounted); Dlg.dismissButton() }
                            is ShellResult.Error -> {
                                Dlg.hideIcon()
                                Dlg.setText("${getString(R.string.wrong)}\n\n${result.message}")
                                Dlg.setYes(R.string.chat) { openLink(this, Device.getVars().groupLink) }
                                Dlg.setNo(R.string.dismiss) { Dlg.close() }
                            }
                        }
                    } else if (isNowMounted) {
                        Dlg.setText("${getString(R.string.mounted)}\n\n${MountManager.getWinPath()}")
                        Dlg.dismissButton()
                    } else {
                        val errorMsg = (result as? ShellResult.Error)?.message ?: "Unknown error"
                        Dlg.showMountError(errorMsg)
                    }
                }
            }.start()
        }
    }

    private fun finishMount(writable: Boolean) {
        Dlg.dialogLoading()
        Thread {
            val result = MountManager.mount(writable)
            postUi {
                updateMountText()
                if (result is ShellResult.Error) Dlg.showError(result)
                else { Dlg.setText("${getString(R.string.mounted)}\n${MountManager.getWinPath()}"); Dlg.dismissButton(); MountWidget.requestUpdate(this) }
            }
        }.start()
    }

    companion object {
        @JvmStatic
        var instance: WeakReference<MainActivity>? = null
        private var autoUpdateChecked = false

        @JvmStatic
        fun isNetworkConnected(context: Context): Boolean {
            val connectivityManager = context.getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager
            val activeNetwork = connectivityManager.activeNetwork
            val capabilities = connectivityManager.getNetworkCapabilities(activeNetwork)
            return capabilities != null && (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                   capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) ||
                   capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET))
        }

        fun getBoot(): String {
            val slotSuffix = ShellManager.exec("getprop ro.boot.slot_suffix")
            if (slotSuffix !in setOf("_a", "_b")) return ""
            val partition = ShellManager.exec("test -b /dev/block/by-name/boot$slotSuffix && realpath /dev/block/by-name/boot$slotSuffix")
            return if (Regex("/dev/block/[A-Za-z0-9_./-]+").matches(partition)) partition else ""
        }

        @JvmStatic
        fun showBlur(activity: MainActivity) {
            activity.blurCount++
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val blurEffect = RenderEffect.createBlurEffect(15f, 15f, Shader.TileMode.CLAMP)
                activity.findViewById<View>(android.R.id.content).setRenderEffect(blurEffect)
            } else {
                runSilently {
                    listOf(activity.mainBinding.blur, activity.settingsBinding.blur, activity.toolboxBinding.blur).forEach {
                        it.visibility = View.VISIBLE
                    }
                }
            }
        }

        @JvmStatic
        fun hideBlur(activity: MainActivity, check: Boolean) {
            if (!check) activity.blurCount = 1
            activity.blurCount--
            if (activity.blurCount > 0) return
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                activity.findViewById<View>(android.R.id.content).setRenderEffect(null)
            } else {
                runSilently {
                    listOf(activity.mainBinding.blur, activity.settingsBinding.blur, activity.toolboxBinding.blur).forEach {
                        it.visibility = View.GONE
                    }
                }
            }
        }

        @JvmStatic
        fun runSilently(action: () -> Unit) {
            try { action() } catch (_: Exception) {}
        }

        fun openLink(context: Context, link: String) {
            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(link)))
        }
    }
}
