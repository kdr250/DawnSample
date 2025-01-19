struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

struct MyUniforms {
    color: vec4f,
    time: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;

const PI = 3.14159265359;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let ratio = 1024.0 / 768.0;

    // Rotate the model in the XY plane
    let angle1 = uMyUniforms.time;
    let c1 = cos(angle1);
    let s1 = sin(angle1);
    let R1 = transpose(mat3x3(
        c1, s1, 0.0,
        -s1, c1, 0.0,
        0.0, 0.0, 1.0,
    ));

    // Tilt the view point in the YZ plane
    let angle2 = 3.0 * PI / 4.0;
    let c2 = cos(angle2);
    let s2 = sin(angle2);
    let R2 = transpose(mat3x3(
        1.0, 0.0, 0.0,
        0.0, c2, s2,
        0.0, -s2, c2,
    ));

    let position = R2 * R1 * in.position;

    out.position = vec4<f32>(position.x, position.y * ratio, position.z * 0.5 + 0.5, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let color = in.color * uMyUniforms.color.rgb;
    return vec4f(color, 1.0);
}
