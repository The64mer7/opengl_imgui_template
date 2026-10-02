#pragma once
#include <glad/gl.h>

#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vec_swizzle.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <format>
#include <iostream>

#include "camera.hpp"

#include "buffer.hpp"
#include "input.hpp"
#include "shader.hpp"
#include "sparse_voxel_tree.hpp"
#include "svo_tree_lut.hpp"
#include "texture.hpp"

#include "allocator.hpp"
#include "chunk_manager.hpp"
#include "terrain_gen.hpp"


enum RenderMode
{
    RenderMode_ALBEDO,
    RenderMode_DEPTH,
    RenderMode_CLOCK,
    RenderMode_ITERATION,
    RenderMode_BRANCHING,
    RenderMode_NORMALS,
    RenderMode_count
};

inline static const char* RenderMode_names[] = {"RenderMode_ALBEDO",    "RenderMode_DEPTH",
                                                "RenderMode_CLOCK",     "RenderMode_ITERATION",
                                                "RenderMode_BRANCHING", "RenderMode_NORMALS"};

class Application
{
public:
    Application(uint32_t width, uint32_t height);

    ~Application() {}

    // GLFW, GLAD, ImGui init
    void init();

    // Start the app
    void run();

    // Example window
    void example_window();

    // Your application logic here
    void update(float dt);

    // GLFW, GLAD, ImGui cleanup
    void cleanup();

    void setup_voxel_data();

    FirstPersonCamera camera;
    RenderMode render_mode = RenderMode_ALBEDO;
    int trace_version = 0;
    int max_create_depth = 3;
    float speed = 64.f;
    float sensitivity = 0.01f;

    ShaderProgramCompute sp_raytrace;
    ShaderProgramCompute sp_raytrace_old;
    bool old_shader = false;

    ShaderProgram sp_screen;

    svo_tree voxel_tree_data;
    GpuBuffer voxel_tree_buffer;
    GpuBuffer contree_bitmask_lut_buffer;
    Texture screen_texture;

    GLFWwindow* window;
    uint32_t width = 1280;
    uint32_t height = 960;
    bool is_running;

    InputState input;
    ContreeBitmaskLUT contree_bitmask_lut;

    // Your application variables here:
    uint32_t counter;
};