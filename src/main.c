/*
 * Photon - Adaptive Hybrid Ray Tracer
 * Main Entry Point
 *
 * Sets up GLFW window, initializes renderer, runs main loop,
 * handles input for debug visualization and quality toggling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "../include/renderer.h"
#include "../include/camera.h"
#include "../include/math_util.h"
#include "../include/scene.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GLFW Stub (compiles without GLFW installed)                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

#ifdef PHOTON_HAS_GLFW
    #include <GLFW/glfw3.h>
#else
    /* Minimal GLFW-like stubs for compilation */
    typedef struct GLFWwindow GLFWwindow;
    typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
    typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);

    #define GLFW_KEY_ESCAPE     256
    #define GLFW_KEY_F1         290
    #define GLFW_KEY_F2         291
    #define GLFW_KEY_F3         292
    #define GLFW_KEY_F4         293
    #define GLFW_KEY_F5         294
    #define GLFW_KEY_F6         295
    #define GLFW_KEY_F7         296
    #define GLFW_KEY_F8         297
    #define GLFW_KEY_F9         298
    #define GLFW_KEY_F10        299
    #define GLFW_KEY_F11        300
    #define GLFW_KEY_F12        301
    #define GLFW_KEY_W          87
    #define GLFW_KEY_A          65
    #define GLFW_KEY_S          83
    #define GLFW_KEY_D          68
    #define GLFW_KEY_Q          81
    #define GLFW_KEY_E          69
    #define GLFW_KEY_1          49
    #define GLFW_KEY_2          50
    #define GLFW_KEY_3          51
    #define GLFW_KEY_4          52
    #define GLFW_KEY_5          53
    #define GLFW_KEY_P          80
    #define GLFW_PRESS          1
    #define GLFW_RELEASE        0
    #define GLFW_TRUE           1
    #define GLFW_FALSE          0
    #define GLFW_CLIENT_API             0x00022001
    #define GLFW_NO_API                 0
    #define GLFW_RESIZABLE              0x00020003

    static int glfw_stub_should_close = 0;
    static int glfw_stub_frame = 0;

    static int glfwInit(void) {
        printf("[Photon] GLFW stub initialized\n");
        return GLFW_TRUE;
    }
    static void glfwTerminate(void) {}
    static void glfwWindowHint(int hint, int value) { (void)hint; (void)value; }
    static GLFWwindow* glfwCreateWindow(int w, int h, const char* t, void* m, void* s) {
        (void)w; (void)h; (void)m; (void)s;
        printf("[Photon] Window created: %s (%dx%d)\n", t, w, h);
        static int dummy;
        return (GLFWwindow*)&dummy;
    }
    static void glfwDestroyWindow(GLFWwindow* w) { (void)w; }
    static int glfwWindowShouldClose(GLFWwindow* w) {
        (void)w;
        return glfw_stub_frame >= 300; /* Run 300 frames in stub mode */
    }
    static void glfwSetWindowShouldClose(GLFWwindow* w, int v) { (void)w; glfw_stub_should_close = v; }
    static void glfwPollEvents(void) { glfw_stub_frame++; }
    static void glfwSwapBuffers(GLFWwindow* w) { (void)w; }
    static double glfwGetTime(void) { return (double)glfw_stub_frame / 60.0; }
    static GLFWkeyfun glfwSetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) { (void)w; (void)cb; return NULL; }
    static GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* w, GLFWframebuffersizefun cb) {
        (void)w; (void)cb; return NULL;
    }
    static void glfwGetFramebufferSize(GLFWwindow* w, int* width, int* height) {
        (void)w; *width = 1920; *height = 1080;
    }
    static int glfwGetKey(GLFWwindow* w, int key) { (void)w; (void)key; return GLFW_RELEASE; }
#endif

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Application State                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GLFWwindow*     window;
    RTRenderer*     renderer;
    RTCamera        camera;
    int             width;
    int             height;
    bool            running;
    bool            paused;
    bool            show_stats;
    DebugVisMode    debug_mode;
    RTQualityPreset quality_preset;
    double          last_time;
    float           delta_time;
    float           camera_speed;
    float           camera_yaw;
    float           camera_pitch;
} AppState;

static AppState g_app;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Input Handling                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;

    if (action != GLFW_PRESS) return;

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;

    /* Debug visualization modes (F1-F10) */
    case GLFW_KEY_F1: g_app.debug_mode = DEBUG_VIS_NONE;           break;
    case GLFW_KEY_F2: g_app.debug_mode = DEBUG_VIS_BVH_DEPTH;      break;
    case GLFW_KEY_F3: g_app.debug_mode = DEBUG_VIS_RAY_HEATMAP;    break;
    case GLFW_KEY_F4: g_app.debug_mode = DEBUG_VIS_NORMALS;        break;
    case GLFW_KEY_F5: g_app.debug_mode = DEBUG_VIS_DEPTH;          break;
    case GLFW_KEY_F6: g_app.debug_mode = DEBUG_VIS_ROUGHNESS;      break;
    case GLFW_KEY_F7: g_app.debug_mode = DEBUG_VIS_METALLIC;       break;
    case GLFW_KEY_F8: g_app.debug_mode = DEBUG_VIS_MOTION_VECTORS; break;
    case GLFW_KEY_F9: g_app.debug_mode = DEBUG_VIS_PROBE_LOCATIONS;break;
    case GLFW_KEY_F10:g_app.debug_mode = DEBUG_VIS_WIREFRAME;      break;

    /* Quality presets (1-5) */
    case GLFW_KEY_1: g_app.quality_preset = RT_QUALITY_POTATO; break;
    case GLFW_KEY_2: g_app.quality_preset = RT_QUALITY_LOW;    break;
    case GLFW_KEY_3: g_app.quality_preset = RT_QUALITY_MEDIUM; break;
    case GLFW_KEY_4: g_app.quality_preset = RT_QUALITY_HIGH;   break;
    case GLFW_KEY_5: g_app.quality_preset = RT_QUALITY_ULTRA;  break;

    /* Pause / Screenshot */
    case GLFW_KEY_P:
        g_app.paused = !g_app.paused;
        printf("[Photon] %s\n", g_app.paused ? "Paused" : "Resumed");
        break;
    case GLFW_KEY_F12:
        rt_capture_frame(g_app.renderer, "photon_capture.png");
        printf("[Photon] Frame captured\n");
        break;

    /* Toggle stats overlay */
    case GLFW_KEY_F11:
        g_app.show_stats = !g_app.show_stats;
        break;
    }

    /* Apply debug vis and quality changes */
    if (g_app.renderer) {
        rt_set_debug_vis(g_app.renderer, g_app.debug_mode);
        rt_set_quality_preset(g_app.renderer, g_app.quality_preset);
    }
}

static void framebuffer_resize_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    g_app.width = width;
    g_app.height = height;
    printf("[Photon] Resized to %dx%d\n", width, height);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Camera Movement                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void update_camera_input(float dt) {
    float speed = g_app.camera_speed * dt;
    vec3 pos = g_app.camera.position;
    vec3 fwd = g_app.camera.forward;
    vec3 right = g_app.camera.right;
    vec3 up = {0.0f, 1.0f, 0.0f};
    bool moved = false;

    if (glfwGetKey(g_app.window, GLFW_KEY_W) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(fwd, speed)); moved = true;
    }
    if (glfwGetKey(g_app.window, GLFW_KEY_S) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(fwd, -speed)); moved = true;
    }
    if (glfwGetKey(g_app.window, GLFW_KEY_A) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(right, -speed)); moved = true;
    }
    if (glfwGetKey(g_app.window, GLFW_KEY_D) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(right, speed)); moved = true;
    }
    if (glfwGetKey(g_app.window, GLFW_KEY_Q) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(up, -speed)); moved = true;
    }
    if (glfwGetKey(g_app.window, GLFW_KEY_E) == GLFW_PRESS) {
        pos = vec3_add(pos, vec3_scale(up, speed)); moved = true;
    }

    if (moved) {
        camera_set_position(&g_app.camera, pos);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Test Scene Setup                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void setup_cornell_box(RTRenderer* r) {
    printf("[Photon] Setting up Cornell Box test scene...\n");

    /* Floor quad (2 triangles) */
    Vertex floor_verts[] = {
        {{-5, 0, -5}, {0,1,0}, {0,0}, {1,0,0,1}},
        {{ 5, 0, -5}, {0,1,0}, {1,0}, {1,0,0,1}},
        {{ 5, 0,  5}, {0,1,0}, {1,1}, {1,0,0,1}},
        {{-5, 0,  5}, {0,1,0}, {0,1}, {1,0,0,1}},
    };
    uint32_t floor_idx[] = {0, 1, 2, 0, 2, 3};

    RTMeshData floor_mesh = {
        .vertices = floor_verts,
        .indices = floor_idx,
        .vertex_count = 4,
        .index_count = 6,
        .material_id = 0,
        .bounds = {{-5,0,-5}, {5,0,5}}
    };

    float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };

    uint32_t floor_id = rt_add_mesh(r, &floor_mesh);
    rt_add_instance(r, floor_id, identity);

    /* Back wall */
    Vertex back_verts[] = {
        {{-5, 0, -5}, {0,0,1}, {0,0}, {1,0,0,1}},
        {{ 5, 0, -5}, {0,0,1}, {1,0}, {1,0,0,1}},
        {{ 5, 10,-5}, {0,0,1}, {1,1}, {1,0,0,1}},
        {{-5, 10,-5}, {0,0,1}, {0,1}, {1,0,0,1}},
    };
    uint32_t back_idx[] = {0, 1, 2, 0, 2, 3};

    RTMeshData back_mesh = {
        .vertices = back_verts,
        .indices = back_idx,
        .vertex_count = 4,
        .index_count = 6,
        .material_id = 0,
        .bounds = {{-5,0,-5}, {5,10,-5}}
    };
    uint32_t back_id = rt_add_mesh(r, &back_mesh);
    rt_add_instance(r, back_id, identity);

    /* Left wall (red) */
    Vertex left_verts[] = {
        {{-5, 0, -5}, {1,0,0}, {0,0}, {0,0,1,1}},
        {{-5, 0,  5}, {1,0,0}, {1,0}, {0,0,1,1}},
        {{-5, 10, 5}, {1,0,0}, {1,1}, {0,0,1,1}},
        {{-5, 10,-5}, {1,0,0}, {0,1}, {0,0,1,1}},
    };
    uint32_t left_idx[] = {0, 1, 2, 0, 2, 3};

    RTMeshData left_mesh = {
        .vertices = left_verts,
        .indices = left_idx,
        .vertex_count = 4,
        .index_count = 6,
        .material_id = 0,
        .bounds = {{-5,0,-5}, {-5,10,5}}
    };
    uint32_t left_id = rt_add_mesh(r, &left_mesh);
    rt_add_instance(r, left_id, identity);

    /* Right wall (green) */
    Vertex right_verts[] = {
        {{5, 0, -5}, {-1,0,0}, {0,0}, {0,0,-1,1}},
        {{5, 0,  5}, {-1,0,0}, {1,0}, {0,0,-1,1}},
        {{5, 10, 5}, {-1,0,0}, {1,1}, {0,0,-1,1}},
        {{5, 10,-5}, {-1,0,0}, {0,1}, {0,0,-1,1}},
    };
    uint32_t right_idx[] = {0, 2, 1, 0, 3, 2};

    RTMeshData right_mesh = {
        .vertices = right_verts,
        .indices = right_idx,
        .vertex_count = 4,
        .index_count = 6,
        .material_id = 0,
        .bounds = {{5,0,-5}, {5,10,5}}
    };
    uint32_t right_id = rt_add_mesh(r, &right_mesh);
    rt_add_instance(r, right_id, identity);

    /* Directional light (sun) */
    RTDirectionalLight sun = {
        .direction = {0.5f, -0.8f, -0.3f},
        .color = {1.0f, 0.95f, 0.85f},
        .intensity = 3.0f,
        .angular_diameter = 0.01f
    };
    rt_add_directional_light(r, &sun);

    /* Point light (interior) */
    RTPointLight interior_light = {
        .position = {0.0f, 9.0f, 0.0f},
        .color = {1.0f, 0.9f, 0.7f},
        .intensity = 10.0f,
        .radius = 0.1f,
        .range = 20.0f
    };
    rt_add_point_light(r, &interior_light);

    printf("[Photon] Cornell Box loaded: 4 meshes, 2 lights\n");
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Statistics Display                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void print_stats(const RTFrameStats* stats, uint64_t frame) {
    if (frame % 60 != 0) return; /* Print every 60 frames */

    printf("\r[Photon] Frame %5llu | %6.2f ms (%4.0f FPS) | "
           "Rays: %7u | Tris: %7u | BVH: %.1f | "
           "GB: %.1f | Shd: %.1f | Ref: %.1f | GI: %.1f | DN: %.1f ms",
           (unsigned long long)frame,
           stats->frame_time_ms,
           stats->frame_time_ms > 0 ? 1000.0f / stats->frame_time_ms : 0.0f,
           stats->rays_traced,
           stats->triangles_tested,
           stats->average_bvh_depth,
           stats->gbuffer_time_ms,
           stats->shadow_trace_time_ms,
           stats->reflection_trace_time_ms,
           stats->gi_trace_time_ms,
           stats->denoise_time_ms);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Print Controls Help                                                       */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void print_help(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║           PHOTON - Adaptive Hybrid Ray Tracer            ║\n");
    printf("║       For Low-End GPU Gaming (GTX 1060 Targeted)         ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Movement:  W/A/S/D - Move camera  |  Q/E - Up/Down     ║\n");
    printf("║                                                          ║\n");
    printf("║  Quality:   1-Potato  2-Low  3-Medium  4-High  5-Ultra   ║\n");
    printf("║                                                          ║\n");
    printf("║  Debug:     F1-Off  F2-BVH  F3-Rays  F4-Normals         ║\n");
    printf("║             F5-Depth  F6-Roughness  F7-Metallic          ║\n");
    printf("║             F8-Motion  F9-Probes  F10-Wireframe          ║\n");
    printf("║                                                          ║\n");
    printf("║  Other:     F11-Stats  F12-Screenshot  P-Pause  ESC-Quit ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Main                                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    print_help();

    /* ─── Initialize GLFW ─── */
    if (!glfwInit()) {
        fprintf(stderr, "[Photon] ERROR: Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    g_app.width = 1920;
    g_app.height = 1080;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); /* Vulkan, no OpenGL */
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    g_app.window = glfwCreateWindow(g_app.width, g_app.height,
                                     "Photon Ray Tracer", NULL, NULL);
    if (!g_app.window) {
        fprintf(stderr, "[Photon] ERROR: Failed to create window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwSetKeyCallback(g_app.window, key_callback);
    glfwSetFramebufferSizeCallback(g_app.window, framebuffer_resize_callback);

    /* ─── Initialize Renderer ─── */
    RTRendererDesc desc = {
        .width              = (uint32_t)g_app.width,
        .height             = (uint32_t)g_app.height,
        .fullscreen         = false,
        .vsync              = true,
        .quality            = RT_QUALITY_MEDIUM,
        .ray_budget         = 4000000,
        .shadow_resolution_scale    = 1.0f,
        .reflection_resolution_scale = 0.5f,
        .gi_resolution_scale        = 0.25f,
        .shadow_samples     = 1,
        .reflection_samples = 1,
        .gi_samples         = 1,
        .denoise_iterations = 5,
        .temporal_blend     = 0.9f,
        .enable_reflections = true,
        .enable_gi          = true,
        .enable_volumetrics = false,
        .enable_checkerboard = true,
        .adaptive_quality   = true,
        .window_title       = "Photon Ray Tracer"
    };

    printf("[Photon] Creating renderer (%ux%u, quality=Medium)...\n",
           desc.width, desc.height);

    g_app.renderer = rt_renderer_create(&desc);
    if (!g_app.renderer) {
        fprintf(stderr, "[Photon] ERROR: Failed to create renderer\n");
        glfwDestroyWindow(g_app.window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /* ─── Setup Camera ─── */
    g_app.camera = camera_create(
        vec3_new(0.0f, 5.0f, 15.0f),   /* position */
        vec3_new(0.0f, 3.0f, 0.0f),    /* target */
        60.0f * (PHOTON_PI / 180.0f),   /* fov (60 degrees) */
        (float)g_app.width / (float)g_app.height
    );
    g_app.camera_speed = 5.0f;
    g_app.camera_yaw = -90.0f;
    g_app.camera_pitch = -15.0f;
    g_app.quality_preset = RT_QUALITY_MEDIUM;
    g_app.debug_mode = DEBUG_VIS_NONE;
    g_app.show_stats = true;
    g_app.running = true;
    g_app.paused = false;

    rt_set_camera(g_app.renderer, &g_app.camera);

    /* ─── Load Test Scene ─── */
    setup_cornell_box(g_app.renderer);

    /* ─── Main Loop ─── */
    printf("[Photon] Entering main loop...\n");
    g_app.last_time = glfwGetTime();
    uint64_t frame_count = 0;

    while (!glfwWindowShouldClose(g_app.window)) {
        /* Delta time */
        double current_time = glfwGetTime();
        g_app.delta_time = (float)(current_time - g_app.last_time);
        g_app.last_time = current_time;

        /* Clamp delta for stability */
        if (g_app.delta_time > 0.1f) g_app.delta_time = 0.1f;

        glfwPollEvents();

        if (!g_app.paused) {
            /* Update camera from input */
            update_camera_input(g_app.delta_time);
            rt_set_camera(g_app.renderer, &g_app.camera);

            /* Render frame */
            rt_render_frame(g_app.renderer);

            /* Stats */
            if (g_app.show_stats) {
                RTFrameStats stats = rt_get_last_frame_stats(g_app.renderer);
                print_stats(&stats, frame_count);
            }

            frame_count++;
        }

        glfwSwapBuffers(g_app.window);
    }

    /* ─── Cleanup ─── */
    printf("\n[Photon] Shutting down...\n");
    printf("[Photon] Total frames rendered: %llu\n", (unsigned long long)frame_count);

    rt_renderer_destroy(g_app.renderer);
    glfwDestroyWindow(g_app.window);
    glfwTerminate();

    printf("[Photon] Goodbye!\n");
    return EXIT_SUCCESS;
}
