# Mimari

## Genel bakış

```
┌─────────────────────────── mobile/ (Godot projesi) ───────────────────────────┐
│                                                                                  │
│   GDScript (UI/orkestrasyon)              native/ (GDExtension, C++)            │
│   ┌──────────────────┐                    ┌─────────────────────────────────┐  │
│   │ scenes/Main.tscn  │  metod/sinyal      │ ArCapture (Godot sınıfı)         │  │
│   │ CaptureService.gd │◄──────────────────►│  ├─ CaptureController            │  │
│   │ (Idle/Connecting/ │  (düşük frekans)   │  │   ├─ ArCoreCaptureSession     │  │
│   │  Streaming/Error) │                    │  │   │   (arcore_c_api.h)       │  │
│   └──────────────────┘                    │  │   └─ Camera2CaptureSession    │  │
│                                             │  │       (NDK libcamera2ndk)    │  │
│                                             │  ├─ ImuSampler (NDK ASensor*)    │  │
│                                             │  ├─ H264Encoder (NDK AMediaCodec)│  │
│                                             │  └─ StreamClient+ProtocolWriter │  │
│                                             │      (BSD sockets, TCP)         │  │
│                                             └──────────────┬──────────────────┘  │
└────────────────────────────────────────────────────────────┼─────────────────────┘
                                                               │ TCP, docs/PROTOCOL.md
                                                               ▼
                                      ┌─────────────────────────────────────────┐
                                      │ server/ (Python referans backend)         │
                                      │  session.py  (HELLO→ACK→CLOCK_SYNC→...)  │
                                      │  protocol.py (aynı format, ayrı impl.)    │
                                      │  video_decoder.py (PyAV, Annex-B→kare)    │
                                      │  sinks/ (disk / queue)                    │
                                      └─────────────────────────────────────────┘
                                                               │
                                                               ▼
                                        (dış adaptör — başka yerde geliştiriliyor,
                                         docs/PROTOCOL.md'ye uyar)
```

## Temel kararlar ve gerekçeleri

| Karar | Gerekçe |
|---|---|
| Godot + GDExtension (C/C++), Kotlin/Swift'te İŞ MANTIĞI yok | ARCore'un resmi C API'si (`arcore_c_api.h`) + Android NDK'nın Camera2/MediaCodec/Sensor C API'leri bunu mümkün kılıyor. Camera2 geri-düşüş, `AMediaCodec` encode, `ASensorManager` IMU — hepsi saf C/C++, JNIEnv gerekmez, doğrulandı. ARCore ve iOS için aşağıdaki dar istisnalar var (bkz. "JNI bootstrap istisnası"). |
| Sıcak veri yolu (yakalama→encode→ağ) tamamen native, GDScript'e hiç uğramıyor | 30fps'de GDScript↔native marshalling'in hiçbir faydası yok; native tarafta kalmak hem performans hem ARCore/NDK API erişimi için doğal. GDScript yalnız UI/yaşam döngüsü. |
| ARCore vs Camera2 geri-düşüş, `CaptureController` içinde runtime kararı | Eski/düşük-uçlu cihazlarda ARCore desteklenmeyebilir (bkz. `docs/DEVICE_COMPATIBILITY.md`) — uygulama çökmek yerine ham kamera+IMU yakalamaya düşer. |
| Tek TCP bağlantısı, özel ikili çerçeveleme (WebRTC/UDP değil) | Godot'un yerleşik WebRTC desteği yalnız data-channel (RTP/media track yok) — video için gerçek bir kazanç sağlamıyor. Basit, debug edilebilir, "sade" felsefesiyle tutarlı. v2'de aynı mesaj formatı farklı bir taşıma üzerinden (WebRTC data-channel, UDP/QUIC) sunulabilir — bkz. `docs/ROADMAP.md`. |
| Annex-B NAL çerçeveleme (AVCC değil) | Hem `AMediaCodec` hem `VTCompressionSession`'ın doğal çıktısı, PyAV/ffmpeg'in doğal girdisi — dönüştürme adımı yok. |
| Python referans sunucu, dış adaptörün YERİNE değil | `server/`, protokolün "yaşayan dokümantasyonu" ve kendi doğrulama alıcımız — üretim ingestion servisi değil. |

## JNI bootstrap istisnası (Android)

Godot 4.x'in GDExtension sistemi, Android'de native koddan `JNIEnv*`/`Activity` context'ine erişim için **resmi bir yol sunmuyor** — Godot 3.x'in GDNative'inde vardı (`godot_android_get_env()`), 4.x'e taşınmadı ve hâlâ açık bir motor eksikliği: [godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734). Topluluğun bulduğu tek çözüm (ve mevcut tek prior-art projesi [`paddy-exe/arcore-gdextension`](https://github.com/paddy-exe/arcore-gdextension)'ın yaptığı da bu): minik bir Android Plugin (Kotlin), Godot tarafından örneklendiğinde eline geçen `JNIEnv*`/`Activity` pointer'larını bir native metoda iletir, başka hiçbir şey yapmaz.

**Kapsam dışı bırakılanlar** (JNIEnv gerekmiyor, saf C/C++, doğrulandı):
- Camera2 geri-düşüş yakalama (`libcamera2ndk`)
- `AMediaCodec` H.264 encode
- `ASensorManager`/`ASensorEventQueue` IMU örnekleme (`ASensorManager_getInstanceForPackage` bir paket adı ister, Context değil)
- Ağ/protokol katmanı (BSD sockets)
- Kamera izni isteme (`OS.request_permission("CAMERA")` — GDScript'ten, Godot'un kendi motor API'si, bizim JNI köprümüze ihtiyaç duymaz)

**Kapsama giren tek şey:** `mobile/android/plugins/jni_bootstrap/` — tek bir Kotlin dosyası (`JniBootstrapPlugin.kt`), Godot Android Plugin sistemine (`@UsedByGodot`) kaydolur, `onMainCreate`/`onMainActivityResult` gibi bir yaşam döngüsü noktasında `JNIEnv*` ve `Activity` referansını bir kez native tarafa (`extern "C"` fonksiyon) iletir. Bundan sonra `ArCoreCaptureSession` bu pointer'ları saklar, `arcore_c_api.h`'nin tüm çağrılarını (`ArSession_create`, `ArSession_update`, vb.) doğrudan C++'tan yapar. Bu dosyada **hiçbir ARCore/iş mantığı yok** — yalnızca pointer aktarımı.

Ayrıntılı kablo formatı: [`PROTOCOL.md`](PROTOCOL.md). Kilometre taşları ve ertelenen kararlar: [`ROADMAP.md`](ROADMAP.md).
