#pragma once

#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>

namespace Jade::Motion {

Vec3 getFront(const Transform& transform);
Vec3 getRight(const Transform& transform);
Vec3 getUp(const Transform& transform);
// Aims -Z at a world target. Does not retain a target or change position.
void lookAt(Transform& transform, Vec3 target, Vec3 up = {0, 1, 0});

// Distances per call. Held input passes speed * event.dt.
void forward(Transform& transform, float distance = 0.1f);
void backward(Transform& transform, float distance = 0.1f);
void right(Transform& transform, float distance = 0.1f);
void left(Transform& transform, float distance = 0.1f);
// Vertical translation remains along world Y.
void up(Transform& transform, float distance = 0.1f);
void down(Transform& transform, float distance = 0.1f);
// Degrees around the current local axes. Positive yaw looks right, pitch up,
// roll clockwise. Free rotation allows inversion; there is no pitch clamp.
void yaw(Transform& transform, double degrees);
void pitch(Transform& transform, double degrees);
void roll(Transform& transform, double degrees);
// Positive scroll magnifies the current projection without altering pose.
void zoom(Camera& camera, double scroll);

} // namespace Jade::Motion
