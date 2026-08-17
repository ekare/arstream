#include "camera2_capture_session.h"

#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImage.h>

namespace arstream {

namespace {

void on_device_disconnected(void *context, ACameraDevice *device) {
	(void)device;
	static_cast<Camera2CaptureSession *>(context)->notify_error("Camera connection lost");
}

void on_device_error(void *context, ACameraDevice *device, int error) {
	(void)device;
	static_cast<Camera2CaptureSession *>(context)->notify_error("Camera error (code " + std::to_string(error) + ")");
}

void on_session_closed(void *context, ACameraCaptureSession *session) {
	(void)context;
	(void)session;
}

void on_session_ready(void *context, ACameraCaptureSession *session) {
	(void)context;
	(void)session;
}

void on_session_active(void *context, ACameraCaptureSession *session) {
	(void)context;
	(void)session;
}

// Called when the AImageReader has a new preview frame. Bails out
// immediately if preview_enabled is false -- so the CPU cost only exists
// while the user has actually turned preview on.
void on_preview_image_available(void *context, AImageReader *reader) {
	auto *self = static_cast<Camera2CaptureSession *>(context);
	AImage *image = nullptr;
	if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || image == nullptr) {
		return;
	}
	if (self->is_preview_enabled()) {
		uint8_t *y_data = nullptr;
		int y_len = 0;
		int32_t row_stride = 0;
		int32_t width = 0, height = 0;
		AImage_getPlaneData(image, 0, &y_data, &y_len);
		AImage_getPlaneRowStride(image, 0, &row_stride);
		AImage_getWidth(image, &width);
		AImage_getHeight(image, &height);
		if (y_data != nullptr) {
			self->notify_preview_frame(y_data, width, height, row_stride);
		}
	}
	AImage_delete(image);
}

} // namespace

Camera2CaptureSession::~Camera2CaptureSession() {
	stop();
}

void Camera2CaptureSession::notify_error(const std::string &message) {
	if (on_error_) {
		on_error_(message);
	}
}

void Camera2CaptureSession::notify_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride) {
	if (on_preview_frame_) {
		on_preview_frame_(y_plane, width, height, row_stride);
	}
}

std::string Camera2CaptureSession::find_back_camera_id() {
	ACameraIdList *id_list = nullptr;
	if (ACameraManager_getCameraIdList(manager_, &id_list) != ACAMERA_OK || id_list == nullptr) {
		return "";
	}

	std::string result;
	for (int i = 0; i < id_list->numCameras; i++) {
		const char *id = id_list->cameraIds[i];
		ACameraMetadata *metadata = nullptr;
		if (ACameraManager_getCameraCharacteristics(manager_, id, &metadata) != ACAMERA_OK || metadata == nullptr) {
			continue;
		}
		ACameraMetadata_const_entry entry;
		if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK && entry.count > 0) {
			if (entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
				result = id;
				ACameraMetadata_free(metadata);
				break;
			}
		}
		ACameraMetadata_free(metadata);
	}

	if (result.empty() && id_list->numCameras > 0) {
		// Fall back to the first camera if no back camera was found (e.g. a
		// front-camera-only device).
		result = id_list->cameraIds[0];
	}

	ACameraManager_deleteCameraIdList(id_list);
	return result;
}

int32_t Camera2CaptureSession::query_back_camera_sensor_orientation() {
	ACameraManager *manager = ACameraManager_create();
	if (manager == nullptr) {
		return 0;
	}
	ACameraIdList *id_list = nullptr;
	if (ACameraManager_getCameraIdList(manager, &id_list) != ACAMERA_OK || id_list == nullptr) {
		ACameraManager_delete(manager);
		return 0;
	}

	int32_t orientation = 0;
	int32_t fallback_orientation = 0;
	bool found_back = false;
	for (int i = 0; i < id_list->numCameras; i++) {
		const char *id = id_list->cameraIds[i];
		ACameraMetadata *metadata = nullptr;
		if (ACameraManager_getCameraCharacteristics(manager, id, &metadata) != ACAMERA_OK || metadata == nullptr) {
			continue;
		}
		int32_t this_orientation = 0;
		ACameraMetadata_const_entry orientation_entry;
		if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION, &orientation_entry) == ACAMERA_OK && orientation_entry.count > 0) {
			this_orientation = orientation_entry.data.i32[0];
		}
		if (i == 0) {
			fallback_orientation = this_orientation;
		}
		ACameraMetadata_const_entry facing_entry;
		if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &facing_entry) == ACAMERA_OK && facing_entry.count > 0 && facing_entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
			orientation = this_orientation;
			found_back = true;
			ACameraMetadata_free(metadata);
			break;
		}
		ACameraMetadata_free(metadata);
	}

	ACameraManager_deleteCameraIdList(id_list);
	ACameraManager_delete(manager);
	return found_back ? orientation : fallback_orientation;
}

bool Camera2CaptureSession::start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
		int32_t preview_width, int32_t preview_height,
		ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
		std::string &out_error) {
	(void)width;
	(void)height;
	on_error_ = on_error;
	on_preview_frame_ = on_preview_frame;

	manager_ = ACameraManager_create();
	if (manager_ == nullptr) {
		out_error = "Could not create ACameraManager";
		return false;
	}

	std::string camera_id = find_back_camera_id();
	if (camera_id.empty()) {
		out_error = "No available camera found";
		ACameraManager_delete(manager_);
		manager_ = nullptr;
		return false;
	}

	ACameraDevice_StateCallbacks device_callbacks = {};
	device_callbacks.context = this;
	device_callbacks.onDisconnected = on_device_disconnected;
	device_callbacks.onError = on_device_error;

	camera_status_t open_status = ACameraManager_openCamera(manager_, camera_id.c_str(), &device_callbacks, &device_);
	if (open_status != ACAMERA_OK || device_ == nullptr) {
		out_error = "Could not open camera (code " + std::to_string(open_status) + ") -- was CAMERA permission granted?";
		ACameraManager_delete(manager_);
		manager_ = nullptr;
		return false;
	}

	// Preview reader: small resolution, YUV420 -- only the Y (luma) plane is
	// used, enough for a simple grayscale preview.
	if (AImageReader_new(preview_width, preview_height, AIMAGE_FORMAT_YUV_420_888, 2, &preview_reader_) != AMEDIA_OK || preview_reader_ == nullptr) {
		out_error = "Could not create preview AImageReader";
		stop();
		return false;
	}
	AImageReader_ImageListener listener{};
	listener.context = this;
	listener.onImageAvailable = on_preview_image_available;
	AImageReader_setImageListener(preview_reader_, &listener);

	ANativeWindow *preview_window = nullptr;
	if (AImageReader_getWindow(preview_reader_, &preview_window) != AMEDIA_OK || preview_window == nullptr) {
		out_error = "Could not get preview surface";
		stop();
		return false;
	}

	ACaptureSessionOutputContainer_create(&output_container_);

	// If there's no encoder_surface (preview-only mode), the encoder target
	// is never added -- encoder_target_/encoder_output_ stay nullptr, and
	// stop() already null-checks them.
	if (encoder_surface != nullptr) {
		ACaptureSessionOutput_create(encoder_surface, &encoder_output_);
		ACaptureSessionOutputContainer_add(output_container_, encoder_output_);
		ACameraOutputTarget_create(encoder_surface, &encoder_target_);
	}

	ACaptureSessionOutput_create(preview_window, &preview_output_);
	ACaptureSessionOutputContainer_add(output_container_, preview_output_);
	ACameraOutputTarget_create(preview_window, &preview_target_);

	ACameraDevice_request_template template_type = (encoder_surface != nullptr) ? TEMPLATE_RECORD : TEMPLATE_PREVIEW;
	if (ACameraDevice_createCaptureRequest(device_, template_type, &request_) != ACAMERA_OK || request_ == nullptr) {
		out_error = "Could not create capture request";
		stop();
		return false;
	}

	// Fixed focus, no OIS, no digital (EIS) stabilization -- applied to
	// BOTH TEMPLATE_RECORD and TEMPLATE_PREVIEW (preview included), so the
	// intrinsics/geometry any downstream consumer sees never shift mid-
	// stream. A moving/refocusing lens or a warping-crop stabilizer breaks
	// the fixed-camera assumption this whole project is built on. 0.0f
	// diopters is the device-independent convention for infinity focus --
	// the right default for room/environment-scale capture (not macro).
	uint8_t af_mode_off = ACAMERA_CONTROL_AF_MODE_OFF;
	ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AF_MODE, 1, &af_mode_off);
	float focus_distance_infinity = 0.0f;
	ACaptureRequest_setEntry_float(request_, ACAMERA_LENS_FOCUS_DISTANCE, 1, &focus_distance_infinity);
	uint8_t ois_off = ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_OFF;
	ACaptureRequest_setEntry_u8(request_, ACAMERA_LENS_OPTICAL_STABILIZATION_MODE, 1, &ois_off);
	uint8_t video_stab_off = ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
	ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE, 1, &video_stab_off);

	if (encoder_target_ != nullptr) {
		ACaptureRequest_addTarget(request_, encoder_target_);
	}
	ACaptureRequest_addTarget(request_, preview_target_);

	ACameraCaptureSession_stateCallbacks session_callbacks = {};
	session_callbacks.context = this;
	session_callbacks.onClosed = on_session_closed;
	session_callbacks.onReady = on_session_ready;
	session_callbacks.onActive = on_session_active;

	if (ACameraDevice_createCaptureSession(device_, output_container_, &session_callbacks, &session_) != ACAMERA_OK || session_ == nullptr) {
		out_error = "Could not create capture session";
		stop();
		return false;
	}

	if (ACameraCaptureSession_setRepeatingRequest(session_, nullptr, 1, &request_, nullptr) != ACAMERA_OK) {
		out_error = "Could not start repeating request";
		stop();
		return false;
	}

	return true;
}

void Camera2CaptureSession::stop() {
	preview_enabled_ = false;

	if (session_ != nullptr) {
		ACameraCaptureSession_stopRepeating(session_);
		ACameraCaptureSession_close(session_);
		session_ = nullptr;
	}
	if (request_ != nullptr) {
		ACaptureRequest_free(request_);
		request_ = nullptr;
	}
	if (encoder_target_ != nullptr) {
		ACameraOutputTarget_free(encoder_target_);
		encoder_target_ = nullptr;
	}
	if (preview_target_ != nullptr) {
		ACameraOutputTarget_free(preview_target_);
		preview_target_ = nullptr;
	}
	if (encoder_output_ != nullptr) {
		ACaptureSessionOutput_free(encoder_output_);
		encoder_output_ = nullptr;
	}
	if (preview_output_ != nullptr) {
		ACaptureSessionOutput_free(preview_output_);
		preview_output_ = nullptr;
	}
	if (output_container_ != nullptr) {
		ACaptureSessionOutputContainer_free(output_container_);
		output_container_ = nullptr;
	}
	if (device_ != nullptr) {
		ACameraDevice_close(device_);
		device_ = nullptr;
	}
	if (preview_reader_ != nullptr) {
		AImageReader_delete(preview_reader_);
		preview_reader_ = nullptr;
	}
	if (manager_ != nullptr) {
		ACameraManager_delete(manager_);
		manager_ = nullptr;
	}
}

} // namespace arstream
