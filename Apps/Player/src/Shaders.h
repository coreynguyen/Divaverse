#pragma once
// Compiled at startup with D3DCompile (d3dcompiler_47.dll ships with Windows 10/11).
// Everything visual besides the video itself (shadow, edit frame, progress) is procedural.

struct ShaderCB {                 // must match cbuffer CB (16-byte packing)
    float rgbRect[4];
    float alphaRect[4];
    float texel[4];               // 1/w,1/h colour tex | 1/w,1/h alpha tex
    float viewPx[2]; float time; float mirror;
    float alphaLo, alphaHi, fullRange, hasAlpha;
    float shadow, groundY, editMode, progress;
    float opacity, paused, alphaFull, anchorU;
};
static_assert(sizeof(ShaderCB) == 112, "cbuffer layout");

static const char kShaderSrc[] = R"HLSL(
cbuffer CB : register(b0) {
    float4 rgbRect; float4 alphaRect; float4 texel;
    float2 viewPx; float time; float mirror;
    float alphaLo; float alphaHi; float fullRange; float hasAlpha;
    float shadow; float groundY; float editMode; float progress;
    float opacity; float paused; float alphaFull; float anchorU;
};
Texture2D<float>  tY  : register(t0);
Texture2D<float2> tUV : register(t1);
Texture2D<float>  tA  : register(t2);
SamplerState      sLin : register(s0);

struct VSO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSO VSMain(uint id : SV_VertexID) {
    VSO o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}

float2 RectUV(float4 r, float2 uv, float2 tx) {
    float2 t = lerp(r.xy, r.zw, uv);
    return clamp(t, r.xy + tx, r.zw - tx);          // never bleed across the packed boundary
}

float3 YuvToRgb(float y, float2 c) {
    if (fullRange < 0.5) { y = (y * 255.0 - 16.0) / 219.0; c = (c * 255.0 - 128.0) / 224.0; }
    else c -= 128.0 / 255.0;
    return saturate(float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x)); // BT.709
}

float Atan2(float y, float x) {   // portable (some HLSL front-ends lack atan2); |err| < 1e-5 rad
    float ax = abs(x), ay = abs(y);
    float a = min(ax, ay) / max(max(ax, ay), 1e-6), s = a * a;
    float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
    if (ay > ax) r = 1.57079637 - r;
    if (x < 0) r = 3.14159274 - r;
    return y < 0 ? -r : r;
}

float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r; return length(max(q, 0)) + min(max(q.x, q.y), 0) - r;
}

float3 Palette(float t) {   // brand ramp magenta -> violet -> cyan (mirrored, loops seamlessly)
    t = frac(t); t = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
    float3 a = float3(1.00, 0.27, 0.66), b = float3(0.62, 0.36, 1.00), c = float3(0.28, 0.86, 1.00);
    return t < 0.5 ? lerp(a, b, smoothstep(0.0, 1.0, t * 2.0)) : lerp(b, c, smoothstep(0.0, 1.0, t * 2.0 - 1.0));
}

float4 PSMain(VSO i) : SV_Target {
    float2 uv = i.uv;
    float2 vuv = float2(mirror > 0.5 ? 1.0 - uv.x : uv.x, uv.y);

    // ---- performer (premultiplied)
    float  Y  = tY.SampleLevel(sLin, RectUV(rgbRect, vuv, texel.xy), 0);
    float2 C  = tUV.SampleLevel(sLin, RectUV(rgbRect, vuv, texel.xy * 2.0), 0);   // chroma texel = 2 luma texels
    float3 rgb = YuvToRgb(Y, C);
    float a = 1.0;
    if (hasAlpha > 0.5) {
        float ay = tA.SampleLevel(sLin, RectUV(alphaRect, vuv, texel.zw), 0);
        a = alphaFull < 0.5 ? saturate((ay * 255.0 - 16.0) / 219.0) : ay;   // the alpha track declares its own range
        a = saturate((a - alphaLo) / max(alphaHi - alphaLo, 1e-3));
    }
    float4 col = float4(rgb * a, a);

    float2 p = uv * viewPx;

    // ---- procedural contact shadow (behind performer)
    if (shadow > 0.0) {
        float2 c = float2((mirror > 0.5 ? 1.0 - anchorU : anchorU) * viewPx.x, groundY * viewPx.y);
        float2 rr = float2(0.26 * viewPx.x, 0.011 * viewPx.y + 2.0);
        float2 q = (p - c) / rr;
        float s = shadow * 0.42 * exp(-2.2 * dot(q, q));
        col += float4(0, 0, 0, s) * (1.0 - col.a);
    }

    // ---- edit-mode frame: animated gradient border, corner brackets, progress pill
    if (editMode > 0.001) {
        float2 h = viewPx * 0.5;
        float2 pc = p - h;
        float d = sdRoundBox(pc, h - 2.0, 14.0);
        float ang = Atan2(pc.y, pc.x) / 6.28318 + time * 0.12;
        float3 hue = Palette(ang);
        float line_ = 1.0 - smoothstep(0.6, 1.8, abs(d + 1.5));
        float glow = exp(-max(-d, 0.0) / 9.0) * 0.35 * step(d, 0.0);
        float2 cq = abs(pc) - (h - 30.0);
        float corner = step(0.0, cq.x) * step(0.0, cq.y) * (1.0 - smoothstep(1.5, 3.5, abs(d + 3.0)));
        float veil = 0.10 * step(d, 0.0);
        // progress pill
        float2 bc = float2(h.x, viewPx.y - 14.0);
        float bw = viewPx.x * 0.5 - 24.0;
        float track = 1.0 - smoothstep(0.0, 1.0, sdRoundBox(p - bc, float2(bw, 2.0), 2.0));
        float fillX = (p.x - (bc.x - bw)) / (2.0 * bw);
        float fill = track * step(fillX, progress);
        // pause glyph
        float2 pg = p - float2(h.x, viewPx.y - 34.0);
        float bars = paused * (1.0 - smoothstep(0.0, 1.0, min(sdRoundBox(pg - float2(-5, 0), float2(2.5, 8), 1.0),
                                                            sdRoundBox(pg - float2( 5, 0), float2(2.5, 8), 1.0))));
        float3 oc = hue * (line_ + glow + corner + fill) + track * 0.25 + bars;   // veil adds dark tint (alpha only)
        float oa = saturate(line_ + glow * 0.8 + corner + veil + track * 0.35 + fill + bars);
        float4 ov = float4(saturate(oc) * oa, oa) * editMode;
        col = ov + col * (1.0 - ov.a);
    }
    return col * opacity;
}
)HLSL";
