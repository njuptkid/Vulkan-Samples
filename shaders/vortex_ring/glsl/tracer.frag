#version 450

layout(location = 0) in float life_ratio;  // From vertex shader: 0=dead, 1=full life
layout(location = 1) in vec3 young_color;
layout(location = 2) in vec3 old_color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord) * 2.0;
    
    if (dist > 1.0)
        discard;
    
    // Softness for circular point sprite
    float softness = smoothstep(0.8, 0.2, dist);
    
    vec3 color = mix(old_color, young_color, life_ratio);
    
    // Alpha based on life and softness
    // Dead tracers (life_ratio=0) are essentially invisible
    float base_alpha = 0.1 + 0.7 * life_ratio;  // 0.1 to 0.8
    float alpha = softness * base_alpha;
    
    // Early discard for nearly dead tracers (optimization)
    if (alpha < 0.01)
        discard;
    
    out_color = vec4(color, alpha);
}