/*
* File: math
* Project: blok
* Author: Collin Longoria
* Created on: 9/11/2025
*/

#ifndef BLOK_MATH_HPP
#define BLOK_MATH_HPP

#include <glm.hpp>

namespace Blok {
    /*
     * glm type wrappers (to maintain consistent style)
     */
    using Vector2 = glm::vec2;
    using Vector3 = glm::vec3;
    using Vector4 = glm::vec4;

    using Matrix2 = glm::mat2;
    using Matrix3 = glm::mat3;
    using Matrix4 = glm::mat4;

}

#endif //BLOK_MATH_HPP