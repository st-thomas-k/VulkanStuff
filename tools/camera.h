#pragma once
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

class Camera {
public:
    glm::vec3 velocity;
    glm::vec3 position;
    float     velScalar { 1.f };
    float     pitch { 0.f };
    float     yaw { 0.f };
    glm::vec3 initialPosition{0.0f};

    glm::mat4 getViewMatrix();
    glm::mat4 getRotationMatrix();
    glm::vec3 getFront();
    void processEvent(GLFWwindow* window);
    void reset();
    void update();
};