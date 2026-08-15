#pragma once

#include <cstdint>
#include <string>

namespace arstream {

// Encode edilmis Annex-B H.264 verisinin gidecegi yer. "save" ve "stream"
// modlari bu arayuzun iki implementasyonu -- ArCapture hangisini kullanacagini
// config'teki "mode" alanina gore secer.
class OutputSink {
public:
	virtual ~OutputSink() = default;

	// destination: save modunda dosya yolu; stream modunda "host:port".
	virtual bool open(const std::string &destination, std::string &out_error) = 0;

	// Encoder'in SPS/PPS (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) ciktisi --
	// write_chunk()'tan AYRI, cunku protokolde kendi mesaj tipi var
	// (VIDEO_CONFIG, 0x05) ve normal karelerle karistirilmamali.
	//
	// rotation_degrees: kamera sensorunun ACAMERA_SENSOR_ORIENTATION degeri
	// (0/90/180/270, saat yonunde) -- kare verisinin KENDISI hic
	// dondurulmuyor (sifir-kopya encode hatti bozulmasin diye), bu deger
	// yalniz metadata olarak tasinir; alici taraf (server/protocol.py,
	// veya baska bir adaptor) kareyi bu kadar dondurup gostermeli. Standart
	// video konteynerlerinin (MP4 "rotation matrix" vb.) yaptigi tam olarak
	// bu -- piksel yerine metadata donduruluyor.
	virtual void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) = 0;
	virtual void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) = 0;
	virtual void close() = 0;
};

} // namespace arstream
