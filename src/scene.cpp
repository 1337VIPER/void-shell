#include "scene.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

constexpr float kOpenSeconds = 0.85f;
constexpr float kCloseSeconds = 0.65f;
constexpr float kBirthSeconds = 0.5f;
// A zoom into another desktop waits here, still well zoomed out, until that desktop's fresh picture arrives.
constexpr float kHoldProgress = 0.3f;
constexpr float kArmSeconds = 3.0f;      // how long a remove badge stays armed after the first click
constexpr float kDragThreshold = 8.0f;   // pixels the mouse must move before a press becomes a drag

// The void's layout: one slot per desktop and a last one for the plus world, in rows of five.
constexpr float kSlotSpacing = 3.0f;
constexpr float kRowSpacing = 3.4f;
constexpr int kSlotsPerRow = 5;
constexpr float kMinOverviewDistance = 6.4f;

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

float SmoothStep(float edge0, float edge1, float x) {
    const float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(2.0f - 2.0f * t, 3.0f) * 0.5f;
}

// Overshoots a little before settling, for worlds popping in.
float EaseOutBack(float t) {
    t = Clamp01(t);
    const float c1 = 1.70158f;
    const float u = t - 1.0f;
    return 1.0f + (c1 + 1.0f) * u * u * u + c1 * u * u;
}

XMFLOAT3 Add(XMFLOAT3 a, XMFLOAT3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XMFLOAT3 Sub(XMFLOAT3 a, XMFLOAT3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

XMFLOAT3 Lerp(XMFLOAT3 a, XMFLOAT3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Frame-rate independent exponential approach.
float Approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - expf(-rate * dt));
}

XMFLOAT3 Approach(XMFLOAT3 current, XMFLOAT3 target, float rate, float dt) {
    return Lerp(current, target, 1.0f - expf(-rate * dt));
}

bool RayHitsSphere(FXMVECTOR origin, FXMVECTOR dir, XMFLOAT3 center, float radius) {
    const XMVECTOR oc = origin - XMLoadFloat3(&center);
    const float b = XMVectorGetX(XMVector3Dot(oc, dir));
    const float c = XMVectorGetX(XMVector3Dot(oc, oc)) - radius * radius;
    const float disc = b * b - c;
    return disc >= 0.0f && (-b + sqrtf(disc)) > 0.0f;
}

void MouseRay(const Camera& camera, float mouseX, float mouseY, UINT width, UINT height, XMVECTOR& origin,
              XMVECTOR& dir) {
    const float ndcX = (mouseX + 0.5f) / float(width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (mouseY + 0.5f) / float(height) * 2.0f;
    origin = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), camera.invViewProj);
    const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), camera.invViewProj);
    dir = XMVector3Normalize(farPoint - origin);
}

}  // namespace

Camera BuildCamera(const FrameDesc& frame, float aspect) {
    const XMVECTOR eye = XMLoadFloat3(&frame.eye);
    const XMVECTOR target = XMLoadFloat3(&frame.target);
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMMATRIX view = XMMatrixLookAtLH(eye, target, worldUp);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(frame.fovY, aspect, 0.05f, 200.0f);

    Camera camera;
    camera.viewProj = XMMatrixMultiply(view, proj);
    camera.invViewProj = XMMatrixInverse(nullptr, camera.viewProj);
    const XMVECTOR forward = XMVector3Normalize(target - eye);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMStoreFloat3(&camera.eye, eye);
    XMStoreFloat3(&camera.right, right);
    XMStoreFloat3(&camera.up, XMVector3Cross(forward, right));
    return camera;
}

XMFLOAT3 WorldCenter(const WorldDesc& world) {
    return {world.offset.x, world.offset.y, world.offset.z + world.scale};
}

bool ProjectToScreen(const Camera& camera, XMFLOAT3 point, UINT width, UINT height, XMFLOAT2& out) {
    const XMVECTOR clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1.0f), camera.viewProj);
    const float w = XMVectorGetW(clip);
    if (w <= 0.0001f) return false;
    out.x = (XMVectorGetX(clip) / w * 0.5f + 0.5f) * float(width);
    out.y = (0.5f - XMVectorGetY(clip) / w * 0.5f) * float(height);
    return true;
}

bool BadgeOnScreen(const Camera& camera, const WorldDesc& world, UINT width, UINT height, XMFLOAT2& center,
                   float& radius) {
    const XMFLOAT3 c = WorldCenter(world);
    const XMFLOAT3 top = {c.x + camera.up.x * world.scale, c.y + camera.up.y * world.scale,
                          c.z + camera.up.z * world.scale};
    XMFLOAT2 middle, edge;
    if (!ProjectToScreen(camera, c, width, height, middle) || !ProjectToScreen(camera, top, width, height, edge))
        return false;
    const float r = sqrtf((edge.x - middle.x) * (edge.x - middle.x) + (edge.y - middle.y) * (edge.y - middle.y));
    center = {middle.x + r * 0.74f, middle.y - r * 0.74f};
    radius = std::max(height / 90.0f, r * 0.14f);
    return true;
}

void VoidScene::SetDesktops(const std::vector<DesktopInfo>& desktops, const GUID& current, bool snap) {
    const GUID focusedId = focused_ >= 0 && focused_ < int(worlds_.size()) ? worlds_[focused_].id : current;
    std::vector<World> next;
    next.reserve(desktops.size());
    for (size_t i = 0; i < desktops.size(); ++i) {
        World world;
        const int existing = IndexOf(desktops[i].id);
        if (existing >= 0 && !snap) {
            world = worlds_[existing];
        } else if (!snap) {
            world.center = plusCenter_;  // born out of the plus world
            world.birth = 0.0f;
        }
        world.id = desktops[i].id;
        world.label = desktops[i].name.empty() ? L"Desktop " + std::to_wstring(i + 1) : desktops[i].name;
        world.current = desktops[i].id == current;
        next.push_back(world);
    }
    worlds_ = std::move(next);
    focused_ = IndexOf(focusedId);
    if (focused_ < 0) focused_ = CurrentIndex();
    // Indexes may have shifted: forget anything that pointed at a world by position.
    armed_ = badgeOwner_ = pressed_ = -1;
    dragging_ = false;
    Layout(snap);
}

XMFLOAT3 VoidScene::SlotCenter(int slot, int slots) const {
    const int rows = (slots + kSlotsPerRow - 1) / kSlotsPerRow;
    const int row = slot / kSlotsPerRow;
    const int inRow = std::min(kSlotsPerRow, slots - row * kSlotsPerRow);
    const int column = slot % kSlotsPerRow;
    return XMFLOAT3((column - (inRow - 1) * 0.5f) * kSlotSpacing, ((rows - 1) * 0.5f - row) * kRowSpacing, 1.0f);
}

void VoidScene::Layout(bool snap) {
    const int slots = int(worlds_.size()) + 1;  // the last slot is the plus world
    const int rows = (slots + kSlotsPerRow - 1) / kSlotsPerRow;
    for (size_t i = 0; i < worlds_.size(); ++i) {
        worlds_[i].target = SlotCenter(int(i), slots);
        if (snap) worlds_[i].center = worlds_[i].target;
    }
    plusTarget_ = SlotCenter(int(worlds_.size()), slots);
    if (snap) plusCenter_ = plusTarget_;

    // Pull back far enough to fit every world and its label.
    const float halfWidth = (std::min(slots, kSlotsPerRow) - 1) * 0.5f * kSlotSpacing + 2.6f;
    const float halfHeight = (rows - 1) * 0.5f * kRowSpacing + 2.0f;
    const float tanHalf = tanf(kFovY * 0.5f);
    overviewDistanceGoal_ = std::max({kMinOverviewDistance, halfWidth / (tanHalf * aspect_), halfHeight / tanHalf});
    if (snap) overviewDistance_ = overviewDistanceGoal_;
}

void VoidScene::Open() {
    if (phase_ == VoidPhase::Hidden) {
        progress_ = 0.0f;
        focused_ = CurrentIndex();
        waitingForSnapshot_ = false;
        hoverPlus_ = pulsePlus_ = hintTimer_ = 0.0f;
        swayX_ = swayY_ = 0.0f;
        armed_ = badgeOwner_ = pressed_ = -1;
        dragging_ = badgeHot_ = false;
        for (World& world : worlds_) world.hover = 0.0f;
    }
    if (phase_ != VoidPhase::Open) phase_ = VoidPhase::Opening;
}

void VoidScene::Enter(int index, bool waitForSnapshot) {
    if (index < 0 || index >= int(worlds_.size())) return;
    // Changing focus at full progress is invisible: the whole scene and the camera shift together.
    focused_ = index;
    waitingForSnapshot_ = waitForSnapshot;
    armed_ = badgeOwner_ = pressed_ = -1;
    dragging_ = false;
    phase_ = VoidPhase::Closing;
}

void VoidScene::Hide() {
    phase_ = VoidPhase::Hidden;
    progress_ = 0.0f;
    waitingForSnapshot_ = false;
}

void VoidScene::ShowHint(Hint hint, float seconds) {
    hint_ = hint;
    hintText_.clear();
    hintTimer_ = seconds;
}

void VoidScene::ShowHintText(const std::wstring& text, float seconds) {
    hint_ = Hint::Default;
    hintText_ = text;
    hintTimer_ = seconds;
}

void VoidScene::ArmRemove(int index, const std::wstring& hint) {
    armed_ = index;
    armTimer_ = kArmSeconds;
    badgeOwner_ = index;
    ShowHintText(hint, kArmSeconds);
}

bool VoidScene::BadgeAllowed(int index) const {
    return canRearrange_ && phase_ == VoidPhase::Open && !dragging_ && worlds_.size() > 1 && index >= 0 &&
           index < int(worlds_.size()) && !worlds_[index].current;
}

void VoidScene::Update(float dt, float mouseX, float mouseY, UINT width, UINT height) {
    time_ += dt;
    if (phase_ == VoidPhase::Opening) {
        progress_ += dt / kOpenSeconds;
        if (progress_ >= 1.0f) {
            progress_ = 1.0f;
            phase_ = VoidPhase::Open;
        }
    } else if (phase_ == VoidPhase::Closing) {
        progress_ -= dt / kCloseSeconds;
        if (waitingForSnapshot_) progress_ = std::max(progress_, kHoldProgress);
        if (progress_ <= 0.0f) {
            progress_ = 0.0f;
            phase_ = VoidPhase::Hidden;
        }
    }

    // The camera drifts a little toward the mouse, which gives the stars some parallax.
    const float nx = std::clamp(mouseX / float(width) * 2.0f - 1.0f, -1.0f, 1.0f);
    const float ny = std::clamp(1.0f - mouseY / float(height) * 2.0f, -1.0f, 1.0f);
    swayX_ = Approach(swayX_, nx, 2.5f, dt);
    swayY_ = Approach(swayY_, ny, 2.5f, dt);

    // A press on a world that moves far enough becomes a drag.
    if (pressed_ >= 0 && !dragging_ && canRearrange_ && worlds_.size() > 1 && phase_ == VoidPhase::Open &&
        hypotf(mouseX - pressX_, mouseY - pressY_) > kDragThreshold * float(height) / 1080.0f) {
        dragging_ = true;
        dragFrom_ = pressed_;
        armed_ = -1;
    }
    if (dragging_) Drag(mouseX, mouseY, width, height);

    for (size_t i = 0; i < worlds_.size(); ++i) {
        World& world = worlds_[i];
        if (!(dragging_ && int(i) == pressed_)) world.center = Approach(world.center, world.target, 6.0f, dt);
        world.birth = std::min(1.0f, world.birth + dt / kBirthSeconds);
    }
    plusCenter_ = Approach(plusCenter_, plusTarget_, 6.0f, dt);
    overviewDistance_ = Approach(overviewDistance_, overviewDistanceGoal_, 4.0f, dt);

    const Hit over = phase_ == VoidPhase::Open && !dragging_ ? Pick(mouseX, mouseY, width, height) : Hit();
    badgeHot_ = over.kind == Hit::Kind::Badge;
    // The badge follows the hovered world, and stays while the mouse moves from the ball onto it.
    if (over.kind == Hit::Kind::World && BadgeAllowed(over.index)) {
        badgeOwner_ = over.index;
    } else if (!badgeHot_) {
        badgeOwner_ = armed_;
    }
    for (size_t i = 0; i < worlds_.size(); ++i) {
        const bool hot = (over.kind == Hit::Kind::World || over.kind == Hit::Kind::Badge) && over.index == int(i);
        worlds_[i].hover = Approach(worlds_[i].hover, hot || (dragging_ && int(i) == pressed_) ? 1.0f : 0.0f, 12.0f, dt);
    }
    hoverPlus_ = Approach(hoverPlus_, over.kind == Hit::Kind::Plus ? 1.0f : 0.0f, 12.0f, dt);
    pulsePlus_ *= expf(-4.0f * dt);
    hintTimer_ = std::max(0.0f, hintTimer_ - dt);
    if (armed_ >= 0) {
        armTimer_ -= dt;
        if (armTimer_ <= 0.0f) armed_ = -1;
    }
}

// The dragged world follows the mouse across the plane of the worlds, and the others make room for it.
void VoidScene::Drag(float mouseX, float mouseY, UINT width, UINT height) {
    const FrameDesc frame = Frame();
    const Camera camera = BuildCamera(frame, float(width) / float(height));
    XMVECTOR origin, dir;
    MouseRay(camera, mouseX, mouseY, width, height, origin, dir);
    const float dz = XMVectorGetZ(dir);
    if (fabsf(dz) < 1e-4f) return;
    const float t = (1.0f - XMVectorGetZ(origin)) / dz;  // the worlds' centres all sit at z = 1
    if (t <= 0.0f) return;
    // Frame() places everything relative to the focused world; undo that to get layout space.
    const XMFLOAT3 shift = Sub(worlds_[focused_].center, {0.0f, 0.0f, 1.0f});
    XMFLOAT3 point;
    XMStoreFloat3(&point, origin + dir * t);
    point = Add(point, shift);
    point.z = 1.0f;
    worlds_[pressed_].center = point;

    const int slots = int(worlds_.size()) + 1;
    int nearest = pressed_;
    float best = 1e9f;
    for (int slot = 0; slot < int(worlds_.size()); ++slot) {
        const XMFLOAT3 c = SlotCenter(slot, slots);
        const float d = (c.x - point.x) * (c.x - point.x) + (c.y - point.y) * (c.y - point.y);
        if (d < best) {
            best = d;
            nearest = slot;
        }
    }
    if (nearest == pressed_) return;
    const GUID focusedId = worlds_[focused_].id;
    World moved = worlds_[pressed_];
    worlds_.erase(worlds_.begin() + pressed_);
    worlds_.insert(worlds_.begin() + nearest, moved);
    pressed_ = nearest;
    focused_ = IndexOf(focusedId);
    Layout(false);
}

void VoidScene::PointerDown(float mouseX, float mouseY, UINT width, UINT height) {
    pressed_ = -1;
    if (phase_ != VoidPhase::Open) return;
    const Hit hit = Pick(mouseX, mouseY, width, height);
    if (hit.kind != Hit::Kind::World) return;
    pressed_ = hit.index;
    pressX_ = mouseX;
    pressY_ = mouseY;
}

SceneAction VoidScene::PointerUp(float mouseX, float mouseY, UINT width, UINT height) {
    SceneAction action;
    if (dragging_) {
        dragging_ = false;
        if (pressed_ != dragFrom_) {
            action.type = SceneAction::Type::Reorder;
            action.index = dragFrom_;
            action.to = pressed_;
        }
        pressed_ = -1;
        Layout(false);
        return action;
    }
    pressed_ = -1;
    if (phase_ != VoidPhase::Open) return action;
    const Hit hit = Pick(mouseX, mouseY, width, height);
    if (hit.kind == Hit::Kind::World) {
        action.type = SceneAction::Type::Enter;
        action.index = hit.index;
    } else if (hit.kind == Hit::Kind::Badge) {
        action.type = SceneAction::Type::Remove;
        action.index = hit.index;
    } else if (hit.kind == Hit::Kind::Plus) {
        action.type = SceneAction::Type::Create;
    }
    if (hit.kind != Hit::Kind::Badge) armed_ = -1;  // clicking anything else cancels a pending remove
    return action;
}

FrameDesc VoidScene::Frame() const {
    const float p = progress_;
    const float cam = EaseInOutCubic(p);
    const float settle = SmoothStep(0.55f, 1.0f, p);
    const float startDistance = 1.0f / tanf(kFovY * 0.5f);

    FrameDesc f = {};
    f.fovY = kFovY;
    f.time = time_;
    f.eye = {0.0f, 0.0f, -startDistance};
    f.badgeWorld = -1;
    if (worlds_.empty()) return f;

    // Everything is placed relative to the focused world, whose flat desktop sits at the origin. At progress 0
    // every term below reduces exactly to the starting view of that desktop, which is what makes the first and
    // last frames of the overlay identical to the real desktop underneath.
    const XMFLOAT3 origin = Sub(worlds_[focused_].center, {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 overviewTarget = Sub({0.0f, 0.0f, 1.0f}, origin);
    const XMFLOAT3 overviewEye =
        Add(overviewTarget, {swayX_ * 0.45f, overviewDistance_ * 0.078f + swayY_ * 0.3f, -overviewDistance_});
    f.eye = Lerp(f.eye, overviewEye, cam);
    f.target = Lerp({0.0f, 0.0f, 0.0f}, overviewTarget, cam);

    for (size_t i = 0; i < worlds_.size(); ++i) {
        const World& world = worlds_[i];
        const bool focused = int(i) == focused_;
        // Other worlds pop in late while zooming out (and shrink away while zooming in).
        const float intro = focused ? 1.0f : EaseOutBack((p - 0.35f) / 0.6f);
        const bool held = dragging_ && int(i) == pressed_;

        WorldDesc w;
        w.id = world.id;
        w.label = world.label;
        w.current = world.current;
        w.morph = focused ? EaseInOutCubic((p - 0.04f) / 0.72f) : 1.0f;
        w.hover = world.hover * settle;
        w.scale = intro * EaseOutBack(world.birth) * (1.0f + 0.04f * w.hover + (held ? 0.06f : 0.0f));
        XMFLOAT3 center = Sub(world.center, origin);
        if (!held) center.y += sinf(time_ * 0.9f + float(i) * 1.3f) * 0.045f * settle;
        w.offset = {center.x, center.y, center.z - w.scale};
        f.worlds.push_back(std::move(w));
    }

    f.plusCenter = Sub(plusCenter_, origin);
    f.plusCenter.y += sinf(time_ * 0.9f + 1.7f) * 0.05f * settle;
    f.plusRadius = kPlusRadius * EaseOutBack((p - 0.45f) / 0.55f) * (1.0f + 0.08f * hoverPlus_ + 0.12f * pulsePlus_);
    f.hoverPlus = hoverPlus_ * settle;
    f.pulsePlus = pulsePlus_;
    f.voidAmount = settle;
    f.labelAlpha = SmoothStep(0.8f, 1.0f, p);
    f.hint = hintTimer_ > 0.0f ? hint_ : Hint::Default;
    if (hintTimer_ > 0.0f) f.hintText = hintText_;
    if (dragging_) f.hintText = L"Drop to move this desktop";
    f.badgeWorld = BadgeAllowed(badgeOwner_) ? badgeOwner_ : -1;
    f.badgeHot = badgeHot_;
    f.badgeArmed = f.badgeWorld >= 0 && f.badgeWorld == armed_;
    return f;
}

int VoidScene::IndexOf(const GUID& id) const {
    for (size_t i = 0; i < worlds_.size(); ++i)
        if (worlds_[i].id == id) return int(i);
    return -1;
}

int VoidScene::CurrentIndex() const {
    for (size_t i = 0; i < worlds_.size(); ++i)
        if (worlds_[i].current) return int(i);
    return 0;
}

void VoidScene::SetProgressForTest(float progress) {
    progress_ = Clamp01(progress);
    if (phase_ == VoidPhase::Hidden || phase_ == VoidPhase::Opening || phase_ == VoidPhase::Open)
        phase_ = progress_ >= 1.0f ? VoidPhase::Open : VoidPhase::Opening;
}

void VoidScene::SetHoverForTest(int world, float plus, float pulse) {
    for (size_t i = 0; i < worlds_.size(); ++i) worlds_[i].hover = int(i) == world ? 1.0f : 0.0f;
    hoverPlus_ = plus;
    pulsePlus_ = pulse;
}

void VoidScene::SetBadgeForTest(int world, bool armed) {
    badgeOwner_ = world;
    armed_ = armed ? world : -1;
    armTimer_ = kArmSeconds;
}

VoidScene::Hit VoidScene::Pick(float mouseX, float mouseY, UINT width, UINT height) const {
    Hit hit;
    const FrameDesc frame = Frame();
    const Camera camera = BuildCamera(frame, float(width) / float(height));
    // The badge sits just outside its ball, so check it first.
    if (frame.badgeWorld >= 0) {
        XMFLOAT2 center;
        float radius = 0.0f;
        if (BadgeOnScreen(camera, frame.worlds[frame.badgeWorld], width, height, center, radius) &&
            hypotf(mouseX - center.x, mouseY - center.y) <= radius * 1.25f) {
            hit.kind = Hit::Kind::Badge;
            hit.index = frame.badgeWorld;
            return hit;
        }
    }
    XMVECTOR origin, dir;
    MouseRay(camera, mouseX, mouseY, width, height, origin, dir);
    for (size_t i = 0; i < frame.worlds.size(); ++i) {
        const WorldDesc& world = frame.worlds[i];
        if (world.scale > 0.05f && RayHitsSphere(origin, dir, WorldCenter(world), world.scale * 1.05f)) {
            hit.kind = Hit::Kind::World;
            hit.index = int(i);
            return hit;
        }
    }
    if (frame.plusRadius > 0.05f && RayHitsSphere(origin, dir, frame.plusCenter, kPlusRadius * 1.15f))
        hit.kind = Hit::Kind::Plus;
    return hit;
}
