// @gips_version=1 @coord=pixel @filter=off

uniform float threshold = 1.0;  // @max=4
uniform float range = 1.0;      // @min=0.01 @max=4
uniform vec4  color = vec4(0.0, 0.0, 0.0, 1.0);  // @color
uniform float preview;          // @switch show only contours
uniform vec3  background = vec3(0.5, 0.5, 0.5);  // @color background

vec4 run(vec2 pos) {
    vec4 center = pixel(pos);
    vec3 p00 = pixel(pos + vec2(-1.0, -1.0)).rgb;
    vec3 p10 = pixel(pos + vec2( 0.0, -1.0)).rgb;
    vec3 p20 = pixel(pos + vec2( 1.0, -1.0)).rgb;
    vec3 p01 = pixel(pos + vec2(-1.0,  0.0)).rgb;
    vec3 p21 = pixel(pos + vec2( 1.0,  0.0)).rgb;
    vec3 p02 = pixel(pos + vec2(-1.0,  1.0)).rgb;
    vec3 p12 = pixel(pos + vec2( 0.0,  1.0)).rgb;
    vec3 p22 = pixel(pos + vec2( 1.0,  1.0)).rgb;
    vec3 Gv = p00 - p02 + 2.0 * (p10 - p12) + p20 - p22;
    vec3 Gh = p00 - p20 + 2.0 * (p01 - p21) + p02 - p22;
    vec3 G = sqrt(Gv*Gv + Gh*Gh);
    return vec4(mix((preview > 0.5) ? background : center.rgb, color.rgb, color.a *
        min(1.0, max(0.0, max(G.r, max(G.g, G.b)) - threshold) / range)
    ), center.a);
}