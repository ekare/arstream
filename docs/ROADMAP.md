# Yol haritası

## Kilometre taşları (aktif geliştirme)

| # | Aşama | Durum |
|---|---|---|
| M0 | Cihaz yetenek doğrulama spike'ı | Kod-gerektirmeyen kısım tamam (bkz. `DEVICE_COMPATIBILITY.md`); runtime `ArCoreApk_checkAvailability()` + sensör zaman damgası drift testi M6/M2'ye ertelendi (JNI bootstrap olmadan ARCore'un C API'si çağrılamıyor) |
| M1 | GDExtension iskeleti (Android arm64) | **Tamamlandı** — SCons+godot-cpp+NDK 29 ile derlendi, Godot 4.6.1 export toolchain'i (export template + JDK + debug keystore) kuruldu, A30s'e APK kurulup ping/pong round-trip doğrulandı |
| M2 | Camera2 geri-düşüş yakalama | **Tamamlandı** (IMU örnekleme hariç — henüz eklenmedi) — NDK Camera2, encoder'ın giriş surface'ine doğrudan (sıfır-kopya) yazıyor, A30s'de kanıtlandı |
| M3 | AMediaCodec H.264 encode (yerel dosya) | **Tamamlandı** — "save"/"stream" seçenekli `OutputSink` mimarisi; `FileSink` A30s'de ~797 kare (14 SPS/PPS+IDR döngüsü, 783 P-frame) geçerli Annex-B akışı üretti, NAL yapısı Python ile doğrulandı; `StreamSink` bilinçli kük (M4/M5) |
| M4 | Protokol v1 (C++ + Python paralel) | **Tamamlandı** — `net/protocol.{h,cpp}` (C++, godot-cpp bağımsız, host+Android+Windows'ta derleniyor) ve `arstream_server.protocol` (Python) `fixtures/*.bin` golden-byte dosyalarına karşı test edildi (34 C++ kontrolü + 16 pytest, ikisi de 0 hata). JSON gövdeli mesajlarda kasıtlı asimetri var: Python tam encode/decode yapıyor, C++ çerçevelemeyi doğruluyor ama JSON'un kendisini ayrıştırmıyor (bağımlılıksız kalmak için) — bkz. `protocol.h` başlığındaki not. `StreamSink` hâlâ M5'i bekliyor. |
| — | UI: portrait mod, sol-üst overlay, kayıttan bağımsız (async) önizleme | **Tamamlandı** — aynı capture session'ın ikinci (640x360) `AImageReader` çıkışı, yalnız `preview_enabled` iken CPU'da işleniyor; A30s'de kayıt fps'i etkilenmeden (29.5) açılıp kapandığı doğrulandı; gerçek kamera verisi geldiği geçici bir tanılama logu ile sayısal kanıtlandı (ortalama piksel değeri) |
| M5 | Uçtan uca aynı-LAN akış testi | Başlamadı |
| M6 | ARCore yakalama yolu (pose/point-cloud/intrinsics) | Başlamadı — gerçek cihaz doğrulaması ARCore-destekli donanım netleşince |
| M7 | GitHub kalite cila + CI | Başlamadı |

## Ertelenen (v2 / sonraki faz, şimdi inşa edilmiyor)

- **iOS/ARKit desteği** — `.mm` (Objective-C++) köprüsü ile `arkit_capture_session.mm`, `avf_capture_session.mm`, `imu_sampler_ios.mm`. Şu an test cihazı yok.
- **v2 taşıma katmanı** — UDP/QUIC veya WebRTC-data-channel üzerinden aynı mesaj formatı; NAT-arkası/kayıplı internet bağlantıları için. v1'in TCP-only tasarımı bunu engellemiyor, yalnız ertelemiş oluyor.
- **TLS/kimlik doğrulama** — v1 güvenilir ağ varsayıyor (bkz. `PROTOCOL.md` §6). Öneri: protokole kripto eklemek yerine WireGuard/Tailscale/SSH tünel.
- **32-bit (`armv7`) hedef derlemesi** — çok eski cihazlar için düşünülebilir, A30s zaten arm64, öncelik değil.
- **Yazılım (software) H.264 encoder geri-düşüşü** — donanım AVC encoder'ı olmayan cihaz nadir kabul ediliyor; böyle bir cihaz gerçekten ortaya çıkarsa o zaman ele alınır.
