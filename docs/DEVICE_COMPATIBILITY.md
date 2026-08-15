# Cihaz Uyumluluk Bulguları

Bu doküman, ARCore/Camera2/IMU yakalama yollarının hangi cihazlarda test edildiğini ve ne bulunduğunu takip eder. `CaptureController`'ın ARCore-vs-geri-düşüş kararı bu bulgulara dayanır.

---

## Samsung Galaxy A30s (SM-A307FN)

**Tarih:** 2026-08-15 · **Yöntem:** ADB üzerinden pasif tanılama (henüz uygulama kodu yazılmadı — M0 spike'ının kod-gerektirmeyen kısmı)

| Alan | Değer |
|---|---|
| Model | SM-A307FN |
| Android sürümü | 11 (API 30) |
| CPU ABI | arm64-v8a (ayrıca armeabi-v7a, armeabi uyumlu) |
| Chipset | Exynos 7904 (`universal7904`) |

### ARCore durumu — **beklenenden iyimser**

Proje planlamasında topluluk kaynaklarına dayanarak "A30s muhtemelen ARCore-sertifikalı cihaz listesinde değil" varsayımıyla yola çıkılmıştı (düz A30 listede, A30s değil, diye raporlanıyordu). Doğrudan cihaz kontrolü bunu **desteklemiyor**:

- `com.google.ar.core` (Google Play Services for AR) **kurulu**, versiyon `1.54.260890083` (`minSdk=29, targetSdk=36`) — yakın zamanda güncellenmiş.
- Google Play Services (`com.google.android.gms`) güncel: `26.30.32`.
- Play Store (`com.android.vending`) mevcut.

**Yorum:** Paket varlığı ve Play Store'un cihaza aktif güncelleme yolluyor olması, cihazın Google'ın uygunluk filtrelerinden tamamen elenmediğine işaret eden güçlü bir sinyal — ama **kesin kanıt değil** (ARCore paketi elle/yan-yükleme ile de kurulabilir). **Kesin cevap yalnız `ArCoreApk_checkAvailability()`'nin gerçek runtime dönüşüyle** gelir — bu, M1'de GDExtension iskeleti ayağa kalkınca ilk doğrulanacak şey.

**Sonuç:** M0'ın önceki "muhtemelen yalnız geri-düşüş yolu test edilebilecek" varsayımı gevşetildi — ARCore yolunun da bu cihazda test edilebilir olma ihtimali yüksek. Yine de `CaptureController` her iki yolu da (ARCore + Camera2 geri düşüş) runtime kararıyla destekleyecek şekilde inşa edilmeye devam ediyor; bu bulgu mimariyi değiştirmiyor, yalnızca hangi yolun önce doğrulanacağına dair beklentiyi güncelliyor.

### Donanım H.264 (AVC) encoder — **doğrulandı**

`/vendor/etc/media_codecs.xml` içinde:
```
<MediaCodec name="OMX.Exynos.AVC.Encoder" type="video/avc" >
<MediaCodec name="OMX.Exynos.AVC.Encoder.secure" type="video/avc-wfd" >
```
`AMediaCodec` (NDK) üzerinden bu encoder'a erişim M3'te doğrulanacak, ama varlığı kesin.

### Bilinen tutarsızlık — minSdk beyanı vs. gerçek gereksinim (M2/M3'te bulundu)

`H264EncoderAndroid`, sıfır-kopya (Camera→Surface→Encoder) yol için `AMediaCodec_createInputSurface`/`AMediaCodec_signalEndOfInputStream` kullanıyor — bunlar **API 26+** gerektiriyor (derleme `android_api_level=26` ile yapılıyor). Ama şu anki basit (non-Gradle) export yolunda `export_presets.cfg`'nin `gradle_build/min_sdk` alanı **kullanılamıyor** ("Use Gradle Build" kapalıyken Godot bunu reddediyor) — yani üretilen APK'nın manifest'i, Godot'un hazır şablonunun gömülü minSdk'sini (muhtemelen 24) taşımaya devam ediyor. Bu, API 24-25 bir cihazda .so'nun eksik sembollerle çökeceği anlamına gelir; A30s (API 30) için sorun değil ama gerçek dağıtımdan önce çözülmeli:
- **A seçeneği:** `gradle_build/use_gradle_build=true`'ya geçip minSdk'yi 26'ya doğru beyan et (daha karmaşık build).
- **B seçeneği:** API 24-25 için `AImageReader`+ByteBuffer tabanlı bir geri-düşüş encoder yolu ekle (daha fazla kod, sıfır-kopya değil).
Şimdilik dokümante edilen bilinen açık; A30s testleri etkilenmiyor.

### IMU — **doğrulandı, hız sınırı not edildi**

`dumpsys sensorservice` çıktısı (STM LSM6DSL paketi):

| Sensör | Sürücü | Mod | Hız aralığı |
|---|---|---|---|
| Accelerometer | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope (uncalibrated) | LSM6DSL | aynı | aynı |

**Not:** Maksimum 100Hz, bazı üst-segment cihazlardaki 200–400Hz gyro'ya göre düşük ama VIO-tarzı kullanım için işlevsel. `SENSOR_DELAY_GAME` bu cihazda pratikte ~100Hz'e tekabül edecek. `ImuSampler`'ı sabit bir hedef hız varsaymadan, `ASensor_getMinDelay()` ile cihazdan okuyacak şekilde yazmak gerekiyor — sihirli sayı yasağıyla tutarlı.

**Doğrulanmadı (kod gerektiriyor, M1/M2'de ele alınacak):** `SensorEvent.timestamp` ile `SystemClock.elapsedRealtimeNanos()` arasındaki epoch/drift tutarlılığı — ADB pasif kontrolüyle ölçülemez, gerçek örnekleme koduyla test edilmeli.

---

## Şablon — yeni cihaz eklerken

```markdown
## <Marka Model> (<kod adı>)

**Tarih:** · **Yöntem:**

| Alan | Değer |
|---|---|
| Android sürümü | |
| CPU ABI | |
| Chipset | |

### ARCore durumu
### Donanım H.264 encoder
### IMU
```
