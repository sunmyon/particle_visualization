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

out float vHsml;
out float val_show;

flat out int nodeFlag;
flat out int vType;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = pointSizes[int(aType)];

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
uniform sampler1D colormaps[6];
uniform int periodicMapping[6]; // 1: periodic display, 0: normal display

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

  //vec3 color = mix(lowColors[vType], highColors[vType], normVal);
  vec3 color;
  if(vType == 0)
    color = texture(colormaps[0], normVal).rgb;
  else if(vType == 1)
    color = texture(colormaps[1], normVal).rgb;
  else if(vType == 2)
    color = texture(colormaps[2], normVal).rgb;
  else if(vType == 3)
    color = texture(colormaps[3], normVal).rgb;
  else if(vType == 4)
    color = texture(colormaps[4], normVal).rgb;
  else if(vType == 5)
    color = texture(colormaps[5], normVal).rgb;

  //vec3 color = texture(colormaps[vType], normVal).rgb; //it doesn't work for Mesa

  // --- point sprite: circle mask ---
  vec2 uv = gl_PointCoord * 2.0 - 1.0;    // [-1,1]
  float r2 = dot(uv, uv);
  if (r2 > 1.0) discard;                 // round points

  // base alpha (soft edge)
  float edge = smoothstep(1.0, 0.90, r2); // outer 10% fade
  float alpha = edge;

  if (nodeFlag == 1) {
    vec3 stressColor = vec3(1.0, 0.9, 0.2);

  // ring mask: only near the edge
  float ring = smoothstep(0.70, 0.82, r2) * (1.0 - smoothstep(0.82, 0.98, r2));

  // optional: very subtle outer glow (still mostly edge)
  float glow = smoothstep(0.55, 0.90, r2) * (1.0 - smoothstep(0.90, 1.00, r2));
  glow *= 0.25; // Keep the glow subtle.

  // Keep center color; tint only where ring/glow is present
  float tint = clamp(ring * 1.0 + glow, 0.0, 1.0);
  color = mix(color, stressColor, tint);

  // Make ring brighter without recoloring center
  color += ring * stressColor * 1.5;

  // alpha: keep soft edge; stressed slightly less transparent near ring
  alpha = max(alpha, ring * 0.9);
  }

  //if (nodeFlag == 1) 
   // color = mix(color, vec3(1.0), 0.5);

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

// Fragment shader for the colorbar, sampling color from a 1D texture.
const char* colorbarFragmentShaderSource = R"(
in vec2 TexCoords;
uniform sampler1D colormap;
out vec4 FragColor;
void main()
{
    // Use the horizontal texture coordinate TexCoords.x to sample the colormap.
    FragColor = texture(colormap, TexCoords.x);
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
uniform float uScale;    // Axis length on screen, in NDC units.
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

#ifdef VOLUME_RENDERING
const char* fullscreenShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){ gl_Position = vec4(V[gl_VertexID],0,1); }
)";

const char* rtFragmentShaderSource = R"(
uniform int uDebugMode;

// ---- BVH TBO ----
uniform samplerBuffer nodeMinTB;    // vec4(bmin.xyz,sigma_avg)
uniform samplerBuffer nodeMaxTB;    // vec4(bmax.xyz,sigma_max)
uniform isamplerBuffer nodeChildTB; // ivec4(left,right,first,count)
uniform samplerBuffer particlesTB;  // vec4(pos.xyz, radius)
uniform samplerBuffer partSigmaTB;  // float sigma

// ---- Camera ----
uniform mat4 invProj;
uniform mat4 invView;    // Omit if unused.
uniform mat4 view;    // Omit if unused.
uniform vec3 uCamForward;
uniform float uFocalPx;  // Screen focal length in px: 0.5*H/tan(fovY/2).
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform int   uLodMode;       // 0:LeafOnly, 1:AutoLOD, 2:ForceNode
uniform float uPxThreshold;   // Example: 1.0 to 2.0 px.

// ---- Optics ----
uniform float uTauMax;        // Early-exit threshold, for example 1.0.
uniform float uStepBias;      // Small positive value for numerical stability.

out vec4 oColor;

// --------- Intersection helpers ---------
bool hitAABB(vec3 mn, vec3 mx, vec3 ro, vec3 invDir, inout float t0, inout float t1) {
    vec3 t1v = (mn - ro) * invDir;
    vec3 t2v = (mx - ro) * invDir;
    vec3 tminv = min(t1v, t2v);
    vec3 tmaxv = max(t1v, t2v);
    t0 = max(max(tminv.x, tminv.y), tminv.z);
    t1 = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
    return t1 >= max(t0, 0.0);
}


float hitSphere(vec3 c, float r, vec3 ro, vec3 rd, out float tIn, out float tOut){
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float c2 = dot(oc, oc) - r*r;
    float disc = b*b - c2;
    if (disc < 0.0) { tIn = tOut = -1.0; return -1.0; }
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;

    // Special handling when the ray starts inside.
    if (t0 < 0.0 && t1 > 0.0) {
        tIn = 0.0; // Start from the current position.
        tOut = t1;
        return t1;
    }

    tIn = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    tOut = t1;
    return (tIn > 0.0) ? tIn : -1.0;
}

// Approximate screen radius in pixels.
float screenRadiusPx(float r_eff, float z_view, float focal_px){
    return (z_view > 0.0) ? (focal_px * r_eff / z_view) : 1e9;
}

bool pointInsideAABB(vec3 p, vec3 mn, vec3 mx){
    return all(greaterThanEqual(p, mn)) && all(lessThanEqual(p, mx));
}

vec3 heat(float t){          // t in [0,1]
    t = clamp(t,0.0,1.0);
    float r = smoothstep(0.5,1.0,t);
    float g = t<0.5 ? smoothstep(0.0,0.5,t) : smoothstep(1.0,0.5,t);
    float b = smoothstep(1.0,0.5,t);
    return vec3(r,g,b);
}

void main(){
    // --- Ray in view space ---
    // Reconstruct NDC manually. Passing uv from the screen size is also fine.
    vec2 ndc = vec2( (gl_FragCoord.x * 2.0) / float(uResolution.x) - 1.0,
                     (gl_FragCoord.y * 2.0) / float(uResolution.y) - 1.0 ); // Adjust to the framework if needed.
    vec4 pN = invProj * vec4(ndc, -1.0, 1.0);
    pN /= pN.w;

    // Origin and direction in view space.
    vec3 ro_view = vec3(0.0);
    vec3 rd_view = normalize(vec3(pN));

    // Convert to world space.
    vec3 ro = (invView * vec4(ro_view, 1.0)).xyz;
    vec3 rd = normalize((invView * vec4(rd_view, 0.0)).xyz);
    vec3 invDir = 1.0 / max(abs(rd), vec3(1e-30)) * sign(rd);

    // --- Debug counters ---
    int leafHits = 0;        // Number of hit leaves or particles.
    int nodeApprox = 0;      // Number of integrations using node-sphere approximation.
    int aabbMiss = 0;        // Number of AABB misses.
    int visits = 0;
    const int MAX_VISITS = 1512;

    const float uSkipEps = 1.e-2;

    // --- Iterative BVH traversal ---
    struct StackItem { int id; float t0; float t1; vec3 center; float r; float sigma; float sigma_max;};
    const int STACK_MAX = 64;
    StackItem stack[STACK_MAX];
    int sp=0;
    float tau = 0.0;

    vec4 rootMin = texelFetch(nodeMinTB, uRoot);
    vec4 rootMax = texelFetch(nodeMaxTB, uRoot);
    float rt0=0.0, rt1=1e30;
    if (hitAABB(rootMin.xyz, rootMax.xyz, ro, invDir, rt0, rt1)) {
        vec3  cRoot   = 0.5 * (rootMin.xyz + rootMax.xyz);
        vec3  extRoot = rootMax.xyz - rootMin.xyz;
        float rRoot   = 0.5 * length(extRoot);                 // Radius of the sphere approximation.
        float sRoot  = rootMin.w;
        float sRootMax = rootMax.w;

        stack[sp++] = StackItem(uRoot, rt0, rt1, cRoot, rRoot, sRoot, sRootMax);
    }

    while(sp>0 && tau < uTauMax && visits < MAX_VISITS){
        visits++;

        StackItem it = stack[--sp];
        int id   = it.id;
        float t0 = it.t0;
        float t1 = it.t1;

        ivec4 ch    = texelFetch(nodeChildTB, id);
        bool isLeaf = (ch.x<0 && ch.y<0);

        if(isLeaf){
            // ---- leaf node ----
            int first = ch.z;
            int count = ch.w; // Currently expected to be 1, but larger counts are allowed.
            for(int k=0;k<count;k++){
                vec4 pr = texelFetch(particlesTB, first+k);
                float sigma = texelFetch(partSigmaTB, first+k).x;
                float tIn,tOut;
                if(hitSphere(pr.xyz, pr.w, ro, rd, tIn, tOut) > 0.0){
                    float seg0 = max(tIn, t0);
                    float seg1 = min(tOut, t1);
                    float L = max(0.0, seg1 - seg0);
                    tau += sigma * max(0.0, L - uStepBias);
                    leafHits++;
                    if(tau >= uTauMax) break;
                }
            }
        }else{
            // ---- LOD ----
            float zView = dot(it.center - ro, uCamForward); 
            bool useApprox = false;
            if(uLodMode==1){
                float r_px = screenRadiusPx(it.r, zView, uFocalPx);
                if(r_px < uPxThreshold) useApprox = true;
            }

            if(useApprox){
                // Approximate the node as a sphere and add it in one step.
                float tIn,tOut;
                if(hitSphere(it.center, it.r, ro, rd, tIn, tOut) > 0.0){
                    float L = max(0.0, tOut - tIn);
                    tau += it.sigma * max(0.0, L - uStepBias);
                    nodeApprox++;

                    if (tau >= uTauMax) break; 
                    continue;
                }
            }

            float possible = it.sigma_max * max(0.0, t1 - t0); // sigma_max is carried in StackItem.
            if (possible < uSkipEps) {
                continue;
            }

            int Li = ch.x, Ri = ch.y;
            bool hL = false; float t0L=0.0, t1L=1e30; vec3 cL=vec3(0.0); float rL=0.0, sL=0.0, sL_max=0.0;
            if (Li >= 0) {
                vec4 lmin = texelFetch(nodeMinTB, Li);
                vec4 lmax = texelFetch(nodeMaxTB, Li);
                hL = hitAABB(lmin.xyz, lmax.xyz, ro, invDir, t0L, t1L);
                if (hL) {
                    // Clip to the parent interval.
                    t0L = max(t0L, t0);
                    t1L = min(t1L, t1);
                    hL = (t1L >= t0L);
                    if (hL) {
                        cL = 0.5 * (lmin.xyz + lmax.xyz);
                        rL = 0.5 * length(lmax.xyz - lmin.xyz);
                        sL = lmin.w;
                        sL_max = lmax.w;
                    }
                }
            }

            // Right child
            bool hR = false; float t0R=0.0, t1R=1e30; vec3 cR=vec3(0.0); float rR=0.0, sR=0.0, sR_max=0.0;
            if (Ri >= 0) {
                vec4 rmin = texelFetch(nodeMinTB, Ri);
                vec4 rmax = texelFetch(nodeMaxTB, Ri);
                hR = hitAABB(rmin.xyz, rmax.xyz, ro, invDir, t0R, t1R);
                if (hR) {
                    t0R = max(t0R, t0);
                    t1R = min(t1R, t1);
                    hR = (t1R >= t0R);
                    if (hR) {
                        cR = 0.5 * (rmin.xyz + rmax.xyz);
                        rR = 0.5 * length(rmax.xyz - rmin.xyz);
                        sR = rmin.w;
                        sR_max = rmax.w;
                    }
                }
            }

            // Process the nearer child first by pushing the farther child first.
            if (hL && hR) {
                bool Lnear = (t0L <= t0R);
                // far
                if (sp < STACK_MAX) {
                    if (Lnear) stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
                    else       stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
                }
                // near
                if (sp < STACK_MAX) {
                    if (Lnear) stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
                    else       stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
                }
            } else if (hL) {
                if (sp < STACK_MAX) stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
            } else if (hR) {
                if (sp < STACK_MAX) stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
            }
        }
    }

    // ===== Debug visualization =====
    if(uDebugMode == 10){
        // Visit-count heat.
        float t = float(visits) / max(1.0, float(MAX_VISITS));
        //float t = float(visits) / 100.;
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 11){
        // Tau heat normalized by uMaxTauVis.
        float t = clamp(tau / max(uTauMax, 1e-6), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 12){
        // Leaf-hit count.
        float t = clamp(float(leafHits)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 13){
        // Approximation-use count.
        float t = clamp(float(nodeApprox)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 14){
        // AABB miss count.
        float t = clamp(float(aabbMiss)/float(visits+1), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }

    // Visualization: tone-map from tau as desired.
    float a = clamp(tau / uTauMax, 0.0, 1.0);
    oColor = vec4(1., 1., 1., 1 - exp(-tau));  // Example: 1 - exp(-tau).
}
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

out vec4 FragColor;

vec3 heat(float t){          // t in [0,1]
    t = clamp(t,0.0,1.0);
    float r = smoothstep(0.5,1.0,t);
    float g = t<0.5 ? smoothstep(0.0,0.5,t) : smoothstep(1.0,0.5,t);
    float b = smoothstep(1.0,0.5,t);
    return vec3(r,g,b);
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


    if (uDebugMode == 1) {
        FragColor = vec4(1,0,1,1);
        return;
    }

    if (uDebugMode == 2) {
        // Obvious test pattern: UV gradient plus checkerboard.
        float cx = step(0.5, fract(gl_FragCoord.x/16.0));
        float cy = step(0.5, fract(gl_FragCoord.y/16.0));
        float chk = mod(cx + cy, 2.0);
        vec2 uv01 = gl_FragCoord.xy / uResolution;
        vec3  col = mix(vec3(uv01, 0.5), vec3(1.0, 0.2, 0.8), chk);
        FragColor = vec4(col, 1.0);
        return;
    }

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
    const int MAX_VISITS = 1000;

    while(sp>0 && visits < MAX_VISITS){
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
        float radius = length(bmax - bmin);   // Simple diameter.
        vec3 center = 0.5 * (bmin + bmax);
        float zView = dot(center - ro, uCamForward);
        //float r_px = screenRadiusPx(radius, zView, uFocalPx);
        float r_px = 1000.;

        bool isLeaf = false;
        ivec4 cA = texelFetch(childATB, id);
        ivec4 cB = texelFetch(childBTB, id);
        isLeaf = (cA.x<0 && cA.y<0 && cA.z<0 && cA.w<0 &&
                  cB.x<0 && cB.y<0 && cB.z<0 && cB.w<0);

        if (isLeaf || r_px < 2.0*uPxThreshold) {
            float dt = max(0.0, t1 - t0);

            // Compute uvw at the interval midpoint and trilerp sigma.
            vec3 pmid = ro + rd * (0.5*(t0+t1));
            vec3 size = max(bmax - bmin, vec3(1e-8));
            vec3 uvw  = clamp((pmid - bmin) / size, 0.0, 1.0);
 
            vec4 sLo = texelFetch(cornerLoTB, id);
            vec4 sHi = texelFetch(cornerHiTB, id);
            float sigma = trilerp8(sLo, sHi, uvw);

            float a  = 1.0 - exp(-sigma * dt);
            //float a  = 1.0 - exp(-sigma_avg * dt);

            vec3 tfc = vec3(0.6,0.7,1.0); // Replace with the desired transfer function.
            color += (1.0 - alpha) * a * tfc;
            alpha  = 1.0 - (1.0 - alpha)*(1.0 - a);
            continue;
        }

        // Move to children with spatial skipping.
        int childIdx[8] = int[8](cA.x,cA.y,cA.z,cA.w,cB.x,cB.y,cB.z,cB.w);

        // Simple priority: for near-order traversal, two-step bubble push by entry t is also fine.
        for(int k=0;k<8;k++){
            int cid = childIdx[k];
            if (cid<0) continue;
            vec3 cmn = texelFetch(nodeMinTB, cid).xyz;
            vec3 cmx = texelFetch(nodeMaxTB, cid).xyz;
            float c0=t0, c1=t1;
            if(!rayBox(ro,invd, cmn, cmx, c0,c1)) continue;
            float cmax = texelFetch(nodeMaxTB, cid).w;
            if (cmax <= 0.0) continue; // Skip empty children.
            if (sp < STACK_MAX){ stack[sp]=cid; t0s[sp]=c0; t1s[sp]=c1; sp++; }
        }

    }

    if(uDebugMode == 10){
        // Visit-count heat.
        //float t = float(visits) / max(1.0, float(MAX_VISITS));
        float t = float(visits) / 100.;
        FragColor = vec4(heat(t), 1.0);
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

// --- Density to sigma conversion. Choose one path. ---
// 1) 1D LUT path, assuming density is normalized to 0..1.
uniform sampler1D uRho2Sigma;       // sigma in .r.

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

    // Density to sigma.
    float rho   = fs.density;
    //float sigma = texture(uRho2Sigma, clamp(rho, 0.0, 1.0)).r;
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
