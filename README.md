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

Erken geliştirme aşaması — mimari kilitlendi, protokol spesifikasyonu v1.0 olarak yazıldı, ilk cihaz (Samsung Galaxy A30s) üzerinde ön-tanılama yapıldı. Kod tabanı henüz iskelet halinde. İlerleme: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Hızlı başlangıç

> Kod tabanı henüz M1 (GDExtension iskeleti) aşamasında değil — bu bölüm o aşama tamamlanınca gerçek build/run adımlarıyla doldurulacak.

Planlanan akış:
```bash
# native (GDExtension) derleme
cd mobile/native && scons platform=android arch=arm64 target=template_debug

# Godot editöründe mobile/ projesini aç, Android export preset'iyle APK üret

# referans sunucu
cd server && pip install -e . && arstream-server --host 0.0.0.0 --port 9999 --out ./captures
```

## Lisans

[Apache-2.0](LICENSE).
