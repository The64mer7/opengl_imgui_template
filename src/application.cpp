#include "application.hpp"

Application::Application(uint32_t width, uint32_t height)
    : width(width), height(height), window(nullptr), is_running(true)
{
}

// GLFW, GLAD, ImGui init

void reload_shader(ShaderProgramCompute& shader)
{
    static int a = 0;

    Shader cs_raytrace;
    if (a == 0)
    {
        cs_raytrace.createShader("resources/tree_ray_trace_new.comp", GL_COMPUTE_SHADER);
        printf("SWITCHED TO NEW\n");
    }
    else
    {
        printf("SWITCHED TO TEST NEW\n");
        cs_raytrace.createShader("resources/tree_ray_trace_neww.comp", GL_COMPUTE_SHADER);
    }

    a = 1 - a;
    shader.cleanup();

    auto status = shader.createProgram(cs_raytrace);
    if (status == Status::FAILURE)
    {
        std::printf("error loading shader\n");
    }
    cs_raytrace.cleanup();
}

void Application::init()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW!\n";
        return;
    }

    contree_bitmask_lut.create();

#if defined(_DEBUG) || defined(DEBUG)
    printf("DEBUG MODE\n");
#else
    printf("NOT DEBUG MODE\n");
#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "Engine Viewport", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window!\n";
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        printf("Failed to initialize GLAD\n");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
        FirstPersonCameraSettings settings;

        settings.position = {0, 0, 0};
        settings.direction = glm::normalize(glm::vec3(1, 0, 1));
        settings.nearPlane = 2048.f;
        settings.farPlane = 0.125f;
        settings.fov_degrees = 45.f;
        settings.width = width;
        settings.height = height;
        camera.Init(settings);
    }

    {
        Shader cs_raytrace;
        Shader cs_raytrace_old;
        cs_raytrace.createShader("resources/tree_ray_trace_new.comp", GL_COMPUTE_SHADER);
        cs_raytrace_old.createShader("resources/tree_ray_trace.comp", GL_COMPUTE_SHADER);
        auto status = sp_raytrace.createProgram(cs_raytrace);
        if (status == Status::FAILURE)
        {
            std::printf("error loading shader\n");
            exit(1);
        }
        status = sp_raytrace_old.createProgram(cs_raytrace_old);
        if (status == Status::FAILURE)
        {
            std::printf("error loading shader\n");
            exit(1);
        }
        cs_raytrace.cleanup();
        cs_raytrace_old.cleanup();
    }

    {
        screen_texture.target = GL_TEXTURE_2D;
        screen_texture.Create();
        screen_texture.internalFormat = GL_RGBA8;
        screen_texture.Allocate(width, height);
    }

    {
        voxel_tree_buffer.create();
        contree_bitmask_lut_buffer.create();
    }
}

// Start the app

void Application::run()
{
    float last_time = (float)glfwGetTime();

    while (is_running && !glfwWindowShouldClose(window))
    {
        float current_time = (float)glfwGetTime();
        float delta_time = current_time - last_time;
        last_time = current_time;

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        bool opt_fullscreen = true;
        bool opt_padding = false;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        if (ImGui::Begin("Engine Dockspace Window", &is_running, window_flags))
        {
            if (opt_padding)
                ImGui::PopStyleVar();

            if (opt_fullscreen)
                ImGui::PopStyleVar(2);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
            }

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Exit", "Alt+F4"))
                    {
                        is_running = false;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();

        update(delta_time);

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }
}

// Your application logic here

void render_nodes(svo_node* nodes, size_t num_nodes, size_t idx)
{
    if (idx >= num_nodes)
        return;

    if (ImGui::TreeNode((void*)idx, "node %zu (Data: %u)###node_%zu", idx, nodes[idx].data, idx))
    {
        svo_node* node = nodes + idx;
        size_t child_count = node->child_count();
        for (size_t i = 0; i < child_count; i++)
        {
            render_nodes(nodes, num_nodes, node->firstchild + i);
        }
        ImGui::TreePop();
    }
}

void Application::update(float dt)
{
    input.update(window);

    glm::vec3 right_vector = glm::normalize(
        glm::cross(camera.GetForwardVector(), WorldDirection::Up));

    if (input.get_key(GLFW_KEY_W))
        camera.Translate(dt * speed * camera.GetForwardVector());
    if (input.get_key(GLFW_KEY_S))
        camera.Translate(-dt * speed * camera.GetForwardVector());
    if (input.get_key(GLFW_KEY_A))
        camera.Translate(-dt * speed * right_vector);
    if (input.get_key(GLFW_KEY_D))
        camera.Translate(dt * speed * right_vector);
    if (input.is_key_clicked(GLFW_KEY_C))
    {
        setup_voxel_data();
    }
    if (input.is_key_clicked(GLFW_KEY_M))
    {
        reload_shader(sp_raytrace);
    }

    if (input.is_key_clicked(GLFW_KEY_TAB))
    {
        old_shader ^= 1;
    }

    if (input.is_key_held(GLFW_KEY_F))
        speed *= glm::pow(2.f, dt);
    if (input.is_key_held(GLFW_KEY_R))
        speed /= glm::pow(2.f, dt);

    if (input.is_button_held(GLFW_MOUSE_BUTTON_RIGHT))
        camera.Rotate(input.get_mouse_dx() * sensitivity, input.get_mouse_dy() * sensitivity);

    ShaderProgramCompute& raytrace_shader = old_shader ? sp_raytrace_old : sp_raytrace;
    raytrace_shader.bind();
    raytrace_shader.uniformImg2D("u_ScreenImage", screen_texture.handle, 0, GL_READ_WRITE,
                                 GL_RGBA8);
    raytrace_shader.uniformMat4("u_InvView", glm::inverse(camera.GetViewMatrix()));
    raytrace_shader.uniformMat4("u_InvProj", glm::inverse(camera.GetProjectionMatrix()));
    raytrace_shader.uniform3f("u_CameraPosition", camera.GetPosition());
    raytrace_shader.uniform1i("u_RenderMode", int(render_mode));
    raytrace_shader.uniform1i("u_TraceVersion", int(trace_version));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, contree_bitmask_lut_buffer.handle());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, voxel_tree_buffer.handle());

    glm::ivec3 group_size = {16, 16, 1};
    glm::ivec3 groups = (glm::ivec3{width, height, 1} + group_size - 1) / group_size;
    dispatch_compute(raytrace_shader, groups, GL_ALL_BARRIER_BITS);

    raytrace_shader.unbind();

    ImGui::Begin("display");

    ImVec2 image_size;
    image_size.x = screen_texture.Width();
    image_size.y = screen_texture.Height();
    ImGui::Image(screen_texture.handle, image_size);
    ImGui::End();

    if (ImGui::Begin("debug"))
    {
        static int dt_count = 1;
        static float dt_sum = dt;
        dt_sum += dt;
        dt_count++;

        ImGui::Text("old_shader: %i", old_shader);
        ImGui::Text("speed: %fu/s", speed);
        ImGui::Text("dt: %fms", 1000.f * dt);
        ImGui::Text("smooth dt: %fms", 1000 * dt_sum / dt_count);
        if (dt_count > 255)
        {
            dt_count = 1;
            dt_sum = dt;
        }
        if (ImGui::Button("VSYNC ON"))
            glfwSwapInterval(1);
        if (ImGui::Button("VSYNC OFF"))
            glfwSwapInterval(0);

        svo_node* nodes = voxel_tree_data.node_data();
        size_t num_nodes = voxel_tree_data.node_count();

        render_nodes(nodes, num_nodes, 0);
    }
    ImGui::End();

    if (ImGui::Begin("lookup table"))
    {
        static int cx = 0;
        static int cy = 0;
        static int ex = 0;
        static int ey = 0;
        static bool show_lut = false;
        ImGui::Checkbox("show_lut", &show_lut);
        if (show_lut)
        {

            uint64_t bitmask = contree_bitmask_lut.get({cx, cy, 0}, {ex, ey, 0});

            for (int y = 0; y < 4; y++)
            {
                for (int x = 0; x < 4; x++)
                {
                    int i = x + 4 * y;
                    ImGui::PushID(i);
                    uint64_t bit = (bitmask >> i) & 0b1ull;
                    if (x == cx && y == cy)
                        ImGui::Text("[%u]", bit);
                    else if (x == ex && y == ey)
                        ImGui::Text("(%u)", bit);
                    else
                        ImGui::Text(" %u ", bit);

                    if (x < 3)
                        ImGui::SameLine();
                    ImGui::PopID();
                }
            }
            for (int y = 0; y < 4; y++)
            {
                for (int x = 0; x < 4; x++)
                {
                    int i = x + 4 * y;

                    ImGui::PushID(i);
                    if (ImGui::Button("[]"))
                    {
                        cx = x;
                        cy = y;
                    }
                    if (x < 3)
                        ImGui::SameLine();
                    ImGui::PopID();
                }
            }
            for (int y = 0; y < 4; y++)
            {
                for (int x = 0; x < 4; x++)
                {
                    int i = 16 + x + 4 * y;

                    ImGui::PushID(i);
                    if (ImGui::Button("[]"))
                    {
                        ex = x;
                        ey = y;
                    }
                    if (x < 3)
                        ImGui::SameLine();
                    ImGui::PopID();
                }
            }
            ImGui::Text("b %u", contree_bitmask_lut.index({cx, cy, 0}));
            ImGui::Text("e %u", contree_bitmask_lut.index({ex, ey, 0}));
        }
        ImGui::SliderInt("max_depth", &max_create_depth, 0, 6);
        for (int i = 0; i < RenderMode_count; i++)
        {
            if (ImGui::Button(RenderMode_names[i]))
            {
                render_mode = RenderMode(i);
            }
            if (render_mode == RenderMode(i))
            {
                ImGui::SameLine();
                ImGui::Text("<--");
            }
        }

        for (int i = 0; i < 5; i++)
        {
            if (ImGui::Button(std::to_string(i).c_str()))
            {
                trace_version = i;
            }
            if (trace_version == i)
            {
                ImGui::SameLine();
                ImGui::Text("<--");
            }
        }
    }
    ImGui::End();
}

// GLFW, GLAD, ImGui cleanup

void Application::cleanup()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}
bool is_aabb_homogeneous(const glm::vec3& box_min, const glm::vec3& box_max,
                         const glm::vec3& sphere_center, float sphere_radius)
{
    glm::vec3 closest_point;
    glm::vec3 farthest_point;

    for (int i = 0; i < 3; ++i)
    {
        closest_point[i] = std::max(box_min[i], std::min(sphere_center[i], box_max[i]));

        if (std::abs(sphere_center[i] - box_min[i]) > std::abs(sphere_center[i] - box_max[i]))
            farthest_point[i] = box_min[i];
        else
            farthest_point[i] = box_max[i];
    }

    float r2 = sphere_radius * sphere_radius;
    float dist_sq_closest = glm::distance2(sphere_center, closest_point);
    float dist_sq_farthest = glm::distance2(sphere_center, farthest_point);

    bool completely_outside = (dist_sq_closest > r2);
    bool completely_inside = (dist_sq_farthest < r2);

    return completely_outside || completely_inside;
}

bool intersect_sphere_aabb_cube(glm::vec3 sphere_center, float sphere_radius, glm::vec3 cube_min,
                                float cube_size)
{
    glm::vec3 cube_max = cube_min + glm::vec3(cube_size);

    glm::vec3 closest_point = glm::clamp(sphere_center, cube_min, cube_max);
    float min_dist_sq = glm::dot(closest_point - sphere_center, closest_point - sphere_center);

    glm::vec3 farthest_point;
    farthest_point.x = (std::abs(sphere_center.x - cube_min.x) >
                        std::abs(sphere_center.x - cube_max.x))
                           ? cube_min.x
                           : cube_max.x;
    farthest_point.y = (std::abs(sphere_center.y - cube_min.y) >
                        std::abs(sphere_center.y - cube_max.y))
                           ? cube_min.y
                           : cube_max.y;
    farthest_point.z = (std::abs(sphere_center.z - cube_min.z) >
                        std::abs(sphere_center.z - cube_max.z))
                           ? cube_min.z
                           : cube_max.z;

    float max_dist_sq = glm::dot(farthest_point - sphere_center, farthest_point - sphere_center);
    float r_sq = sphere_radius * sphere_radius;

    return (min_dist_sq <= r_sq) && (max_dist_sq >= r_sq);
}

class RandomContree
{
public:
    static svo::node_creation_status example_create_node_random(svo_tree::NodeCreationContext& ctx)
    {
        float random = rand() / float(RAND_MAX);
        float split_chance = 0.25f;
        if (ctx.parent)
        {
            if (ctx.parent->data == 0)
                return svo::node_creation_status_stop;
        }
        if (ctx.current_depth <= 0)
        {
            return svo::node_creation_status_create;
        }
        if (random < split_chance)
            *ctx.node_data = ctx.current_depth >= 3 ? rand() % 2 : (rand() % 255 + 1);
        return random < split_chance ? svo::node_creation_status_create
                                     : svo::node_creation_status_stop;
    }
    static bool example_homogeneous(svo_tree::HomogeneousContext& ctx) { return false; }
};

class SphereContree
{
public:
    inline static float root_size = 64.f;
    inline static glm::vec3 sphere_center = glm::vec3(32.f);
    inline static float sphere_radius = 20.f;
    static svo::node_creation_status create(svo_tree::NodeCreationContext& ctx)
    {
        uint64_t coords[3];
        uint64_t depth;
        int branch_factor = 2;
        get_morton_coords<3>(ctx.morton_index, coords, branch_factor, depth);

        float node_size = root_size / powf(float(1 << branch_factor), depth);
        glm::vec3 min;
        min.x = node_size * coords[0];
        min.y = node_size * coords[1];
        min.z = node_size * coords[2];

        if (intersect_sphere_aabb_cube(sphere_center, sphere_radius, min, node_size))
        {
            *ctx.node_data = 1;
            if (depth == 4)
                *ctx.node_data = rand() % 2;
        }
        else
        {
            *ctx.node_data = 0;
            return svo::node_creation_status_stop;
        }
        return svo::node_creation_status_create;
    }
    inline static uint32_t count = 0;
    static bool is_homogeneous(svo_tree::HomogeneousContext& ctx)
    {
        uint64_t coords[3];
        uint64_t depth;
        int branch_factor = 2;
        get_morton_coords<3>(ctx.morton_index, coords, branch_factor, depth);

        float node_size = root_size / powf(float(1 << branch_factor), depth);
        glm::vec3 min;
        min.x = node_size * coords[0];
        min.y = node_size * coords[1];
        min.z = node_size * coords[2];
        bool is_homogeneous = is_aabb_homogeneous(min, min + node_size, sphere_center,
                                                  sphere_radius);

        if (count++ < 256)
        {
            for (int i = 0; i < depth; i++)
                printf(" ");
            std::cout << std::format("{}x{}x{} -> {} {}\n", min.x, min.y, min.z, node_size,
                                     is_homogeneous);
        }

        return is_homogeneous;
    }
};

void Application::setup_voxel_data()
{
    voxel_tree_data.create(TerrainContree::create, TerrainContree::is_homogeneous,
                           max_create_depth);
    printf("created %u nodes\n", voxel_tree_data.node_count());

    contree_bitmask_lut_buffer.allocate(sizeof(contree_bitmask_lut), contree_bitmask_lut.get_data(),
                                        GL_STATIC_DRAW);

    voxel_tree_buffer.allocate(voxel_tree_data.node_count() * sizeof(svo_node),
                               voxel_tree_data.node_data(), GL_STREAM_DRAW);
}