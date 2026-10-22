#include "renderer.h"

#include "brand.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Compiled by build.bat from shaders/*.hlsl.
#include "BodyPS.h"
#include "BodyVS.h"
#include "GlowPS.h"
#include "GlowVS.h"
#include "NebulaPS.h"
#include "BadgePS.h"
#include "SpritePS.h"
#include "SpriteVS.h"
#include "VoidPS.h"
#include "VoidVS.h"
#include "WorldPS.h"
#include "WorldVS.h"

using namespace DirectX;

namespace {

// Venom Board's palette (see brand.h), as the void uses it.
constexpr XMFLOAT3 kFang = {venom::kFang.r, venom::kFang.g, venom::kFang.b};
constexpr XMFLOAT3 kSheen = {venom::kSheen.r, venom::kSheen.g, venom::kSheen.b};
constexpr XMFLOAT3 kBone = {venom::kBone.r, venom::kBone.g, venom::kBone.b};
constexpr XMFLOAT3 kBoneDim = {venom::kBoneDim.r, venom::kBoneDim.g, venom::kBoneDim.b};

// Mirrors cbuffer Frame in shaders/common.hlsli.
struct FrameConstants {
    XMFLOAT4X4 viewProj;
    XMFLOAT4X4 invViewProj;
    XMFLOAT3 camPos;
    float time;
    XMFLOAT2 screen;
    float voidAmount;
    float pad0;
    XMFLOAT3 camRight;
    float pad1;
    XMFLOAT3 camUp;
    float pad2;
};

// Mirrors cbuffer Object in shaders/common.hlsli.
struct ObjectConstants {
    XMFLOAT3 center;
    float radius;
    float morph;
    float aspect;
    float lonSpan;
    float latSpan;
    XMFLOAT4 tint;
    float hover;
    float pulse;
    float alpha;
    float kind;
    XMFLOAT4 rect;
};

static_assert(sizeof(FrameConstants) == 192, "FrameConstants must match the HLSL layout");
static_assert(sizeof(ObjectConstants) == 80, "ObjectConstants must match the HLSL layout");

constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr UINT kGridColumns = 128;
constexpr UINT kGridRows = 72;
constexpr UINT kSphereStacks = 48;
constexpr UINT kSphereSlices = 96;
constexpr UINT kNebulaSize = 512;

template <typename T>
void Upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const T& data) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(T));
        context->Unmap(buffer, 0);
    }
}

}  // namespace

bool Renderer::Init(IDXGIAdapter* adapter) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) {
        Log("Hardware Direct3D device failed (0x%08X); falling back to WARP", unsigned(hr));
        CHECK_HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device_, nullptr, &context_));
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    CHECK_HR(device_.As(&dxgiDevice));
    ComPtr<IDXGIAdapter> actual;
    CHECK_HR(dxgiDevice->GetAdapter(&actual));
    DXGI_ADAPTER_DESC adapterDesc;
    if (SUCCEEDED(actual->GetDesc(&adapterDesc))) {
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName_, sizeof(adapterName_), nullptr,
                            nullptr);
    }

    UINT quality = 0;
    if (SUCCEEDED(device_->CheckMultisampleQualityLevels(kColorFormat, 4, &quality)) && quality > 0) sampleCount_ = 4;
    UINT support = 0;
    mipsSupported_ =
        SUCCEEDED(device_->CheckFormatSupport(kColorFormat, &support)) && (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN);
    QueryPerformanceFrequency(&frequency_);

    return CreateShaders() && CreateStates() && CreateMeshes() && CreateNebula() && CreateEmptyWorld();
}

// A dim violet gradient for a desktop that has no picture yet.
bool Renderer::CreateEmptyWorld() {
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 36;
    std::vector<uint32_t> pixels(kWidth * kHeight);
    for (UINT y = 0; y < kHeight; ++y) {
        for (UINT x = 0; x < kWidth; ++x) {
            const float t = float(y) / (kHeight - 1);
            const float glow = 1.0f - fabsf(float(x) / (kWidth - 1) - 0.5f);
            const uint32_t r = uint32_t(30 + 30 * glow * (1 - t));
            const uint32_t g = uint32_t(22 + 18 * glow);
            const uint32_t b = uint32_t(60 + 50 * (1 - t));
            pixels[y * kWidth + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init = {pixels.data(), kWidth * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    CHECK_HR(device_->CreateTexture2D(&desc, &init, &texture));
    CHECK_HR(device_->CreateShaderResourceView(texture.Get(), nullptr, &emptyWorld_));
    return true;
}

void Renderer::Reset() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    *this = Renderer();
}

bool Renderer::CreateShaders() {
    CHECK_HR(device_->CreateVertexShader(g_VoidVS, sizeof(g_VoidVS), nullptr, &voidVS_));
    CHECK_HR(device_->CreatePixelShader(g_VoidPS, sizeof(g_VoidPS), nullptr, &voidPS_));
    CHECK_HR(device_->CreatePixelShader(g_NebulaPS, sizeof(g_NebulaPS), nullptr, &nebulaPS_));
    CHECK_HR(device_->CreateVertexShader(g_WorldVS, sizeof(g_WorldVS), nullptr, &worldVS_));
    CHECK_HR(device_->CreatePixelShader(g_WorldPS, sizeof(g_WorldPS), nullptr, &worldPS_));
    CHECK_HR(device_->CreateVertexShader(g_BodyVS, sizeof(g_BodyVS), nullptr, &bodyVS_));
    CHECK_HR(device_->CreatePixelShader(g_BodyPS, sizeof(g_BodyPS), nullptr, &bodyPS_));
    CHECK_HR(device_->CreateVertexShader(g_GlowVS, sizeof(g_GlowVS), nullptr, &glowVS_));
    CHECK_HR(device_->CreatePixelShader(g_GlowPS, sizeof(g_GlowPS), nullptr, &glowPS_));
    CHECK_HR(device_->CreateVertexShader(g_SpriteVS, sizeof(g_SpriteVS), nullptr, &spriteVS_));
    CHECK_HR(device_->CreatePixelShader(g_SpritePS, sizeof(g_SpritePS), nullptr, &spritePS_));
    CHECK_HR(device_->CreatePixelShader(g_BadgePS, sizeof(g_BadgePS), nullptr, &badgePS_));

    const D3D11_INPUT_ELEMENT_DESC gridElements[] = {
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    CHECK_HR(device_->CreateInputLayout(gridElements, 1, g_WorldVS, sizeof(g_WorldVS), &gridLayout_));
    const D3D11_INPUT_ELEMENT_DESC sphereElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    CHECK_HR(device_->CreateInputLayout(sphereElements, 1, g_BodyVS, sizeof(g_BodyVS), &sphereLayout_));

    D3D11_BUFFER_DESC cb = {};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameConstants);
    CHECK_HR(device_->CreateBuffer(&cb, nullptr, &frameCB_));
    cb.ByteWidth = sizeof(ObjectConstants);
    CHECK_HR(device_->CreateBuffer(&cb, nullptr, &objectCB_));
    return true;
}

bool Renderer::CreateStates() {
    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    // Slightly sharp mip choice keeps the ball crisp, and at 1:1 it pins sampling to the full-size image.
    sampler.MipLODBias = -0.35f;
    CHECK_HR(device_->CreateSamplerState(&sampler, &desktopSampler_));
    sampler.MipLODBias = 0.0f;
    CHECK_HR(device_->CreateSamplerState(&sampler, &linearSampler_));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = TRUE;
    CHECK_HR(device_->CreateRasterizerState(&raster, &raster_));

    D3D11_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    CHECK_HR(device_->CreateDepthStencilState(&depth, &depthOff_));
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    CHECK_HR(device_->CreateDepthStencilState(&depth, &depthWrite_));
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    CHECK_HR(device_->CreateDepthStencilState(&depth, &depthRead_));

    D3D11_BLEND_DESC blend = {};
    D3D11_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_ONE;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
    rt.DestBlendAlpha = D3D11_BLEND_ONE;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    CHECK_HR(device_->CreateBlendState(&blend, &additive_));
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    CHECK_HR(device_->CreateBlendState(&blend, &alphaBlend_));
    return true;
}

bool Renderer::CreateStaticBuffer(const void* data, UINT bytes, UINT bindFlags, ComPtr<ID3D11Buffer>& out) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = bindFlags;
    D3D11_SUBRESOURCE_DATA init = {data, 0, 0};
    CHECK_HR(device_->CreateBuffer(&desc, &init, &out));
    return true;
}

bool Renderer::CreateMeshes() {
    // The desktop: a grid of UVs. The vertex shader places each one on the flat screen or on the ball.
    std::vector<XMFLOAT2> grid;
    for (UINT y = 0; y <= kGridRows; ++y)
        for (UINT x = 0; x <= kGridColumns; ++x) grid.push_back({float(x) / kGridColumns, float(y) / kGridRows});
    std::vector<uint16_t> gridIndices;
    for (UINT y = 0; y < kGridRows; ++y) {
        for (UINT x = 0; x < kGridColumns; ++x) {
            const uint16_t i0 = uint16_t(y * (kGridColumns + 1) + x);
            const uint16_t i1 = uint16_t(i0 + 1);
            const uint16_t i2 = uint16_t(i0 + kGridColumns + 1);
            const uint16_t i3 = uint16_t(i2 + 1);
            gridIndices.insert(gridIndices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }
    gridIndexCount_ = UINT(gridIndices.size());

    // A unit sphere, for the planet body and the plus world.
    std::vector<XMFLOAT3> sphere;
    for (UINT stack = 0; stack <= kSphereStacks; ++stack) {
        const float theta = XM_PI * float(stack) / kSphereStacks;
        for (UINT slice = 0; slice <= kSphereSlices; ++slice) {
            const float phi = XM_2PI * float(slice) / kSphereSlices;
            sphere.push_back({sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi)});
        }
    }
    std::vector<uint16_t> sphereIndices;
    for (UINT stack = 0; stack < kSphereStacks; ++stack) {
        for (UINT slice = 0; slice < kSphereSlices; ++slice) {
            const uint16_t i0 = uint16_t(stack * (kSphereSlices + 1) + slice);
            const uint16_t i1 = uint16_t(i0 + 1);
            const uint16_t i2 = uint16_t(i0 + kSphereSlices + 1);
            const uint16_t i3 = uint16_t(i2 + 1);
            sphereIndices.insert(sphereIndices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    sphereIndexCount_ = UINT(sphereIndices.size());

    return CreateStaticBuffer(grid.data(), UINT(grid.size() * sizeof(XMFLOAT2)), D3D11_BIND_VERTEX_BUFFER, gridVB_) &&
           CreateStaticBuffer(gridIndices.data(), UINT(gridIndices.size() * sizeof(uint16_t)), D3D11_BIND_INDEX_BUFFER,
                              gridIB_) &&
           CreateStaticBuffer(sphere.data(), UINT(sphere.size() * sizeof(XMFLOAT3)), D3D11_BIND_VERTEX_BUFFER,
                              sphereVB_) &&
           CreateStaticBuffer(sphereIndices.data(), UINT(sphereIndices.size() * sizeof(uint16_t)),
                              D3D11_BIND_INDEX_BUFFER, sphereIB_);
}

// The nebula is expensive noise, so it is baked once into a cube map and sampled by view direction.
bool Renderer::CreateNebula() {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = kNebulaSize;
    desc.MipLevels = 1;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> cube;
    CHECK_HR(device_->CreateTexture2D(&desc, nullptr, &cube));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    CHECK_HR(device_->CreateShaderResourceView(cube.Get(), &srvDesc, &nebulaSrv_));

    // Face order and up vectors follow Direct3D's cube map convention.
    const XMVECTOR looks[6] = {XMVectorSet(1, 0, 0, 0), XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0),
                               XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 0, -1, 0)};
    const XMVECTOR ups[6] = {XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, -1, 0),
                             XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 1, 0, 0)};
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, 10.0f);

    const D3D11_VIEWPORT viewport = {0, 0, float(kNebulaSize), float(kNebulaSize), 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(raster_.Get());
    context_->OMSetDepthStencilState(depthOff_.Get(), 0);
    context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(voidVS_.Get(), nullptr, 0);
    context_->PSSetShader(nebulaPS_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, frameCB_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, frameCB_.GetAddressOf());

    for (UINT face = 0; face < 6; ++face) {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        ComPtr<ID3D11RenderTargetView> rtv;
        CHECK_HR(device_->CreateRenderTargetView(cube.Get(), &rtvDesc, &rtv));

        const XMMATRIX view = XMMatrixLookToLH(XMVectorZero(), looks[face], ups[face]);
        FrameConstants constants = {};
        XMStoreFloat4x4(&constants.invViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, view * proj)));
        Upload(context_.Get(), frameCB_.Get(), constants);
        context_->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        context_->Draw(3, 0);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

bool Renderer::CreateSwapChain(HWND window, UINT width, UINT height) {
    ComPtr<IDXGIDevice> dxgiDevice;
    CHECK_HR(device_.As(&dxgiDevice));
    ComPtr<IDXGIAdapter> adapter;
    CHECK_HR(dxgiDevice->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> factory;
    CHECK_HR(adapter->GetParent(IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    CHECK_HR(factory->CreateSwapChainForHwnd(device_.Get(), window, &desc, nullptr, nullptr, &swapChain_));
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    swapWidth_ = width;
    swapHeight_ = height;
    return true;
}

bool Renderer::ResizeSwapChain(UINT width, UINT height) {
    if (width == swapWidth_ && height == swapHeight_) return true;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    CHECK_HR(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
    swapWidth_ = width;
    swapHeight_ = height;
    return true;
}

bool Renderer::StoreSnapshot(const GUID& desktop, ID3D11Texture2D* capture, bool crossFade) {
    // Always a new texture, so the old one can still be drawn while it fades out.
    D3D11_TEXTURE2D_DESC captureDesc;
    capture->GetDesc(&captureDesc);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = captureDesc.Width;
    desc.Height = captureDesc.Height;
    desc.MipLevels = mipsSupported_ ? 0 : 1;  // 0 = full chain
    desc.ArraySize = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mipsSupported_ ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = mipsSupported_ ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    CHECK_HR(device_->CreateTexture2D(&desc, nullptr, &texture));
    CHECK_HR(device_->CreateShaderResourceView(texture.Get(), nullptr, &srv));
    context_->CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, capture, 0, nullptr);
    if (mipsSupported_) context_->GenerateMips(srv.Get());

    Snapshot& snapshot = Current()[desktop];
    snapshot.previous = crossFade ? snapshot.srv : nullptr;
    snapshot.srv = srv;
    QueryPerformanceCounter(&snapshot.updatedAt);
    return true;
}

bool Renderer::StorePixels(const GUID& desktop, const uint8_t* bgra, UINT width, UINT height) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init = {bgra, width * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    CHECK_HR(device_->CreateTexture2D(&desc, &init, &texture));
    return StoreSnapshot(desktop, texture.Get(), false);
}

bool Renderer::HasSnapshot(const GUID& desktop) const {
    const auto pictures = pictures_.find(monitor_);
    return pictures != pictures_.end() && pictures->second.count(desktop) != 0;
}

void Renderer::RetainSnapshots(const std::vector<GUID>& desktops) {
    for (auto& [monitor, pictures] : pictures_) {
        for (auto it = pictures.begin(); it != pictures.end();) {
            const bool keep = std::find(desktops.begin(), desktops.end(), it->first) != desktops.end();
            it = keep ? std::next(it) : pictures.erase(it);
        }
    }
}

bool Renderer::EnsureFrameTargets(UINT width, UINT height) {
    if (color_ && width == targetWidth_ && height == targetHeight_) return true;
    ReleaseFrameTargets();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = sampleCount_;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    CHECK_HR(device_->CreateTexture2D(&desc, nullptr, &color_));
    CHECK_HR(device_->CreateRenderTargetView(color_.Get(), nullptr, &colorRtv_));

    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHECK_HR(device_->CreateTexture2D(&desc, nullptr, &depth_));
    CHECK_HR(device_->CreateDepthStencilView(depth_.Get(), nullptr, &depthDsv_));

    targetWidth_ = width;
    targetHeight_ = height;
    return true;
}

void Renderer::ReleaseFrameTargets() {
    if (context_) context_->OMSetRenderTargets(0, nullptr, nullptr);
    colorRtv_.Reset();
    color_.Reset();
    depthDsv_.Reset();
    depth_.Reset();
    targetWidth_ = targetHeight_ = 0;
}

bool Renderer::CreateTextSprite(const wchar_t* text, int pixelHeight, bool semibold, TextSprite& out) {
    out = TextSprite();
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, semibold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                             venom::UiFont());
    HGDIOBJ previousFont = SelectObject(dc, font);
    RECT measure = {0, 0, 0, 0};
    DrawTextW(dc, text, -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    const int pad = 2;
    const int width = measure.right + pad * 2;
    const int height = measure.bottom + pad * 2;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bitmap) {
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        memset(bits, 0, size_t(width) * height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT area = {pad, pad, width - pad, height - pad};
        DrawTextW(dc, text, -1, &area, DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();

        // White text on black: the red channel is the glyph coverage, used as alpha by SpritePS.
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = UINT(width);
        desc.Height = UINT(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = kColorFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {bits, UINT(width) * 4, 0};
        ComPtr<ID3D11Texture2D> texture;
        ok = SUCCEEDED(device_->CreateTexture2D(&desc, &init, &texture)) &&
             SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, &out.srv));
        out.width = UINT(width);
        out.height = UINT(height);
        SelectObject(dc, previousBitmap);
        DeleteObject(bitmap);
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    DeleteDC(dc);
    return ok;
}

void Renderer::EnsureText(UINT height) {
    if (height == textHeight_) return;
    textHeight_ = height;
    labelPixels_ = std::max(14, int(height / 46));
    hintPixels_ = std::max(12, int(height / 62));
    labels_.clear();
    hintLabels_.clear();
    CreateTextSprite(L"New workspace", labelPixels_, true, labelPlus_);
    CreateTextSprite(L"Click a world to enter   ·   drag to reorder   ·   1–9 jump   ·   Esc to return",
                     hintPixels_, false, hints_[int(Hint::Default)]);
    CreateTextSprite(L"Couldn't switch to that desktop", hintPixels_, false, hints_[int(Hint::SwitchFailed)]);
    CreateTextSprite(L"Couldn't create a new desktop", hintPixels_, false, hints_[int(Hint::CreateFailed)]);
}

const TextSprite& Renderer::Label(const std::wstring& text) {
    auto found = labels_.find(text);
    if (found != labels_.end()) return found->second;
    TextSprite& sprite = labels_[text];
    CreateTextSprite(text.c_str(), labelPixels_, true, sprite);
    return sprite;
}

const TextSprite& Renderer::HintSprite(const std::wstring& text) {
    auto found = hintLabels_.find(text);
    if (found != hintLabels_.end()) return found->second;
    if (hintLabels_.size() > 32) hintLabels_.clear();  // hints come and go; don't let them pile up
    TextSprite& sprite = hintLabels_[text];
    CreateTextSprite(text.c_str(), hintPixels_, false, sprite);
    return sprite;
}

bool Renderer::HasPicturesFor(const std::wstring& monitor) const {
    const auto pictures = pictures_.find(monitor);
    return pictures != pictures_.end() && !pictures->second.empty();
}

void Renderer::DrawSphere() {
    const UINT stride = sizeof(XMFLOAT3);
    const UINT offset = 0;
    context_->IASetInputLayout(sphereLayout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetVertexBuffers(0, 1, sphereVB_.GetAddressOf(), &stride, &offset);
    context_->IASetIndexBuffer(sphereIB_.Get(), DXGI_FORMAT_R16_UINT, 0);
    context_->VSSetShader(bodyVS_.Get(), nullptr, 0);
    context_->PSSetShader(bodyPS_.Get(), nullptr, 0);
    context_->DrawIndexed(sphereIndexCount_, 0, 0);
}

void Renderer::DrawGlow(XMFLOAT3 center, float radius, XMFLOAT3 color, float strength) {
    ObjectConstants object = {};
    object.center = center;
    object.radius = radius;
    object.tint = {color.x, color.y, color.z, 1.0f};
    object.alpha = strength;
    Upload(context_.Get(), objectCB_.Get(), object);
    context_->Draw(4, 0);
}

void Renderer::DrawSprite(const TextSprite& sprite, float x, float y, float alpha, XMFLOAT3 color) {
    if (!sprite.srv || alpha <= 0.001f) return;
    ObjectConstants object = {};
    object.rect = {floorf(x), floorf(y), float(sprite.width), float(sprite.height)};  // whole pixels keep text crisp
    object.tint = {color.x, color.y, color.z, 1.0f};
    object.alpha = alpha;
    Upload(context_.Get(), objectCB_.Get(), object);
    context_->PSSetShaderResources(0, 1, sprite.srv.GetAddressOf());
    context_->Draw(4, 0);
}

// A desktop world: its planet body, then its picture, flat or wrapped around the body.
void Renderer::DrawWorld(const WorldDesc& world, float aspect) {
    ID3D11DeviceContext* ctx = context_.Get();
    if (world.morph > 0.001f) {
        ObjectConstants body = {};
        body.center = WorldCenter(world);
        body.radius = world.scale * 0.992f * world.morph;  // grows as the desktop curls around it
        const XMFLOAT3 rim = world.current ? kSheen : kFang;
        body.tint = {rim.x, rim.y, rim.z, 1.0f};
        body.hover = world.hover;
        body.kind = 0.0f;
        Upload(ctx, objectCB_.Get(), body);
        DrawSphere();
    }

    // The picture, cross-fading from the previous one for a moment after a refresh.
    ID3D11ShaderResourceView* picture = emptyWorld_.Get();
    ID3D11ShaderResourceView* previous = nullptr;
    float fade = 1.0f;
    Pictures& pictures = Current();
    auto found = pictures.find(world.id);
    if (found != pictures.end()) {
        Snapshot& snapshot = found->second;
        picture = snapshot.srv.Get();
        if (snapshot.previous) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            fade = float(double(now.QuadPart - snapshot.updatedAt.QuadPart) / double(frequency_.QuadPart)) / 0.3f;
            if (fade >= 1.0f) {
                fade = 1.0f;
                snapshot.previous.Reset();
            } else {
                previous = snapshot.previous.Get();
            }
        }
    }
    if (!previous) previous = picture;

    ObjectConstants cap = {};
    cap.center = world.offset;
    cap.radius = world.scale;
    cap.morph = world.morph;
    cap.aspect = aspect;
    cap.lonSpan = WorldLonSpan(aspect);
    cap.latSpan = kWorldLatSpan;
    const XMFLOAT3 seam = world.current ? kSheen : kFang;
    cap.tint = {seam.x, seam.y, seam.z, 1.0f};
    cap.hover = world.hover;
    cap.alpha = fade;
    Upload(ctx, objectCB_.Get(), cap);
    const UINT stride = sizeof(XMFLOAT2);
    const UINT offset = 0;
    ctx->IASetInputLayout(gridLayout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, gridVB_.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(gridIB_.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->VSSetShader(worldVS_.Get(), nullptr, 0);
    ctx->PSSetShader(worldPS_.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &picture);
    ctx->PSSetShaderResources(2, 1, &previous);
    ctx->DrawIndexed(gridIndexCount_, 0, 0);
}

bool Renderer::Render(const FrameDesc& frame, ID3D11Texture2D* target, UINT width, UINT height) {
    if (!EnsureFrameTargets(width, height)) return false;
    EnsureText(height);

    const float aspect = float(width) / float(height);
    const Camera camera = BuildCamera(frame, aspect);
    ID3D11DeviceContext* ctx = context_.Get();

    FrameConstants constants = {};
    XMStoreFloat4x4(&constants.viewProj, XMMatrixTranspose(camera.viewProj));
    XMStoreFloat4x4(&constants.invViewProj, XMMatrixTranspose(camera.invViewProj));
    constants.camPos = camera.eye;
    constants.time = frame.time;
    constants.screen = {float(width), float(height)};
    constants.voidAmount = frame.voidAmount;
    constants.camRight = camera.right;
    constants.camUp = camera.up;
    Upload(ctx, frameCB_.Get(), constants);

    ID3D11Buffer* buffers[] = {frameCB_.Get(), objectCB_.Get()};
    ctx->VSSetConstantBuffers(0, 2, buffers);
    ctx->PSSetConstantBuffers(0, 2, buffers);
    ID3D11SamplerState* samplers[] = {desktopSampler_.Get(), linearSampler_.Get()};
    ctx->PSSetSamplers(0, 2, samplers);
    ctx->RSSetState(raster_.Get());
    const D3D11_VIEWPORT viewport = {0, 0, float(width), float(height), 0, 1};
    ctx->RSSetViewports(1, &viewport);
    ctx->OMSetRenderTargets(1, colorRtv_.GetAddressOf(), depthDsv_.Get());
    ctx->ClearDepthStencilView(depthDsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    // 1. The void: baked nebula plus live stars, behind everything.
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(depthOff_.Get(), 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(voidVS_.Get(), nullptr, 0);
    ctx->PSSetShader(voidPS_.Get(), nullptr, 0);
    ctx->PSSetShaderResources(1, 1, nebulaSrv_.GetAddressOf());
    ctx->Draw(3, 0);

    // 2. Desktop worlds. Only the focused one can be flat; nothing else is ever in front of its plane, so at
    //    progress 0 the frame is exactly its picture.
    ctx->OMSetDepthStencilState(depthWrite_.Get(), 0);
    for (const WorldDesc& world : frame.worlds) {
        if (world.scale > 0.001f) DrawWorld(world, aspect);
    }

    // 3. The plus world.
    if (frame.plusRadius > 0.001f) {
        ObjectConstants object = {};
        object.center = frame.plusCenter;
        object.radius = frame.plusRadius;
        object.tint = {kFang.x, kFang.y, kFang.z, 1.0f};
        object.hover = frame.hoverPlus;
        object.pulse = frame.pulsePlus;
        object.kind = 1.0f;
        Upload(ctx, objectCB_.Get(), object);
        DrawSphere();
    }

    // 4. Halos, added on top and hidden by the balls' own depth where they overlap. The world you're on glows
    //    blue, the others violet.
    if (frame.voidAmount > 0.001f) {
        ctx->OMSetBlendState(additive_.Get(), nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(depthRead_.Get(), 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(glowVS_.Get(), nullptr, 0);
        ctx->PSSetShader(glowPS_.Get(), nullptr, 0);
        for (const WorldDesc& world : frame.worlds) {
            if (world.scale <= 0.001f) continue;
            // The world you're on glows sheen blue; the others glow Venom crimson.
            const XMFLOAT3 color = world.current ? kSheen : kFang;
            const float base = world.current ? 0.6f : 0.4f;
            DrawGlow(WorldCenter(world), world.scale, color, frame.voidAmount * (base + 0.65f * world.hover));
        }
        if (frame.plusRadius > 0.001f) {
            DrawGlow(frame.plusCenter, frame.plusRadius, kFang,
                     frame.voidAmount * (0.45f + 0.7f * frame.hoverPlus + 0.8f * frame.pulsePlus));
        }
    }

    // 5. Labels and the hint line. Every world in a row sits at the same height, so labels hung a fixed distance
    //    below the centres line up.
    if (frame.labelAlpha > 0.001f) {
        ctx->OMSetBlendState(alphaBlend_.Get(), nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(depthOff_.Get(), 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(spriteVS_.Get(), nullptr, 0);
        ctx->PSSetShader(spritePS_.Get(), nullptr, 0);

        constexpr float kLabelDrop = 1.32f;
        const XMFLOAT3 labelColor = kBone;
        XMFLOAT2 at;
        for (const WorldDesc& world : frame.worlds) {
            if (world.scale <= 0.05f) continue;
            const XMFLOAT3 center = WorldCenter(world);
            if (!ProjectToScreen(camera, {center.x, center.y - kLabelDrop, center.z}, width, height, at)) continue;
            const TextSprite& label = Label(world.label);
            const float emphasis = world.current ? 1.0f : 0.65f;
            DrawSprite(label, at.x - label.width * 0.5f, at.y,
                       frame.labelAlpha * std::min(1.0f, world.scale) * std::min(1.0f, emphasis + 0.35f * world.hover),
                       labelColor);
        }
        if (frame.plusRadius > 0.05f &&
            ProjectToScreen(camera, {frame.plusCenter.x, frame.plusCenter.y - kLabelDrop, frame.plusCenter.z}, width,
                            height, at)) {
            DrawSprite(labelPlus_, at.x - labelPlus_.width * 0.5f, at.y,
                       frame.labelAlpha * (0.65f + 0.35f * frame.hoverPlus), labelColor);
        }
        const TextSprite& hint = frame.hintText.empty() ? hints_[int(frame.hint)] : HintSprite(frame.hintText);
        DrawSprite(hint, (float(width) - hint.width) * 0.5f, float(height) - hint.height - height * 0.05f,
                   frame.labelAlpha * 0.8f, {kBoneDim.x, kBoneDim.y, kBoneDim.z});

        // The remove badge: a small disc with a cross off the hovered world's shoulder; crimson once hot or armed.
        XMFLOAT2 badge;
        float badgeRadius = 0.0f;
        if (frame.badgeWorld >= 0 && frame.badgeWorld < int(frame.worlds.size()) &&
            BadgeOnScreen(camera, frame.worlds[frame.badgeWorld], width, height, badge, badgeRadius)) {
            const bool lit = frame.badgeHot || frame.badgeArmed;
            const float pulse = frame.badgeArmed ? 0.75f + 0.25f * sinf(frame.time * 10.0f) : 1.0f;
            ObjectConstants object = {};
            const float extent = badgeRadius / 0.62f;  // the shader's disc fills 62% of the quad
            object.rect = {badge.x - extent, badge.y - extent, extent * 2.0f, extent * 2.0f};
            object.tint = lit ? XMFLOAT4(kFang.x, kFang.y, kFang.z, 1.0f) : XMFLOAT4(0.16f, 0.15f, 0.22f, 1.0f);
            object.alpha = frame.labelAlpha * pulse;
            Upload(ctx, objectCB_.Get(), object);
            ctx->PSSetShader(badgePS_.Get(), nullptr, 0);
            ctx->Draw(4, 0);
        }
    }

    ID3D11ShaderResourceView* noViews[3] = {};
    ctx->PSSetShaderResources(0, 3, noViews);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (sampleCount_ > 1) {
        ctx->ResolveSubresource(target, 0, color_.Get(), 0, kColorFormat);
    } else {
        ctx->CopyResource(target, color_.Get());
    }
    return true;
}

bool Renderer::Present(const FrameDesc& frame) {
    ComPtr<ID3D11Texture2D> backBuffer;
    CHECK_HR(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)));
    if (!Render(frame, backBuffer.Get(), swapWidth_, swapHeight_)) return false;
    backBuffer.Reset();
    CHECK_HR(swapChain_->Present(1, 0));
    return true;
}

bool Renderer::ReadPixels(ID3D11Texture2D* texture, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = desc.Width;
    stagingDesc.Height = desc.Height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    CHECK_HR(device_->CreateTexture2D(&stagingDesc, nullptr, &staging));
    context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped;
    CHECK_HR(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    width = desc.Width;
    height = desc.Height;
    pixels.resize(size_t(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        memcpy(&pixels[size_t(y) * width * 4], static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
               size_t(width) * 4);
    }
    context_->Unmap(staging.Get(), 0);
    return true;
}
