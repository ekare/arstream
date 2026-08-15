# arstream

Bir mobil uygulama (Godot, Android önce/iOS sonra), ARCore ile kamera görüntüsü ve IMU verisini yakalar, gerçek zamanlı olarak donanım H.264 encoder'ıyla sıkıştırır ve tek bir TCP bağlantısı üzerinden özel, versiyonlanmış bir ikili protokolle bir "destination" sunucusuna akıtır. Depo ayrıca bu protokolü konuşan bir Python referans sunucusu içerir — protokolün hem çalışan bir örneği hem de yaşayan dokümantasyonu.

Native (kamera yakalama, encode, ağ) tarafı **GDExtension (C/C++)** ile yazılıyor — Kotlin/Swift'te iş mantığı yok; aynı C++ kod tabanı platforma göre derlenir. İki dar istisna, ikisi de mantık değil yalnızca köprü: (1) Android'de Godot'un GDExtension'ı JNIEnv/Activity context'ine resmi bir yolla veremediği için (bkz. [godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734)) ARCore'u başlatabilmek amacıyla ~20 satırlık, sıfır iş-mantığı içeren bir Kotlin "bootstrap shim" kullanılıyor — tek işi bu iki pointer'ı native tarafa iletmek; ARCore'un tüm gerçek mantığı (`arcore_c_api.h` ile) C++'ta kalıyor. (2) iOS'ta ARKit/AVFoundation/CoreMotion'ın C API'si olmadığından ince bir Objective-C++ köprüsü gerekecek (henüz inşa edilmedi — bkz. Yol Haritası).

## Mimari

Kısa özet: telefon → `ArCapture` (GDExtension) → ARCore veya Camera2 geri-düşüş → `AMediaCodec` H.264 → özel ikili protokol → TCP → `server/` (Python, PyAV ile çözer).

Ayrıntılı diyagram ve tasarım gerekçeleri: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Kablo protokolü

Dış bir "adaptör" bu akışı tüketecekseniz tek ihtiyacınız olan doküman: [`docs/PROTOCOL.md`](docs/PROTOCOL.md). Sürüm: v1.0.

## Cihaz uyumluluğu

ARCore, tüm Android cihazlarda çalışmaz (Android 7.0+ VE Google'ın sertifikalı-cihaz listesi gerekir). Uygulama, ARCore yoksa/desteklenmiyorsa ham `Camera2` + `SensorManager` yakalamaya zarif biçimde düşer. Test edilen cihazlar ve bulgular: [`docs/DEVICE_COMPATIBILITY.md`](docs/DEVICE_COMPATIBILITY.md).

## Durum

M1 tamamlandı: `ArCapture` GDExtension'ı (C++) Android arm64 için derleniyor, Godot 4.6.1 export zinciriyle APK üretiliyor, Samsung Galaxy A30s'e kurulup GDScript↔native ping/pong round-trip'i doğrulandı. Gerçek kamera/IMU yakalama, encode ve ağ katmanı henüz yok (M2+). İlerleme: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Hızlı başlangıç

Gerekli araçlar (sürümler `mobile/native/SConstruct` ve bu depoda doğrulandı):

- Godot 4.6.x editör (export şablonlarıyla birlikte)
- Android NDK **29.0.14206865**, `$ANDROID_HOME/ndk/29.0.14206865` altında (veya `ANDROID_NDK_ROOT` ortam değişkeni)
- Android SDK (build-tools + platform-tools), Godot Editor Settings'te `export/android/android_sdk_path`
- JDK 21+ (apksigner için), Editor Settings'te `export/android/java_sdk_path`
- Debug imzalama anahtarı — Editor Settings veya `mobile/export_presets.cfg`'deki `keystore/debug` yolunda bir PKCS12/JKS dosyası (`openssl req`+`openssl pkcs12 -export` ile üretilebilir, `keytool` şart değil)
- SCons (`pip install scons`)

```bash
# native (GDExtension) derleme -- Android
cd mobile/native
ANDROID_NDK_ROOT=/path/to/ndk/29.0.14206865 scons platform=android arch=arm64 target=template_debug

# editorun kendi platformunda da derlemek gerekir (aksi halde Godot projeyi acarken
# GDExtension'i yukleyemez ve export'u engeller):
scons platform=windows target=template_debug   # veya linux/macos, host'a gore

# headless export (Godot editorunu hic acmadan)
godot --headless --path mobile --export-debug "Android" ../build/arstream-debug.apk

# cihaza kur
adb install -r build/arstream-debug.apk

# referans sunucu
cd server && pip install -e . && arstream-server --host 0.0.0.0 --port 9999 --out ./captures
```

## Lisans

[Apache-2.0](LICENSE).
