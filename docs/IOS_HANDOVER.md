# iOS/ARKit Handover Dokümanı

Bu doküman, Mac (Mini veya başka) eline geçtiğinde iOS tarafına sıfırdan
başlamak zorunda kalmamak için yazıldı. Android tarafında (`mobile/native/`)
zaten inşa edilmiş, test edilmiş ve A30s'de doğrulanmış mimarinin iOS
karşılığını nasıl kuracağını anlatıyor — kod yazmıyor (Mac olmadan
derlenemez/test edilemez), ama TAM OLARAK ne yazılacağını, hangi dosyaların
hazır olduğunu, hangi kısmın Android'den bile daha kolay olduğunu ve
bilinmeyen/araştırılması gereken noktaları netleştiriyor.

**Önce oku:** [`ARCHITECTURE.md`](ARCHITECTURE.md) (özellikle "Mimari karar"
ve "JNI bootstrap istisnası" bölümleri) ve [`PROTOCOL.md`](PROTOCOL.md).
Aşağıdaki her şey bunların üzerine kurulu.

---

## 1. İyi haber: iOS, Android'den DAHA BASİT olacak

Android'de en büyük sürtünme noktası şuydu: Godot'un GDExtension'ı Android'de
`JNIEnv*`/`Activity` context'ine resmi bir yolla ulaşamıyor (bkz.
[godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734)),
bu yüzden ARCore'u başlatmak için minik bir Kotlin "bootstrap shim" gerekti.

**iOS'ta bu sorun YOK.** Objective-C++ (`.mm`), C++'ın üst kümesi — ARKit,
AVFoundation, CoreMotion sınıflarını (`ARSession`, `AVCaptureSession`,
`CMMotionManager`) DOĞRUDAN, hiçbir köprü/context-aktarımı olmadan
çağırabilirsiniz. Yani iOS tarafında **Swift'te hiçbir iş mantığı olmayacağı
gibi, Android'deki gibi bile minik bir bootstrap dosyasına gerek yok** —
sadece `.mm` uzantılı, saf C++ mantığının içine ARKit/AVFoundation çağrıları
serpiştirilmiş dosyalar.

Tek gerçek istisna, ARKit/AVFoundation/CoreMotion'ın C API'si olmaması —
bu yüzden bu üç framework'e dokunan kod `.mm` olmak zorunda (Obj-C++), ama
bu dosyaların içindeki mantık yine bizim kodumuz, üçüncü bir "plugin
sistemi" değil.

**VideoToolbox (H.264 encoder) ise C API'dir** (`VTCompressionSession`,
`<VideoToolbox/VideoToolbox.h>`) — Android'deki `AMediaCodec` ile birebir
aynı durum. Yani `encode/h264_encoder_ios.cpp` **saf C++ olarak
yazılabilir**, `.mm` olması bile gerekmez.

---

## 2. Değişmeden kullanılacak (zaten hazır, dokunmayın)

Bunların hiçbiri Android'e özgü değil — platform-bağımsız C++, olduğu gibi
iOS'ta da derlenip çalışacak:

| Dosya | Not |
|---|---|
| `net/protocol.h`, `net/protocol.cpp` | godot-cpp'ye bile bağımlı değil, tamamen taşınabilir. `mobile/native/tests/protocol_test.cpp` ile zaten test edildi. |
| `sink/output_sink.h` | Arayüz, platform yok. |
| `sink/file_sink.h/.cpp` | `fopen`/`fwrite` — POSIX, iOS'ta aynen çalışır. |
| `sink/stream_sink.h/.cpp` | Bellek-önce/disk-taşma tampon mantığı, `std::thread`/`std::mutex` — hepsi taşınabilir. **Az önce küçük bir düzeltme yapıldı** (bkz. §6). |
| `net/stream_client.h/.cpp` | POSIX BSD sockets (`socket`, `connect`, `send`, `getaddrinfo`) — Android ve iOS/Darwin'de birebir aynı API. **Az önce `IOS_ENABLED` icin de acildi** (bkz. §6). |

`ArCapture` (`ar_capture.h/.cpp`) da büyük ölçüde aynı kalır — yalnızca
Android-özgü `#ifdef ANDROID_ENABLED` bloklarının yanına paralel bir
`#ifdef IOS_ENABLED` bloğu eklenecek (bkz. §4).

---

## 3. Yeni yazılacak dosyalar (Android'deki karşılığıyla)

| iOS | Android karşılığı | Ne yapar |
|---|---|---|
| `capture/ios/arkit_capture_session.h/.mm` | `capture/android/camera2_capture_session.h/.cpp` | `ARSession` başlatır (`ARWorldTrackingConfiguration`), `ARSessionDelegate` ile kare+poz+point-cloud+intrinsics alır. |
| `capture/ios/avf_capture_session.h/.mm` | Camera2CaptureSession'ın geri-düşüş rolü | ARKit yoksa/istenmiyorsa `AVCaptureSession` + `AVCaptureVideoDataOutput` ile ham yakalama. |
| `capture/ios/imu_sampler_ios.h/.mm` | `ImuSampler` (Android'de ayrı dosya olarak hiç yazılmadı, NDK `ASensorManager` gerekirse eklenecekti) | `CMMotionManager` ile ham accelerometer/gyro. |
| `encode/h264_encoder_ios.h/.cpp` | `encode/h264_encoder_android.h/.cpp` | `VTCompressionSession`, saf C API — `.mm` bile olmasına gerek yok. |

SConstruct'ta bu dosyalar için glob zaten hazır (M1'den beri orada,
kullanılmadı):
```python
elif env["platform"] == "ios":
    sources += Glob("src/capture/ios/*.mm")
    sources += Glob("src/encode/*ios*.cpp")
```

**Eksik olan tek şey**: iOS'ta çıktı `SharedLibrary` değil `StaticLibrary`
olmalı (godot-cpp'nin kendi test projesindeki desen, bkz.
`mobile/thirdparty/godot-cpp/test/SConstruct`):
```python
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary("../bin/libarcapture.{}.{}.simulator.a".format(env["platform"], env["target"]), source=sources)
    else:
        library = env.StaticLibrary("../bin/libarcapture.{}.{}.a".format(env["platform"], env["target"]), source=sources)
else:
    library = env.SharedLibrary(...)  # mevcut kod
```
Bunu `mobile/native/SConstruct`'a eklemeden iOS derlemesi (muhtemelen)
linker hatasıyla patlar.

---

## 4. `ar_capture.h/.cpp`'de yapılacak değişiklik

Şu an:
```cpp
#ifdef ANDROID_ENABLED
#include "capture/android/camera2_capture_session.h"
#include "encode/h264_encoder_android.h"
...
#endif
```
Yanına, aynı şablonla:
```cpp
#elif defined(IOS_ENABLED)
#include "capture/ios/arkit_capture_session.h"
#include "encode/h264_encoder_ios.h"
...
#endif
```
`start_capture()`/`stop_capture()`/`on_preview_frame()` vb. içindeki gerçek
mantık neredeyse birebir kopyalanabilir — sınıf isimleri değişir
(`Camera2CaptureSession` → `ArkitCaptureSession`/`AvfCaptureSession`),
akış aynı kalır: encoder önce başlar (surface/pixel-buffer-havuzu hazır
olsun diye), sonra capture session başlar, `sensor_orientation_` sorgulanır
(bkz. §5).

`preview_width_`/`preview_height_`/`_update_preview_texture` tamamen
platform-bağımsız (Image/ImageTexture, Godot'un kendi sınıfları) — hiç
dokunulmaz.

---

## 5. Bilinmeyen/araştırılması gereken noktalar (Mac'e geçince ilk iş)

Bunlar Android'de karşılığı olup iOS'ta henüz doğrulanmamış şeyler —
"M0" muadili bir doğrulama turu gerekiyor:

1. **İzin isteme**: `OS.request_permission("android.permission.CAMERA")`
   Android'e özgüydü. Godot'un `OS` sınıfının iOS'ta kamera izni için bir
   karşılığı var mı (muhtemelen `OS.request_permission()` bir miktar
   cross-platform ama iOS desteği doğrulanmadı), yoksa native tarafta
   `AVCaptureDevice.requestAccess(for: .video)` mi çağırmak gerekecek —
   araştırılmalı. `Info.plist`'e `NSCameraUsageDescription` şart (export
   preset'te ayarlanır).

2. **Rotasyon**: Android'de `ACAMERA_SENSOR_ORIENTATION` (bkz.
   `camera2_capture_session.h`'deki `query_back_camera_sensor_orientation`)
   ile çözüldü. iOS'ta muadili `AVCaptureConnection.videoRotationAngle`
   (iOS 17+) veya eski `videoOrientation` API'si — hangisinin
   kullanılacağı hedef minimum iOS sürümüne bağlı, araştırılmalı. Protokol
   tarafında değişiklik gerekmez — `VIDEO_CONFIG`'in `rotation` alanı zaten
   platform-bağımsız (bkz. `PROTOCOL.md` §3.3).

3. **Zero-copy kare akışı**: Android'de `AMediaCodec_createInputSurface()`
   ile kamera doğrudan encoder'ın Surface'ine yazıyordu (CPU kopyası yok).
   iOS'ta `ARFrame.capturedImage`/`AVCaptureVideoDataOutput` bir
   `CVPixelBuffer` verir; bunu `VTCompressionSessionEncodeFrame()`'e
   doğrudan vermek mümkün ama gerçek sıfır-kopya için pixel buffer
   havuzunun (`CVPixelBufferPool`) encoder'ın beklediği formatla eşleşmesi
   gerekiyor — ilk denemede CPU kopyalı basit yol ile başlayıp
   (`AMediaCodec` öncesi Android yaklaşımına benzer), sonra optimize etmek
   makul bir sıra.

4. **Minimum iOS sürümü**: Android'de API 26 gereksinimi derleme sırasında
   ortaya çıktı (`AMediaCodec_createInputSurface` API 26+). iOS'ta ARKit
   zaten iOS 11+ istiyor, VideoToolbox çok daha eski — muhtemelen sorun
   olmaz ama derleme sırasında netleşecek.

5. **Editör-içi yükleme**: M1'de Windows için ayrı bir derleme gerekmişti
   (Godot editörü kendi platformunda uzantıyı yükleyemezse export'u
   engelliyordu). Mac'te editör macOS native çalışacağı için bu sefer
   `scons platform=macos target=template_debug` gerekecek (Android/iOS'a
   ek olarak, ayrı bir üçüncü derleme) — `mobile/arcapture.gdextension`'a
   `macos.debug`/`macos.release` girdilerini de eklemek gerekir.

---

## 6. Bu oturumda şimdiden yapılan hazırlıklar

- **`stream_client.cpp`**: derleme koşulu `#ifdef ANDROID_ENABLED` →
  `#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)` olarak genişletildi.
  POSIX sockets kodu Android ve iOS'ta birebir aynı olduğu için bu güvenli,
  mekanik bir düzeltmeydi (Mac gerektirmedi, Android derlemesiyle
  doğrulandı) — StreamSink/StreamClient artık iOS'ta da gerçekten
  çalışacak, ek kod gerekmeden.
- Bu doküman (`docs/IOS_HANDOVER.md`).

---

## 7. Araç zinciri (Mac'te kurulacaklar)

- Xcode (en güncel stabil sürüm) — App Store'dan.
- Godot 4.6.x editör (macOS `.dmg`/`.zip`) — export template paketi
  (`Godot_v4.6.1-stable_export_templates.tpz`, zaten indirilmiş
  Windows'takiyle **aynı dosya**, platform-bağımsız) iOS ve macOS
  template'lerini de içeriyor, ayrıca indirmeye gerek yok, `%APPDATA%`
  yerine `~/Library/Application Support/Godot/export_templates/`'e
  aynı şekilde açılır.
- SCons (`pip install scons` — Windows'takiyle aynı adım).
- `godot-cpp` submodule'ü zaten repoda (`git submodule update --init`).
- Apple ID (ücretsiz) fiziksel cihaza debug build kurmak için yeterli
  (7 günde bir yeniden imzalama gerekir); TestFlight/App Store için
  ücretli Apple Developer hesabı ($99/yıl) gerekir — şimdilik gerekmez.

**Derleme komutları** (Android/Windows deseninin aynısı):
```bash
# iOS cihaz (arm64, simulator degil)
cd mobile/native
scons platform=ios arch=arm64 ios_simulator=no target=template_debug

# editorun kendi platformu (macOS) icin de derlemek gerekir
scons platform=macos target=template_debug
```

---

## 8. Önerilen kilometre taşı sırası (Android M1-M6 ile paralel)

| # | Aşama | Doğrulama |
|---|---|---|
| iM0 | Araç zinciri + `platform=macos` derlemesi + boş projede editör açılışı | Android M1'in editör-yükleme kısmına denk |
| iM1 | `avf_capture_session.mm` (ARKit değil, önce ham AVFoundation) + `imu_sampler_ios.mm` | Kare/Hz sayaçları, crash yok |
| iM2 | `h264_encoder_ios.cpp` (VideoToolbox), yerel dosyaya kayıt | `ffprobe` ile doğrula (Android M3 ile aynı yöntem) |
| iM3 | Uçtan uca aynı-LAN akış testi | **`server/` hiç değişmeden çalışmalı** — protokol zaten platform-bağımsız test edildi |
| iM4 | Rotasyon (§5.2) | Ekran görüntüsüyle görsel doğrulama (Android'de yaptığımız gibi) |
| iM5 | `arkit_capture_session.mm` (ARKit'in kendisi) | Gerçek cihazda pose/point-cloud/intrinsics — **Simulator'da ARKit çalışmaz, gerçek iPhone/iPad şart** |

iM0-iM4 aslında **fiziksel bir iPhone olmadan, sadece Mac ile** yapılabilir
(Simulator'da AVFoundation kamera desteklenmez ama derleme/link/protokol
doğrulaması Mac tek başına yeterli — bkz. konuşmamızdaki SimCam notu,
CI'da kare-üretimi testi için ileride kullanılabilir). iM5 (ARKit'in
gerçekten çalışması) için gerçek cihaz şart.
