#version 430 compatibility

layout(binding = 0) uniform sampler2DArray displacements;

uniform int num_cascades;
uniform vec4 map_scales[4]; // xy = scale, zw = unused
uniform float time;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vFoam;

void main() {
    vUV = gl_Vertex.xy; // Assuming plane is on XY or XZ. OpenMW water is usually Z-up.
    // OpenMW passes vertex in object space.
    // Water is usually a grid.
    
    vec4 worldPos = gl_ModelViewMatrix * gl_Vertex; // Wait, ModelView transforms to View space.
    // We want World space for the FFT lookup usually.
    // OpenMW's water.vert uses: varying vec3 worldPos; ... worldPos = (gl_ModelViewMatrixInverse * gl_ModelViewMatrix * gl_Vertex).xyz;
    // effectively gl_Vertex if model matrix is identity?
    // Actually, OpenMW water is a separate drawable.
    
    vec3 pos = gl_Vertex.xyz;
    vec3 total_displacement = vec3(0.0);
    float total_foam = 0.0;
    
    for (int i = 0; i < num_cascades; ++i) {
        vec2 uv = pos.xy * map_scales[i].x; // Scaling
        // Sample displacement
        // We need to wrap UVs.
        vec3 disp = texture(displacements, vec3(uv, float(i))).xyz;
        total_displacement += disp;
        
        // Foam is usually stored in alpha or separate channel
        // In our unpack shader: imageStore(displacement_map, id, vec4(hx, hy, hz, 0) * sign_shift);
        // So displacement map has 0 in alpha.
        // Normal map has foam in alpha.
    }
    
    pos += total_displacement;
    
    vWorldPos = pos;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);
    
    // We'll sample normals in fragment shader for better detail
}
