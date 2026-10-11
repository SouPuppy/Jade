#pragma once

#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>

namespace Jade::CameraMotion {

// Upright camera operations in parent coordinates (world coordinates for roots).
// Yaw uses fixed Y, pitch stays within +/-89 degrees, roll is removed.
void constrain(Transform& pose);
void look(Transform& pose, double yawDegrees, double pitchDegrees);
// Input axes: x right, y ascend, z forward. Lengths above one are normalized.
void move(Transform& pose, Vec3 input, float distance);
// Grab-style image-plane translation. Perspective scale uses a fixed depth plane.
void pan(Transform& pose, const Camera& lens, double dx, double dy,
         int windowHeight, float distance = 10);

} // namespace Jade::CameraMotion
