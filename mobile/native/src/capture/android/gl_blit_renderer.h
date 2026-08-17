#pragma once

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <cstdint>
#include <string>
#include <vector>

namespace arstream {

// Minimal EGL/GLES2 helper: owns a headless EGL context, can bind to an
// ANativeWindow (the encoder's input Surface) as its render target, and
// draws a GL_TEXTURE_EXTERNAL_OES camera texture (ARCore's format) to it
// via a textured-quad shader. Also supports rendering into an offscreen
// FBO + glReadPixels, for the CPU-side preview path (see
// ArCoreCaptureSession) -- ARCore has no zero-copy preview path the way
// Camera2's AImageReader does; preview here means "render, then read
// back," at preview resolution only (cheap enough at ~640x360).
//
// Not thread-safe by itself -- ArCoreCaptureSession confines all calls to
// its own single render thread, matching EGL's own current-context-per-
// thread requirement.
class GlBlitRenderer {
public:
	~GlBlitRenderer();

	// Creates a pbuffer-backed EGL context (no window yet) and compiles the
	// blit shader. Call once, before create_camera_texture()/bind_window().
	bool init(std::string &out_error);

	// GL_TEXTURE_EXTERNAL_OES texture ARCore writes the camera image into
	// (see ArSession_setCameraTextureName). Created once, reused for the
	// session's lifetime.
	uint32_t create_camera_texture();

	// Wraps `window` (the encoder's ANativeWindow) as the EGL surface
	// blit_to_window() draws to. Call once the encoder surface exists;
	// destroyed in stop() via unbind_window().
	bool bind_window(ANativeWindow *window, std::string &out_error);
	void unbind_window();

	// Draws `camera_texture` as a full-screen quad to the currently bound
	// window surface, then eglSwapBuffers -- this is what feeds the
	// encoder (the ARCore-backend equivalent of Camera2's zero-copy
	// Surface write; here the GPU blit plays that role instead, since
	// ARCore hands over a GL texture, not a Surface it can write to directly).
	void blit_to_window(uint32_t camera_texture);

	// Renders `camera_texture` into an internal FBO at (width, height) and
	// reads it back as tightly-packed, top-down RGBA8 (glReadPixels itself
	// returns bottom-up; this flips rows before returning so callers don't
	// have to think about GL's coordinate convention). Used for the
	// preview path only -- ArCapture's preview texture is much smaller
	// than the capture resolution.
	std::vector<uint8_t> blit_to_rgba_buffer(uint32_t camera_texture, int32_t width, int32_t height);

private:
	EGLDisplay display_ = EGL_NO_DISPLAY;
	EGLContext context_ = EGL_NO_CONTEXT;
	EGLConfig config_ = nullptr;
	EGLSurface pbuffer_surface_ = EGL_NO_SURFACE; // dummy surface: current context outside of window/FBO rendering
	EGLSurface window_surface_ = EGL_NO_SURFACE;

	uint32_t program_ = 0;
	uint32_t vbo_ = 0;

	uint32_t fbo_ = 0;
	uint32_t fbo_texture_ = 0;
	int32_t fbo_width_ = 0;
	int32_t fbo_height_ = 0;

	bool ensure_fbo(int32_t width, int32_t height);
	void draw_quad(uint32_t camera_texture);
};

} // namespace arstream
