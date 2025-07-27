#include <iostream>
#include "mesh.h"


int main() {
    uint32_t width = 1024;
    uint32_t height = 768;
    const char* name = "I SUCK AT VULKAN";
    Mesh mesh(width, height, name);

    mesh.run();

    return 0;
}