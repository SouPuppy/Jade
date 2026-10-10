#pragma once

struct GLFWwindow;

namespace Jade::Detail {

class InputState;
void InstallInputCallbacks(GLFWwindow* window, InputState& input);

} // namespace Jade::Detail
