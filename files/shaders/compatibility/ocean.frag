#version 430 compatibility

layout(binding = 1) uniform sampler2DArray normals; // xy = slope, z = jacobian/foam?
// In unpack: imageStore(normal_map, id, vec4(gradient, dhx_dx, foam));
// So xy = gradient, z = dhx_dx (partial derivative), w = foam.

uniform int num_cascades;
uniform vec4 map_scales[4];
uniform vec3 sun_dir;
uniform vec3 sun_color;
uniform vec3 water_color;

in vec3 vWorldPos;
in vec2 vUV;

void main() {
    vec3 total_normal = vec3(0.0, 0.0, 1.0); // Start with up vector
    float total_foam = 0.0;
    
    for (int i = 0; i < num_cascades; ++i) {
        vec2 uv = vWorldPos.xy * map_scales[i].x;
        vec4 sample_norm = texture(normals, vec3(uv, float(i)));
        
        vec2 gradient = sample_norm.xy;
        float foam = sample_norm.w;
        
        // Combine gradients?
        // Ideally we sum gradients and then normalize.
        total_normal.xy -= gradient; // Assuming gradient is slope
        total_foam += foam;
    }
    
    total_normal = normalize(total_normal);
    
    // Simple lighting
    vec3 view_dir = normalize(-vWorldPos); // Simplified view dir (assuming camera at 0,0,0 in view space? No, vWorldPos is world)
    // We need camera position.
    // OpenMW provides osg_ViewMatrixInverse usually?
    // Let's assume simple directional light for now.
    
    float diff = max(dot(total_normal, sun_dir), 0.0);
    vec3 color = water_color * diff + sun_color * pow(diff, 32.0); // Specular
    
    color += vec3(total_foam); // Add foam
    
    gl_FragColor = vec4(color, 1.0);
}
