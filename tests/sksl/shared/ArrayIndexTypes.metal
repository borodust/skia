#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct Inputs {
};
struct Outputs {
    float4 sk_FragColor [[color(0)]];
};
fragment Outputs fragmentMain(Inputs _in [[stage_in]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _out;
    (void)_out;
    _out.sk_FragColor = float4(array<float, 4>{1.0, 2.0, 3.0, 4.0}[0], array<float, 4>{1.0, 2.0, 3.0, 4.0}[1u], array<float, 4>{1.0, 2.0, 3.0, 4.0}[2], array<float, 4>{1.0, 2.0, 3.0, 4.0}[3u]);
    return _out;
}
