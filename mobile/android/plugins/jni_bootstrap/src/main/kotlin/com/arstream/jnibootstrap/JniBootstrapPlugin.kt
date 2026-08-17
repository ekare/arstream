package com.arstream.jnibootstrap

import android.app.Activity
import android.util.Log
import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin
import java.util.zip.ZipFile

// JNIEnv*/Activity referansini native GDExtension'a bir kez aktarir. Bu
// dosya sifir ARCore/is-mantigi icerir -- yalniz pointer aktarimi. Tum
// ARCore mantigi C++ tarafinda yasar (mobile/native/src/capture/android/),
// arcore_c_api.h'ye bu sinifin aktardigi JNIEnv*/Activity ile dogrudan
// erisir. Bkz. docs/ARCHITECTURE.md "JNI bootstrap istisnasi".
class JniBootstrapPlugin(godot: Godot) : GodotPlugin(godot) {

    companion object {
        private const val TAG = "JniBootstrap"
    }

    override fun getPluginName(): String = "JniBootstrap"

    // Godot'un cekirdek motoru (ve bizim GDExtension'imiz) baslatmayi
    // bitirdiginde bir kez cagrilir -- context aktarimi icin guvenli nokta.
    override fun onGodotMainLoopStarted() {
        super.onGodotMainLoopStarted()
        val act = activity ?: return
        if (loadArcaptureLibrary(act)) {
            nativeSetContext(act)
            // onGodotMainLoopStarted() calisir Godot'un kendi motor
            // thread'inde ("VkThread"), Android'in gercek UI thread'inde
            // DEGIL (gozlemlendi -- ilk varsayimimizin aksine, Activity
            // yasam-dongusu callback'i gibi görünse de Godot bunu kendi
            // ic thread'inden cagiriyor). ArCoreApk_checkAvailabilityAsync
            // Looper'i olmayan bir thread'den cagrilinca native tarafta
            // SIGABRT ile cokuyor (gozlemlendi, iki kez). Bu yuzden
            // runOnUiThread ile acikca Android'in gercek UI thread'ine
            // (Looper'i olan tek thread) tasiyoruz.
            act.runOnUiThread {
                nativeCheckArcoreAvailability()
            }
        }
    }

    // Godot'un SCons derlemesi GDExtension .so'sunu sabit "libarcapture.so"
    // degil, "libarcapture.<platform>.<target>.<arch>.so" olarak
    // adlandiriyor (bkz. mobile/native/SConstruct) -- ad build varyantina
    // gore degisiyor. Godot motoru bu dosyayi kendi ic dlopen'iyla zaten
    // yukluyor (tam yoldan, raw C dlopen -- Java ClassLoader'in kutuphane
    // takip mekanizmasindan tamamen bagimsiz), ama JNI'nin external fun
    // icin yaptigi otomatik sembol aramasi yalniz BU sinifin ClassLoader'i
    // uzerinden System.load/loadLibrary ile yuklenmis kutuphanelerde
    // ariyor -- Godot'un kendi dlopen'i bunu kapsamiyor, "No implementation
    // found" ile sonuclaniyordu (gozlemlendi, bkz. commit gecmisi).
    //
    // File(nativeLibraryDir).listFiles() ile bulmayi denedik ama bu cihaz
    // native kutuphaneleri diske cikarmiyor, dogrudan (sikistirilmamis)
    // APK icinden yukluyor (bkz. logcat'teki "base.apk!/lib/arm64-v8a"
    // izi) -- o dizin bos donuyordu (gozlemlendi). Bunun yerine dosya adini
    // APK'nin kendi zip girdilerinden okuyup System.loadLibrary (ad-
    // tabanli, ClassLoader.findLibrary uzerinden) kullaniyoruz --
    // PathClassLoader'in bu cagirisi hem cikarilmis-dizin hem APK-ici
    // sikistirilmamis-kutuphane durumlarini seffaf sekilde ele aliyor.
    private fun loadArcaptureLibrary(act: Activity): Boolean {
        val apkPath = act.applicationInfo.sourceDir
        var libBaseName: String? = null
        ZipFile(apkPath).use { zip ->
            for (entry in zip.entries()) {
                val fileName = entry.name.substringAfterLast('/')
                if (entry.name.startsWith("lib/") && fileName.startsWith("libarcapture") && fileName.endsWith(".so")) {
                    libBaseName = fileName.removePrefix("lib").removeSuffix(".so")
                    break
                }
            }
        }
        val libName = libBaseName
        if (libName == null) {
            Log.e(TAG, "libarcapture*.so not found in APK ($apkPath)")
            return false
        }
        System.loadLibrary(libName)
        return true
    }

    // JNI, ikinci parametreden itibaren (JNIEnv*, jobject bu sinifin
    // ornegi) otomatik saglar; `activity` uculcu, acik parametredir.
    // Karsiligi: mobile/native/src/platform/android/android_context.cpp,
    // Java_com_arstream_jnibootstrap_JniBootstrapPlugin_nativeSetContext.
    private external fun nativeSetContext(activity: Activity)

    // Karsiligi: mobile/native/src/ar_capture.cpp,
    // Java_com_arstream_jnibootstrap_JniBootstrapPlugin_nativeCheckArcoreAvailability.
    private external fun nativeCheckArcoreAvailability()
}
