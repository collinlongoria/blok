/*
* File: camera
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: Camera struct for 3D navigation
*/
#include "camera.hpp"
#include <gtc/matrix_transform.hpp>

using namespace blok;

glm::vec3 Camera::forward() const {
    glm::vec3 dir;
    dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    dir.y = sin(glm::radians(pitch));
    dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    return glm::normalize(dir);
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f,1.0f,0.0f)));
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

void Camera::processKeyboard(char key, float dt) {
    float speed = 10.0f * dt;
    if (key == 'W') position += forward() * speed;
    if (key == 'S') position -= forward() * speed;
    if (key == 'A') position -= right() * speed;
    if (key == 'D') position += right() * speed;
}

void Camera::processMouse(float dx, float dy) {
    float sens = 0.01f;
    yaw   += dx * sens;
    pitch += dy * sens;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}