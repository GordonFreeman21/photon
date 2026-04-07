#version 450

/*
 * Photon - G-Buffer Vertex Shader
 * Transforms geometry and outputs per-vertex data for G-buffer generation
 */

/* Push constants for per-draw data */
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 prev_model;
    uint material_id;
    uint mesh_id;
} push;

/* Camera UBO */
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 prev_view_projection;
    vec4 camera_position;       // xyz = position, w = unused
    vec4 jitter;                // xy = current jitter, zw = previous jitter
} camera;

/* Vertex inputs */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

/* Outputs to fragment shader */
layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec4 frag_tangent;
layout(location = 4) out vec4 frag_cur_pos;   // current clip position
layout(location = 5) out vec4 frag_prev_pos;  // previous clip position (for motion vectors)
layout(location = 6) flat out uint frag_material_id;
layout(location = 7) flat out uint frag_mesh_id;

void main() {
    vec4 world_pos = push.model * vec4(in_position, 1.0);
    frag_world_pos = world_pos.xyz;
    
    /* Transform normal to world space (use inverse transpose for non-uniform scale) */
    mat3 normal_matrix = transpose(inverse(mat3(push.model)));
    frag_normal = normalize(normal_matrix * in_normal);
    
    frag_uv = in_uv;
    frag_tangent = vec4(normalize(normal_matrix * in_tangent.xyz), in_tangent.w);
    
    /* Current and previous clip positions for motion vectors */
    frag_cur_pos = camera.view_projection * world_pos;
    vec4 prev_world_pos = push.prev_model * vec4(in_position, 1.0);
    frag_prev_pos = camera.prev_view_projection * prev_world_pos;
    
    /* Apply jitter for TAA (only to output position, not to motion vector calc) */
    vec4 jittered_pos = frag_cur_pos;
    jittered_pos.xy += camera.jitter.xy * jittered_pos.w;
    
    frag_material_id = push.material_id;
    frag_mesh_id = push.mesh_id;
    
    gl_Position = jittered_pos;
}
