#include "window/rft_window.h"

#include "utils/rft_assert.h"
#include "utils/rft_gen_utils.h"
#include "utils/rft_hints.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <string.h>

static HOT ALWAYS_INLINE inline void key_cb(GLFWwindow* handle, int key, int scancode, int action, int mods);
static HOT ALWAYS_INLINE inline void mouse_button_cb(GLFWwindow* handle, int button, int action, int mods);
static HOT ALWAYS_INLINE inline void mouse_pos_cb(GLFWwindow* handle, double x, double y);

void rft_window_init(rft_window* window, const rft_window_config* config)
{
	ASSERT_FATAL(window);
	ASSERT_FATAL(config);
	ASSERT_FATAL(config->title);
	ASSERT_FATAL(config->size.x > 0);
	ASSERT_FATAL(config->size.y > 0);

	memset(window, 0, sizeof(*window));

	window->cfg			 = *config;
	GLFWmonitor* monitor = NULL;

	if (window->cfg.fullscreen)
	{
		monitor = glfwGetPrimaryMonitor();
		ASSERT_FATAL(monitor);
	}

	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, window->cfg.gl_major);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, window->cfg.gl_minor);

	if (window->cfg.core_profile)
	{
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	}

	else
	{
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
	}

	glfwWindowHint(GLFW_SAMPLES, window->cfg.samples);

	window->handle = glfwCreateWindow(window->cfg.size.x, window->cfg.size.y, window->cfg.title, monitor, NULL);
	ASSERT_FATAL(window->handle);

	glfwMakeContextCurrent(window->handle);
	glfwSwapInterval(window->cfg.vsync ? 1 : 0);

	glfwGetWindowPos(window->handle, &window->pos.x, &window->pos.y);

	glfwSetWindowUserPointer(window->handle, window);

	glfwSetKeyCallback(window->handle, key_cb);
	glfwSetMouseButtonCallback(window->handle, mouse_button_cb);
	glfwSetCursorPosCallback(window->handle, mouse_pos_cb);

	rft_input_init(&window->input);
}

void rft_window_destroy(rft_window* window)
{
	ASSERT_FATAL(window);

	if (window->handle)
	{
		glfwDestroyWindow(window->handle);
		window->handle = NULL;
	}
}

void rft_window_begin_frame(rft_window* window)
{
	ASSERT_FATAL(window);
	rft_input_begin_frame(&window->input);
}

bool rft_window_should_close(const rft_window* window)
{
	ASSERT_FATAL(window);
	ASSERT_FATAL(window->handle);

	return glfwWindowShouldClose(window->handle);
}

void rft_window_swap_buffers(rft_window* window)
{
	ASSERT_FATAL(window);
	ASSERT_FATAL(window->handle);

	glfwSwapBuffers(window->handle);
}

void rft_window_poll_events(void)
{
	glfwPollEvents();
}

static HOT ALWAYS_INLINE inline void key_cb(GLFWwindow* handle, int key, int scancode, int action, int mods)
{
	UNUSED(scancode);
	UNUSED(mods);

	rft_window* window = glfwGetWindowUserPointer(handle);
	ASSERT_FATAL(window);

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(handle, 1);
	}

	static int wireframe_enabled = 0;

	if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
	{
		wireframe_enabled = !wireframe_enabled;
		glPolygonMode(GL_FRONT_AND_BACK, wireframe_enabled ? GL_LINE : GL_FILL);
	}

	rft_input_key_callback(&window->input, key, action);
}

static HOT ALWAYS_INLINE inline void mouse_button_cb(GLFWwindow* handle, int button, int action, int mods)
{
	UNUSED(mods);

	rft_window* window = glfwGetWindowUserPointer(handle);
	ASSERT_FATAL(window);

	rft_input_mouse_button_callback(&window->input, button, action);
}

static HOT ALWAYS_INLINE inline void mouse_pos_cb(GLFWwindow* handle, double x, double y)
{
	rft_window* window = glfwGetWindowUserPointer(handle);
	ASSERT_FATAL(window);

	rft_input_mouse_pos(&window->input, x, y);
}
