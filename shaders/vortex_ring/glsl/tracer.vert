#version 450

layout(location = 0) in vec4 position;  // xyz: position, w: life

layout(push_constant) uniform TracerRenderPC
{
    mat4 view_projection;
    float physical_radius;
    float viewport_height;
    float fov_y;
    float max_life;
    vec3 young_color;
    float pad1;
    vec3 old_color;
    float pad2;
} pc;

layout(location = 0) out float out_life_ratio;  // Pass life to fragment shader
layout(location = 1) out vec3 out_young_color;
layout(location = 2) out vec3 out_old_color;
void main()
{
    if (position.w < 0.0)
    {
        gl_Position = vec4(-10.0, -10.0, -10.0, 0.0);
        gl_PointSize = 0.0;
        return;
    }

    vec4 clip_pos = pc.view_projection * vec4(position.xyz, 1.0);
    gl_Position = clip_pos;
    
    // Calculate life ratio (0 = dead, 1 = full life)
    // Special handling:
    // 1. life > 1000.0 (FLT_MAX): Tracer eternal life
    // 2. pc.max_life == 0.0: Vorton particle flag (eternal life)
    float life = max(position.w, 0.0);
    float life_ratio;
    if (life > 1000.0 || pc.max_life == 0.0)  // infinite life
    {
        life_ratio = 1.0;  // Always fully visible
    }
    else
    {
        life_ratio = clamp(life / pc.max_life, 0.0, 1.0);
    }
    out_life_ratio = life_ratio;
    out_young_color = pc.young_color;
    out_old_color = pc.old_color;
    
    // Extract view-space position for size calculation
    // Approximate distance from w component
    float distance = max(clip_pos.w, 0.1);
    float tan_half_fov = tan(pc.fov_y * 0.5);
    
    // Tracer size based on life (young tracers are larger)
    float life_size_factor = 0.5 + 0.5 * life_ratio;
    float projected_size = pc.physical_radius * pc.viewport_height * life_size_factor / (distance * tan_half_fov);
    gl_PointSize = clamp(projected_size, 1.0, 32.0);
}