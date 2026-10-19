// Blockfall - Direct3D 11 renderer
//
// Passes, each frame:
//   1. shadow map from the sun (Shadows setting)
//   2. scene into a colour + normal + depth buffer (Mesh Detail, Smooth Shading, Normal Maps,
//      Materials, Lighting and Shadows settings happen here)
//   3. lighting post pass: ambient occlusion and colour bounce (Lighting setting)
//   4. half-res blur used for bloom and depth of field
//   5. compose: fog, bloom, tonemap, colour grading, depth of field, vignette (Post-Processing)
//   6. anti-aliasing: FXAA or temporal (Anti-Aliasing setting)
//   7. HUD
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include "../game/hud.h"
#include "../game/model.h"
#include "../game/scene.h"
#include "../game/settings.h"

namespace bf {

template <typename T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

class RendererD3D11 {
public:
    bool init(HWND hwnd, int width, int height, std::string& error);
    void shutdown();
    void resize(int width, int height);
    void setGraphics(const GraphicsSettings& g);
    void setTerrain(const World& world);
    // The rigged character and its skin textures (assets/character.glb, assets/skins/*.png).
    bool setCharacterModel(const CharacterModel& model, const std::string& skinDir, std::string& error);
    // World material textures (assets/textures/<name>_albedo.png + _normal.png). Optional: without
    // them the shader's procedural bumps are used.
    bool loadMaterialTextures(const std::string& dir, std::string& error);
    bool hasMaterialTextures() const { return matAlbedoSrv_ != nullptr; }
    int skinTextureCount() const { return (int)skinSrv_.size(); }
    bool hasCharacterAtlas() const { return charNormalSrv_ && charMaterialSrv_; }
    // 0 stylized (cartoon steps + outline), 1 realistic (PBR) characters.
    void setCharacterStyle(int style) { charStyle_ = style; }
    // The map picture for the mini map / map screen. `generation` changes when the map does.
    void setMapImage(const std::vector<uint32_t>& rgba, int size, int generation);
    // dofFocus: metres in front of the camera that stay sharp (0 = no depth of field this frame).
    void render(const SceneList& scene, const std::vector<HudQuad>& hud, float dofFocus = 0);
    void present(bool vsync);
    // Copies the last presented frame to a 24-bit BMP (used by --shots for testing).
    bool saveScreenshot(const char* path);
    int width() const { return width_; }
    int height() const { return height_; }
    const char* adapterName() const { return adapterName_.c_str(); }
    // Triangles drawn in the last frame (for the FPS/debug line).
    unsigned lastTriangles() const { return triangles_; }

private:
    struct Mesh {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        UINT indexCount = 0;
    };
    struct Target {
        ID3D11Texture2D* tex = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
    };
    bool createTargets(std::string& error);
    void releaseTargets();
    bool createTarget(Target& t, int w, int h, DXGI_FORMAT fmt);
    void releaseTarget(Target& t);
    bool createShadowMap(int size);
    bool createMesh(const std::vector<MeshVertex>& v, const std::vector<uint32_t>& idx, Mesh& out);
    void releaseMesh(Mesh& m);
    bool compileShaders(std::string& error, bool sm5);
    void uploadInstances(const SceneList& scene);
    void drawScene(const SceneList& scene, bool shadowPass);
    void drawCharacters(const SceneList& scene, bool shadowPass);
    bool loadSkinTexture(const std::string& path, ID3D11ShaderResourceView** out);
    void fullscreen(ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, int w, int h, ID3D11ShaderResourceView* const* srvs, int n);
    void ensureInstanceCapacity(size_t count);
    void ensureHudCapacity(size_t verts);

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;   // window
    int rw_ = 0, rh_ = 0;           // internal render size (2x the window on Anti-Aliasing High)
    int renderScale_ = 1;
    std::string adapterName_;
    GraphicsSettings gfx_;
    unsigned frame_ = 0;
    unsigned triangles_ = 0;
    bool historyValid_ = false;
    Mat4 prevViewProj_ = Mat4::identity();

    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    IDXGISwapChain* swap_ = nullptr;
    ID3D11RenderTargetView* backRtv_ = nullptr;

    // Scene buffers.
    Target color_, normal_, lit_, blurA_, blurB_, ldr_, taa_[2];
    ID3D11Texture2D* depthTex_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11ShaderResourceView* depthSrv_ = nullptr;
    ID3D11Texture2D* shadowTex_ = nullptr;
    ID3D11DepthStencilView* shadowDsv_ = nullptr;
    ID3D11ShaderResourceView* shadowSrv_ = nullptr;
    int shadowSize_ = 0;

    // Shaders.
    ID3D11VertexShader* vsScene_ = nullptr;
    ID3D11PixelShader* psScene_ = nullptr;
    ID3D11VertexShader* vsShadow_ = nullptr;
    ID3D11InputLayout* layoutScene_ = nullptr;
    ID3D11VertexShader* vsFull_ = nullptr;
    ID3D11PixelShader* psLight_ = nullptr;
    ID3D11PixelShader* psDown_ = nullptr;
    ID3D11PixelShader* psBlur_ = nullptr;
    ID3D11PixelShader* psCompose_ = nullptr;
    ID3D11PixelShader* psFxaa_ = nullptr;
    ID3D11PixelShader* psTaa_ = nullptr;
    ID3D11PixelShader* psCopy_ = nullptr;
    ID3D11PixelShader* psDownsample_ = nullptr;
    ID3D11VertexShader* vsHud_ = nullptr;
    ID3D11PixelShader* psHud_ = nullptr;
    ID3D11InputLayout* layoutHud_ = nullptr;

    ID3D11Buffer* cbFrame_ = nullptr;
    ID3D11Buffer* cbPost_ = nullptr;
    ID3D11Buffer* cbHud_ = nullptr;
    ID3D11Buffer* instanceBuf_ = nullptr;
    size_t instanceCap_ = 0;
    std::vector<Instance> instanceScratch_;
    struct DrawRange { int mesh, lod; UINT first, count; bool transparent; };
    std::vector<DrawRange> ranges_;
    ID3D11Buffer* hudBuf_ = nullptr;
    size_t hudCap_ = 0;

    ID3D11RasterizerState* rsSolid_ = nullptr;
    ID3D11RasterizerState* rsShadow_ = nullptr;
    ID3D11DepthStencilState* dsOpaque_ = nullptr;
    ID3D11DepthStencilState* dsTransparent_ = nullptr;
    ID3D11DepthStencilState* dsOff_ = nullptr;
    ID3D11BlendState* bsOpaque_ = nullptr;
    ID3D11BlendState* bsAlpha_ = nullptr;
    ID3D11SamplerState* sampLinear_ = nullptr;
    ID3D11SamplerState* sampPoint_ = nullptr;
    ID3D11SamplerState* sampShadow_ = nullptr;
    ID3D11Texture2D* atlasTex_ = nullptr;
    ID3D11ShaderResourceView* atlasSrv_ = nullptr;
    ID3D11Texture2D* mapTex_ = nullptr;
    ID3D11ShaderResourceView* mapSrv_ = nullptr;
    int mapGeneration_ = -1;

    Mesh meshes_[MESH_COUNT][MESH_LODS];

    // Skinned characters.
    const CharacterModel* charModel_ = nullptr;
    std::vector<Mesh> charMeshes_;
    std::vector<ID3D11ShaderResourceView*> skinSrv_;
    std::vector<ID3D11Texture2D*> skinTex_;
    ID3D11VertexShader* vsSkin_ = nullptr;
    ID3D11VertexShader* vsSkinShadow_ = nullptr;
    ID3D11VertexShader* vsSkinOutline_ = nullptr;
    ID3D11VertexShader* vsSkinShadowUV_ = nullptr;
    ID3D11PixelShader* psCharShadowClip_ = nullptr;
    // Shared character detail atlas (assets/character_normal.png, character_material.png).
    ID3D11ShaderResourceView* charNormalSrv_ = nullptr;
    ID3D11ShaderResourceView* charMaterialSrv_ = nullptr;
    int charStyle_ = 0;
    ID3D11PixelShader* psChar_ = nullptr;
    ID3D11PixelShader* psOutline_ = nullptr;
    ID3D11InputLayout* layoutSkin_ = nullptr;
    ID3D11Buffer* cbBones_ = nullptr;
    ID3D11Buffer* cbChar_ = nullptr;
    ID3D11RasterizerState* rsCullFront_ = nullptr;
    ID3D11SamplerState* sampSkin_ = nullptr;
    ID3D11SamplerState* sampWrap_ = nullptr;

    // World material textures: one array slice per MaterialId.
    ID3D11Texture2D* matAlbedoTex_ = nullptr;
    ID3D11ShaderResourceView* matAlbedoSrv_ = nullptr;
    ID3D11Texture2D* matNormalTex_ = nullptr;
    ID3D11ShaderResourceView* matNormalSrv_ = nullptr;
    int terrainGeneration_ = -1;
    int terrainStep_ = 0;
};

}  // namespace bf
