/*
* File: math
* Project: blok
* Author: Collin Longoria
* Created on: 9/11/2025
*
* Description: Math wrapper
*/

#ifndef BLOK_MATH_HPP
#define BLOK_MATH_HPP

#include <glm.hpp>
#include <gtc/quaternion.hpp>

namespace blok {
    /*
     * glm type wrappers (to maintain consistent style)
     */
    using Vector2 = glm::vec2;
    using Vector3 = glm::vec3;
    using Vector4 = glm::vec4;

    using Matrix2 = glm::mat2;
    using Matrix3 = glm::mat3;
    using Matrix4 = glm::mat4;

    using Quaternion = glm::quat;


    struct Transform {
        Vector3 position = { 0,0,0 };
        Vector3 scale = { 1,1,1 };
        Quaternion rotation = { 1,0,0,0 };

        //Move in a given direction.
        void Translate(Vector3 direction)
        {
            position += direction;
        }

        //Scale all axis by the same factor.
        void Scale(float scalefactor)
        {
            if (scalefactor == 0) return;

            scale *= scalefactor;
        }
        
        //Scale each axis seperately. (0s will be ignored)
        void Scale(Vector3 scalefactor)
        {
            if (scalefactor.x != 0)
                scale.x *= scalefactor.x;

            if (scalefactor.y != 0)
                scale.y *= scalefactor.y;

            if (scalefactor.z != 0)
                scale.z *= scalefactor.z;
        }

        Matrix3 GetRotationMatrix()
        {
            Matrix3 rotmat;

            rotmat[0][0] = 1 - (2 * (rotation.y * rotation.y + rotation.z * rotation.z));
            rotmat[0][1] = 2 * (rotation.x * rotation.y - rotation.w * rotation.z);
            rotmat[0][2] = 2 * (rotation.x * rotation.z + rotation.w * rotation.y);

            rotmat[1][0] = 2 * (rotation.x * rotation.y + rotation.w * rotation.z);
            rotmat[1][1] = 1 - (2 * (rotation.x * rotation.x + rotation.z * rotation.z));
            rotmat[1][2] = 2 * (rotation.y * rotation.z - rotation.w * rotation.x);

            rotmat[2][0] = 2 * (rotation.x * rotation.z - rotation.w * rotation.y);
            rotmat[2][1] = 2 * (rotation.y * rotation.z + rotation.w * rotation.x);
            rotmat[2][2] = 1 - (2 * (rotation.x * rotation.x + rotation.y * rotation.y));

            return rotmat;
        }

        //Rotate using a quaternion.
        void Rotate(Quaternion rotate)
        {
            rotation = rotate * rotation;
        }

        //Rotate using an angle in degrees and a unit vector to rotate around.
        void Rotate(float angleDeg, Vector3 axis)
        {
            Quaternion rotate = glm::angleAxis(glm::radians(angleDeg), axis);

            rotation = rotate * rotation;
        }
    };

}

#endif //BLOK_MATH_HPP