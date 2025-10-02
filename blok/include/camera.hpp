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

namespace blok {

class Camera {
public:
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yaw   = -90.0f;
    float pitch = 0.0f;
    float fov   = 60.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    void processKeyboard(char key, float dt);
    void processMouse(float dx, float dy);
};

} // namespace blok
#endif // CAMERA_HPP