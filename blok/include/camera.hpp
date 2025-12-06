/*
* File: camera.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef CAMERA_HPP
#define CAMERA_HPP
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace blok {

struct Camera {
public:
    glm::vec3 position{0.0f, 0.0f, 1.0f};
    float yaw   = -90.0f;
    float pitch = 0.0f;
    float fov   = 60.0f;

    mutable bool cameraChanged = false;

    [[nodiscard]]
    inline glm::vec3 forward() const {
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(dir);
    }

    [[nodiscard]]
    inline glm::vec3 right() const {
        return glm::normalize(glm::cross(forward(), glm::vec3(0.0f,1.0f,0.0f)));
    }

    [[nodiscard]]
    inline glm::vec3 up() const {
        return glm::normalize(glm::cross(right(), forward()));
    }

    [[nodiscard]]
    inline glm::vec3 worldUp() const {
        return {0.0f, 1.0f, 0.0f};
    }

    [[nodiscard]]
    inline glm::mat4 view() const {
        const glm::vec3 f = forward();
        return glm::lookAt(position, position + f, up());
    }

    [[nodiscard]]
    inline glm::mat4 projection(float aspect, float zNear, float zFar) const {
        glm::mat4 p = glm::perspective(glm::radians(fov), aspect, zNear, zFar);
        p[1][1] *= -1.0f; // vulkan requirement
        return p;
    }

    void processKeyboard(char key, float dt) {
        float speed = 40.0f * dt;
        if (key == 'W') position += forward() * speed;
        if (key == 'S') position -= forward() * speed;
        if (key == 'A') position -= right() * speed;
        if (key == 'D') position += right() * speed;
        if (key == 'P') position += worldUp() * speed;
        if (key == 'C') position -= worldUp() * speed;

        cameraChanged = true;
    }
    void processMouse(float dx, float dy) {
        float sens = 0.01f;
        yaw   += dx * sens;
        pitch += dy * sens;

        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        cameraChanged = true;
    }
};

}

#endif //CAMERA_HPP