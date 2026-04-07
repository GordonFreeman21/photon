#version 450

/*
 * Photon - G-Buffer Fragment Shader
 * Outputs multiple render targets for deferred shading:
 *   RT0: RGB albedo (8-bit)
 *   RT1: RG normal (octahedral 16-bit), BA roughness+metallic
 *   RT2: RGB emissive (11-11-10 float), A flags
 *   RT3: RG motion vectors (16-bit float), B depth derivative, A mesh ID
 */

/* Material UBO */
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color;
    float metallic;
    float roughness;
    float ior;
    float emissive_strength;
    vec3  emissive_color;
    uint  flags;
} material;

/* Textures */
layout(set = 1, binding = 1) uniform sampler2D albedo_map;
layout(set = 1, binding = 2) uniform sampler2D normal_map;
layout(set = 1, binding = 3) uniform sampler2D metallic_roughness_map;
layout(set = 1, binding = 4) uniform sampler2D emissive_map;

/* Inputs from vertex shader */
layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec4 frag_tangent;
layout(location = 4) in vec4 frag_cur_pos;
layout(location = 5) in vec4 frag_prev_pos;
layout(location = 6) flat in uint frag_material_id;
layout(location = 7) flat in uint frag_mesh_id;

/* G-Buffer outputs */
layout(location = 0) out vec4 out_albedo;       // RT0: RGB albedo
layout(location = 1) out vec4 out_normal_rm;     // RT1: RG octahedral normal, B roughness, A metallic
layout(location = 2) out vec4 out_emissive;      // RT2: RGB emissive, A flags
layout(location = 3) out vec4 out_motion_depth;  // RT3: RG motion vectors, B depth deriv, A mesh ID

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Octahedral Normal Encoding                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec2 octahedral_encode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        vec2 wrapped = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = wrapped;
    }
    return n.xy * 0.5 + 0.5; // map to [0, 1]
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Normal Mapping                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec3 apply_normal_map(vec3 normal_sample, vec3 N, vec4 T) {
    vec3 tangent_normal = normal_sample * 2.0 - 1.0;
    
    vec3 tangent = normalize(T.xyz);
    vec3 bitangent = normalize(cross(N, tangent) * T.w);
    mat3 TBN = mat3(tangent, bitangent, N);
    
    return normalize(TBN * tangent_normal);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Main                                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

void main() {
    /* Alpha test */
    vec4 albedo = material.base_color * texture(albedo_map, frag_uv);
    if ((material.flags & 0x01u) != 0u && albedo.a < 0.5)
        discard;
    
    /* Normal (optionally from normal map) */
    vec3 N = normalize(frag_normal);
    vec3 normal_sample = texture(normal_map, frag_uv).rgb;
    if (length(normal_sample) > 0.01) {
        N = apply_normal_map(normal_sample, N, frag_tangent);
    }
    
    /* Metallic-roughness */
    vec2 mr = texture(metallic_roughness_map, frag_uv).bg; // glTF convention: B=metallic, G=roughness
    float metallic_val = material.metallic * mr.x;
    float roughness_val = material.roughness * mr.y;
    
    /* Emissive */
    vec3 emissive = material.emissive_color * material.emissive_strength;
    emissive += texture(emissive_map, frag_uv).rgb;
    
    /* Motion vectors: screen-space velocity */
    vec2 cur_ndc = (frag_cur_pos.xy / frag_cur_pos.w) * 0.5 + 0.5;
    vec2 prev_ndc = (frag_prev_pos.xy / frag_prev_pos.w) * 0.5 + 0.5;
    vec2 motion = cur_ndc - prev_ndc;
    
    /* Depth derivative (for edge detection in denoiser) */
    float depth_deriv = length(vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z)));
    
    /* Encode flags as normalized float */
    float flags_encoded = float(material.flags) / 255.0;
    
    /* Write G-buffer */
    out_albedo = vec4(albedo.rgb, 1.0);
    out_normal_rm = vec4(octahedral_encode(N), roughness_val, metallic_val);
    out_emissive = vec4(emissive, flags_encoded);
    out_motion_depth = vec4(motion, depth_deriv, float(frag_mesh_id) / 65535.0);
}
