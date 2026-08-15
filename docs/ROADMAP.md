# Yol haritası

## Kilometre taşları (aktif geliştirme)

| # | Aşama | Durum |
|---|---|---|
| M0 | Cihaz yetenek doğrulama spike'ı | Kod-gerektirmeyen kısım tamam (bkz. `DEVICE_COMPATIBILITY.md`); runtime `ArCoreApk_checkAvailability()` + sensör zaman damgası drift testi M6/M2'ye ertelendi (JNI bootstrap olmadan ARCore'un C API'si çağrılamıyor) |
| M1 | GDExtension iskeleti (Android arm64) | **Tamamlandı** — SCons+godot-cpp+NDK 29 ile derlendi, Godot 4.6.1 export toolchain'i (export template + JDK + debug keystore) kuruldu, A30s'e APK kurulup ping/pong round-trip doğrulandı |
| M2 | Camera2 geri-düşüş + IMU yakalama | Başlamadı |
| M3 | AMediaCodec H.264 encode (yerel dosya) | Başlamadı |
| M4 | Protokol v1 (C++ + Python paralel) | Başlamadı — spesifikasyon hazır (`PROTOCOL.md`) |
| M5 | Uçtan uca aynı-LAN akış testi | Başlamadı |
| M6 | ARCore yakalama yolu (pose/point-cloud/intrinsics) | Başlamadı — gerçek cihaz doğrulaması ARCore-destekli donanım netleşince |
| M7 | GitHub kalite cila + CI | Başlamadı |

## Ertelenen (v2 / sonraki faz, şimdi inşa edilmiyor)

- **iOS/ARKit desteği** — `.mm` (Objective-C++) köprüsü ile `arkit_capture_session.mm`, `avf_capture_session.mm`, `imu_sampler_ios.mm`. Şu an test cihazı yok.
- **v2 taşıma katmanı** — UDP/QUIC veya WebRTC-data-channel üzerinden aynı mesaj formatı; NAT-arkası/kayıplı internet bağlantıları için. v1'in TCP-only tasarımı bunu engellemiyor, yalnız ertelemiş oluyor.
- **TLS/kimlik doğrulama** — v1 güvenilir ağ varsayıyor (bkz. `PROTOCOL.md` §6). Öneri: protokole kripto eklemek yerine WireGuard/Tailscale/SSH tünel.
- **32-bit (`armv7`) hedef derlemesi** — çok eski cihazlar için düşünülebilir, A30s zaten arm64, öncelik değil.
- **Yazılım (software) H.264 encoder geri-düşüşü** — donanım AVC encoder'ı olmayan cihaz nadir kabul ediliyor; böyle bir cihaz gerçekten ortaya çıkarsa o zaman ele alınır.
