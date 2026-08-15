# arstream Kablo Protokolü — v1.0

Bu doküman, mobil istemci (telefon) ile destination sunucusu (ve onun ardındaki adaptör) arasındaki **kanonik** kablo formatını tanımlar. Protokolün iki bağımsız implementasyonu bu depoda yaşar — `mobile/native/src/net/protocol_writer.cpp` (C++, yazan taraf) ve `server/src/arstream_server/protocol.py` (Python, okuyan taraf) — ikisi de bu dokümanın birebir yansıması olmalı. Üçüncü bir implementasyon (dış adaptör) yazacaksanız, doğru olan tek kaynak budur.

**Durum:** v1.0 — sabit, dokümante edilmiş sınırlarıyla üretim-öncesi ilk sürüm.

---

## 0. Tasarım ilkeleri

1. **Tek kalıcı TCP bağlantısı.** Telefon **client**'tır (sunucuya dışarı bağlanır), sunucu dinler. Bağlantı koptuğunda istemci `HELLO`'dan yeniden başlar.
2. **Tüm mesajlar aynı 12 byte'lık başlığı taşır.** Bilinmeyen `msg_type` bile `payload_length` sayesinde güvenle atlanabilir — bu, protokolü baştan **ileri-uyumlu** yapar: eski bir okuyucu, yeni bir yazıcının bilmediği mesaj tiplerini kırılmadan atlayabilir.
3. **Ham veri her zaman gider, ARCore alanları opsiyoneldir.** `HELLO`'daki `capabilities` ve `HELLO_ACK`'teki `negotiated` bunu açıkça pazarlık eder.
4. **Zaman damgaları ikiye ayrılır**: cihazın kendi donanım saati (göreli sıralama/delta için — asıl önemli olan) ile `CLOCK_SYNC` üzerinden hesaplanan kaba mutlak-saat ofseti (loglama/hizalama için) birbirine karıştırılmaz.
5. **v1'de şifreleme/kimlik doğrulama yok.** Güvenilir ağ varsayılır (bkz. §6). Bu bilinçli bir sınırdır, gözden kaçan bir eksiklik değil.

---

## 1. Taşıma katmanı

- **Protokol:** TCP, tek bağlantı, istemci → sunucu yönünde açılır.
- **Byte sırası:** Tüm çok-byte'lı tamsayılar **big-endian** (network byte order).
- **String kodlama:** Tüm metin alanları UTF-8.
- **JSON payload'lar:** Sıkıştırılmamış, tek satır, UTF-8.

---

## 2. Mesaj başlığı (her mesajda, 12 byte, sabit)

| Ofset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | `payload_length` | `uint32` | Başlıktan sonraki byte sayısı |
| 4 | `msg_type` | `uint8` | Aşağıdaki tablo |
| 5 | `flags` | `uint8` | v1'de rezerve, `0x00` |
| 6 | `protocol_version` | `uint16` | v1 için `0x0001` |
| 8 | `sequence_number` | `uint32` | Bağlantı başına, `msg_type` başına monoton artan sayaç (tanılama + ileride UDP/v2 uyumluluğu için) |

Toplam: 12 byte başlık + `payload_length` byte payload.

**Bilinmeyen `msg_type` işleme kuralı:** Bir alıcı tanımadığı bir `msg_type` görürse, `payload_length` kadar byte'ı atlayıp bir sonraki başlığı okumaya devam eder. Bağlantıyı asla kapatmaz.

---

## 3. Mesaj tipleri

### 3.1 El sıkışma

**`0x01 HELLO`** (client→server) — bağlantı açıldıktan hemen sonra, tek sefer.

```json
{
  "device_id": "string (kalıcı, cihaza özgü UUID)",
  "device_model": "string (örn. SM-A307FN)",
  "os_version": "string (örn. Android 11 / API 30)",
  "app_version": "string (semver)",
  "capabilities": {
    "arcore_available": false,
    "point_cloud_available": false,
    "intrinsics_available": false
  },
  "video": {
    "codec": "h264",
    "profile": "baseline|main|high",
    "width": 1280,
    "height": 720,
    "fps": 30,
    "bitrate_kbps": 4000
  },
  "imu": {
    "accel_hz": 100,
    "gyro_hz": 100
  }
}
```

**`0x02 HELLO_ACK`** (server→client) — `HELLO`'ya yanıt.

```json
{
  "accepted": true,
  "session_id": "uuid",
  "server_time_ns": 1234567890123456789,
  "reason": "string (yalnız accepted=false ise)",
  "negotiated": {
    "video": { "codec": "h264", "width": 1280, "height": 720, "fps": 30, "bitrate_kbps": 4000 },
    "imu": true,
    "pose": false,
    "point_cloud": false,
    "intrinsics": false
  }
}
```
`negotiated`, istemcinin teklifiyle sunucunun kabul ettiğinin **AND**'idir — sunucu bir alanı istemiyorsa `false`/küçültülmüş değer döner, istemci ona uymalıdır.

### 3.2 Saat senkronizasyonu

**`0x03 CLOCK_SYNC_REQUEST`** (client→server): `int64 client_send_time_ns` (8 byte payload).

**`0x04 CLOCK_SYNC_RESPONSE`** (server→client): `int64 client_send_time_ns, int64 server_recv_time_ns, int64 server_send_time_ns` (24 byte payload).

İstemci ofseti şöyle hesaplar:
```
offset = ((server_recv_time_ns - client_send_time_ns) + (server_send_time_ns - client_recv_time_ns)) / 2
```
Bu ofset yalnız **mutlak** zaman hizalaması/loglama içindir — `VIDEO_CHUNK`/`IMU_BATCH` içindeki zaman damgaları HAM cihaz saatidir, bu ofsetle düzeltilmez (göreli delta hesapları bozulmasın diye).

### 3.3 Video

**`0x05 VIDEO_CONFIG`** (client→server) — `HELLO_ACK` sonrası bir kez, ve her reconnect'te veya orta-akış yeniden yapılandırmada tekrar gönderilir.

Payload: `uint16 json_len` + UTF-8 JSON (`codec, profile, width, height, fps, bitrate_kbps`) + ham SPS/PPS NAL byte'ları (Annex-B, start-code dahil).

**`0x10 VIDEO_CHUNK`** (client→server) — her video karesi için bir tane.

Payload: `int64 capture_timestamp_ns` (8) + `uint8 flags` (1, bit0=keyframe) + kalan tüm byte'lar Annex-B start-code delimited H.264 NAL birimi.

Keyframe aralığı varsayılan **2 saniye** (~60 kare @ 30fps), `VIDEO_CONFIG`'de negotiable.

### 3.4 IMU

**`0x20 IMU_BATCH`** (client→server) — ~50–100ms'de bir gruplu gönderilir (kare-başına DEĞİL).

Payload: `uint16 sample_count` + `sample_count` kere tekrarlanan:
| Alan | Tip |
|---|---|
| `sensor_type` | `uint8` (`0`=accelerometer, `1`=gyroscope) |
| `timestamp_ns` | `int64` |
| `x, y, z` | `float32` × 3 |

### 3.5 ARCore opsiyonel alanlar

**`0x30 POSE_SAMPLE`** (client→server, opsiyonel): `int64 timestamp_ns, uint8 tracking_state, float32 x,y,z, float32 qx,qy,qz,qw` (37 byte).

**`0x31 POINT_CLOUD`** (client→server, opsiyonel): `int64 timestamp_ns, uint32 point_count` + `point_count` kere `float32 x,y,z,confidence` (16 byte/nokta).

**`0x32 CAMERA_INTRINSICS`** (client→server, opsiyonel, bir kez veya değiştiğinde): `float32 fx,fy,cx,cy, uint32 width,height` (24 byte).

### 3.6 Kontrol

**`0x40 STATUS`** (çift yön): JSON `{"level": "info|warn|error", "code": "string", "message": "string"}` — örn. sunucu bitrate düşürmeyi isteyebilir.

**`0xF0 GOODBYE`** (çift yön): JSON `{"reason": "string"}` — bağlantıyı düzgün kapatmadan önce.

---

## 4. Bağlantı yaşam döngüsü

```
istemci bağlanır (TCP connect)
  → HELLO gönderir
  ← HELLO_ACK bekler (accepted=false ise bağlantı kapanır)
  → CLOCK_SYNC_REQUEST / ← CLOCK_SYNC_RESPONSE (opsiyonel ama önerilir, bağlantı başına 1 kez yeterli)
  → VIDEO_CONFIG gönderir
  → (döngü) VIDEO_CHUNK, IMU_BATCH, [POSE_SAMPLE, POINT_CLOUD, CAMERA_INTRINSICS]
  → GOODBYE (düzgün kapanışta) veya bağlantı kopar (istemci reconnect dener, HELLO'dan başlar)
```

---

## 5. Versiyonlama

`protocol_version` her başlıkta taşınır. Aynı major sürüm = tel-uyumlu. Bilinmeyen `msg_type`'lar §2'deki kuralla güvenle atlanabildiğinden, aynı-major bir istemci/sunucu çifti yeni mesaj tipleri eklense bile birbirini kilitlemez.

**Planlanan v2 (henüz yok, `docs/ROADMAP.md`):** UDP/QUIC veya WebRTC-data-channel taşıma — yalnız *taşımayı* değiştirir, yukarıdaki mesaj sözlüğünü/semantiğini değiştirmez.

---

## 6. Güvenlik — bilinçli v1 sınırı

v1'de TLS yok, kimlik doğrulama yok. Varsayım: istemci ve sunucu güvenilir bir ağda (aynı LAN, veya VPN/tünel arkasında). İnternet üzerinden kullanım gerekiyorsa önerilen yol protokole özel kripto eklemek değil, **WireGuard/Tailscale/SSH port-forward gibi bir tünel** kurmaktır — bu, "sade" tasarım tercihiyle ve ağ topolojisinin dağıtım katmanında çözülmesi gerektiği kararıyla tutarlıdır.

---

## 7. Örnek byte dizileri (golden fixtures)

`protocol_writer_test.cpp` ve `server/tests/test_protocol.py`'nin ikisi de bu bölümdeki (veya `fixtures/` altındaki) hex-encoded örnek mesajlara karşı test edilir, böylece iki bağımsız implementasyon sessizce ayrışamaz. M4'te doldurulacak.

---

## 8. Değişiklik günlüğü

- **v1.0** — ilk sürüm. Mesaj tipleri 0x01–0x05, 0x10, 0x20, 0x30–0x32, 0x40, 0xF0.
