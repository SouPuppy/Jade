#pragma once

#include <Jade/Objects/Camera.hpp>

namespace Jade::Motion {

// Distances are world units per call. Use speed * dt for held keys.
void forward(Camera& camera, float distance = 0.1f);
void backward(Camera& camera, float distance = 0.1f);
void right(Camera& camera, float distance = 0.1f);
void left(Camera& camera, float distance = 0.1f);
// Vertical movement follows the world Y axis.
void up(Camera& camera, float distance = 0.1f);
void down(Camera& camera, float distance = 0.1f);
// Degrees relative to the look-at frame: positive yaw turns right;
// positive pitch looks up. Pitch is limited to [-89, 89] degrees.
void yaw(Camera& camera, double degrees);
void pitch(Camera& camera, double degrees);
// Positive roll banks clockwise along the viewing direction.
void roll(Camera& camera, double degrees);
// Positive scroll zooms in, negative scroll zooms out, without moving the camera.
// Scales perspective FOV [1, 179] degrees or orthographic view height [0.01, 10000].
void zoom(Camera& camera, double scroll);

} // namespace Jade::Motion
