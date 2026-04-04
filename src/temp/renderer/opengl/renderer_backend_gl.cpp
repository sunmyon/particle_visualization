#include "render_backend_gl.h"
#include <cstdio>

// ---- 最小シェーダ（必要なら差し替え可） ----
// ------------------------------
// シェーダーソースコード
// ------------------------------
const char* particleVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location=1) in uint aType;
layout (location=2) in uint aFlag;
layout (location = 3) in float aHsml;
#ifdef SAVE_GPU_MEMORY
layout (location = 4) in float aValShow;
#else
layout (location = 4) in float aDensity;      // 追加：密度
layout (location = 5) in float aTemperature;  // 追加：温度
layout (location = 6) in float aVal;
layout (location = 7) in float aVal2;
#endif

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float pointSizes[6];

out float vHsml;
#ifdef SAVE_GPU_MEMORY
out float val_show;
#else
out float temperature;
out float density;
out float val;
out float val2;
#endif

flat out int nodeFlag;
flat out int vType;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = pointSizes[int(aType)];

    vHsml = aHsml;
#ifdef SAVE_GPU_MEMORY
    val_show = aValShow;
#else
    val = aVal;
    val2 = aVal2;
    density = aDensity;
    temperature = aTemperature;
#endif

    vType = int(aType);
    nodeFlag = int(aFlag);
}
)";

const char* particleFragmentShaderSource = R"(
in float vHsml;
#ifdef SAVE_GPU_MEMORY
in float val_show;
#else
in float density;
in float temperature;
in float val;
in float val2;
#endif

flat in int vType;
flat in int nodeFlag;
out vec4 FragColor;

#ifndef SAVE_GPU_MEMORY
uniform int colorMode[6];
#endif

uniform float valueMin[6];
uniform float valueMax[6];
uniform int useLog[6]; // 0:通常, 1:対数表示

uniform vec3 lowColors[6];
uniform vec3 highColors[6];
uniform sampler1D colormaps[6];
uniform int periodicMapping[6]; // 1:周期的に表示、0:通常表示

void main()
{
  float varValue;

#ifdef SAVE_GPU_MEMORY
  varValue = val_show;
#else
  int colormode = colorMode[vType];
  if(colormode == 0)
    varValue = density;
  else if(colormode == 1)
    varValue = temperature;
  else if(colormode == 2)
    varValue = val;
  else if(colormode == 3)
    varValue = val2;
  else
    varValue = density; // デフォルトは val
#endif

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
  
  if (nodeFlag == 1) 
    color = mix(color, vec3(1.0), 0.5);

  FragColor = vec4(color, 1.);
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

// カラーバー用のグローバル変数
GLuint colorbarVAO, colorbarVBO, colorbarEBO;
GLuint colorbarProgram;

// 現在使用するカラーマップテクスチャ（たとえば UI で選択したもの）
// これは、InitColorMaps() 等で初期化しているもの（例：jetTex, viridisTex など）から選択します。
GLuint currentColorMapTex = 0;  // ※ 既に定義済みのグローバル変数とする


// カラーバー用頂点シェーダー（スクリーン空間の NDC 座標で描画）
const char* colorbarVertexShaderSource = R"(
layout(location = 0) in vec2 aPos;       // 位置（NDC座標）
layout(location = 1) in vec2 aTexCoord;    // テクスチャ座標
out vec2 TexCoords;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoords = aTexCoord;
}
)";

// カラーバー用フラグメントシェーダー（1D テクスチャから色をサンプリング）
const char* colorbarFragmentShaderSource = R"(
in vec2 TexCoords;
uniform sampler1D colormap;
out vec4 FragColor;
void main()
{
    // 横方向のテクスチャ座標（TexCoords.x）でカラーマップから色を取得
    FragColor = texture(colormap, TexCoords.x);
}
)";

// 例: velocity_arrow.vert の内容を生の文字列リテラルとして定義
const char* velocityArrowVertexShaderSource = R"(
// モデル空間で定義した矢印の形状（ここでは(0,0,0)→(0,0,1)の直線）
layout (location = 0) in vec3 aPos; 

// インスタンス属性：粒子の位置と速度
layout (location = 1) in vec3 instancePos; 
layout (location = 2) in vec3 instanceVel; 

uniform mat4 view;
uniform mat4 projection;
// 速度ベクトルの大きさにかけるスケール（任意の調整用）
uniform float scaleFactor;
// 1.0ならログスケールを使う、0.0ならそのまま
uniform float logScale;

out vec3 fragColor;

void main() {
    // 速度の大きさを取得
    float speed = length(instanceVel);
    // ログスケールを使う場合（+1でゼロ除算を防ぐ）
    float arrowLength = (logScale > 0.5) ? log(speed + 1.0) : speed;
    arrowLength *= scaleFactor;

    // 回転行列を初期化
    mat3 rotationMatrix = mat3(1.0);
    if (speed > 1e-6) {
        // デフォルトの方向 (0,0,1) を、粒子の速度方向に合わせる
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

    // aPos を arrowLength でスケールし、回転を適用する
    vec3 arrowPos = aPos * arrowLength;
    vec3 transformedArrowPos = rotationMatrix * arrowPos;
    
    // 最終的な位置は、粒子の位置に回転・スケーリングした矢印を足す
    vec3 pos = instancePos + transformedArrowPos;
    gl_Position = projection * view * vec4(pos, 1.0);
    
    // 色は赤（任意）
    fragColor = vec3(1.0, 0.0, 0.0);
}
)";

// 同様に、フラグメントシェーダーも
const char* velocityArrowFragmentShaderSource = R"(
in vec3 fragColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, 1.0);
}
)";



// simple_tex.vert
const char *colormap2DShaderSource = R"(

layout (location = 0) in vec2 inPos;       // 頂点の位置
layout (location = 1) in vec2 inTexCoord;  // テクスチャ座標

out vec2 TexCoord;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0); // そのままクリップ空間に
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
    Normal  = mat3(transpose(inverse(model))) * aNormal; // モデル空間→ワールド空間
    gl_Position = projection * view * worldPos;
}
)";

const char* isocontourFragmentShaderSource = R"(   
in vec3 FragPos;
in vec3 Normal;

uniform vec3 lightPos;
uniform vec3 viewPos;      // カメラ位置（視点）
uniform vec3 diffuseColor; // 例: vec3(1.0, 0.2, 0.2)
uniform vec3 ambientColor; // 例: vec3(0.1, 0.1, 0.1)
uniform float opacity;

out vec4 FragColor;

void main() {
    // ノーマルと光線ベクトル
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - FragPos);
    // ディフューズ項
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * diffuseColor;
    // アンビエント項
    vec3 ambient = ambientColor * diffuseColor;
    vec3 result = ambient + diffuse;
    //FragColor = vec4(result, 0.2);
    FragColor = vec4(1.0,1.0,1.0, opacity);
}
)";
#endif

#ifdef GEOMETRICAL_ANALYSIS
const char* ellipseVertexShaderSource = R"(
layout(location=0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* ellipseFragmentShaderSource = R"(
out vec4 FragColor;

uniform vec3 color;
uniform float opacity;

void main() {
    FragColor = vec4(1.,1.,1., opacity);
}
)";

const char* ellipsoidVertexShaderSource = R"(
layout(location=0) in vec3 aPos;
uniform mat4 model, view, projection;

void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
)";

const char* ellipsoidFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
uniform float opacity;

void main(){
    FragColor  = vec4(color, opacity);
}
)";

const char* diskVertexShaderSource = R"(
layout(location=0) in vec3 aPos;
uniform mat4 model, view, projection;
void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
)";

const char* diskFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3 color;
uniform float opacity;
void main(){ FragColor = vec4(color, opacity); }
)";
#endif

#ifdef STREAM_LINE
const char* streamlineVertexShaderSource = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main(){
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";


const char* streamlineFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
uniform float opacity;

void main()
{
    FragColor = vec4(color, opacity);
}
)";
#endif

const char* cubicShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in mat4 instanceModel;
layout(location = 5) in float instanceOpacity;

uniform mat4 view;
uniform mat4 projection;

out float vOpacity;

void main(){
    gl_Position = projection * view * instanceModel * vec4(aPos, 1.0);
    vOpacity = instanceOpacity;
}
)";


const char* cubicFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
in  float vOpacity;  

void main()
{
    FragColor = vec4(color, vOpacity);
}
)";


const char* coordShaderSource = R"(
layout(location=0) in vec3 aPos;  
layout(location=1) in vec3 aColor;

uniform mat3 uCamRot;    // カメラの「ワールド→カメラ」の逆回転行列の転置＝カメラ向き
uniform float uScale;    // スクリーン上での軸の長さ（ndc単位）
uniform vec2  uOffset;   // スクリーン右下の基準点（ndc単位）

out vec3 vColor;

void main(){
    // 1) ワールド軸をカメラ向きに回転
    vec3 dir = uCamRot * aPos;       // 例: aPos=(0,0,1) → カメラから見たZ方向

    // 2) スクリーンXYに取り出す
    vec2 sc = dir.xy * uScale + uOffset;

    // 3) gl_Position にスクリーン座標を直接セット
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

// ---- カメラ ----
uniform mat4 invProj;
uniform mat4 invView;    // 使わなければ省略
uniform mat4 view;    // 使わなければ省略
uniform vec3 uCamForward;
uniform float uFocalPx;  // 画面の焦点距離(px) = 0.5*H/tan(fovY/2)
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform int   uLodMode;       // 0:LeafOnly, 1:AutoLOD, 2:ForceNode
uniform float uPxThreshold;   // 例: 1.0〜2.0 px

// ---- 光学 ----
uniform float uTauMax;        // 途中終了の閾値 (例: 1.0)
uniform float uStepBias;      // 数値安定用の微小 >0

out vec4 oColor;

// --------- 交差系 ---------
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

    // 内側にいる場合の特別処理
    if (t0 < 0.0 && t1 > 0.0) {
        tIn = 0.0; // 今の位置からすぐ
        tOut = t1;
        return t1;
    }

    tIn = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    tOut = t1;
    return (tIn > 0.0) ? tIn : -1.0;
}

// 画面半径(px)の概算
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
    // --- レイを view 空間で ---
    // NDC を自前で復元（画面サイズから uv を渡してもOK）
    vec2 ndc = vec2( (gl_FragCoord.x * 2.0) / float(uResolution.x) - 1.0,
                     (gl_FragCoord.y * 2.0) / float(uResolution.y) - 1.0 ); // フレームワークに合わせて修正
    vec4 pN = invProj * vec4(ndc, -1.0, 1.0);
    pN /= pN.w;

    // ビュー空間の原点と方向
    vec3 ro_view = vec3(0.0);
    vec3 rd_view = normalize(vec3(pN));

    // ワールドへ変換
    vec3 ro = (invView * vec4(ro_view, 1.0)).xyz;
    vec3 rd = normalize((invView * vec4(rd_view, 0.0)).xyz);
    vec3 invDir = 1.0 / max(abs(rd), vec3(1e-30)) * sign(rd);

    // --- デバッグ用カウンタ ---
    int leafHits = 0;        // ヒットした葉（粒子）数
    int nodeApprox = 0;      // 近似（ノード球）で積分した回数
    int aabbMiss = 0;        // AABB ミス回数（ヒットしなかったノード）
    int visits = 0;
    const int MAX_VISITS = 1512;

    const float uSkipEps = 1.e-2;

    // --- 反復BVH ---
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
        float rRoot   = 0.5 * length(extRoot);                 // 球近似の半径
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
            int count = ch.w; // 今は 1 前提でもOK
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
                // ノードを「球」で近似して一括加算
                float tIn,tOut;
                if(hitSphere(it.center, it.r, ro, rd, tIn, tOut) > 0.0){
                    float L = max(0.0, tOut - tIn);
                    tau += it.sigma * max(0.0, L - uStepBias);
                    nodeApprox++;

                    if (tau >= uTauMax) break; 
                    continue;
                }
            }

            float possible = it.sigma_max * max(0.0, t1 - t0); // sigma_max は StackItem に追加
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
                    // 親区間でクリップ
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

            // 近い方から処理（遠い方を先に push）
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

    // ===== デバッグ可視化 =====
    if(uDebugMode == 10){
        // 訪問回数 heat
        float t = float(visits) / max(1.0, float(MAX_VISITS));
        //float t = float(visits) / 100.;
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 11){
        // τ heat（uMaxTauVisで正規化）
        float t = clamp(tau / max(uTauMax, 1e-6), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 12){
        // 葉ヒット回数
        float t = clamp(float(leafHits)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 13){
        // 近似使用回数
        float t = clamp(float(nodeApprox)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 14){
        // AABB miss 回数
        float t = clamp(float(aabbMiss)/float(visits+1), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }

    // 可視化：τからトーンマップ（お好みで）
    float a = clamp(tau / uTauMax, 0.0, 1.0);
    oColor = vec4(1., 1., 1., 1 - exp(-tau));  // 例：1-exp(-τ)
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

// ---- カメラ ----
uniform mat4 invProj;
uniform mat4 invView;    // 使わなければ省略
uniform mat4 view;    // 使わなければ省略
uniform vec3 uCamForward;
uniform float uFocalPx;  // 画面の焦点距離(px) = 0.5*H/tan(fovY/2)
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform float uPxThreshold;   // 例: 1.0〜2.0 px

// ---- 光学 ----
uniform float uTauMax;        // 途中終了の閾値 (例: 1.0)
uniform float uStepBias;      // 数値安定用の微小 >0

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
    // レイ生成（ビュー空間→ワールド）
    vec2 ndc = vec2( (gl_FragCoord.x*2.0)/uResolution.x - 1.0,
                     (gl_FragCoord.y*2.0)/uResolution.y - 1.0 );


    if (uDebugMode == 1) {
        FragColor = vec4(1,0,1,1);
        return;
    }

    if (uDebugMode == 2) {
        // 見逃しづらいテストパターン：UVグラデ＋チェッカ
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

    // ルートと交差
    vec3 rootMin = texelFetch(nodeMinTB, uRoot).xyz;
    vec3 rootMax = texelFetch(nodeMaxTB, uRoot).xyz;
    float t0=0.0, t1=1e30;
    if(!rayBox(ro, invd, rootMin, rootMax, t0, t1)){
        FragColor=vec4(0);
        return;
    }

    // 手動スタック（410でもOK）
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

        // 画素LOD：セルが画素より十分小さければ近似で一発
        float radius = length(bmax - bmin);   // 簡易直径
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

            // 区間の中心点で uvw を取って σ を trilerp
            vec3 pmid = ro + rd * (0.5*(t0+t1));
            vec3 size = max(bmax - bmin, vec3(1e-8));
            vec3 uvw  = clamp((pmid - bmin) / size, 0.0, 1.0);
 
            vec4 sLo = texelFetch(cornerLoTB, id);
            vec4 sHi = texelFetch(cornerHiTB, id);
            float sigma = trilerp8(sLo, sHi, uvw);

            float a  = 1.0 - exp(-sigma * dt);
            //float a  = 1.0 - exp(-sigma_avg * dt);

            vec3 tfc = vec3(0.6,0.7,1.0); // 好きなTFに
            color += (1.0 - alpha) * a * tfc;
            alpha  = 1.0 - (1.0 - alpha)*(1.0 - a);
            continue;
        }

        // 子へ。空間スキップ
        int childIdx[8] = int[8](cA.x,cA.y,cA.z,cA.w,cB.x,cB.y,cB.z,cB.w);

        // 簡単優先：近い順にしたいなら entry t でバブル2段pushでもOK
        for(int k=0;k<8;k++){
            int cid = childIdx[k];
            if (cid<0) continue;
            vec3 cmn = texelFetch(nodeMinTB, cid).xyz;
            vec3 cmx = texelFetch(nodeMaxTB, cid).xyz;
            float c0=t0, c1=t1;
            if(!rayBox(ro,invd, cmn, cmx, c0,c1)) continue;
            float cmax = texelFetch(nodeMaxTB, cid).w;
            if (cmax <= 0.0) continue; // 空ならスキップ
            if (sp < STACK_MAX){ stack[sp]=cid; t0s[sp]=c0; t1s[sp]=c1; sp++; }
        }

    }

    if(uDebugMode == 10){
        // 訪問回数 heat
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
layout (location=0) in vec3  aPos;      // 粒子位置（world）
layout (location=3) in float aHsml;     // smoothing length [world]
layout (location=4) in float aDensity;  // 密度

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// 画面の “焦点距離(px)” = 0.5*H / tan(fovY/2) を CPU 側で渡す
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

    vs.zView     = Pv.z;        // 参考：必要なら FS で使える
    vs.hsmlWorld = aHsml;
    vs.density   = aDensity;

    // OpenGL の右手系だとカメラは -Z を向く前提が多いので depth は -Pv.z を使うのが無難
    float R = (uKernelMode == 0) ? (uGaussNSigma * aHsml)   // Gaussian: R = nσ h
                                 : (uEnlargeHsml * aHsml);  // Poly6:   R = h
    vs.spriteRadius = R;

    float depth   = max(1e-6, abs(Pv.z));
    float sizePx  = max(1.0, uFocalPx * (2.0 * R) / depth); // 直径 px
    gl_PointSize  = sizePx;
})";


const char *wboitParticleFragmentShaderSource = R"(
in VS_OUT {
    float hsmlWorld;
    float density;
    float zView;
    float spriteRadius;
} fs;

// --- density → sigma 変換（どちらかを使う） ---
// 1) 1D LUT の場合（密度は 0..1 に正規化されている想定）
uniform sampler1D uRho2Sigma;       // .r に sigma

// 着色（必要なら TF を使って差し替え）
uniform vec3  uBaseColor = vec3(1.0);
uniform int   uKernelMode;     // 0=Gaussian, 1=Poly6
uniform float uGaussNSigma;    // Gaussian のみ
uniform float uEnlargeHsml; 

// WBOIT 出力（蓄積ターゲット 2 枚）
layout(location=0) out vec4  oAccum;   // rgb: color*alpha*weight, a: alpha*weight
layout(location=1) out float oReveal;  // 透過の積（*で蓄積するため log で加算にする実装もあるがここは単純に）

void main(){
    // point sprite 内の円形カットアウト
    vec2  uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    float R = fs.spriteRadius;
    float r = sqrt(r2);
    float b = r * R;

    // 密度→σ
    float rho   = fs.density;
    //float sigma = texture(uRho2Sigma, clamp(rho, 0.0, 1.0)).r;
    float sigma = 0.1;
    if(rho < 0.01)
        sigma = 0.;

    float tau;
    if (uKernelMode == 0) {
        // --- Gaussian： τ = σ * √π h * exp(-(b/h)^2) ---
        float q2 = (b*b) / max(1e-12, fs.hsmlWorld*fs.hsmlWorld);
        tau = sigma * sqrt(3.14159265) * fs.hsmlWorld * exp(-q2);
        
        // ※ R は nσ*h にしているので、スプライト端(ρ=1)で exp(-(nσ)^2) までしか描かない
        //   例: nσ=2.5 → exp(-6.25)=0.0019 で十分減衰
    }
    else {
        // --- Poly6 近似（見た目寄せ） ---
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

    // WBOIT の weight（シンプル版。必要に応じて McGuire の式に）
    // 例: weight = clamp(alpha + 1e-2, 1e-3, 1.0);
    float weight = clamp(alpha + 1e-2, 1e-3, 1.0);

    vec3 color = uBaseColor; // ここを TF にすると視覚的にわかりやすい

    oAccum  = vec4(color * alpha * weight, alpha * weight);
    oReveal = alpha;   // 後段で積算（最終 α = 1 - Π(1-α_i) の近似）
}
)";

const char *wboitResolveShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){
gl_Position = vec4(V[gl_VertexID],0,1);
}
)";

const char *wboitResolveFragmentShaderSource = R"(
uniform sampler2D uAccumTex;   // RGBA（蓄積）
uniform sampler2D uRevealTex;  // R   （1-α の積の近似）
out vec4 FragColor;

void main(){
    vec2  uv   = gl_FragCoord.xy / vec2(textureSize(uAccumTex, 0));
    vec4  acc  = texture(uAccumTex,  uv);
    float rev  = texture(uRevealTex, uv).r;

    // 逆重み取り（acc.a が weight*alpha の総和）
    vec3  col  = (acc.a > 1e-6) ? (acc.rgb / acc.a) : vec3(0.0);
    float aFin = 1.0 - clamp(rev, 0.0, 1.0);

    FragColor = vec4(col, aFin);
}
)";
#endif

// renderer/opengl/render_backend_gl.cpp
#include "render_backend_gl.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ----------------------------- 内部: シェーダユーティリティ -----------------------------
GLuint RenderBackendGL::compile_(GLenum type, const char* src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if(!ok){
		GLint len=0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
		std::string log(len, '\0');
		glGetShaderInfoLog(s, len, nullptr, log.data());
		std::fprintf(stderr, "[GL] shader compile error: %s\n", log.c_str());
		glDeleteShader(s);
		return 0;
	}
	return s;
}
GLuint RenderBackendGL::link_(GLuint vs, GLuint fs) {
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
	glDetachShader(p, vs);
	glDetachShader(p, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	if(!ok){
		GLint len=0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
		std::string log(len, '\0');
		glGetProgramInfoLog(p, len, nullptr, log.data());
		std::fprintf(stderr, "[GL] program link error: %s\n", log.c_str());
		glDeleteProgram(p);
		return 0;
	}
	return p;
}

GLuint RenderBackendGL::createProgramFromSources(const char* vs, const char* fs){
	GLuint v = compile_(GL_VERTEX_SHADER,   vs);
	GLuint f = compile_(GL_FRAGMENT_SHADER, fs);
	if(!v || !f) return 0;
	return link_(v,f);
}

GLuint RenderBackendGL::createProgramWithHeader(const char* vs, const char* fs, const char* header){
	// header を先頭に足してコンパイル
	std::string vsh = std::string(header?header:"") + vs;
	std::string fsh = std::string(header?header:"") + fs;
	GLuint v = compile_(GL_VERTEX_SHADER,   vsh.c_str());
	GLuint f = compile_(GL_FRAGMENT_SHADER, fsh.c_str());
	if(!v || !f) return 0;
	return link_(v,f);
}

// ----------------------------- ライフサイクル / フレーム -----------------------------
bool RenderBackendGL::init(GLFWwindow* window, bool vsync){
	window_ = window;
	if (glfwGetCurrentContext() != window_) glfwMakeContextCurrent(window_);
	
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::fprintf(stderr, "Failed to initialize GLAD\n");
		return false;
	}
	glfwSwapInterval(vsync ? 1 : 0);
	
	// GLデフォルトステート（あなたの main.cpp に合わせる）
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_PROGRAM_POINT_SIZE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	// フルスクリーン三角形 VAO だけ初期化しておくと便利
	createFullscreenTriangleVAO();
	
	return true;
}

void RenderBackendGL::shutdown(){
	// 各種 VAO/VBO/テクスチャ/FBO を片付け
	destroyParticleVAO();
	destroyCrossVAO();
	destroyVelocityArrowVAO();
	destroyColorBarQuad();
	destroyCoordAxesVAO();
	destroyStreamlineVAO();
	destroyEllipsoid();
	destroyFlatDisk();
	destroyUnitCubeVAO();
	destroyFullscreenTriangleVAO();
	destroyRtFbo();
	destroyWboitFbo();
	
	// プログラムは外部管理でもよいが、ここで持っているなら削除（保持していれば）
	// ここでは setProgram* で外部から渡される前提なので削除しない
}

void RenderBackendGL::beginFrame(int fbWidth, int fbHeight){
	glViewport(0, 0, fbWidth, fbHeight);
}

void RenderBackendGL::endFrame(){
	// ここで特になし
}

void RenderBackendGL::setClearColor(float r,float g,float b,float a){
	glClearColor(r,g,b,a);
}
void RenderBackendGL::clear(bool color, bool depth){
	GLbitfield bits = 0;
	if (color) bits |= GL_COLOR_BUFFER_BIT;
	if (depth) bits |= GL_DEPTH_BUFFER_BIT;
	if (bits) glClear(bits);
}

// ----------------------------- パーティクル ------------------------------------------
void RenderBackendGL::createParticleVAO(GLsizei strideBytes,
										GLint offPos, GLint offTypeU8, GLint offFlagU8,
										GLint offHsml, GLint offDensity, GLint offTemp,
										GLint offVal, GLint offVal2,
										bool saveGpuMemory){
	if (!vaoParticle_) glGenVertexArrays(1, &vaoParticle_);
	if (!vboParticle_) glGenBuffers(1, &vboParticle_);
	glBindVertexArray(vaoParticle_);
	glBindBuffer(GL_ARRAY_BUFFER, vboParticle_);
	
	// layout(location=0) pos3
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offPos);
	
	// layout(location=1) type (u8)
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, strideBytes, (void*)(size_t)offTypeU8);
	
	// layout(location=2) flag (u8)
	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, strideBytes, (void*)(size_t)offFlagU8);
	
	// layout(location=3) Hsml
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offHsml);
	
	if (saveGpuMemory) {
		// layout(location=4) val_show
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offVal);
	} else {
		// layout(location=4..7)
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offDensity);
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offTemp);
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offVal);
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, strideBytes, (void*)(size_t)offVal2);
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}
void RenderBackendGL::destroyParticleVAO(){
	if (vboParticle_) { glDeleteBuffers(1, &vboParticle_); vboParticle_=0; }
	if (vaoParticle_) { glDeleteVertexArrays(1, &vaoParticle_); vaoParticle_=0; }
}

void RenderBackendGL::updateParticles(const void* particleData, std::size_t count, GLsizei strideBytes){
	if (!vboParticle_) return;
	glBindBuffer(GL_ARRAY_BUFFER, vboParticle_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * (size_t)strideBytes), particleData, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RenderBackendGL::drawParticles(GLuint program, const glm::mat4& model,
									const glm::mat4& view, const glm::mat4& proj,
									const ParticleDrawParams& params,
									GLsizei drawCount){
	if (!vaoParticle_ || program==0 || params.hideAll) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(model));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	
	if (params.pointSizes6) glUniform1fv(glGetUniformLocation(program,"pointSizes"), 6, params.pointSizes6);
	if (params.valueMin6)   glUniform1fv(glGetUniformLocation(program,"valueMin"),   6, params.valueMin6);
	if (params.valueMax6)   glUniform1fv(glGetUniformLocation(program,"valueMax"),   6, params.valueMax6);
	if (params.useLog6)     glUniform1iv(glGetUniformLocation(program,"useLog"),     6, params.useLog6);
#ifndef SAVE_GPU_MEMORY
	if (params.colorMode6)  glUniform1iv(glGetUniformLocation(program,"colorMode"),  6, params.colorMode6);
#endif
	if (params.periodic6)   glUniform1iv(glGetUniformLocation(program,"periodicMapping"), 6, params.periodic6);
	
	// 1D colormap×6 を 0..5 にバインド
	for (int i=0;i<6;i++){
		glActiveTexture(GL_TEXTURE0+i);
		glBindTexture(GL_TEXTURE_1D, params.colormapTex1D[i]);
	}
	{
		int samplers[6] = {0,1,2,3,4,5};
		glUniform1iv(glGetUniformLocation(program,"colormaps"), 6, samplers);
	}
	
	glBindVertexArray(vaoParticle_);
	glDrawArrays(GL_POINTS, 0, drawCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- 交差マーカー ------------------------------------------
void RenderBackendGL::createCrossVAO(int numLines){
	if (!vaoCross_) glGenVertexArrays(1, &vaoCross_);
	if (!vboCross_) glGenBuffers(1, &vboCross_);
	glBindVertexArray(vaoCross_);
	glBindBuffer(GL_ARRAY_BUFFER, vboCross_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(2*numLines*3*sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 3*sizeof(float), (void*)0);
	glBindVertexArray(0);
}
void RenderBackendGL::destroyCrossVAO(){
	if (vboCross_) { glDeleteBuffers(1, &vboCross_); vboCross_=0; }
	if (vaoCross_) { glDeleteVertexArrays(1, &vaoCross_); vaoCross_=0; }
}
void RenderBackendGL::updateCrossVertices(const float* xyz, std::size_t countFloats){
	if (!vboCross_) return;
	glBindBuffer(GL_ARRAY_BUFFER, vboCross_);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(countFloats*sizeof(float)), xyz);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void RenderBackendGL::drawLinesSimple(GLuint program, const glm::mat4& view, const glm::mat4& proj,
									  const glm::vec4& color, GLsizei vertexCount){
	if (!vaoCross_ || program==0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform4fv(glGetUniformLocation(program,"color"),1,glm::value_ptr(color));
	glBindVertexArray(vaoCross_);
	glDrawArrays(GL_LINES, 0, vertexCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- 速度ベクトル（インスタンス） ---------------------------
void RenderBackendGL::createVelocityArrowVAO(){
	if (!vaoVelArrow_) glGenVertexArrays(1, &vaoVelArrow_);
	if (!vboVelArrow_) glGenBuffers(1, &vboVelArrow_);
	if (!vboVelInstance_) glGenBuffers(1, &vboVelInstance_);
	
	glBindVertexArray(vaoVelArrow_);
	
	// モデル線分 (0,0,0)-(0,0,1)
	const float arrowVertices[6] = {0,0,0,  0,0,1};
	glBindBuffer(GL_ARRAY_BUFFER, vboVelArrow_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(arrowVertices), arrowVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 3*sizeof(float), (void*)0);
	
	// インスタンス (pos3, vel3)
	glBindBuffer(GL_ARRAY_BUFFER, vboVelInstance_);
	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE, 6*sizeof(float), (void*)0);
	glVertexAttribDivisor(1,1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
	glVertexAttribDivisor(2,1);
	
	glBindVertexArray(0);
}
void RenderBackendGL::destroyVelocityArrowVAO(){
	if (vboVelInstance_) { glDeleteBuffers(1, &vboVelInstance_); vboVelInstance_=0; }
	if (vboVelArrow_)    { glDeleteBuffers(1, &vboVelArrow_);    vboVelArrow_=0; }
	if (vaoVelArrow_)    { glDeleteVertexArrays(1, &vaoVelArrow_); vaoVelArrow_=0; }
}
void RenderBackendGL::updateVelocityInstances(const float* posVel6Array, std::size_t countInstances){
	if (!vboVelInstance_) return;
	arrowInstanceCount_ = (GLsizei)countInstances;
	glBindBuffer(GL_ARRAY_BUFFER, vboVelInstance_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(countInstances*6*sizeof(float)), posVel6Array, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void RenderBackendGL::drawVelocityArrows(GLuint program, const glm::mat4& view, const glm::mat4& proj,
										 float scaleFactor, bool useLogScale){
	if (!vaoVelArrow_ || !arrowInstanceCount_ || program==0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform1f(glGetUniformLocation(program,"scaleFactor"), scaleFactor);
	glUniform1f(glGetUniformLocation(program,"logScale"),    useLogScale?1.0f:0.0f);
	
	glBindVertexArray(vaoVelArrow_);
	glDrawArraysInstanced(GL_LINES, 0, arrowModelVertexCount_, arrowInstanceCount_);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- 1D カラーマップ / カラーバー ---------------------------
GLuint RenderBackendGL::createColorMap1D(const float* rgbTriplets, int count){
	GLuint tex=0;
	glGenTextures(1,&tex);
	glBindTexture(GL_TEXTURE_1D, tex);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, count, 0, GL_RGB, GL_FLOAT, rgbTriplets);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_1D, 0);
	return tex;
}
void RenderBackendGL::destroyTexture1D(GLuint tex1D){
	if (tex1D) glDeleteTextures(1, &tex1D);
}

void RenderBackendGL::createColorBarQuad(){
	if (!vaoColorbar_) glGenVertexArrays(1,&vaoColorbar_);
	if (!vboColorbar_) glGenBuffers(1,&vboColorbar_);
	if (!eboColorbar_) glGenBuffers(1,&eboColorbar_);
	
	// 初期はダミー（のちに updateColorBarVertices で差し替え）
	const float dummy[16] = {
							 // x,y, u,v  ×4
							 0.7f,-0.95f, 0,0,
							 0.85f,-0.95f,1,0,
							 0.85f,-0.90f,1,1,
							 0.7f,-0.90f, 0,1
							 };
	const unsigned idx[6] = {0,1,2, 2,3,0};
	
	glBindVertexArray(vaoColorbar_);
	glBindBuffer(GL_ARRAY_BUFFER, vboColorbar_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(dummy), dummy, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE, 4*sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboColorbar_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
	
	glBindVertexArray(0);
}
void RenderBackendGL::destroyColorBarQuad(){
	if (eboColorbar_) { glDeleteBuffers(1,&eboColorbar_); eboColorbar_=0; }
	if (vboColorbar_) { glDeleteBuffers(1,&vboColorbar_); vboColorbar_=0; }
	if (vaoColorbar_) { glDeleteVertexArrays(1,&vaoColorbar_); vaoColorbar_=0; }
}
void RenderBackendGL::updateColorBarVertices(const float* xy_uv_4x4){
	if (!vboColorbar_) return;
	glBindBuffer(GL_ARRAY_BUFFER, vboColorbar_);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(16*sizeof(float)), xy_uv_4x4);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void RenderBackendGL::drawColorBar(GLuint program, GLuint colormapTex1D){
	if (!vaoColorbar_ || program==0) return;
	glDisable(GL_DEPTH_TEST);
	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_1D, colormapTex1D);
	glUniform1i(glGetUniformLocation(program,"colormap"), 0);
	
	glBindVertexArray(vaoColorbar_);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
	
	glUseProgram(0);
	glEnable(GL_DEPTH_TEST);
}

// ----------------------------- 右下座標軸 ---------------------------------------------
void RenderBackendGL::createCoordAxesVAO(){
	if (!vaoCoord_) glGenVertexArrays(1,&vaoCoord_);
	if (!vboCoord_) glGenBuffers(1,&vboCoord_);
	
	// 位置+色（6頂点の2属性）
	const float axesVertsColored[] = {
									  // pos xyz        color rgb
									  0,0,0,           1,0,0,
									  1,0,0,           1,0,0,
									  0,0,0,           0,1,0,
									  0,1,0,           0,1,0,
									  0,0,0,           0,0,1,
									  0,0,1,           0,0,1
									  };
	glBindVertexArray(vaoCoord_);
	glBindBuffer(GL_ARRAY_BUFFER, vboCoord_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(axesVertsColored), axesVertsColored, GL_STATIC_DRAW);
	
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 6*sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
	
	glBindVertexArray(0);
}
void RenderBackendGL::destroyCoordAxesVAO(){
	if (vboCoord_) { glDeleteBuffers(1,&vboCoord_); vboCoord_=0; }
	if (vaoCoord_) { glDeleteVertexArrays(1,&vaoCoord_); vaoCoord_=0; }
}
void RenderBackendGL::drawCoordAxes(GLuint program, const glm::mat4& modelAxes,
									const glm::mat4& view, const glm::mat4& projOrtho){
	if (!vaoCoord_ || program==0) return;
	glDisable(GL_DEPTH_TEST); // オーバーレイ前提
	glDepthMask(GL_FALSE);
	
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(modelAxes));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(projOrtho));
	
	glBindVertexArray(vaoCoord_);
	glLineWidth(50.0f); // あなたのコード準拠
	glDrawArrays(GL_LINES, 0, 6);
	glBindVertexArray(0);
	
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glUseProgram(0);
}

// ----------------------------- Streamline --------------------------------------------
void RenderBackendGL::createStreamlineVAO(){
	if (!vaoStream_) glGenVertexArrays(1,&vaoStream_);
	if (!vboStream_) glGenBuffers(1,&vboStream_);
	glBindVertexArray(vaoStream_);
	glBindBuffer(GL_ARRAY_BUFFER, vboStream_);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 0, (void*)0);
	glBindVertexArray(0);
}
void RenderBackendGL::destroyStreamlineVAO(){
	if (vboStream_) { glDeleteBuffers(1,&vboStream_); vboStream_=0; }
	if (vaoStream_) { glDeleteVertexArrays(1,&vaoStream_); vaoStream_=0; }
}
void RenderBackendGL::uploadStreamlineVertices(const float* verts, std::size_t countFloats){
	if (!vboStream_) return;
	glBindBuffer(GL_ARRAY_BUFFER, vboStream_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(countFloats*sizeof(float)), verts, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void RenderBackendGL::drawStreamlines(GLuint program,
									  const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj,
									  const GLint* firsts, const GLsizei* counts, GLsizei drawCount,
									  const glm::vec3& color, float opacity){
	if (!vaoStream_ || program==0 || drawCount<=0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(model));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform3fv(glGetUniformLocation(program,"color"),1,glm::value_ptr(color));
	glUniform1f(glGetUniformLocation(program,"opacity"), opacity);
	
	glBindVertexArray(vaoStream_);
	glMultiDrawArrays(GL_LINE_STRIP, firsts, counts, drawCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- Ellipsoid / Disk / Isocontour --------------------------
void RenderBackendGL::createEllipsoidWire(const glm::vec3* verts, std::size_t count){
	ellipsoidWire_ = true;
	if (!vaoEllipsoid_) glGenVertexArrays(1,&vaoEllipsoid_);
	if (!vboEllipsoid_) glGenBuffers(1,&vboEllipsoid_);
	
	glBindVertexArray(vaoEllipsoid_);
	glBindBuffer(GL_ARRAY_BUFFER, vboEllipsoid_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count*sizeof(glm::vec3)), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, sizeof(glm::vec3), (void*)0);
	glBindVertexArray(0);
	
	ellipsoidIndexCount_ = (GLsizei)count;
}
void RenderBackendGL::createEllipsoidUvSphere(const void* interleavedPosNrm, std::size_t vtxBytes,
											  const uint32_t* indices, std::size_t idxCount){
	ellipsoidWire_ = false;
	if (!vaoEllipsoid_) glGenVertexArrays(1,&vaoEllipsoid_);
	if (!vboEllipsoid_) glGenBuffers(1,&vboEllipsoid_);
	if (!iboEllipsoid_) glGenBuffers(1,&iboEllipsoid_);
	// nboEllipsoid_ は不要（pos&nrm インターリーブの想定）
	
	glBindVertexArray(vaoEllipsoid_);
	glBindBuffer(GL_ARRAY_BUFFER, vboEllipsoid_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vtxBytes, interleavedPosNrm, GL_STATIC_DRAW);
	
	// layout 0: pos, layout 1: nrm（構造体 {vec3 pos; vec3 nrm;} と仮定）
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, sizeof(float)*6, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE, sizeof(float)*6, (void*)(sizeof(float)*3));
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboEllipsoid_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxCount*sizeof(uint32_t)), indices, GL_STATIC_DRAW);
	
	glBindVertexArray(0);
	
	ellipsoidIndexCount_ = (GLsizei)idxCount;
}
void RenderBackendGL::destroyEllipsoid(){
	if (iboEllipsoid_) { glDeleteBuffers(1,&iboEllipsoid_); iboEllipsoid_=0; }
	if (vboEllipsoid_) { glDeleteBuffers(1,&vboEllipsoid_); vboEllipsoid_=0; }
	if (nboEllipsoid_) { glDeleteBuffers(1,&nboEllipsoid_); nboEllipsoid_=0; }
	if (vaoEllipsoid_) { glDeleteVertexArrays(1,&vaoEllipsoid_); vaoEllipsoid_=0; }
	ellipsoidIndexCount_=0; ellipsoidWire_=false;
}
void RenderBackendGL::drawEllipsoidWire(GLuint program, const glm::mat4& model,
										const glm::mat4& view, const glm::mat4& proj,
										float opacity){
	if (!vaoEllipsoid_ || program==0 || !ellipsoidWire_) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(model));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform1f(glGetUniformLocation(program,"opacity"), opacity);
	
	glBindVertexArray(vaoEllipsoid_);
	// あなたのコードでは3つのリングを個別 draw だったが、ここでは連続配列前提
	glDrawArrays(GL_LINE_LOOP, 0, ellipsoidIndexCount_);
	glBindVertexArray(0);
	glUseProgram(0);
}
void RenderBackendGL::drawEllipsoidSolid(GLuint program, const glm::mat4& model,
										 const glm::mat4& view, const glm::mat4& proj,
										 const glm::vec3& color, float opacity){
	if (!vaoEllipsoid_ || program==0 || ellipsoidWire_) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(model));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform3fv(glGetUniformLocation(program,"color"),1,glm::value_ptr(color));
	glUniform1f(glGetUniformLocation(program,"opacity"), opacity);
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	
	glBindVertexArray(vaoEllipsoid_);
	glDrawElements(GL_TRIANGLES, ellipsoidIndexCount_, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
	
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glUseProgram(0);
}

void RenderBackendGL::createFlatDisk(const glm::vec3* verts, std::size_t vcount,
									 const uint32_t* inds, std::size_t icount){
	if (!vaoDisk_) glGenVertexArrays(1,&vaoDisk_);
	if (!vboDisk_) glGenBuffers(1,&vboDisk_);
	if (!eboDisk_) glGenBuffers(1,&eboDisk_);
	
	glBindVertexArray(vaoDisk_);
	glBindBuffer(GL_ARRAY_BUFFER, vboDisk_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcount*sizeof(glm::vec3)), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboDisk_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(icount*sizeof(uint32_t)), inds, GL_STATIC_DRAW);
	
	glBindVertexArray(0);
	diskIndexCount_ = (GLsizei)icount;
}
void RenderBackendGL::destroyFlatDisk(){
	if (eboDisk_) { glDeleteBuffers(1,&eboDisk_); eboDisk_=0; }
	if (vboDisk_) { glDeleteBuffers(1,&vboDisk_); vboDisk_=0; }
	if (vaoDisk_) { glDeleteVertexArrays(1,&vaoDisk_); vaoDisk_=0; }
	diskIndexCount_=0;
}
void RenderBackendGL::drawFlatDisk(GLuint program, const glm::mat4& model,
								   const glm::mat4& view, const glm::mat4& proj,
								   const glm::vec3& color, float opacity){
	if (!vaoDisk_ || program==0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(model));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"), 1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform3fv(glGetUniformLocation(program,"color"),1,glm::value_ptr(color));
	glUniform1f(glGetUniformLocation(program,"opacity"), opacity);
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	
	glBindVertexArray(vaoDisk_);
	glDrawElements(GL_TRIANGLES, diskIndexCount_, GL_UNSIGNED_INT, 0);
	
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
	glUseProgram(0);
}

// Isocontour（頂点＋法線を内部生成）
void RenderBackendGL::uploadMeshWithNormals(const float* verts, std::size_t vcount3,
											const unsigned* inds, std::size_t icount){
	// 法線は CPU で計算済みを渡す設計もありうるが、
	// ここでは「既に verts は pos3、法線は別で計算済み」と仮定せず、最小実装に留める：
	// → posのみアップロード。必要なら拡張。
	if (!vaoEllipsoid_) glGenVertexArrays(1,&vaoEllipsoid_);
	if (!vboEllipsoid_) glGenBuffers(1,&vboEllipsoid_);
	if (!nboEllipsoid_) glGenBuffers(1,&nboEllipsoid_);
	if (!iboEllipsoid_) glGenBuffers(1,&iboEllipsoid_);
	
	// ここでは pos/nrm を同サイズで受け取っていないため、頂点だけアップし、
	// シェーダ側でフラットシェーディング等にするのが簡単。
	glBindVertexArray(vaoEllipsoid_);
	glBindBuffer(GL_ARRAY_BUFFER, vboEllipsoid_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcount3*sizeof(float)), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 0, (void*)0);
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboEllipsoid_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(icount*sizeof(unsigned)), inds, GL_STATIC_DRAW);
	
	glBindVertexArray(0);
	ellipsoidIndexCount_ = (GLsizei)icount;
}
void RenderBackendGL::destroyIsocontour(){
	// ellipsoidのリソースと共有しているため、ここでは何もしない or 明示的に別管理にする
}

// ----------------------------- 立方体インスタンシング ---------------------------------
void RenderBackendGL::createUnitCubeVAO(const float* cubeVerts, std::size_t cubeVertsBytes,
										const unsigned* cubeIdx,  std::size_t cubeIdxBytes){
	if (!vaoCube_) glGenVertexArrays(1,&vaoCube_);
	if (!vboCube_) glGenBuffers(1,&vboCube_);
	if (!eboCube_) glGenBuffers(1,&eboCube_);
	if (!vboCubeInst_) glGenBuffers(1,&vboCubeInst_);
	if (!vboCubeOpacity_) glGenBuffers(1,&vboCubeOpacity_);
	
	glBindVertexArray(vaoCube_);
	glBindBuffer(GL_ARRAY_BUFFER, vboCube_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cubeVertsBytes, cubeVerts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, 3*sizeof(float), (void*)0);
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboCube_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cubeIdxBytes, cubeIdx, GL_STATIC_DRAW);
	cubeIndexCount_ = (GLsizei)(cubeIdxBytes/sizeof(unsigned));
	
	// インスタンス mat4 (locations 1..4)
	glBindBuffer(GL_ARRAY_BUFFER, vboCubeInst_);
	GLsizei vec4 = sizeof(float)*4;
	for (GLuint i=0;i<4;i++){
		glEnableVertexAttribArray(1+i);
		glVertexAttribPointer(1+i, 4, GL_FLOAT, GL_FALSE, sizeof(float)*16, (void*)(size_t)(i*vec4));
		glVertexAttribDivisor(1+i, 1);
	}
	// opacity (location 5)
	glBindBuffer(GL_ARRAY_BUFFER, vboCubeOpacity_);
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE, sizeof(float), (void*)0);
	glVertexAttribDivisor(5,1);
	
	glBindVertexArray(0);
}

void RenderBackendGL::destroyUnitCubeVAO(){
	if (vboCubeOpacity_) { glDeleteBuffers(1,&vboCubeOpacity_); vboCubeOpacity_=0; }
	if (vboCubeInst_)    { glDeleteBuffers(1,&vboCubeInst_);    vboCubeInst_=0; }
	if (eboCube_)        { glDeleteBuffers(1,&eboCube_);        eboCube_=0; }
	if (vboCube_)        { glDeleteBuffers(1,&vboCube_);        vboCube_=0; }
	if (vaoCube_)        { glDeleteVertexArrays(1,&vaoCube_);   vaoCube_=0; }
	cubeIndexCount_=36; cubeInstanceCount_=0;
}

void RenderBackendGL::updateCubeInstances(const glm::mat4* models, std::size_t count,
										  const float* opacities){
	cubeInstanceCount_ = (GLsizei)count;
	
	glBindBuffer(GL_ARRAY_BUFFER, vboCubeInst_);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count*sizeof(glm::mat4)), models, GL_DYNAMIC_DRAW);
	
	if (opacities){
		glBindBuffer(GL_ARRAY_BUFFER, vboCubeOpacity_);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count*sizeof(float)), opacities, GL_DYNAMIC_DRAW);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RenderBackendGL::drawCubesInstanced(GLuint program, const glm::mat4& view, const glm::mat4& proj,
										 GLsizei indexCount, GLsizei instanceCount){
	if (!vaoCube_ || program==0 || instanceCount<=0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	
	glBindVertexArray(vaoCube_);
	glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- 一時ライン ---------------------------------------------
GLuint RenderBackendGL::createDynamicLineVAO(){
	GLuint vao=0,vbo=0;
	glGenVertexArrays(1,&vao);
	glGenBuffers(1,&vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, sizeof(glm::vec3), (void*)0);
	glBindVertexArray(0);
	return vao; // vbo は呼び出し側で保持したいなら別で返す設計でもOK
}
void RenderBackendGL::destroyDynamicLineVAO(GLuint vao, GLuint vbo){
	if (vbo) glDeleteBuffers(1,&vbo);
	if (vao) glDeleteVertexArrays(1,&vao);
}
void RenderBackendGL::updateDynamicLineVertices(GLuint vbo, const glm::vec3* verts, std::size_t count){
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count*sizeof(glm::vec3)), verts, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void RenderBackendGL::drawDynamicLines(GLuint program, GLuint vao, GLsizei vertexCount,
									   const glm::mat4& view, const glm::mat4& proj,
									   const glm::vec4& color){
	if (!vao || program==0) return;
	glUseProgram(program);
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,glm::value_ptr(proj));
	glUniform4fv(glGetUniformLocation(program,"color"),1,glm::value_ptr(color));
	glBindVertexArray(vao);
	glDrawArrays(GL_LINES, 0, vertexCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- Fullscreen Triangle -----------------------------------
void RenderBackendGL::createFullscreenTriangleVAO(){
	if (!vaoFsTri_) glGenVertexArrays(1, &vaoFsTri_);
	// 頂点バッファ不要：VSで頂点IDから生成する想定でもよいし、
	// 必要なら小さなVBOを作ってもOK。ここではVAOのみ。
}
void RenderBackendGL::destroyFullscreenTriangleVAO(){
	if (vaoFsTri_) { glDeleteVertexArrays(1, &vaoFsTri_); vaoFsTri_=0; }
}
void RenderBackendGL::drawFullscreenTriangle(GLuint program){
	if (!vaoFsTri_ || program==0) return;
	glUseProgram(program);
	glBindVertexArray(vaoFsTri_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------- 2D テクスチャ（ImGui など） ---------------------------
IRenderBackend::Texture RenderBackendGL::createTexture2D(int w, int h){
	GLuint t=0;
	glGenTextures(1,&t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w,h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	
	Texture handle = nextTex_++;
	tex2D_[handle] = t;
	return handle;
}
void RenderBackendGL::updateTexture2D(Texture t, int w, int h, const unsigned char* rgb){
	auto it = tex2D_.find(t);
	if (it==tex2D_.end()) return;
	GLuint tex = it->second;
	glBindTexture(GL_TEXTURE_2D, tex);
	GLint prev; glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	// サイズ変更対応
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w,h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
	glPixelStorei(GL_UNPACK_ALIGNMENT, prev);
	glBindTexture(GL_TEXTURE_2D, 0);
}
void RenderBackendGL::drawTexture2D(Texture /*t*/, int /*w*/, int /*h*/, int /*dstX*/, int /*dstY*/){
	// 今回は ImGui::Image を使う想定なので空実装でOK
}
void RenderBackendGL::destroyTexture(Texture t){
	auto it = tex2D_.find(t);
	if (it!=tex2D_.end()){
		if (it->second) glDeleteTextures(1,&it->second);
		tex2D_.erase(it);
	}
}

// ImGui のプロジェクション画像アップロード
GLuint RenderBackendGL::ensureProjectionTexture(const unsigned char* rgb, int w, int h, uint64_t version,
												uint64_t& ioUploadedVersion, int& ioW, int& ioH){
	if (!projTex_) {
		glGenTextures(1,&projTex_);
		glBindTexture(GL_TEXTURE_2D, projTex_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		ioW=ioH=0;
	}
	if (version!=ioUploadedVersion){
		glBindTexture(GL_TEXTURE_2D, projTex_);
		GLint prev; glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (w!=ioW || h!=ioH){
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w,h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
			ioW=w; ioH=h;
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, w,h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, prev);
		glBindTexture(GL_TEXTURE_2D, 0);
		ioUploadedVersion = version;
	}
	return projTex_;
}

// ----------------------------- WBOIT --------------------------------------------------
void RenderBackendGL::createOrResizeWboitFbo(int W, int H){
	if (W<=0 || H<=0) return;
	if (!fboWboit_) glGenFramebuffers(1,&fboWboit_);
	glBindFramebuffer(GL_FRAMEBUFFER, fboWboit_);
	
	if (!texAccum_) glGenTextures(1,&texAccum_);
	glBindTexture(GL_TEXTURE_2D, texAccum_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W,H, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texAccum_, 0);
	
	if (!texReveal_) glGenTextures(1,&texReveal_);
	glBindTexture(GL_TEXTURE_2D, texReveal_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, W,H, 0, GL_RED, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texReveal_, 0);
	
	GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
	glDrawBuffers(2, bufs);
	
	GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (st != GL_FRAMEBUFFER_COMPLETE)
	std::fprintf(stderr,"[WBOIT] FBO incomplete: 0x%x\n", st);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	wboitW_=W; wboitH_=H;
}
void RenderBackendGL::destroyWboitFbo(){
	if (texReveal_) { glDeleteTextures(1,&texReveal_); texReveal_=0; }
	if (texAccum_)  { glDeleteTextures(1,&texAccum_);  texAccum_=0; }
	if (fboWboit_)  { glDeleteFramebuffers(1,&fboWboit_); fboWboit_=0; }
	wboitW_=wboitH_=0;
}
void RenderBackendGL::beginWboitPass(GLuint accumLocation, GLuint revealLocation){
	glBindFramebuffer(GL_FRAMEBUFFER, fboWboit_);
	GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
	glDrawBuffers(2, bufs);
	const GLfloat zero[4] = {0,0,0,0};
	const GLfloat one [4] = {1,0,0,0};
	glClearBufferfv(GL_COLOR, 0, zero);
	glClearBufferfv(GL_COLOR, 1, one);
	
	glEnablei(GL_BLEND, 0);
	glBlendFunci(0, GL_ONE, GL_ONE); // accum add
	glBlendEquationi(0, GL_FUNC_ADD);
	
	glEnablei(GL_BLEND, 1);
	glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	
	glDisable(GL_DEPTH_TEST); // OIT では基本OFF（必要なら個別に）
}
void RenderBackendGL::endWboitPass(){
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisablei(GL_BLEND, 0);
	glDisablei(GL_BLEND, 1);
}
void RenderBackendGL::compositeWboit(GLuint program, GLuint fsQuadVao){
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texAccum_);
	glUniform1i(glGetUniformLocation(program,"uAccumTex"), 0);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texReveal_);
	glUniform1i(glGetUniformLocation(program,"uRevealTex"), 1);
	glBindVertexArray(fsQuadVao?fsQuadVao:vaoFsTri_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
}

// ----------------------------- RT: FBO / TBO / ドロー --------------------------------
void RenderBackendGL::createOrResizeRtFbo(int w, int h){
	if (w<=0 || h<=0) return;
	if (!texRT_) glGenTextures(1,&texRT_);
	glBindTexture(GL_TEXTURE_2D, texRT_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w,h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	if (!fboRT_) glGenFramebuffers(1,&fboRT_);
	glBindFramebuffer(GL_FRAMEBUFFER, fboRT_);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texRT_, 0);
	GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (st != GL_FRAMEBUFFER_COMPLETE)
	std::fprintf(stderr, "[RT] FBO incomplete: 0x%x\n", st);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	rtW_=w; rtH_=h;
}
void RenderBackendGL::destroyRtFbo(){
	if (fboRT_) { glDeleteFramebuffers(1,&fboRT_); fboRT_=0; }
	if (texRT_) { glDeleteTextures(1,&texRT_);     texRT_=0; }
	rtW_=rtH_=0;
}

TBO RenderBackendGL::makeTBO(GLenum internalFormat, GLsizeiptr bytes, const void* data){
	TBO t{};
	glGenBuffers(1,&t.buf);
	glBindBuffer(GL_TEXTURE_BUFFER, t.buf);
	glBufferData(GL_TEXTURE_BUFFER, bytes, data, GL_STATIC_DRAW);
	glGenTextures(1,&t.tex);
	glBindTexture(GL_TEXTURE_BUFFER, t.tex);
	glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, t.buf);
	glBindBuffer(GL_TEXTURE_BUFFER, 0);
	glBindTexture(GL_TEXTURE_BUFFER, 0);
	return t;
}
void RenderBackendGL::bindTBO(GLuint program, const char* uniformName, GLuint texture, GLint unit){
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_BUFFER, texture);
	glUniform1i(glGetUniformLocation(program, uniformName), unit);
}

void RenderBackendGL::drawRaytraceBVH(GLuint program, const RtBVHParams& p,
									  int lowW, int lowH, GLuint fsTriangleVao,
									  const TBO& nodeMin, const TBO& nodeMax, const TBO& nodeChild,
									  const TBO& particles, const TBO& partSigma){
	glBindFramebuffer(GL_FRAMEBUFFER, fboRT_);
	glViewport(0,0, lowW, lowH);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	
	glUseProgram(program);
	// TBO bind
	bindTBO(program,"nodeMinTB",   nodeMin.tex,   0);
	bindTBO(program,"nodeMaxTB",   nodeMax.tex,   1);
	bindTBO(program,"nodeChildTB", nodeChild.tex, 2);
	bindTBO(program,"particlesTB", particles.tex, 3);
	bindTBO(program,"partSigmaTB", partSigma.tex, 4);
	
	glUniform1i(glGetUniformLocation(program,"uRoot"), p.root);
	glUniform1i(glGetUniformLocation(program,"uLodMode"), p.lodMode);
	glUniform1f(glGetUniformLocation(program,"uPxThreshold"), p.pxThreshold);
	glUniform1f(glGetUniformLocation(program,"uTauMax"), p.tauMax);
	glUniform1f(glGetUniformLocation(program,"uStepBias"), p.stepBias);
	glUniform1f(glGetUniformLocation(program,"uFocalPx"), p.focalPx);
	glUniformMatrix4fv(glGetUniformLocation(program,"invProj"),1,GL_FALSE,glm::value_ptr(p.invProj));
	glUniformMatrix4fv(glGetUniformLocation(program,"invView"),1,GL_FALSE,glm::value_ptr(p.invView));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),   1,GL_FALSE,glm::value_ptr(p.view));
	glUniform3fv(glGetUniformLocation(program,"uCamForward"),1,glm::value_ptr(p.camForward));
	glUniform2f(glGetUniformLocation(program,"uResolution"), (float)lowW, (float)lowH);
	glUniform1i(glGetUniformLocation(program,"uDebugMode"), p.debugMode);
	
	glBindVertexArray(fsTriangleVao?fsTriangleVao:vaoFsTri_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderBackendGL::drawRaytraceOctree(GLuint program, const RtOctreeParams& p,
										 int lowW, int lowH, GLuint fsTriangleVao,
										 const GpuOctree& oct){
	glBindFramebuffer(GL_FRAMEBUFFER, fboRT_);
	glViewport(0,0, lowW, lowH);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	
	glUseProgram(program);
	bindTBO(program,"nodeMinTB",  oct.nodeMin.tex,  0);
	bindTBO(program,"nodeMaxTB",  oct.nodeMax.tex,  1);
	bindTBO(program,"childATB",   oct.texChildA.tex,2);
	bindTBO(program,"childBTB",   oct.texChildB.tex,3);
	bindTBO(program,"cornerLoTB", oct.cornerLo.tex, 4);
	bindTBO(program,"cornerHiTB", oct.cornerHi.tex, 5);
	
	glUniform1i(glGetUniformLocation(program,"uRoot"), p.root);
	glUniform1f(glGetUniformLocation(program,"uPxThreshold"), p.pxThreshold);
	glUniform1f(glGetUniformLocation(program,"uTauMax"), p.tauMax);
	glUniform1f(glGetUniformLocation(program,"uStepBias"), p.stepBias);
	glUniform1f(glGetUniformLocation(program,"uFocalPx"), p.focalPx);
	glUniformMatrix4fv(glGetUniformLocation(program,"invProj"),1,GL_FALSE,glm::value_ptr(p.invProj));
	glUniformMatrix4fv(glGetUniformLocation(program,"invView"),1,GL_FALSE,glm::value_ptr(p.invView));
	glUniformMatrix4fv(glGetUniformLocation(program,"view"),   1,GL_FALSE,glm::value_ptr(p.view));
	glUniform3fv(glGetUniformLocation(program,"uCamForward"),1,glm::value_ptr(p.camForward));
	glUniform2f(glGetUniformLocation(program,"uResolution"), (float)lowW, (float)lowH);
	glUniform1i(glGetUniformLocation(program,"uDebugMode"), p.debugMode);
	
	glBindVertexArray(fsTriangleVao?fsTriangleVao:vaoFsTri_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderBackendGL::upscaleToScreen(GLuint program, GLuint lowTex, GLuint fsTriangleVao,
									  int viewportX, int viewportY, int viewportW, int viewportH){
	glViewport(viewportX, viewportY, viewportW, viewportH);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	
	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, lowTex);
	glUniform1i(glGetUniformLocation(program,"uLow"), 0);
	glBindVertexArray(fsTriangleVao?fsTriangleVao:vaoFsTri_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	
	glDisable(GL_BLEND);
	glBindVertexArray(0);
	glUseProgram(0);
}
