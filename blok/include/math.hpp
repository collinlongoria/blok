/*
* File: math
* Project: blok
* Author: Collin Longoria
* Created on: 9/11/2025
*/

#ifndef BLOK_MATH_HPP
#define BLOK_MATH_HPP

#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <iostream>


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

    using Quaternion = glm::quat;

    //Euclidean Rotation in degrees (pitch, yaw, roll == x, y, z)
    using Rotation = Vector3;

    struct Transform {
    public:
        Vector3 translation = { 0,0,0 };
        Vector3 scale = { 1,1,1 };
        Quaternion rotation = { 1,0,0,0 };
        
        //Sets Quaternian rotation using Euclidean angles in degrees
        void setRotation(Rotation newRotation)
        {
            rotation = glm::quat(glm::vec3(glm::radians(newRotation.x), glm::radians(newRotation.y), glm::radians(newRotation.z)));
        }

        //Gets Eucliadean Rotation
        Rotation getRotation()
        {
            Rotation rot = glm::degrees(glm::eulerAngles(rotation));
        }

        //Rotate using a quaternion.
        void rotate(Quaternion rotate)
        {
            rotation = rotate * rotation;
        }
        
        //Rotate using an angle in degrees and a unit vector to rotate around.
        void rotate(float angleDeg, Vector3 axis)
        {
            Quaternion rotate = glm::angleAxis(glm::radians(angleDeg), axis);

            rotation = rotate * rotation;
        }

        //Get transform matrix from translation, rotation, and scale
        Matrix4 getTransformMatrix()
        {
            Matrix4 transform = glm::translate(Matrix4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(Matrix4(1.0f), scale);

            return transform;
        }
    };

    /*
    std::ostream& operator<<(std::ostream& os, const Vector3& v) {
        os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
        return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const Quaternion& q) {
        os << "(" << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ")";
        return os;
    }


    std::ostream& operator<<(std::ostream& os, const Transform& t) {
        os << "{translation: " << t.translation << ", rotation: " << t.rotation << ", scale: " << t.scale << "}";
        return os; 
    }
    */
}

#endif //BLOK_MATH_HPP