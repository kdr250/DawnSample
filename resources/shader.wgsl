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
    @location(3) viewDirection: vec3f,
};

struct MyUniforms {
    projectionMatrix: mat4x4f,
    viewMatrix: mat4x4f,
    modelMatrix: mat4x4f,
    color: vec4f,
    cameraWorldPosition: vec3f,
    time: f32,
};

struct LightingUniforms {
    directions: array<vec4f, 2>,
    colors: array<vec4f, 2>,
    hardness: f32,
    kd: f32,
    ks: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var normalTexture: texture_2d<f32>;
@group(0) @binding(3) var textureSampler: sampler;
@group(0) @binding(4) var<uniform> uLighting: LightingUniforms;

const PI = 3.14159265359;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPosition = uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
    out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * worldPosition;
    
    out.color = in.color;
    out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
    out.uv = in.uv;

    let cameraWorldPosition = uMyUniforms.cameraWorldPosition;
    out.viewDirection = cameraWorldPosition - worldPosition.xyz;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample normal
    let encodedN = textureSample(normalTexture, textureSampler, in.uv).rgb;
    let N = normalize(encodedN - 0.5);
    let V = normalize(in.viewDirection);

    // Sample texture
    let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;
    let kd = uLighting.kd; // strength of the diffuse effect
    let ks = uLighting.ks; // strength of the specular effect
    let hardness = uLighting.hardness;

    var color = vec3f(0.0);
    for (var i: i32 = 0; i < 2; i++) {
        let lightColor = uLighting.colors[i].rgb;

        let L = normalize(uLighting.directions[i].xyz);
        let R = reflect(-L, N); // equivalent to 2.0 * dot(N, L) * N - L

        let diffuse = max(0.0, dot(L, N)) * lightColor;

        // We clamp the dot product to 0 when it is negative
        let RoV = max(0.0, dot(R, V));
        let specular = pow(RoV, hardness);

        color += baseColor * kd * diffuse + ks * specular;
    }

    return vec4f(color, 1.0);
}
