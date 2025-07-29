#include "camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <iostream>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

glm::mat4 Camera::getViewMatrix() {
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 cameraRotation = getRotationMatrix();

    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix() {
    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

glm::vec3 Camera::getFront() {

    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    return glm::normalize(direction);
;

}

void Camera::processEvent(GLFWwindow *window) {
    velocity.z = 0;
    velocity.x = 0;
    velocity.y = 0;

    static bool cDown = false;
    static bool vDown = false;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) velocity.z = -1 * velScalar;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) velocity.z = 1 * velScalar;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) velocity.x = -1 * velScalar;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) velocity.x = 1 * velScalar;
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) reset();

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) velocity.y = 1 * velScalar;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) velocity.y = -1 * velScalar;
    bool cPressed = (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS);
    bool vPressed = (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS);

    if (cPressed && !cDown) velScalar *= 0.5f;
    if (vPressed && !vDown) velScalar *= 1.5;

    cDown = cPressed;
    vDown = vPressed;

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    static double lastX = xpos, lastY = ypos;

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    yaw += xoffset / 200.f;
    pitch += yoffset / 200.f;

    lastX = xpos;
    lastY = ypos;
}

void Camera::reset() {
    velocity.z = 0;
    velocity.x = 0;
    velocity.y = 0;
    pitch = 0.0f;
    yaw = 0.0f;
    position = initialPosition;
}

void Camera::update() {
    glm::mat4 cameraRotation = getRotationMatrix();
    glm::vec3 forward = glm::vec3(cameraRotation * glm::vec4(0, 0, velocity.z, 0));
    glm::vec3 right = glm::vec3(cameraRotation * glm::vec4(velocity.x, 0, 0, 0));
    glm::vec3 up = glm::vec3(0, velocity.y, 0);

    position += (forward + right + up) * 0.5f;
}

void Camera::updateFrustum(const glm::mat4& proj) {
    glm::mat4 view = getViewMatrix();
    glm::mat4 projMat = proj;

    projMat[1][1] *= -1;

    cullData.viewProj = projMat * view;

    auto& viewProj = cullData.viewProj;

    // L
    cullData.frustumPlanes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        );

    // R
    cullData.frustumPlanes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // B
    cullData.frustumPlanes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // T
    cullData.frustumPlanes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // N
    cullData.frustumPlanes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );

    // F
    cullData.frustumPlanes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(cullData.frustumPlanes[i]));
        cullData.frustumPlanes[i] /= len;
    }
}
