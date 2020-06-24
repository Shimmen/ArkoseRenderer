#ifndef WATER_GLSL
#define WATER_GLSL

// Source: https://www.shadertoy.com/view/MdXyzX
// This version is slightly modified to make it work
// better in this context, but the core is the same.
// --------------------------------------------------

//afl_ext 2017-2019

#define DRAG_MULT 0.048
#define ITERATIONS_RAYMARCH 13
#define ITERATIONS_NORMAL 48

float _waterTime;

vec2 wavedx(vec2 position, vec2 direction, float speed, float frequency, float timeshift) {
    float x = dot(direction, position) * frequency + timeshift * speed;
    float wave = exp(sin(x) - 1.0);
    float dx = wave * cos(x);
    return vec2(wave, -dx);
}

float getwaves(vec2 position, int iterations){
	float iter = 0.0;
    float phase = 6.0;
    float speed = 2.0;
    float weight = 1.0;
    float w = 0.0;
    float ws = 0.0;
    for(int i=0;i<iterations;i++){
        vec2 p = vec2(sin(iter), cos(iter));
        vec2 res = wavedx(position, p, speed, phase, _waterTime);
        position += normalize(p) * res.y * weight * DRAG_MULT;
        w += res.x * weight;
        iter += 12.0;
        ws += weight;
        weight = mix(weight, 0.0, 0.2);
        phase *= 1.18;
        speed *= 1.07;
    }
    return w / ws;
}

float raymarchwater(vec3 camera, vec3 start, vec3 end, float depth){
    vec3 pos = start;
    float h = 0.0;
    float hupper = depth;
    float hlower = 0.0;
    vec2 zer = vec2(0.0);
    vec3 dir = normalize(end - start);
    for(int i=0;i<318;i++){
        h = getwaves(pos.xz * 0.1, ITERATIONS_RAYMARCH) * depth - depth;
        if(h + 0.01 > pos.y) {
            return distance(pos, camera);
        }
        pos += dir * (pos.y - h);
    }
    return -1.0;
}

vec3 normal(vec2 pos, float e, float depth){
    vec2 ex = vec2(e, 0);
    float H = getwaves(pos.xy * 0.1, ITERATIONS_NORMAL) * depth;
    vec3 a = vec3(pos.x, H, pos.y);
    return normalize(cross(normalize(a-vec3(pos.x - e, getwaves(pos.xy * 0.1 - ex.xy * 0.1, ITERATIONS_NORMAL) * depth, pos.y)), 
                           normalize(a-vec3(pos.x, getwaves(pos.xy * 0.1 + ex.yx * 0.1, ITERATIONS_NORMAL) * depth, pos.y + e))));
}

float intersectPlane(vec3 origin, vec3 direction, vec3 point, vec3 normal)
{ 
    return clamp(dot(point - origin, normal) / dot(direction, normal), -1.0, 9991999.0); 
}

vec3 extra_cheap_atmosphere(vec3 raydir, vec3 sundir){
    sundir.y = max(sundir.y, -0.07);
    float special_trick = 1.0 / (raydir.y * 1.0 + 0.1);
    float special_trick2 = 1.0 / (sundir.y * 11.0 + 1.0);
    float raysundt = pow(abs(dot(sundir, raydir)), 2.0);
    float sundt = pow(max(0.0, dot(sundir, raydir)), 8.0);
    float mymie = sundt * special_trick * 0.2;
    vec3 suncolor = mix(vec3(1.0), max(vec3(0.0), vec3(1.0) - vec3(5.5, 13.0, 22.4) / 22.4), special_trick2);
    vec3 bluesky= vec3(5.5, 13.0, 22.4) / 22.4 * suncolor;
    vec3 bluesky2 = max(vec3(0.0), bluesky - vec3(5.5, 13.0, 22.4) * 0.002 * (special_trick + -6.0 * sundir.y * sundir.y));
    bluesky2 *= special_trick * (0.24 + raysundt * 0.24);
    return bluesky2 * (1.0 + 1.0 * pow(1.0 - raydir.y, 3.0)) + mymie * suncolor;
}

vec3 getatm(vec3 ray) {
    return extra_cheap_atmosphere(ray, normalize(vec3(1.0))) * 0.5;
}

float sun(vec3 ray) {
    vec3 sd = normalize(vec3(1.0));   
    return pow(max(0.0, dot(ray, sd)), 528.0) * 110.0;
}

vec4 water(in vec3 ray, in float height, in float time) {
    _waterTime = time;
    ray = normalize(ray);

    float waterdepth = 2.1;
    vec3 wfloor = vec3(0.0, -waterdepth, 0.0);
    vec3 wceil = vec3(0.0, 0.0, 0.0);
    vec3 orig = vec3(0.0, height, 0.0);

    float hihit = intersectPlane(orig, ray, wceil, vec3(0.0, 1.0, 0.0));
    if(ray.y >= -0.01){
        vec3 C = getatm(ray) * 2.0 + sun(ray);
        return vec4(C, 1.0);
    }
    float lohit = intersectPlane(orig, ray, wfloor, vec3(0.0, 1.0, 0.0));
    vec3 hipos = orig + ray * hihit;
    vec3 lopos = orig + ray * lohit;
    float dist = raymarchwater(orig, hipos, lopos, waterdepth);
    vec3 pos = orig + ray * dist;

    vec3 N = normal(pos.xz, 0.001, waterdepth);
    vec2 velocity = N.xz * (1.0 - N.y);
    N = mix(vec3(0.0, 1.0, 0.0), N, 1.0 / (dist * dist * 0.01 + 1.0));
    vec3 R = reflect(ray, N);
    float fresnel = (0.04 + (1.0-0.04)*(pow(1.0 - max(0.0, dot(-N, ray)), 5.0)));

    vec3 C = fresnel * getatm(R) * 2.0 + fresnel * sun(R);
    return vec4(C, 1.0);
}


#endif // WATER_GLSL
