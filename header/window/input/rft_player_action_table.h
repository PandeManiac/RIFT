#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define RFT_PLAYER_ACTION_TABLE(X)                                                                                     \
	X(RFT_PLAYER_MOVE_FORWARD, "move_forward", RFT_INPUT_KEY, GLFW_KEY_W, 1.0f)                                        \
	X(RFT_PLAYER_MOVE_BACK, "move_back", RFT_INPUT_KEY, GLFW_KEY_S, 1.0f)                                              \
	X(RFT_PLAYER_MOVE_LEFT, "move_left", RFT_INPUT_KEY, GLFW_KEY_A, 1.0f)                                              \
	X(RFT_PLAYER_MOVE_RIGHT, "move_right", RFT_INPUT_KEY, GLFW_KEY_D, 1.0f)                                            \
	X(RFT_PLAYER_MOVE_UP, "move_up", RFT_INPUT_KEY, GLFW_KEY_SPACE, 1.0f)                                              \
	X(RFT_PLAYER_MOVE_DOWN, "move_down", RFT_INPUT_KEY, GLFW_KEY_LEFT_SHIFT, 1.0f)                                     \
	X(RFT_PLAYER_LOOK_X, "look_x", RFT_INPUT_GAMEPAD_AXIS, GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f)                            \
	X(RFT_PLAYER_LOOK_Y, "look_y", RFT_INPUT_GAMEPAD_AXIS, GLFW_GAMEPAD_AXIS_RIGHT_Y, -1.0f)
