#include "render/opengl/shader_sources.h"

const char* particleVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location=1) in uint aType;
layout (location=2) in uint aFlag;
layout (location = 3) in float aHsml;
layout (location = 4) in float aValShow;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float pointSizes[6];
uniform float pointScale;

out float vHsml;
out float val_show;

flat out int nodeFlag;
flat out int vType;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = max(pointSizes[int(aType)] * pointScale, 1.0);

    vHsml = aHsml;
    val_show = aValShow;

    vType = int(aType);
    nodeFlag = int(aFlag);
}
)";

const char* particleFragmentShaderSource = R"(
in float vHsml;
in float val_show;

flat in int vType;
flat in int nodeFlag;
out vec4 FragColor;

uniform float valueMin[6];
uniform float valueMax[6];
uniform int useLog[6]; // 0: linear, 1: log display

uniform vec3 lowColors[6];
uniform vec3 highColors[6];
uniform sampler2D colormaps[6];
uniform int periodicMapping[6]; // 1: periodic display, 0: normal display
uniform int colorMode; // 0: colormap, 1: fixed color overlay
uniform vec4 fixedColor;
uniform float globalAlpha;

void main()
{
  float varValue;

  varValue = val_show;

  float normVal = (varValue - valueMin[vType]) / (valueMax[vType] - valueMin[vType]);
  if (useLog[vType] == 1) 
    normVal = (log(varValue)/log(10.0) - valueMin[vType]) / (valueMax[vType] - valueMin[vType]);

  if (periodicMapping[vType] == 1) {
    normVal = fract(normVal);
  } else {
    normVal = clamp(normVal, 0.0, 1.0);
  }

  vec3 color;
  if (colorMode == 1) {
    color = fixedColor.rgb;
  } else if(vType == 0) {
    color = texture(colormaps[0], vec2(normVal, 0.5)).rgb;
  } else if(vType == 1) {
    color = texture(colormaps[1], vec2(normVal, 0.5)).rgb;
  } else if(vType == 2) {
    color = texture(colormaps[2], vec2(normVal, 0.5)).rgb;
  } else if(vType == 3) {
    color = texture(colormaps[3], vec2(normVal, 0.5)).rgb;
  } else if(vType == 4) {
    color = texture(colormaps[4], vec2(normVal, 0.5)).rgb;
  } else if(vType == 5) {
    color = texture(colormaps[5], vec2(normVal, 0.5)).rgb;
  }

  // --- point sprite: circle mask ---
  vec2 uv = gl_PointCoord * 2.0 - 1.0;    // [-1,1]
  float r2 = dot(uv, uv);
  if (r2 > 1.0) discard;                 // round points

  // base alpha (soft edge)
  float edge = smoothstep(1.0, 0.90, r2); // outer 10% fade
  float alpha = edge * ((colorMode == 1) ? fixedColor.a : globalAlpha);

  FragColor = vec4(color, alpha);
}
)";

const char* lineVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main()
{
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";

const char* lineFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec4 color;
void main()
{
    FragColor = color;
}
)";

// Global variables for the colorbar.

// Currently selected colormap texture, for example from the UI.
// Select from textures initialized by InitColorMaps(), such as jetTex or viridisTex.

// Vertex shader for the colorbar, drawn in screen-space NDC coordinates.
const char* colorbarVertexShaderSource = R"(
layout(location = 0) in vec2 aPos;       // Position in NDC coordinates.
layout(location = 1) in vec2 aTexCoord;    // Texture coordinates.
out vec2 TexCoords;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoords = aTexCoord;
}
)";

// Fragment shader for the colorbar, sampling a height-1 2D colormap texture.
const char* colorbarFragmentShaderSource = R"(
in vec2 TexCoords;
uniform sampler2D colormap;
out vec4 FragColor;
void main()
{
    // Use the horizontal texture coordinate TexCoords.x to sample the colormap.
    FragColor = texture(colormap, vec2(TexCoords.x, 0.5));
}
)";

// Define the contents of velocity_arrow.vert as a raw string literal.
const char* velocityArrowVertexShaderSource = R"(
// Arrow shape defined in model space, here a line from (0,0,0) to (0,0,1).
layout (location = 0) in vec3 aPos; 

// Instance attributes: particle position and velocity.
layout (location = 1) in vec3 instancePos; 
layout (location = 2) in vec3 instanceVel; 

uniform mat4 view;
uniform mat4 projection;
// Scale factor applied to velocity-vector magnitude.
uniform float scaleFactor;
// 1.0 uses log scale, 0.0 uses the raw value.
uniform float logScale;

out vec3 fragColor;

void main() {
    // Get velocity magnitude.
    float speed = length(instanceVel);
    // Use log scale when requested; +1 avoids log of zero.
    float arrowLength = (logScale > 0.5) ? log(speed + 1.0) : speed;
    arrowLength *= scaleFactor;

    // Initialize the rotation matrix.
    mat3 rotationMatrix = mat3(1.0);
    if (speed > 1e-6) {
        // Align the default direction (0,0,1) with the particle velocity direction.
        vec3 defaultDir = vec3(0.0, 0.0, 1.0);
        vec3 dir = normalize(instanceVel);
        float cosAngle = dot(defaultDir, dir);
        if (cosAngle < 0.9999) {
            vec3 axis = normalize(cross(defaultDir, dir));
            float angle = acos(cosAngle);
            float s = sin(angle);
            float c = cos(angle);
            float oc = 1.0 - c;
            rotationMatrix = mat3(
                oc * axis.x * axis.x + c,         oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
                oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,         oc * axis.y * axis.z - axis.x * s,
                oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
            );
        }
    }

    // Scale aPos by arrowLength and apply rotation.
    vec3 arrowPos = aPos * arrowLength;
    vec3 transformedArrowPos = rotationMatrix * arrowPos;
    
    // Final position is particle position plus the rotated, scaled arrow offset.
    vec3 pos = instancePos + transformedArrowPos;
    gl_Position = projection * view * vec4(pos, 1.0);
    
    // Use red by default.
    fragColor = vec3(1.0, 0.0, 0.0);
}
)";

// Matching fragment shader.
const char* velocityArrowFragmentShaderSource = R"(
in vec3 fragColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, 1.0);
}
)";


// simple_tex.vert
const char *colormap2DShaderSource = R"(

layout (location = 0) in vec2 inPos;       // Vertex position.
layout (location = 1) in vec2 inTexCoord;  // Texture coordinates.

out vec2 TexCoord;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0); // Pass through to clip space.
    TexCoord = inTexCoord;
}
)";

// simple_tex.frag
const char* colormap2DFragmentShaderSource = R"(

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    FragColor = texture(uTexture, TexCoord);
}
)";


#ifdef ISO_CONTOUR
const char* isocontourVertexShaderSource = R"(   
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = mat3(transpose(inverse(model))) * aNormal; // Model space to world space.
    gl_Position = projection * view * worldPos;
}
)";

const char* isocontourFragmentShaderSource = R"(   
in vec3 FragPos;
in vec3 Normal;

uniform vec3 lightPos;
uniform vec3 viewPos;      // Camera position.
uniform vec3 diffuseColor; // Example: vec3(1.0, 0.2, 0.2)
uniform vec3 ambientColor; // Example: vec3(0.1, 0.1, 0.1)
uniform float opacity;

out vec4 FragColor;

void main() {
    // Normal and light vector.
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - FragPos);
    // Diffuse term.
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * diffuseColor;
    // Ambient term.
    vec3 ambient = ambientColor * diffuseColor;
    vec3 result = ambient + diffuse;
    //FragColor = vec4(result, 0.2);
    FragColor = vec4(1.0,1.0,1.0, opacity);
}
)";
#endif

const char* instancedSolidVertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec3 aPos;

layout(location = 1) in vec4 iModel0;
layout(location = 2) in vec4 iModel1;
layout(location = 3) in vec4 iModel2;
layout(location = 4) in vec4 iModel3;

layout(location = 5) in vec3 iColor;
layout(location = 6) in float iOpacity;

uniform mat4 view;
uniform mat4 projection;

out vec3 vColor;
out float vOpacity;

void main()
{
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vColor = iColor;
    vOpacity = iOpacity;
}
)";

const char* instancedSolidFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
in float vOpacity;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, vOpacity);
}
)";

const char* coordShaderSource = R"(
layout(location=0) in vec3 aPos;  
layout(location=1) in vec3 aColor;

uniform mat3 uCamRot;    // Transposed inverse of the world-to-camera rotation; camera orientation.
uniform vec2  uScale;    // Axis length on screen, aspect-corrected NDC units.
uniform vec2  uOffset;   // Reference point at the lower-right of the screen, in NDC units.

out vec3 vColor;

void main(){
    // 1) Rotate world axes into camera orientation.
    vec3 dir = uCamRot * aPos;       // Example: aPos=(0,0,1) becomes Z direction as seen by the camera.

    // 2) Extract screen XY.
    vec2 sc = dir.xy * uScale + uOffset;

    // 3) Set screen coordinates directly in gl_Position.
    gl_Position = vec4(sc, 0.0, 1.0);

    vColor = aColor;
}
)";


const char* coordFragmentShaderSource = R"(
in  vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

const char* textureBlitVertexShaderSource = R"(
out vec2 vUV;
void main(){
    const vec2 pos[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    const vec2 uv[3]  = vec2[3](vec2(0,0),   vec2(2,0),  vec2(0,2));
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    vUV = uv[gl_VertexID];
}
)";

const char* textureBlitFragmentShaderSource = R"(
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uLow;
void main(){
    FragColor = texture(uLow, vUV);
}
)";

#ifdef VOLUME_RENDERING
const char* fullscreenShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){ gl_Position = vec4(V[gl_VertexID],0,1); }
)";

const char* upscaleVS = R"(
out vec2 vUV;
void main(){
    const vec2 pos[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    const vec2 uv[3]  = vec2[3](vec2(0,0),   vec2(2,0),  vec2(0,2));
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    vUV = uv[gl_VertexID];
}
)";

const char* upscaleFS = R"(
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uLow;
void main(){
    FragColor = texture(uLow, vUV);
}
)";

const char* octrayFragmentShaderSource = R"(
uniform samplerBuffer nodeMinTB;   // vec4(bmin.xyz, sigma_avg)
uniform samplerBuffer nodeMaxTB;   // vec4(bmax.xyz, sigma_max)
uniform isamplerBuffer childATB;   // ivec4(children 0..3)
uniform isamplerBuffer childBTB;   // ivec4(children 4..7)
uniform samplerBuffer cornerLoTB;  // vec4: sigma[0..3]
uniform samplerBuffer cornerHiTB;  // vec4: sigma[4..7]

uniform int uDebugMode;

// ---- Camera ----
uniform mat4 invProj;
uniform mat4 invView;    // Omit if unused.
uniform mat4 view;    // Omit if unused.
uniform vec3 uCamForward;
uniform float uFocalPx;  // Screen focal length in px: 0.5*H/tan(fovY/2).
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform float uPxThreshold;   // Example: 1.0 to 2.0 px.

// ---- Optics ----
uniform float uTauMax;        // Early-exit threshold, for example 1.0.
uniform float uStepBias;      // Small positive value for numerical stability.
uniform float uSkipEps;       // Skip cells whose maximum possible opacity is tiny.
uniform vec3  uVolumeColor;
uniform int   uColorMode;
uniform float uTfValueMin;
uniform float uTfValueMax;
uniform float uTfSigmaScale;
uniform float uTfMaxSigma;
uniform int   uTfLogScale;
uniform int   uTfComponentCount;
uniform int   uTfType[16];
uniform int   uTfLogDomain[16];
uniform float uTfCenter[16];
uniform float uTfWidth[16];
uniform float uTfAmp[16];

out vec4 FragColor;

vec3 heat(float t){          // t in [0,1]
    t = clamp(t,0.0,1.0);
    float r = smoothstep(0.5,1.0,t);
    float g = t<0.5 ? smoothstep(0.0,0.5,t) : smoothstep(1.0,0.5,t);
    float b = smoothstep(1.0,0.5,t);
    return vec3(r,g,b);
}

float gaussianComponent(float value, int i) {
    float width = max(uTfWidth[i], 1.0e-12);
    float x = 0.0;
    if (uTfLogDomain[i] != 0) {
        if (value <= 0.0 || uTfCenter[i] <= 0.0) {
            return 0.0;
        }
        x = (log(max(value, 1.0e-30)) / log(10.0) -
             log(max(uTfCenter[i], 1.0e-30)) / log(10.0)) / width;
    } else {
        x = (value - uTfCenter[i]) / width;
    }
    return uTfAmp[i] * exp(-0.5 * x * x);
}

float transferNorm(float value) {
    float lo = uTfValueMin;
    float hi = max(uTfValueMax, lo + 1.0e-6);
    float t = 0.0;
    if (uTfLogScale != 0) {
        if (value <= 0.0 || lo <= 0.0) {
            return 0.0;
        }
        float llo = log(max(lo, 1.0e-30)) / log(10.0);
        float lhi = log(max(hi, 1.0e-30)) / log(10.0);
        t = (log(max(value, 1.0e-30)) / log(10.0) - llo) /
            max(lhi - llo, 1.0e-6);
    } else {
        t = (value - lo) / max(hi - lo, 1.0e-6);
    }
    return clamp(t, 0.0, 1.0);
}

float transferSigma(float value) {
    float sigma = 0.0;
    int n = min(max(uTfComponentCount, 0), 16);
    for (int i = 0; i < n; ++i) {
        if (uTfType[i] == 0) {
            sigma += gaussianComponent(value, i);
        } else if (uTfType[i] == 1) {
            sigma += (abs(value - uTfCenter[i]) <= max(uTfWidth[i], 0.0))
                ? uTfAmp[i] : 0.0;
        } else {
            float dx = abs(value - uTfCenter[i]);
            float width = max(uTfWidth[i], 1.0e-12);
            sigma += (dx < width) ? uTfAmp[i] * (1.0 - dx / width) : 0.0;
        }
    }
    return max(uTfSigmaScale, 0.0) * max(sigma, 0.0);
}

vec3 volumeColor(float value, float sigma) {
    if (uColorMode == 1) {
        return heat(transferNorm(value));
    }
    return uVolumeColor;
}

float screenRadiusPx(float r_eff, float z_view, float focal_px){
    return (z_view > 0.0) ? (focal_px * r_eff / z_view) : 1e9;
}

bool rayBox(vec3 ro, vec3 invd, vec3 mn, vec3 mx, inout float t0, inout float t1) {
    vec3 t1v = (mn - ro) * invd;
    vec3 t2v = (mx - ro) * invd;
    vec3 tminv = min(t1v, t2v);
    vec3 tmaxv = max(t1v, t2v);
    float lo = max(max(tminv.x, tminv.y), tminv.z);
    float hi = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
    t0 = max(t0, lo);
    t1 = min(t1, hi);
    return (t1 >= max(t0, 0.0));
}

float trilerp8(vec4 lo, vec4 hi, vec3 uvw) {
    //  lo.x: idx0 = (0,0,0) = c000
    //  lo.y: idx1 = (1,0,0) = c100
    //  lo.z: idx2 = (1,1,0) = c110
    //  lo.w: idx3 = (0,1,0) = c010
    //  hi.x: idx4 = (0,0,1) = c001
    //  hi.y: idx5 = (1,0,1) = c101
    //  hi.z: idx6 = (1,1,1) = c111
    //  hi.w: idx7 = (0,1,1) = c011
    float c000 = lo.x;
    float c100 = lo.y;
    float c110 = lo.z;
    float c010 = lo.w;
    float c001 = hi.x;
    float c101 = hi.y;
    float c111 = hi.z;
    float c011 = hi.w;

    float ux = clamp(uvw.x, 0.0, 1.0);
    float uy = clamp(uvw.y, 0.0, 1.0);
    float uz = clamp(uvw.z, 0.0, 1.0);

    float c00 = mix(c000, c100, ux); // z=0, y=0
    float c10 = mix(c010, c110, ux); // z=0, y=1
    float c01 = mix(c001, c101, ux); // z=1, y=0
    float c11 = mix(c011, c111, ux); // z=1, y=1
    float c0  = mix(c00,  c10,  uy);
    float c1  = mix(c01,  c11,  uy);
    return mix(c0, c1, uz);
}

void main(){
    // Generate ray from view space to world space.
    vec2 ndc = vec2( (gl_FragCoord.x*2.0)/uResolution.x - 1.0,
                     (gl_FragCoord.y*2.0)/uResolution.y - 1.0 );

    vec4 pN  = invProj * vec4(ndc, -1.0, 1.0);
    pN      /= pN.w;
    vec3 ro  = (invView * vec4(0,0,0,1)).xyz;
    vec3 rd  = normalize((invView * vec4(vec3(pN),0)).xyz);
    vec3 invd= 1.0 / max(abs(rd), vec3(1e-30)) * sign(rd);

    // Intersect with the root.
    vec3 rootMin = texelFetch(nodeMinTB, uRoot).xyz;
    vec3 rootMax = texelFetch(nodeMaxTB, uRoot).xyz;
    float t0=0.0, t1=1e30;
    if(!rayBox(ro, invd, rootMin, rootMax, t0, t1)){
        FragColor=vec4(0);
        return;
    }

    // Manual stack. 410 can also work.
    const int STACK_MAX = 64;
    int   stack[STACK_MAX];
    float t0s[STACK_MAX];
    float t1s[STACK_MAX];
    int sp=0;

    stack[sp]=uRoot;
    t0s[sp]=t0;
    t1s[sp]=t1;
    sp++;

    float alpha=0.0; vec3 color=vec3(0.0);

    int visits = 0;
    int leafStops = 0;
    int lodStops = 0;
    int emptySkips = 0;
    const int MAX_VISITS = 1000;

    while(sp>0 && alpha < 0.995 && visits < MAX_VISITS){
        int   id = stack[--sp];
        t0 = t0s[sp]; t1 = t1s[sp];

        visits++;

        vec4 vmin = texelFetch(nodeMinTB, id);
        vec4 vmax = texelFetch(nodeMaxTB, id);
        vec3 bmin = vmin.xyz;
        vec3 bmax = vmax.xyz;
        float sigma_avg = vmin.w;
        float sigma_max = vmax.w;

        // Pixel LOD: approximate in one step when the cell is sufficiently smaller than a pixel.
        float radius = 0.5 * length(bmax - bmin);
        vec3 center = 0.5 * (bmin + bmax);
        float zView = dot(center - ro, uCamForward);
        float r_px = screenRadiusPx(radius, zView, uFocalPx);

        bool isLeaf = false;
        ivec4 cA = texelFetch(childATB, id);
        ivec4 cB = texelFetch(childBTB, id);
        isLeaf = (cA.x<0 && cA.y<0 && cA.z<0 && cA.w<0 &&
                  cB.x<0 && cB.y<0 && cB.z<0 && cB.w<0);

        if (uTfMaxSigma <= 0.0 || uTfMaxSigma * max(0.0, t1 - t0) < uSkipEps) {
            emptySkips++;
            continue;
        }

        bool useLod = (!isLeaf && r_px < 2.0*uPxThreshold);
        if (isLeaf || useLod) {
            if (isLeaf) leafStops++;
            else lodStops++;

            float dt = max(0.0, t1 - t0);

            // Compute uvw at the interval midpoint and trilerp sigma.
            vec3 pmid = ro + rd * (0.5*(t0+t1));
            vec3 size = max(bmax - bmin, vec3(1e-8));
            vec3 uvw  = clamp((pmid - bmin) / size, 0.0, 1.0);
 
            vec4 sLo = texelFetch(cornerLoTB, id);
            vec4 sHi = texelFetch(cornerHiTB, id);
            float value = trilerp8(sLo, sHi, uvw);
            float sigma = transferSigma(value);

            float a  = 1.0 - exp(-sigma * dt);
            //float a  = 1.0 - exp(-sigma_avg * dt);

            vec3 tfc = volumeColor(value, sigma);
            color += (1.0 - alpha) * a * tfc;
            alpha  = 1.0 - (1.0 - alpha)*(1.0 - a);
            continue;
        }

        // Move to children with spatial skipping.  Push far-to-near because
        // the explicit stack is LIFO, so traversal itself is near-to-far.
        int childIdx[8] = int[8](cA.x,cA.y,cA.z,cA.w,cB.x,cB.y,cB.z,cB.w);

        int hitId[8];
        float hitT0[8];
        float hitT1[8];
        int hitCount = 0;

        for(int k=0;k<8;k++){
            int cid = childIdx[k];
            if (cid<0) continue;
            vec3 cmn = texelFetch(nodeMinTB, cid).xyz;
            vec3 cmx = texelFetch(nodeMaxTB, cid).xyz;
            float c0=t0, c1=t1;
            if(!rayBox(ro,invd, cmn, cmx, c0,c1)) continue;
            if (uTfMaxSigma <= 0.0 || uTfMaxSigma * max(0.0, c1 - c0) < uSkipEps) {
                emptySkips++;
                continue;
            }
            hitId[hitCount] = cid;
            hitT0[hitCount] = c0;
            hitT1[hitCount] = c1;
            hitCount++;
        }

        for (int i = 1; i < hitCount; ++i) {
            int idv = hitId[i];
            float t0v = hitT0[i];
            float t1v = hitT1[i];
            int j = i - 1;
            while (j >= 0 && hitT0[j] > t0v) {
                hitId[j + 1] = hitId[j];
                hitT0[j + 1] = hitT0[j];
                hitT1[j + 1] = hitT1[j];
                j--;
            }
            hitId[j + 1] = idv;
            hitT0[j + 1] = t0v;
            hitT1[j + 1] = t1v;
        }

        for (int i = hitCount - 1; i >= 0; --i) {
            if (sp < STACK_MAX){
                stack[sp]=hitId[i];
                t0s[sp]=hitT0[i];
                t1s[sp]=hitT1[i];
                sp++;
            }
        }

    }

    if(uDebugMode == 10){
        // Visit-count heat.
        //float t = float(visits) / max(1.0, float(MAX_VISITS));
        float t = float(visits) / 100.;
        FragColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 11){
        float t = clamp(float(leafStops)/64.0, 0.0, 1.0);
        FragColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 12){
        float t = clamp(float(lodStops)/64.0, 0.0, 1.0);
        FragColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 13){
        float t = clamp(float(emptySkips)/64.0, 0.0, 1.0);
        FragColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 14){
        FragColor = vec4(vec3(alpha), 1.0);
        return;
    }

//    FragColor = vec4(1,0,1,1);
    FragColor = vec4(color, alpha);
}
)";


const char *wboitParticleShaderSource = R"(
layout (location=0) in vec3  aPos;      // Particle position in world space.
layout (location=3) in float aHsml;     // smoothing length [world]
layout (location=4) in float aDensity;  // Density.

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Pass the screen focal length from CPU: 0.5*H / tan(fovY/2), in pixels.
uniform float uFocalPx;
uniform int   uKernelMode;  // 0=Gaussian, 1=Poly6
uniform float uGaussNSigma; // 2.5
uniform float uEnlargeHsml; // 2.0

out VS_OUT {
    float hsmlWorld;
    float density;
    float zView;
    float spriteRadius;
} vs;

void main() {
    vec4 Pw = model * vec4(aPos, 1.0);
    vec4 Pv = view  * Pw;
    gl_Position = projection * Pv;

    vs.zView     = Pv.z;        // Reference value, available to FS if needed.
    vs.hsmlWorld = aHsml;
    vs.density   = aDensity;

    // In OpenGL right-handed convention, cameras usually look along -Z, so -Pv.z is a safe depth.
    float R = (uKernelMode == 0) ? (uGaussNSigma * aHsml)   // Gaussian: R = nσ h
                                 : (uEnlargeHsml * aHsml);  // Poly6:   R = h
    vs.spriteRadius = R;

    float depth   = max(1e-6, abs(Pv.z));
    float sizePx  = max(1.0, uFocalPx * (2.0 * R) / depth); // Diameter in pixels.
    gl_PointSize  = sizePx;
})";


const char *wboitParticleFragmentShaderSource = R"(
in VS_OUT {
    float hsmlWorld;
    float density;
    float zView;
    float spriteRadius;
} fs;

// Coloring. Replace with a transfer function if needed.
uniform vec3  uBaseColor = vec3(1.0);
uniform int   uKernelMode;     // 0=Gaussian, 1=Poly6
uniform float uGaussNSigma;    // Gaussian only.
uniform float uEnlargeHsml; 

// WBOIT outputs: two accumulation targets.
layout(location=0) out vec4  oAccum;   // rgb: color*alpha*weight, a: alpha*weight
layout(location=1) out float oReveal;  // Product of transmittance. This simple version stores it directly.

void main(){
    // Circular cutout within the point sprite.
    vec2  uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    float R = fs.spriteRadius;
    float r = sqrt(r2);
    float b = r * R;

    float rho   = fs.density;
    float sigma = 0.1;
    if(rho < 0.01)
        sigma = 0.;

    float tau;
    if (uKernelMode == 0) {
        // --- Gaussian: tau = sigma * sqrt(pi) * h * exp(-(b/h)^2) ---
        float q2 = (b*b) / max(1e-12, fs.hsmlWorld*fs.hsmlWorld);
        tau = sigma * sqrt(3.14159265) * fs.hsmlWorld * exp(-q2);
        
        // R is nSigma*h, so the sprite edge at rho=1 only draws down to exp(-(nSigma)^2).
        // Example: nSigma=2.5 gives exp(-6.25)=0.0019, which is sufficiently attenuated.
    }
    else {
        // --- Poly6 approximation for visual matching ---
        if (b < R) {
            float x = 1.0 - (b*b) / (R*R); // 0..1
            float kernel = x*x*x;                // (1 - (b/h)^2)^3
            float thickness = 2.0 * sqrt(max(0.0, R*R - b*b));
            tau = sigma * thickness * kernel;
        } else {
            tau = 0.0;
        }
    }

    float alpha = clamp(1.0 - exp(-tau), 0.0, 1.0);

    // WBOIT weight, simple version. Replace with McGuire's formula if needed.
    // Example: weight = clamp(alpha + 1e-2, 1e-3, 1.0);
    float weight = clamp(alpha + 1e-2, 1e-3, 1.0);

    vec3 color = uBaseColor; // A transfer function here can make the result easier to interpret.

    oAccum  = vec4(color * alpha * weight, alpha * weight);
    oReveal = alpha;   // Accumulated later, approximating final alpha = 1 - product(1-alpha_i).
}
)";

const char *wboitResolveShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){
gl_Position = vec4(V[gl_VertexID],0,1);
}
)";

const char *wboitResolveFragmentShaderSource = R"(
uniform sampler2D uAccumTex;   // RGBA accumulation.
uniform sampler2D uRevealTex;  // R channel, approximation of product(1-alpha).
out vec4 FragColor;

void main(){
    vec2  uv   = gl_FragCoord.xy / vec2(textureSize(uAccumTex, 0));
    vec4  acc  = texture(uAccumTex,  uv);
    float rev  = texture(uRevealTex, uv).r;

    // Remove weighting. acc.a stores the sum of weight*alpha.
    vec3  col  = (acc.a > 1e-6) ? (acc.rgb / acc.a) : vec3(0.0);
    float aFin = 1.0 - clamp(rev, 0.0, 1.0);

    FragColor = vec4(col, aFin);
}
)";
#endif
