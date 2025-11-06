/*
* File: camera
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: Camera struct for 3D navigation
*/
#ifndef CAMERA_HPP
#define CAMERA_HPP
#include <glm.hpp>

#include "math.hpp"

namespace blok {

class Camera {
public:
    Vector3 position{0.0f, 0.0f, 3.0f};
    float yaw   = -90.0f;
    float pitch = 0.0f;
    float fov   = 60.0f;

    [[nodiscard]] Vector3 forward() const;
    [[nodiscard]] Vector3 right() const;
    [[nodiscard]] Vector3 up() const;

    [[nodiscard]] Matrix4 view() const;
    [[nodiscard]] Matrix4 projection(float aspect, float zNear, float zFar) const;

    void processKeyboard(char key, float dt);
    void processMouse(float dx, float dy);
};

} // namespace blok
#endif // CAMERA_HPP