#pragma once

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "scene.h"

struct TextSprite {
    ComPtr<ID3D11ShaderResourceView> srv;
    UINT width = 0;
    UINT height = 0;
};

struct GuidLess {
    bool operator()(const GUID& a, const GUID& b) const { return memcmp(&a, &b, sizeof(GUID)) < 0; }
};

// Draws the void: desktop pictures morphing into balls, the plus world, glows, labels and the starfield.
class Renderer {
public:
    // A null adapter picks the default hardware adapter; WARP is the last resort.
    bool Init(IDXGIAdapter* adapter);
    void Reset();
    bool CreateSwapChain(HWND window, UINT width, UINT height);
    bool ResizeSwapChain(UINT width, UINT height);

    // One picture per virtual desktop and monitor; the calls below use the monitor set here (its device name).
    // With crossFade, a refreshed picture fades in over the old one.
    void SetMonitor(const std::wstring& monitor) { monitor_ = monitor; }
    bool StoreSnapshot(const GUID& desktop, ID3D11Texture2D* capture, bool crossFade);
    bool StorePixels(const GUID& desktop, const uint8_t* bgra, UINT width, UINT height);
    bool HasSnapshot(const GUID& desktop) const;
    bool HasPicturesFor(const std::wstring& monitor) const;
    // Forgets pictures of desktops that no longer exist, on every monitor.
    void RetainSnapshots(const std::vector<GUID>& desktops);

    bool Render(const FrameDesc& frame, ID3D11Texture2D* target, UINT width, UINT height);
    bool Present(const FrameDesc& frame);

    // Frees the multisampled frame buffers while the overlay is hidden.
    void ReleaseFrameTargets();

    bool ReadPixels(ID3D11Texture2D* texture, std::vector<uint8_t>& pixels, UINT& width, UINT& height);

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }
    const char* AdapterName() const { return adapterName_; }
    UINT SampleCount() const { return sampleCount_; }

private:
    struct Snapshot {
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11ShaderResourceView> previous;  // fading out after a refresh
        LARGE_INTEGER updatedAt = {};
    };

    bool CreateShaders();
    bool CreateStates();
    bool CreateMeshes();
    bool CreateNebula();
    bool CreateEmptyWorld();
    bool CreateStaticBuffer(const void* data, UINT bytes, UINT bindFlags, ComPtr<ID3D11Buffer>& out);
    bool EnsureFrameTargets(UINT width, UINT height);
    void EnsureText(UINT height);
    bool CreateTextSprite(const wchar_t* text, int pixelHeight, bool semibold, TextSprite& out);
    const TextSprite& Label(const std::wstring& text);
    const TextSprite& HintSprite(const std::wstring& text);
    void DrawSphere();
    void DrawWorld(const WorldDesc& world, float aspect);
    void DrawGlow(DirectX::XMFLOAT3 center, float radius, DirectX::XMFLOAT3 color, float strength);
    void DrawSprite(const TextSprite& sprite, float x, float y, float alpha, DirectX::XMFLOAT3 color);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    UINT swapWidth_ = 0;
    UINT swapHeight_ = 0;
    char adapterName_[128] = "unknown";
    UINT sampleCount_ = 1;
    bool mipsSupported_ = false;
    LARGE_INTEGER frequency_ = {};

    ComPtr<ID3D11VertexShader> voidVS_, worldVS_, bodyVS_, glowVS_, spriteVS_;
    ComPtr<ID3D11PixelShader> voidPS_, nebulaPS_, worldPS_, bodyPS_, glowPS_, spritePS_, badgePS_;
    ComPtr<ID3D11InputLayout> gridLayout_, sphereLayout_;
    ComPtr<ID3D11Buffer> frameCB_, objectCB_;
    ComPtr<ID3D11Buffer> gridVB_, gridIB_, sphereVB_, sphereIB_;
    UINT gridIndexCount_ = 0;
    UINT sphereIndexCount_ = 0;
    ComPtr<ID3D11SamplerState> desktopSampler_, linearSampler_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depthOff_, depthWrite_, depthRead_;
    ComPtr<ID3D11BlendState> additive_, alphaBlend_;
    ComPtr<ID3D11ShaderResourceView> nebulaSrv_;
    ComPtr<ID3D11ShaderResourceView> emptyWorld_;  // shown until a desktop has a picture

    using Pictures = std::map<GUID, Snapshot, GuidLess>;
    std::map<std::wstring, Pictures> pictures_;  // by monitor
    std::wstring monitor_;
    Pictures& Current() { return pictures_[monitor_]; }

    ComPtr<ID3D11Texture2D> color_, depth_;
    ComPtr<ID3D11RenderTargetView> colorRtv_;
    ComPtr<ID3D11DepthStencilView> depthDsv_;
    UINT targetWidth_ = 0;
    UINT targetHeight_ = 0;

    std::map<std::wstring, TextSprite> labels_;
    std::map<std::wstring, TextSprite> hintLabels_;
    TextSprite labelPlus_;
    TextSprite hints_[3];
    UINT textHeight_ = 0;
    int labelPixels_ = 14;
    int hintPixels_ = 12;
};
