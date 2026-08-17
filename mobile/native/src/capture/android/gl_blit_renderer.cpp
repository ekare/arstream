#include "gl_blit_renderer.h"

#include <GLES2/gl2ext.h>
#include <android/log.h>

#include <cstring>

namespace arstream {

namespace {

constexpr char kVertexShaderSrc[] =
		"attribute vec2 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"varying vec2 v_texcoord;\n"
		"void main() {\n"
		"    gl_Position = vec4(a_position, 0.0, 1.0);\n"
		"    v_texcoord = a_texcoord;\n"
		"}\n";

constexpr char kFragmentShaderSrc[] =
		"#extension GL_OES_EGL_image_external : require\n"
		"precision mediump float;\n"
		"uniform samplerExternalOES u_texture;\n"
		"varying vec2 v_texcoord;\n"
		"void main() {\n"
		"    gl_FragColor = texture2D(u_texture, v_texcoord);\n"
		"}\n";

// Full-screen quad, position (x,y) interleaved with texcoord (u,v). ARCore's
// camera texture coordinates already account for display rotation IF
// ArSession_setDisplayGeometry was called with the correct
// rotation/width/height (see ArCoreCaptureSession::start) -- no manual
// rotation here, unlike Camera2CaptureSession's CPU-side preview rotation.
// Whether this v-orientation is right for ARCore's texture specifically is
// confirmed by the on-device visual check called for in the approved plan,
// not assumed here.
constexpr float kQuadVertices[] = {
	// x,     y,    u,    v
	-1.0f, -1.0f, 0.0f, 0.0f,
	1.0f, -1.0f, 1.0f, 0.0f,
	-1.0f, 1.0f, 0.0f, 1.0f,
	1.0f, 1.0f, 1.0f, 1.0f,
};

uint32_t compile_shader(uint32_t type, const char *src) {
	uint32_t shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);
	int32_t status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == 0) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		__android_log_print(ANDROID_LOG_ERROR, "arstream", "GL shader compile failed: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

} // namespace

GlBlitRenderer::~GlBlitRenderer() {
	unbind_window();
	if (context_ != EGL_NO_CONTEXT) {
		eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (pbuffer_surface_ != EGL_NO_SURFACE) {
			eglDestroySurface(display_, pbuffer_surface_);
		}
		eglDestroyContext(display_, context_);
		eglTerminate(display_);
	}
}

bool GlBlitRenderer::init(std::string &out_error) {
	display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
		out_error = "eglGetDisplay/eglInitialize failed";
		return false;
	}

	const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	int32_t num_configs = 0;
	if (!eglChooseConfig(display_, config_attribs, &config_, 1, &num_configs) || num_configs == 0) {
		out_error = "eglChooseConfig failed";
		return false;
	}

	const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attribs);
	if (context_ == EGL_NO_CONTEXT) {
		out_error = "eglCreateContext failed";
		return false;
	}

	// A 1x1 pbuffer just to have a current context to compile shaders with,
	// before the real window surface exists (and again afterward, whenever
	// rendering into the preview FBO instead of the window).
	const EGLint pbuffer_attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
	pbuffer_surface_ = eglCreatePbufferSurface(display_, config_, pbuffer_attribs);
	if (pbuffer_surface_ == EGL_NO_SURFACE) {
		out_error = "eglCreatePbufferSurface failed";
		return false;
	}
	if (!eglMakeCurrent(display_, pbuffer_surface_, pbuffer_surface_, context_)) {
		out_error = "eglMakeCurrent (pbuffer) failed";
		return false;
	}

	uint32_t vs = compile_shader(GL_VERTEX_SHADER, kVertexShaderSrc);
	uint32_t fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
	if (vs == 0 || fs == 0) {
		out_error = "shader compile failed";
		return false;
	}
	program_ = glCreateProgram();
	glAttachShader(program_, vs);
	glAttachShader(program_, fs);
	glBindAttribLocation(program_, 0, "a_position");
	glBindAttribLocation(program_, 1, "a_texcoord");
	glLinkProgram(program_);
	glDeleteShader(vs);
	glDeleteShader(fs);
	int32_t link_status = 0;
	glGetProgramiv(program_, GL_LINK_STATUS, &link_status);
	if (link_status == 0) {
		out_error = "GL program link failed";
		return false;
	}

	glGenBuffers(1, &vbo_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

	return true;
}

uint32_t GlBlitRenderer::create_camera_texture() {
	uint32_t texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return texture;
}

bool GlBlitRenderer::bind_window(ANativeWindow *window, std::string &out_error) {
	window_surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
	if (window_surface_ == EGL_NO_SURFACE) {
		out_error = "eglCreateWindowSurface failed";
		return false;
	}
	if (!eglMakeCurrent(display_, window_surface_, window_surface_, context_)) {
		out_error = "eglMakeCurrent (window) failed";
		return false;
	}
	return true;
}

void GlBlitRenderer::unbind_window() {
	if (window_surface_ != EGL_NO_SURFACE) {
		if (context_ != EGL_NO_CONTEXT) {
			eglMakeCurrent(display_, pbuffer_surface_, pbuffer_surface_, context_);
		}
		eglDestroySurface(display_, window_surface_);
		window_surface_ = EGL_NO_SURFACE;
	}
}

void GlBlitRenderer::draw_quad(uint32_t camera_texture) {
	glUseProgram(program_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, camera_texture);
	glUniform1i(glGetUniformLocation(program_, "u_texture"), 0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GlBlitRenderer::blit_to_window(uint32_t camera_texture) {
	if (window_surface_ == EGL_NO_SURFACE) {
		return;
	}
	eglMakeCurrent(display_, window_surface_, window_surface_, context_);
	EGLint width = 0, height = 0;
	eglQuerySurface(display_, window_surface_, EGL_WIDTH, &width);
	eglQuerySurface(display_, window_surface_, EGL_HEIGHT, &height);
	glViewport(0, 0, width, height);
	draw_quad(camera_texture);
	eglSwapBuffers(display_, window_surface_);
}

bool GlBlitRenderer::ensure_fbo(int32_t width, int32_t height) {
	if (fbo_ != 0 && fbo_width_ == width && fbo_height_ == height) {
		return true;
	}
	if (fbo_ != 0) {
		glDeleteFramebuffers(1, &fbo_);
		glDeleteTextures(1, &fbo_texture_);
	}
	glGenTextures(1, &fbo_texture_);
	glBindTexture(GL_TEXTURE_2D, fbo_texture_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenFramebuffers(1, &fbo_);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_texture_, 0);
	bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	fbo_width_ = width;
	fbo_height_ = height;
	return ok;
}

std::vector<uint8_t> GlBlitRenderer::blit_to_rgba_buffer(uint32_t camera_texture, int32_t width, int32_t height) {
	// The pbuffer (not the window surface) is the current draw surface
	// here -- the FBO attachment is what actually receives the draw, the
	// bound surface just needs to have a valid current EGL context.
	eglMakeCurrent(display_, pbuffer_surface_, pbuffer_surface_, context_);
	if (!ensure_fbo(width, height)) {
		return {};
	}
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glViewport(0, 0, width, height);
	draw_quad(camera_texture);

	std::vector<uint8_t> raw(static_cast<size_t>(width) * height * 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// glReadPixels is always bottom-up (GL's coordinate origin is
	// bottom-left) -- flip rows so callers get ordinary top-down rows.
	std::vector<uint8_t> flipped(raw.size());
	size_t row_bytes = static_cast<size_t>(width) * 4;
	for (int32_t row = 0; row < height; row++) {
		memcpy(flipped.data() + row * row_bytes, raw.data() + (height - 1 - row) * row_bytes, row_bytes);
	}

	// Restore the window as current so a subsequent blit_to_window() call
	// doesn't need to re-bind it.
	if (window_surface_ != EGL_NO_SURFACE) {
		eglMakeCurrent(display_, window_surface_, window_surface_, context_);
	}
	return flipped;
}

} // namespace arstream
