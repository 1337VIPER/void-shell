#pragma once

#include <string>
#include <vector>

#include "common.h"
#include "desktops.h"

// Layout numbers shared by the scene, the renderer and the shaders' inputs.
constexpr float kFovY = 0.8726646f;     // 50 degrees vertical
constexpr float kWorldLatSpan = 0.80f;  // half the desktop's height as an angle on the ball, in radians
constexpr float kPlusRadius = 0.55f;

// Half the desktop's width as an angle on the ball; keeps the desktop's aspect near the ball's equator.
inline float WorldLonSpan(float aspect) {
    const float span = kWorldLatSpan * aspect;
    return span < 1.5f ? span : 1.5f;
}

// One desktop world as drawn this frame. Its ball centre is offset + (0, 0, scale). At morph 0 it is instead a
// flat quad of the desktop exactly filling the starting view.
struct WorldDesc {
    GUID id;
    std::wstring label;
    DirectX::XMFLOAT3 offset;
    float scale;
    float morph;
    float hover;
    bool current;
};

enum class Hint { Default = 0, SwitchFailed = 1, CreateFailed = 2 };

// Everything the renderer needs to draw one frame.
struct FrameDesc {
    DirectX::XMFLOAT3 eye;
    DirectX::XMFLOAT3 target;
    float fovY;
    std::vector<WorldDesc> worlds;
    DirectX::XMFLOAT3 plusCenter;
    float plusRadius;
    float hoverPlus;
    float pulsePlus;
    float voidAmount;  // 0 on the desktop, 1 once the void is fully open
    float labelAlpha;
    Hint hint;
    std::wstring hintText;  // overrides `hint` when set
    int badgeWorld;         // the world showing a remove badge, or -1
    bool badgeHot;          // the mouse is on the badge
    bool badgeArmed;        // clicked once: the next click removes
    float time;
};

struct Camera {
    DirectX::XMMATRIX viewProj;
    DirectX::XMMATRIX invViewProj;
    DirectX::XMFLOAT3 eye;
    DirectX::XMFLOAT3 right;
    DirectX::XMFLOAT3 up;
};

Camera BuildCamera(const FrameDesc& frame, float aspect);
DirectX::XMFLOAT3 WorldCenter(const WorldDesc& world);
bool ProjectToScreen(const Camera& camera, DirectX::XMFLOAT3 point, UINT width, UINT height,
                     DirectX::XMFLOAT2& out);
// Where a world's remove badge sits on screen: just off the ball's top-right edge. Pixels.
bool BadgeOnScreen(const Camera& camera, const WorldDesc& world, UINT width, UINT height, DirectX::XMFLOAT2& center,
                   float& radius);

enum class VoidPhase { Hidden, Opening, Open, Closing };

struct SceneAction {
    enum class Type { None, Enter, Create, Remove, Reorder };
    Type type = Type::None;
    int index = -1;  // the world entered or removed; for Reorder, where the dragged world started
    int to = -1;     // Reorder: where it was dropped
};

// One world per virtual desktop plus the plus world, the zoom between a desktop and the void, hover, clicks and
// dragging worlds to reorder them. Progress runs 0 (a desktop filling the screen) to 1 (the void). Opening zooms
// out of the current desktop; entering zooms into any world, which then becomes the one at progress 0.
class VoidScene {
public:
    // Replaces the worlds. Known desktops keep their place; new ones are born out of the plus world.
    // `snap` places everything at once instead of animating.
    void SetDesktops(const std::vector<DesktopInfo>& desktops, const GUID& current, bool snap);
    void SetAspect(float aspect) { aspect_ = aspect; }
    // Whether desktops can be removed and reordered here (needs the shell's desktop API).
    void SetCanRearrange(bool can) { canRearrange_ = can; }
    void Open();
    // Zooms into a world. With waitForSnapshot the zoom pauses part-way until SnapshotReady is called.
    void Enter(int index, bool waitForSnapshot);
    void SnapshotReady() { waitingForSnapshot_ = false; }
    void Hide();
    void Update(float dt, float mouseX, float mouseY, UINT width, UINT height);
    void PointerDown(float mouseX, float mouseY, UINT width, UINT height);
    SceneAction PointerUp(float mouseX, float mouseY, UINT width, UINT height);
    bool Dragging() const { return dragging_; }
    // The two-click remove: the first click arms a world's badge for a few seconds.
    void ArmRemove(int index, const std::wstring& hint);
    int ArmedRemove() const { return armed_; }
    void PulsePlus() { pulsePlus_ = 1.0f; }
    void ShowHint(Hint hint, float seconds);
    void ShowHintText(const std::wstring& text, float seconds);
    FrameDesc Frame() const;

    VoidPhase Phase() const { return phase_; }
    float Progress() const { return progress_; }
    int WorldCount() const { return int(worlds_.size()); }
    GUID WorldId(int index) const { return worlds_[index].id; }
    std::wstring WorldLabel(int index) const { return worlds_[index].label; }
    int IndexOf(const GUID& id) const;
    int CurrentIndex() const;

    // Test hooks: pin the animation and hover state for headless renders.
    void SetProgressForTest(float progress);
    void SetHoverForTest(int world, float plus, float pulse);
    void SetBadgeForTest(int world, bool armed);

private:
    struct Hit {
        enum class Kind { None, World, Plus, Badge };
        Kind kind = Kind::None;
        int index = -1;
    };

    struct World {
        GUID id = {};
        std::wstring label;
        DirectX::XMFLOAT3 center = {};  // ball centre in layout space (animated)
        DirectX::XMFLOAT3 target = {};  // where the layout wants it
        float birth = 1.0f;             // 0..1 while popping out of the plus world
        float hover = 0.0f;
        bool current = false;
    };

    DirectX::XMFLOAT3 SlotCenter(int slot, int slots) const;
    void Layout(bool snap);
    Hit Pick(float mouseX, float mouseY, UINT width, UINT height) const;
    bool BadgeAllowed(int index) const;
    void Drag(float mouseX, float mouseY, UINT width, UINT height);

    std::vector<World> worlds_;
    int focused_ = 0;  // the world that is flat at progress 0
    DirectX::XMFLOAT3 plusCenter_ = {};
    DirectX::XMFLOAT3 plusTarget_ = {};
    float overviewDistance_ = 6.4f;
    float overviewDistanceGoal_ = 6.4f;
    float aspect_ = 16.0f / 9.0f;
    bool canRearrange_ = false;

    VoidPhase phase_ = VoidPhase::Hidden;
    float progress_ = 0.0f;
    bool waitingForSnapshot_ = false;
    float time_ = 0.0f;
    float hoverPlus_ = 0.0f;
    float pulsePlus_ = 0.0f;
    Hint hint_ = Hint::Default;
    std::wstring hintText_;
    float hintTimer_ = 0.0f;
    float swayX_ = 0.0f;
    float swayY_ = 0.0f;

    // Remove badge and dragging.
    int badgeOwner_ = -1;
    bool badgeHot_ = false;
    int armed_ = -1;
    float armTimer_ = 0.0f;
    int pressed_ = -1;  // world under the mouse when the button went down
    float pressX_ = 0.0f;
    float pressY_ = 0.0f;
    bool dragging_ = false;
    int dragFrom_ = -1;
};
