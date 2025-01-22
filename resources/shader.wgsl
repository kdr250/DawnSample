struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec3f,
    @location(3) uv: vec2f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};

struct MyUniforms {
    projectionMatrix: mat4x4f,
    viewMatrix: mat4x4f,
    modelMatrix: mat4x4f,
    color: vec4f,
    time: f32,
};

struct LightingUniforms {
    directions: array<vec4f, 2>,
    colors: array<vec4f, 2>,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var textureSampler: sampler;
@group(0) @binding(3) var<uniform> uLighting: LightingUniforms;

const PI = 3.14159265359;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
    out.color = in.color;
    out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
    out.uv = in.uv;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Compute shading
    let normal = normalize(in.normal);
    var shading = vec3f(0.0);
    for (var i: i32 = 0; i < 2; i++) {
        let directions = normalize(uLighting.directions[i].xyz);
        let color = uLighting.colors[i].rgb;
        shading += max(0.0, dot(directions, normal)) * color;
    }

    // Sample texture
    let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;

    // Combine texture and lighting
    let color = baseColor * shading;
    return vec4f(color, 1.0);
}
