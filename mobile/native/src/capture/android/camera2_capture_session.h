#pragma once

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>

#include <atomic>
#include <functional>
#include <string>

namespace arstream {

// ARCore yokken (ya da devre disiyken) kullanilan geri-dusus yol: ham Camera2
// NDK yakalamasi. Ayni capture request iki hedefe BIRDEN yazar -- encoder'in
// giris surface'i (sifir-kopya, hep aktif) ve bir onizleme AImageReader'i
// (kucuk cozunurluk, yalniz preview_enabled=true iken CPU'da islenir). Boylece
// onizlemeyi ac/kapat kaydi hic kesintiye ugratmaz -- ayni oturum, ayni
// repeating request, tek fark islenip islenmedigi.
class Camera2CaptureSession {
public:
	using ErrorCallback = std::function<void(const std::string &message)>;
	// y_plane verisi callback donduktan sonra gecersiz olur -- alan senkron kopyalamali.
	using PreviewFrameCallback = std::function<void(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride)>;

	~Camera2CaptureSession();

	bool start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
			int32_t preview_width, int32_t preview_height,
			ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
			std::string &out_error);
	void stop();

	void set_preview_enabled(bool enabled) { preview_enabled_ = enabled; }
	bool is_preview_enabled() const { return preview_enabled_; }

	// C callback'lerin erisebilmesi icin public -- gercek mantik yok, sadece yonlendirme.
	void notify_error(const std::string &message);
	void notify_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride);
	AImageReader *preview_reader() const { return preview_reader_; }

private:
	ACameraManager *manager_ = nullptr;
	ACameraDevice *device_ = nullptr;
	ACameraCaptureSession *session_ = nullptr;
	ACaptureSessionOutputContainer *output_container_ = nullptr;
	ACaptureSessionOutput *encoder_output_ = nullptr;
	ACameraOutputTarget *encoder_target_ = nullptr;
	ACaptureSessionOutput *preview_output_ = nullptr;
	ACameraOutputTarget *preview_target_ = nullptr;
	AImageReader *preview_reader_ = nullptr;
	ACaptureRequest *request_ = nullptr;
	ErrorCallback on_error_;
	PreviewFrameCallback on_preview_frame_;
	std::atomic<bool> preview_enabled_{ false };

	std::string find_back_camera_id();
};

} // namespace arstream
