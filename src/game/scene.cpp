// Blockfall - turns game state into a list of mesh instances for the renderer
#include "scene.h"
#include "charanim.h"
#include "model.h"
#include "skins.h"
#include <cmath>
#include <unordered_map>

namespace bf {

namespace {


const Vec4 WOOD(0.72f, 0.52f, 0.31f, 1);
const Vec4 GUN_DARK(0.16f, 0.17f, 0.19f, 1);
const Vec4 STEEL(0.62f, 0.65f, 0.70f, 1);
const Vec4 HANDLE(0.45f, 0.30f, 0.18f, 1);

Vec4 skinTone(int i) {
    static const Vec4 tones[] = {{0.96f, 0.80f, 0.66f, 1}, {0.86f, 0.66f, 0.50f, 1}, {0.68f, 0.48f, 0.34f, 1},
                                 {0.50f, 0.34f, 0.24f, 1}, {0.93f, 0.74f, 0.58f, 1}};
    return tones[((i % 5) + 5) % 5];
}

Instance inst(const Mat4& m, const Vec4& c, float glow = 0, int mat = MAT_DEFAULT) {
    return {m, c, Vec4(glow, (float)mat, 0, 0)};
}

void addBox(std::vector<Instance>& list, const AABB& b, const Vec4& c, float glow = 0, int mat = MAT_DEFAULT) {
    list.push_back(inst(Mat4::scale(b.size()) * Mat4::translation(b.center()), c, glow, mat));
}

// Box stretched between two points (used for bullet tracers).
void addBeam(std::vector<Instance>& list, const Vec3& a, const Vec3& b, float thick, const Vec4& c) {
    Vec3 d = b - a;
    float len = length(d);
    if (len < 1e-3f) return;
    Vec3 z = d / len;
    Vec3 up = std::fabs(z.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 x = normalize(cross(up, z));
    Vec3 y = cross(z, x);
    Mat4 r = Mat4::identity();
    r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z;
    r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z;
    r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z;
    list.push_back(inst(Mat4::scale(Vec3(thick, thick, len)) * r * Mat4::translation((a + b) * 0.5f), c, 1.0f));
}

// A gun built from boxes/cylinders to match its hotbar icon. Local +z is the barrel direction,
// the origin is at the trigger, y is up.
void addGunModel(SceneList& s, const Mat4& root, const Item& gun) {
    std::vector<Instance>& C = s.opaque[MESH_CUBE];
    std::vector<Instance>& Y = s.opaque[MESH_CYLINDER];
    Vec4 accent = rarityColor(gun.rarity);
    auto box = [&](float sx, float sy, float sz, float x, float y, float z, const Vec4& c, float glow = 0) {
        C.push_back(inst(Mat4::scale(Vec3(sx, sy, sz)) * Mat4::translation(Vec3(x, y, z)) * root, c, glow, MAT_METAL));
    };
    auto tube = [&](float r, float len, float x, float y, float z, const Vec4& c) {  // along z
        Y.push_back(inst(Mat4::scale(Vec3(r * 2, len, r * 2)) * Mat4::rotationX(PI * 0.5f) * Mat4::translation(Vec3(x, y, z)) * root, c, 0, MAT_METAL));
    };
    switch (gun.gun) {
        case GunType::AssaultRifle:
            box(0.08f, 0.1f, 0.26f, 0, 0.09f, -0.3f, GUN_DARK);            // stock
            box(0.1f, 0.14f, 0.42f, 0, 0.12f, 0.02f, GUN_DARK);            // body
            box(0.1f, 0.03f, 0.22f, 0, 0.21f, 0.05f, accent, 0.4f);        // rail
            box(0.09f, 0.1f, 0.24f, 0, 0.11f, 0.35f, GUN_DARK);            // handguard
            tube(0.02f, 0.3f, 0, 0.13f, 0.6f, STEEL);                      // barrel
            box(0.07f, 0.2f, 0.07f, 0, -0.03f, 0.06f, GUN_DARK);           // curved magazine
            box(0.06f, 0.14f, 0.06f, 0, -0.02f, -0.1f, GUN_DARK);          // grip
            break;
        case GunType::Shotgun:
            box(0.08f, 0.12f, 0.28f, 0, 0.08f, -0.32f, HANDLE);            // wooden stock
            box(0.1f, 0.13f, 0.3f, 0, 0.12f, -0.02f, GUN_DARK);            // receiver
            tube(0.025f, 0.55f, 0, 0.17f, 0.4f, STEEL);                    // barrel
            tube(0.02f, 0.45f, 0, 0.1f, 0.35f, GUN_DARK);                  // tube
            box(0.1f, 0.08f, 0.16f, 0, 0.1f, 0.32f, HANDLE);               // pump
            box(0.06f, 0.14f, 0.06f, 0, -0.02f, -0.1f, GUN_DARK);          // grip
            break;
        case GunType::SMG:
            box(0.06f, 0.03f, 0.22f, 0, 0.16f, -0.28f, STEEL);             // wire stock
            box(0.06f, 0.03f, 0.22f, 0, 0.06f, -0.28f, STEEL);
            box(0.1f, 0.14f, 0.34f, 0, 0.11f, 0.0f, GUN_DARK);             // body
            box(0.05f, 0.05f, 0.08f, 0, 0.21f, 0.0f, accent, 0.4f);        // sight
            tube(0.02f, 0.2f, 0, 0.13f, 0.27f, STEEL);                     // short barrel
            box(0.07f, 0.26f, 0.07f, 0, -0.08f, 0.04f, GUN_DARK);          // long magazine
            box(0.06f, 0.12f, 0.06f, 0, -0.01f, -0.1f, GUN_DARK);          // grip
            break;
        case GunType::Pistol:
            box(0.08f, 0.1f, 0.3f, 0, 0.1f, 0.08f, GUN_DARK);              // slide
            box(0.03f, 0.03f, 0.2f, 0, 0.16f, 0.08f, accent, 0.4f);        // top rail
            box(0.07f, 0.17f, 0.08f, 0, -0.04f, -0.04f, GUN_DARK);         // grip
            box(0.03f, 0.05f, 0.08f, 0, 0.0f, 0.06f, STEEL);               // trigger guard
            break;
        default:  // Sniper
            box(0.08f, 0.12f, 0.34f, 0, 0.08f, -0.36f, HANDLE);            // wooden stock
            box(0.1f, 0.12f, 0.4f, 0, 0.12f, 0.0f, GUN_DARK);              // body
            tube(0.018f, 0.75f, 0, 0.14f, 0.6f, STEEL);                    // long barrel
            tube(0.04f, 0.3f, 0, 0.26f, -0.02f, GUN_DARK);                 // scope tube
            tube(0.05f, 0.05f, 0, 0.26f, 0.15f, GUN_DARK);                 // scope ends
            tube(0.05f, 0.05f, 0, 0.26f, -0.19f, GUN_DARK);
            tube(0.035f, 0.01f, 0, 0.26f, 0.18f, Vec4(0.35f, 0.65f, 1.0f, 1));  // lens
            box(0.03f, 0.06f, 0.04f, 0, 0.2f, 0.06f, accent, 0.4f);        // mounts
            box(0.03f, 0.06f, 0.04f, 0, 0.2f, -0.1f, accent, 0.4f);
            box(0.06f, 0.12f, 0.08f, 0, -0.02f, -0.12f, GUN_DARK);         // grip
            box(0.06f, 0.1f, 0.08f, 0, -0.02f, 0.06f, GUN_DARK);           // magazine
            break;
    }
}

Vec4 mixColor(const Vec4& a, const Vec4& b, float t) {
    return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)};
}

struct FigurePose {
    Vec3 feet;
    float yaw = 0;
    float walkPhase = 0;
    float moveAmount = 0;
    Vec4 shirt;
    int look = 0;            // varies skin tone
    int skin = 0;            // outfit from the locker
    bool teamTint = false;   // teammates get a cyan stripe so you can tell them apart
    const Item* held = nullptr;  // gun or pickaxe in hand
    float aimPitch = 0;
    float hurt = 0;
    Anim anim = Anim::Idle;
    float animT = 0;         // seconds in this animation (idle bob, reload progress...)
    float reloadFrac = 0;    // 0..1 through a reload
    float swing = -1;        // pickaxe swing progress 0..1, or -1
    float shot = 0;          // recoil 0..1
    float verticalVel = 0;   // for the jump pose
    int animKey = -1;        // which character this is (keeps its animation state between frames)
    float clock = 0;         // game time, for blending
    bool lobby = false;
    bool dance = false;
    float eyeDistance = 0;   // for the level of detail
};

// A pickaxe held in a hand: handle along +z, head at the far end. `root` = hand space.
void addPickaxeModel(SceneList& s, const Mat4& root) {
    s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.06f, 0.95f, 0.06f)) * Mat4::rotationX(PI * 0.5f) *
                                              Mat4::translation(Vec3(0, 0, 0.32f)) * root, HANDLE, 0, MAT_WOOD));
    s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(0.1f, 0.75f, 0.13f)) * Mat4::translation(Vec3(0, -0.06f, 0.78f)) * root, STEEL, 0, MAT_METAL));
}

// The rigged, textured character from assets/character.glb. Returns false if it isn't loaded.
bool addSkinnedFigure(SceneList& s, const FigurePose& f) {
    const CharacterModel* m = characterModel();
    if (!m || !m->loaded) return false;
    static std::unordered_map<int, CharAnimState> states;
    CharAnimState& st = states[f.animKey];

    CharAnimInput in;
    in.anim = f.anim;
    in.gun = f.held && f.held->kind == ItemKind::Gun;
    in.pickaxe = f.held && f.held->kind == ItemKind::Pickaxe;
    in.moveAmount = f.moveAmount;
    in.walkPhase = f.walkPhase;
    in.aimPitch = f.aimPitch;
    in.reloadFrac = f.reloadFrac;
    in.swing = f.swing;
    in.shot = clampf(f.shot, 0.0f, 1.0f);
    in.animT = f.animT;
    in.clock = f.clock;
    in.lobby = f.lobby;
    in.dance = f.dance;
    Pose pose;
    if (!evalCharacterPose(*m, st, in, pose)) return false;

    Mat4 world[MAX_BONES];
    m->boneWorld(pose, world);
    Mat4 root = Mat4::rotationY(f.yaw) * Mat4::translation(f.feet);

    CharacterDraw d;
    d.world = root;
    d.tint = Vec4(1, 1, 1, 1);
    d.accent = Vec4(0.3f, 0.9f, 1.0f, 1);
    d.params = Vec4(0, f.hurt, f.teamTint ? 1.0f : 0.0f, 0);
    d.skin = f.skin;
    const Skin& outfit = skinAt(f.skin);
    d.hair = outfit.hairStyle;
    d.hat = outfit.hatStyle;
    d.headband = outfit.headband || f.teamTint;
    d.distance = f.eyeDistance;
    d.boneOffset = (int)s.boneMats.size();
    d.boneCount = (int)m->bones.size();
    Mat4 skinMats[MAX_BONES];
    m->skinMatrices(world, skinMats);
    for (int i = 0; i < d.boneCount; ++i) s.boneMats.push_back(skinMats[i]);
    s.characters.push_back(d);

    // Things held in the right hand follow the Hand_R bone.
    int handR = m->boneIndex("Hand_R");
    if (f.held && handR >= 0) {
        Mat4 handW = world[handR] * root;
        if (in.gun) {
            // The gun points where the character aims; only its position comes from the hand, so
            // it stays level however the arm bends (sprinting, reloading, recoil).
            Vec3 hand = transformPoint(Vec3(0, 0.07f, 0), handW);
            float pitch = (f.anim == Anim::Sprint || f.lobby) ? 0.0f : f.aimPitch;
            Mat4 gunRoot = Mat4::translation(Vec3(0, 0.03f, -0.16f)) * Mat4::rotationX(-pitch) * Mat4::rotationY(f.yaw) * Mat4::translation(hand);
            addGunModel(s, gunRoot, *f.held);
            if (f.anim == Anim::Reload && f.reloadFrac > 0.25f && f.reloadFrac < 0.75f) {
                float t = (f.reloadFrac - 0.25f) / 0.5f;
                s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.06f, 0.18f, 0.08f)) *
                                                       Mat4::translation(Vec3(0, -0.15f - 0.25f * std::sin(t * PI), -0.05f)) * gunRoot,
                                                   GUN_DARK, 0, MAT_METAL));
            }
        } else if (in.pickaxe) {
            // Pickaxe space -> hand space: handle (+z) along hand -z, so it sticks out of the fist.
            Mat4 P = Mat4::identity();
            P.m[1][1] = -1;
            P.m[2][2] = -1;
            addPickaxeModel(s, P * Mat4::translation(Vec3(0, 0.08f, 0)) * handW);
        }
    }
    return true;
}

// A rounded person: capsule-like limbs, oval body, round head. Every action has its own pose.
void addFigure(SceneList& s, const FigurePose& f) {
    if (addSkinnedFigure(s, f)) return;
    std::vector<Instance>& SPH = s.opaque[MESH_SPHERE];
    std::vector<Instance>& CYL = s.opaque[MESH_CYLINDER];
    const Vec4 white(1, 1, 1, 1);
    Vec4 sh = mixColor(f.shirt, white, f.hurt);
    const Skin& outfit = skinAt(f.skin);
    Vec4 pa = mixColor(outfit.pants, white, f.hurt);
    Vec4 sk = mixColor(skinTone(f.look), white, f.hurt);
    Vec4 hair = mixColor(outfit.hair, white, f.hurt);

    bool gun = f.held && f.held->kind == ItemKind::Gun;
    bool pick = f.held && f.held->kind == ItemKind::Pickaxe;
    const bool crouched = f.anim == Anim::Crouch;
    const bool sprint = f.anim == Anim::Sprint;
    const bool jump = f.anim == Anim::Jump;

    // Whole-body lean, bob and leg length depend on the action.
    float lean = 0, bob = 0, legLen = 0.86f;
    float swing = std::sin(f.walkPhase) * 0.65f * f.moveAmount;
    float armSwing = -swing;
    if (sprint) {
        lean = 0.22f;
        swing = std::sin(f.walkPhase) * 0.95f * std::max(0.6f, f.moveAmount);
        armSwing = -swing * 1.1f;
        bob = std::fabs(std::sin(f.walkPhase)) * 0.06f;
    } else if (f.anim == Anim::Walk) {
        bob = std::fabs(std::sin(f.walkPhase)) * 0.03f;
    } else if (crouched) {
        legLen = 0.52f;
        lean = 0.12f;
    } else if (jump) {
        legLen = 0.66f;
        lean = -0.05f;
    } else if (f.anim == Anim::Idle) {
        bob = 0.012f * std::sin(f.animT * 2.2f);
    } else if (f.anim == Anim::Heal) {
        lean = 0.15f;
        legLen = 0.72f;
    }
    Mat4 root = Mat4::rotationX(lean) * Mat4::rotationY(f.yaw) * Mat4::translation(f.feet + Vec3(0, bob, 0));
    float hipY = legLen + 0.05f;

    // Legs: thigh + shin with a knee, swinging when moving, tucked when jumping, bent when crouched.
    for (int side = -1; side <= 1; side += 2) {
        float a = swing * (float)side;
        if (jump) a = f.verticalVel > 0 ? -0.9f + 0.2f * side : -0.35f;
        if (crouched) a = 0.3f;
        float knee = jump ? 1.2f : (crouched ? 1.1f : std::max(0.0f, -a) * 0.8f + 0.08f);
        float thigh = legLen * 0.52f, shin = legLen * 0.48f;
        Mat4 hip = Mat4::rotationX(a) * Mat4::translation(Vec3(0.11f * side, hipY, 0)) * root;
        CYL.push_back(inst(Mat4::scale(Vec3(0.19f, thigh, 0.2f)) * Mat4::translation(Vec3(0, -thigh * 0.5f, 0)) * hip, pa, 0, MAT_CLOTH));
        Mat4 kneeM = Mat4::rotationX(knee) * Mat4::translation(Vec3(0, -thigh, 0)) * hip;
        SPH.push_back(inst(Mat4::scale(Vec3(0.19f, 0.19f, 0.2f)) * kneeM, pa, 0, MAT_CLOTH));
        CYL.push_back(inst(Mat4::scale(Vec3(0.16f, shin, 0.17f)) * Mat4::translation(Vec3(0, -shin * 0.5f, 0)) * kneeM, pa, 0, MAT_CLOTH));
        // Boots: a dark cuff up the shin and a chunky foot.
        CYL.push_back(inst(Mat4::scale(Vec3(0.19f, shin * 0.45f, 0.2f)) * Mat4::translation(Vec3(0, -shin + shin * 0.22f, 0)) * kneeM,
                           Vec4(0.14f, 0.12f, 0.12f, 1), 0, MAT_CLOTH));
        SPH.push_back(inst(Mat4::scale(Vec3(0.21f, 0.15f, 0.34f)) * Mat4::translation(Vec3(0, -shin + 0.03f, 0.07f)) * kneeM,
                           Vec4(0.12f, 0.12f, 0.14f, 1), 0, MAT_CLOTH));
    }
    // Hips, belt, waist, V-shaped chest, shoulder pads, neck, head. Idle breathing swells the chest.
    float breathe = f.anim == Anim::Idle ? 1.0f + 0.02f * std::sin(f.animT * 2.2f) : 1.0f;
    const Vec4 dark(0.1f, 0.1f, 0.12f, 1);
    Vec4 accent = mixColor(outfit.accent, white, f.hurt);
    if (f.teamTint) accent = Vec4(0.3f, 0.9f, 1.0f, 1);  // teammates: cyan trim
    SPH.push_back(inst(Mat4::scale(Vec3(0.38f, 0.22f, 0.26f)) * Mat4::translation(Vec3(0, hipY + 0.02f, 0)) * root, pa, 0, MAT_CLOTH));
    CYL.push_back(inst(Mat4::scale(Vec3(0.4f, 0.07f, 0.28f)) * Mat4::translation(Vec3(0, hipY + 0.13f, 0)) * root, dark, 0, MAT_CLOTH));       // belt
    s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.08f, 0.06f, 0.03f)) * Mat4::translation(Vec3(0, hipY + 0.13f, 0.14f)) * root, STEEL, 0.2f, MAT_METAL));  // buckle
    CYL.push_back(inst(Mat4::scale(Vec3(0.32f, 0.26f, 0.22f)) * Mat4::translation(Vec3(0, hipY + 0.3f, 0)) * root, sh, 0, MAT_CLOTH));        // waist
    CYL.push_back(inst(Mat4::scale(Vec3(0.44f * breathe, 0.3f, 0.27f * breathe)) * Mat4::translation(Vec3(0, hipY + 0.52f, 0)) * root, sh, 0, MAT_CLOTH));  // chest
    SPH.push_back(inst(Mat4::scale(Vec3(0.56f * breathe, 0.26f, 0.3f * breathe)) * Mat4::translation(Vec3(0, hipY + 0.64f, 0)) * root, sh, 0, MAT_CLOTH));  // upper chest, wide
    s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.46f, 0.07f, 0.02f)) * Mat4::translation(Vec3(0, hipY + 0.5f, 0.135f)) * root, accent, 0, MAT_CLOTH));  // chest stripe
    for (int side = -1; side <= 1; side += 2)
        SPH.push_back(inst(Mat4::scale(Vec3(0.22f, 0.16f, 0.22f)) * Mat4::translation(Vec3(0.3f * side, hipY + 0.72f, 0)) * root, accent, 0, MAT_CLOTH));  // shoulder pads
    CYL.push_back(inst(Mat4::scale(Vec3(0.12f, 0.14f, 0.12f)) * Mat4::translation(Vec3(0, hipY + 0.8f, 0)) * root, sk, 0, MAT_SKIN));
    float headY = hipY + 1.02f;
    float headPitch = f.anim == Anim::Heal ? 0.35f : (gun ? -f.aimPitch * 0.5f : 0.0f);
    Mat4 head = Mat4::rotationX(headPitch) * Mat4::translation(Vec3(0, headY, 0)) * root;
    SPH.push_back(inst(Mat4::scale(Vec3(0.3f, 0.36f, 0.32f)) * head, sk, 0, MAT_SKIN));
    SPH.push_back(inst(Mat4::scale(Vec3(0.26f, 0.14f, 0.28f)) * Mat4::translation(Vec3(0, -0.1f, 0.02f)) * head, sk, 0, MAT_SKIN));  // jaw
    // Hair by style, and a headband in the outfit's trim colour.
    SPH.push_back(inst(Mat4::scale(Vec3(0.33f, 0.2f, 0.34f)) * Mat4::translation(Vec3(0, 0.11f, -0.03f)) * head, hair, 0, MAT_CLOTH));
    if (outfit.hairStyle == 0) {  // spiky, swept back
        for (int k = 0; k < 5; ++k) {
            float ang = -0.35f + k * 0.22f;
            float sx = (k - 2) * 0.06f;
            Mat4 spike = Mat4::scale(Vec3(0.09f, 0.26f, 0.09f)) * Mat4::rotationX(-0.9f + ang * 0.6f) * Mat4::rotationZ((float)(k - 2) * 0.25f) *
                         Mat4::translation(Vec3(sx, 0.26f, -0.02f + (float)std::fabs(k - 2) * 0.03f)) * head;
            SPH.push_back(inst(spike, hair, 0, MAT_CLOTH));
        }
    } else if (outfit.hairStyle == 2) {  // mohawk
        for (int k = 0; k < 4; ++k)
            SPH.push_back(inst(Mat4::scale(Vec3(0.08f, 0.3f, 0.12f)) * Mat4::rotationX(-0.5f + k * 0.35f) *
                                   Mat4::translation(Vec3(0, 0.3f, 0.1f - k * 0.08f)) * head, hair, 0, MAT_CLOTH));
    } else if (outfit.hairStyle == 3) {  // long, down the back
        SPH.push_back(inst(Mat4::scale(Vec3(0.3f, 0.45f, 0.14f)) * Mat4::translation(Vec3(0, -0.12f, -0.17f)) * head, hair, 0, MAT_CLOTH));
    }
    if (outfit.headband || f.teamTint)
        CYL.push_back(inst(Mat4::scale(Vec3(0.33f, 0.05f, 0.35f)) * Mat4::translation(Vec3(0, 0.07f, 0)) * head, accent, 0, MAT_CLOTH));  // headband
    for (int side = -1; side <= 1; side += 2) {
        SPH.push_back(inst(Mat4::scale(Vec3(0.055f, 0.06f, 0.03f)) * Mat4::translation(Vec3(0.065f * side, 0.02f, 0.145f)) * head, dark, 0, MAT_DEFAULT));  // eyes
        s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.07f, 0.015f, 0.02f)) * Mat4::rotationZ(0.2f * side) *
                                               Mat4::translation(Vec3(0.065f * side, 0.075f, 0.15f)) * head, hair, 0, MAT_CLOTH));  // eyebrows
        SPH.push_back(inst(Mat4::scale(Vec3(0.05f, 0.08f, 0.04f)) * Mat4::translation(Vec3(0.15f * side, 0.0f, 0.0f)) * head, sk, 0, MAT_SKIN));  // ears
    }
    SPH.push_back(inst(Mat4::scale(Vec3(0.05f, 0.06f, 0.05f)) * Mat4::translation(Vec3(0, -0.03f, 0.16f)) * head, sk, 0, MAT_SKIN));  // nose
    s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.06f, 0.012f, 0.01f)) * Mat4::translation(Vec3(0, -0.11f, 0.15f)) * head, Vec4(0.45f, 0.2f, 0.18f, 1), 0, MAT_SKIN));  // mouth

    // Arms: upper arm + forearm from the shoulders. Rotation about X: 0 = hanging down, -PI/2 = forward.
    float shoulderY = hipY + 0.68f;
    float aimA = -PI * 0.5f - f.aimPitch;
    for (int side = -1; side <= 1; side += 2) {
        float a = armSwing * (float)side;
        float inward = 0;
        Vec3 extra(0, 0, 0);
        switch (f.anim) {
            case Anim::Jump: a = side > 0 ? -2.4f : -2.1f; break;                 // arms up
            case Anim::Heal: a = -1.3f; inward = 0.5f; break;                      // hands to the chest
            case Anim::Build: a = -1.35f + (f.animT < 0.2f ? -0.5f * (1 - f.animT / 0.2f) : 0); inward = 0.25f; break;
            case Anim::Pickaxe:
                if (side > 0) {
                    if (f.swing >= 0) {
                        float t = f.swing;  // wind up, chop down, recover
                        a = t < 0.35f ? lerpf(-1.0f, -2.7f, t / 0.35f)
                                      : (t < 0.6f ? lerpf(-2.7f, -0.45f, (t - 0.35f) / 0.25f) : lerpf(-0.45f, -1.0f, (t - 0.6f) / 0.4f));
                    } else {
                        a = -1.0f;
                    }
                } else {
                    a = -0.35f;
                }
                break;
            default: break;
        }
        if (gun) {
            a = aimA;
            inward = side > 0 ? -0.08f : 0.18f;
            if (f.anim == Anim::Aim) { a = aimA + 0.05f; inward = side > 0 ? -0.18f : 0.28f; }
            if (f.anim == Anim::Shoot || f.shot > 0) a += 0.25f * f.shot;  // recoil lifts the arms
            if (f.anim == Anim::Reload && side < 0) {
                // Left hand drops to the magazine and comes back.
                float t = f.reloadFrac;
                float down = t < 0.3f ? t / 0.3f : (t > 0.75f ? (1 - t) / 0.25f : 1.0f);
                a = lerpf(aimA, -0.6f, down);
                inward = 0.4f * down;
                extra = Vec3(0.1f * down, 0, 0);
            }
            if (sprint) a = -0.9f, inward = side > 0 ? -0.3f : 0.3f;  // gun down while sprinting
        }
        Mat4 arm = Mat4::rotationX(a) * Mat4::rotationY(inward * (float)side) *
                   Mat4::translation(Vec3(0.3f * side, shoulderY, 0) + extra) * root;
        SPH.push_back(inst(Mat4::scale(Vec3(0.16f, 0.16f, 0.16f)) * arm, sh, 0, MAT_CLOTH));
        CYL.push_back(inst(Mat4::scale(Vec3(0.13f, 0.3f, 0.13f)) * Mat4::translation(Vec3(0, -0.15f, 0)) * arm, sh, 0, MAT_CLOTH));
        // Elbow bends a little when relaxed, more when holding something.
        float elbow = gun || pick || f.anim == Anim::Heal || f.anim == Anim::Build ? -0.35f : -0.15f;
        Mat4 fore = Mat4::rotationX(elbow) * Mat4::translation(Vec3(0, -0.3f, 0)) * arm;
        SPH.push_back(inst(Mat4::scale(Vec3(0.13f, 0.13f, 0.13f)) * fore, sh, 0, MAT_CLOTH));
        CYL.push_back(inst(Mat4::scale(Vec3(0.11f, 0.28f, 0.11f)) * Mat4::translation(Vec3(0, -0.14f, 0)) * fore, sk, 0, MAT_SKIN));
        CYL.push_back(inst(Mat4::scale(Vec3(0.125f, 0.08f, 0.125f)) * Mat4::translation(Vec3(0, -0.26f, 0)) * fore, dark, 0, MAT_CLOTH));  // glove cuff
        SPH.push_back(inst(Mat4::scale(Vec3(0.11f, 0.14f, 0.07f)) * Mat4::translation(Vec3(0, -0.32f, 0)) * fore, dark, 0, MAT_CLOTH));    // gloved hand
        arm = fore;  // tools hang from the hand
        if (pick && side > 0) {
            CYL.push_back(inst(Mat4::scale(Vec3(0.06f, 0.95f, 0.06f)) * Mat4::rotationX(PI * 0.5f) *
                                   Mat4::translation(Vec3(0, -0.32f, 0.3f)) * arm,
                               HANDLE, 0, MAT_WOOD));
            SPH.push_back(inst(Mat4::scale(Vec3(0.1f, 0.75f, 0.13f)) * Mat4::translation(Vec3(0, -0.38f, 0.76f)) * arm, STEEL, 0, MAT_METAL));
        }
    }
    if (gun) {
        float gunA = -f.aimPitch;
        Vec3 gunPos(0.1f, shoulderY - 0.06f, 0.5f);
        if (sprint) { gunA = 0.6f; gunPos = Vec3(0.2f, shoulderY - 0.45f, 0.3f); }
        if (f.anim == Anim::Reload) gunA += 0.35f * std::sin(f.reloadFrac * PI);  // tilts while reloading
        if (f.anim == Anim::Aim) gunPos = Vec3(0.0f, shoulderY + 0.14f, 0.5f);     // raised to the eye
        gunPos.z -= 0.12f * f.shot;                                                 // recoil kick
        Mat4 gunRoot = Mat4::rotationX(gunA) * Mat4::translation(gunPos) * root;
        addGunModel(s, Mat4::translation(Vec3(0, -0.1f, 0.05f)) * gunRoot, *f.held);
        // Magazine drops out during a reload.
        if (f.anim == Anim::Reload && f.reloadFrac > 0.25f && f.reloadFrac < 0.75f) {
            float t = (f.reloadFrac - 0.25f) / 0.5f;
            s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.06f, 0.18f, 0.08f)) *
                                                   Mat4::translation(Vec3(0, -0.15f - 0.25f * std::sin(t * PI), -0.05f)) * gunRoot,
                                               GUN_DARK, 0, MAT_METAL));
        }
    }
}

void addBlobShadow(SceneList& s, const World& w, const Vec3& feet) {
    float g = w.groundHeight(feet.x, feet.z, 0.4f, feet.y + 0.1f, 0.2f);
    if (feet.y - g > 6.0f) return;
    s.transparent[MESH_CYLINDER].push_back(
        inst(Mat4::scale(Vec3(0.95f, 0.02f, 0.95f)) * Mat4::translation(Vec3(feet.x, g + 0.03f, feet.z)), Vec4(0, 0, 0, 0.3f)));
}

// Piece geometry for either the real piece or the build preview.
void addPieceShape(std::vector<Instance>& boxList, std::vector<Instance>& coneList, const Piece& p,
                   const Vec4& color, float glow, bool ignoreEdits) {
    const Slot& s = p.slot;
    Piece full = p;
    full.alive = true;
    if (ignoreEdits) full.removed = 0;
    if (s.type == PieceType::Wall || s.type == PieceType::Floor) {
        std::vector<AABB> boxes;
        pieceBoxes(full, boxes);
        for (const AABB& b : boxes) addBox(boxList, b, color, glow, MAT_WOOD);
    } else if (s.type == PieceType::Ramp && (full.removed & 0xF) != 0) {
        std::vector<Tri> tris;
        pieceTris(full, tris);
        for (const Tri& t : tris) {
            coneList.push_back(inst(triangleMatrix(t.a, t.b, t.c), color, glow, MAT_WOOD));
            Vec3 d(0, -0.25f, 0);
            coneList.push_back(inst(triangleMatrix(t.a + d, t.c + d, t.b + d), color, glow, MAT_WOOD));
        }
    } else if (s.type == PieceType::Ramp) {
        float L = std::sqrt(TILE * TILE + STORY * STORY);
        Vec3 c((s.i + 0.5f) * TILE, s.k * STORY + STORY * 0.5f - 0.14f, (s.j + 0.5f) * TILE);
        Mat4 m = Mat4::scale(Vec3(TILE, 0.25f, L)) * Mat4::rotationX(-std::atan2(STORY, TILE)) *
                 Mat4::rotationY((float)(s.dir & 3) * PI * 0.5f) * Mat4::translation(c);
        boxList.push_back(inst(m, color, glow, MAT_WOOD));
    } else if (s.type == PieceType::Cone) {
        // The cone surface (with its edits) as triangles, plus a thin underside so it isn't paper.
        std::vector<Tri> tris;
        pieceTris(full, tris);
        for (const Tri& t : tris) {
            coneList.push_back(inst(triangleMatrix(t.a, t.b, t.c), color, glow, MAT_WOOD));
            Vec3 d(0, -0.22f, 0);
            coneList.push_back(inst(triangleMatrix(t.a + d, t.c + d, t.b + d), color, glow, MAT_WOOD));
        }
    }
}

// Tree trunks, leaf blobs and rocks are drawn round; their collision stays a box.
void addProp(SceneList& s, const World& w, const StaticBox& b) {
    float wobble = 0;
    if (b.group >= 0 && b.group < (int)w.groups.size()) {
        float t = w.groups[b.group].hitAnim;
        wobble = t > 0 ? std::sin(t * 60.0f) * 0.05f * (t / 0.25f) : 0;
    }
    Vec3 size = b.box.size(), c = b.box.center();
    Vec3 sc(1 + wobble, 1 - wobble * 0.5f, 1 + wobble);
    switch (b.kind) {
        case PropKind::Trunk:
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * sc.x, size.y, size.z * sc.z)) * Mat4::translation(c), b.color, 0, MAT_BARK));
            break;
        case PropKind::Leaves:
            s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(size.x * 1.12f * sc.x, size.y * 1.25f * sc.y, size.z * 1.12f * sc.z)) *
                                                     Mat4::translation(c),
                                                 b.color, 0, MAT_LEAF));
            break;
        case PropKind::Barrel:
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * sc.x, size.y, size.z * sc.z)) * Mat4::translation(c), b.color, 0, MAT_METAL));
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * 1.06f, 0.08f, size.z * 1.06f)) * Mat4::translation(c + Vec3(0, 0.25f, 0)), Vec4(0.3f, 0.32f, 0.36f, 1), 0, MAT_METAL));
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * 1.06f, 0.08f, size.z * 1.06f)) * Mat4::translation(c - Vec3(0, 0.25f, 0)), Vec4(0.3f, 0.32f, 0.36f, 1), 0, MAT_METAL));
            break;
        case PropKind::Rock: {
            // Seat the rock in the ground: the box is sunk 0.5 m.
            Vec3 rc = c + Vec3(0, 0.1f, 0);
            s.opaque[MESH_ROCK].push_back(inst(Mat4::scale(Vec3(size.x * 1.1f * sc.x, size.y * 1.15f * sc.y, size.z * 1.1f * sc.z)) *
                                                   Mat4::rotationY(c.x * 1.7f + c.z) * Mat4::translation(rc),
                                               b.color, 0, MAT_STONE));
            break;
        }
        case PropKind::Silo:
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * sc.x, size.y, size.z * sc.z)) * Mat4::translation(c), b.color, 0, MAT_METAL));
            s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(size.x * 1.02f, size.x * 0.6f, size.z * 1.02f)) * Mat4::translation(c + Vec3(0, size.y * 0.5f, 0)), b.color, 0, MAT_METAL));
            break;
        case PropKind::Well:
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * sc.x, size.y, size.z * sc.z)) * Mat4::translation(c), b.color, 0, MAT_STONE));
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(size.x * 0.7f, 0.2f, size.z * 0.7f)) * Mat4::translation(c + Vec3(0, size.y * 0.5f, 0)), Vec4(0.1f, 0.15f, 0.25f, 1), 0, MAT_DEFAULT));
            break;
        case PropKind::Log:
        case PropKind::Hay: {
            // Lying along x.
            Mat4 m = Mat4::scale(Vec3(size.y * sc.y, size.x, size.z * sc.z)) * Mat4::rotationZ(PI * 0.5f) * Mat4::translation(c);
            s.opaque[MESH_CYLINDER].push_back(inst(m, b.color, 0, b.kind == PropKind::Log ? MAT_WOOD : MAT_CLOTH));
            break;
        }
        default: {
            int mat = MAT_PLASTER;
            switch (b.kind) {
                case PropKind::HouseRoof: mat = MAT_ROOF; break;
                case PropKind::HouseFloor: case PropKind::Road: case PropKind::Stone: case PropKind::Step: mat = MAT_STONE; break;
                case PropKind::Fence: case PropKind::Crate: case PropKind::Post: mat = MAT_WOOD; break;
                case PropKind::Canopy: mat = MAT_METAL; break;
                default: break;
            }
            s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(size.x * sc.x, size.y * sc.y, size.z * sc.z)) * Mat4::translation(c), b.color, 0, mat));
            break;
        }
    }
}

}  // namespace

const char* animName(Anim a) {
    static const char* names[] = {"IDLE", "WALK", "SPRINT", "CROUCH", "JUMP", "AIM", "SHOOT", "RELOAD", "BUILD", "HEAL", "PICKAXE", "SKYDIVE", "GLIDE"};
    return names[(int)a];
}

Anim playerAnim(const Game& g) {
    const Player& p = g.player;
    const Item& held = g.heldItem();
    bool moving = lengthXZ(p.body.vel) > 0.4f;
    if (!p.body.grounded) return Anim::Jump;
    if (p.medkitTimer >= 0) return Anim::Heal;
    if (p.mode == Mode::Build || p.mode == Mode::Edit) return Anim::Build;
    if (held.kind == ItemKind::Pickaxe && p.swingAnim > 0) return Anim::Pickaxe;
    if (held.kind == ItemKind::Gun) {
        if (p.gunStates[p.selected].reloadTimer > 0) return Anim::Reload;
        if (p.shotAnim > 0) return Anim::Shoot;
        if (p.ads) return Anim::Aim;
    }
    if (p.crouched) return Anim::Crouch;
    if (p.sprinting && moving) return Anim::Sprint;
    if (moving) return Anim::Walk;
    return held.kind == ItemKind::Pickaxe ? Anim::Pickaxe : Anim::Idle;
}

Anim botAnim(const Game& g, const Bot& b) {
    (void)g;
    float speed = lengthXZ(b.body.vel);
    if (!b.body.grounded) return Anim::Jump;
    if (b.healTimer >= 0) return Anim::Heal;
    if (b.gunState.reloadTimer > 0) return Anim::Reload;
    if (b.shotAnim > 0) return Anim::Shoot;
    if (speed > 5.0f) return Anim::Sprint;
    if (speed > 0.4f) return Anim::Walk;
    if (b.state == AIState::Shoot) return Anim::Aim;
    return Anim::Idle;
}

Vec4 teamColor(int team, bool isPlayer) {
    if (team == 0) return isPlayer ? Vec4(0.20f, 0.45f, 0.90f, 1) : Vec4(0.20f, 0.70f, 0.80f, 1);
    static const Vec4 enemy[] = {{0.85f, 0.22f, 0.20f, 1}, {0.92f, 0.52f, 0.15f, 1}, {0.60f, 0.25f, 0.75f, 1},
                                 {0.90f, 0.78f, 0.20f, 1}, {0.88f, 0.35f, 0.60f, 1}, {0.55f, 0.35f, 0.20f, 1},
                                 {0.55f, 0.12f, 0.18f, 1}, {0.50f, 0.58f, 0.18f, 1}};
    return enemy[(team - 1) % 8];
}

Mat4 cameraView(const Camera& c) { return Mat4::lookAtLH(c.eye, c.eye + c.forward, Vec3(0, 1, 0)); }
Mat4 cameraProj(const Camera& c, float aspect) { return Mat4::perspectiveLH(c.fovY, aspect, 0.1f, 600.0f); }

void buildScene(const Game& g, float aspect, SceneList& s) {
    s.clear();
    s.drawTerrain = true;
    s.view = cameraView(g.camera);
    s.proj = cameraProj(g.camera, aspect);
    s.eye = g.camera.eye;
    s.sunDir = normalize(Vec3(-0.45f, -1.0f, -0.3f));
    s.sky = Vec4(0.53f, 0.76f, 0.97f, 1);
    const World& w = g.world;

    s.opaque[MESH_TERRAIN].push_back(inst(Mat4::identity(), Vec4(1, 1, 1, 1), 0, MAT_GRASS));

    // Map edge walls (see-through).
    {
        float e = MAP_HALF;
        Vec4 c(0.4f, 0.7f, 1.0f, 0.12f);
        addBox(s.transparent[MESH_CUBE], AABB({-e, -2, e}, {e, 40, e + 0.5f}), c);
        addBox(s.transparent[MESH_CUBE], AABB({-e, -2, -e - 0.5f}, {e, 40, -e}), c);
        addBox(s.transparent[MESH_CUBE], AABB({e, -2, -e}, {e + 0.5f, 40, e}), c);
        addBox(s.transparent[MESH_CUBE], AABB({-e - 0.5f, -2, -e}, {-e, 40, e}), c);
    }

    for (const StaticBox& b : w.statics)
        if (b.alive) addProp(s, w, b);

    // Player structures.
    for (const Piece& p : w.build.pieces()) {
        if (!p.alive) continue;
        float dmg = 1.0f - p.hp / p.maxHp;
        static const Vec4 matCol[3] = {WOOD, {0.72f, 0.42f, 0.34f, 1}, {0.55f, 0.58f, 0.62f, 1}};
        Vec4 c = mixColor(matCol[(int)p.mat], Vec4(0.25f, 0.18f, 0.12f, 1), dmg * 0.6f);
        if (p.age < 0.15f) c = mixColor(Vec4(1, 1, 1, 1), c, p.age / 0.15f);  // flash when placed
        size_t cubeStart = s.opaque[MESH_CUBE].size(), triStart = s.opaque[MESH_TRI].size();
        addPieceShape(s.opaque[MESH_CUBE], s.opaque[MESH_TRI], p, c, 0, false);
        int surf = p.mat == BuildMat::Brick ? MAT_PLASTER : (p.mat == BuildMat::Metal ? MAT_METAL : MAT_WOOD);
        for (size_t i = cubeStart; i < s.opaque[MESH_CUBE].size(); ++i) s.opaque[MESH_CUBE][i].params.y = (float)surf;
        for (size_t i = triStart; i < s.opaque[MESH_TRI].size(); ++i) s.opaque[MESH_TRI][i].params.y = (float)surf;
    }


    // Build preview.
    if (g.player.alive && g.player.mode == Mode::Build) {
        Piece prev;
        prev.slot = g.buildTarget.slot;
        prev.alive = true;
        Vec4 c = g.buildTarget.valid ? Vec4(0.35f, 0.65f, 1.0f, 0.38f) : Vec4(1.0f, 0.3f, 0.3f, 0.32f);
        addPieceShape(s.transparent[MESH_CUBE], s.transparent[MESH_TRI], prev, c, 0.6f, true);
    }

    // Edit grid.
    if (g.player.alive && g.player.mode == Mode::Edit) {
        const Piece* p = w.build.get(g.player.edit.pieceId);
        if (p && p->alive) {
            int n = editGridSize(p->slot.type);
            int hover = pieceTileUnderRay(*p, g.camera.aimRay, 60.0f);
            for (int t = 0; t < n * n; ++t) {
                AABB b = pieceTileBounds(*p, t);
                b.min -= Vec3(0.03f, 0.03f, 0.03f);
                b.max += Vec3(0.03f, 0.03f, 0.03f);
                Vec3 shrink = b.size() * 0.04f;  // small gap between tiles so the grid reads
                b.min += shrink;
                b.max -= shrink;
                bool sel = (g.player.edit.selected >> t) & 1u;
                Vec4 c = sel ? Vec4(0.25f, 0.55f, 1.0f, 0.6f) : Vec4(1, 1, 1, 0.22f);
                if (t == hover) c.w += 0.18f;
                addBox(s.transparent[MESH_CUBE], b, c, 0.7f);
            }
        }
    }

    // Chests.
    for (const Chest& c : w.chests) {
        Mat4 root = Mat4::rotationY(c.yaw) * Mat4::translation(c.pos);
        float glow = c.opened ? 0.0f : 0.25f + 0.2f * std::sin(g.time * 4.0f);
        Vec4 gold = c.opened ? Vec4(0.55f, 0.42f, 0.2f, 1) : Vec4(0.86f, 0.62f, 0.18f, 1);
        s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(1.0f, 0.55f, 0.7f)) * Mat4::translation(Vec3(0, 0.28f, 0)) * root, gold, glow, MAT_WOOD));
        // Rounded lid, hinged at the back.
        float lidA = c.openAnim * 1.9f;
        // Cylinder lying along x; half of it pokes out of the box top like a rounded lid.
        Mat4 lid = Mat4::scale(Vec3(0.34f, 1.04f, 0.72f)) * Mat4::rotationZ(PI * 0.5f) * Mat4::translation(Vec3(0, 0.0f, 0.36f)) *
                   Mat4::rotationX(-lidA) * Mat4::translation(Vec3(0, 0.55f, -0.36f)) * root;
        s.opaque[MESH_CYLINDER].push_back(inst(lid, mixColor(gold, Vec4(0.4f, 0.25f, 0.1f, 1), 0.3f), glow, MAT_METAL));
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(0.16f, 0.2f, 0.08f)) * Mat4::translation(Vec3(0, 0.5f, 0.36f)) * root,
                                             Vec4(0.3f, 0.3f, 0.32f, 1)));
    }

    // Dropped items: float, spin and glow in their rarity colour.
    for (const Pickup& p : w.pickups) {
        if (!p.alive) continue;
        float bob = p.settled ? 0.12f * std::sin(g.time * 2.5f + p.pos.x) : 0.0f;
        Vec3 c = p.pos + Vec3(0, bob, 0);
        Mat4 root = Mat4::rotationY(g.time * 1.5f + p.pos.z) * Mat4::translation(c);
        Vec4 rc = p.item.kind == ItemKind::Gun ? rarityColor(p.item.rarity)
                                               : (isConsumable(p.item.kind) ? rarityColor(p.item.rarity) : rarityColor(Rarity::Common));
        if (p.item.kind == ItemKind::Gun) {
            // Lying flat and slowly turning, like in the hotbar icon.
            addGunModel(s, Mat4::rotationZ(PI * 0.5f) * Mat4::translation(Vec3(0, -0.05f, 0)) * root, p.item);
        } else if (p.item.kind == ItemKind::BigPot || p.item.kind == ItemKind::Mini) {
            bool big = p.item.kind == ItemKind::BigPot;
            Vec4 glass(0.35f, 0.65f, 1.0f, 1);
            float bh = big ? 0.6f : 0.34f, bw = big ? 0.3f : 0.18f;
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(bw, bh, bw)) * root, glass, 0.6f, MAT_METAL));
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(bw * 0.5f, 0.1f, bw * 0.5f)) * Mat4::translation(Vec3(0, bh * 0.5f + 0.05f, 0)) * root, Vec4(0.85f, 0.85f, 0.9f, 1), 0, MAT_METAL));
            if (!big)  // minis come in a pair
                s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(bw, bh, bw)) * Mat4::translation(Vec3(0.25f, 0, 0)) * root, glass, 0.6f, MAT_METAL));
        } else if (p.item.kind == ItemKind::Mats) {
            static const Vec4 matCol[] = {{0.6f, 0.42f, 0.22f, 1}, {0.7f, 0.35f, 0.3f, 1}, {0.6f, 0.62f, 0.66f, 1}};
            int mi = std::max(0, std::min(2, p.item.mat));
            for (int k = 0; k < 3; ++k)
                s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.5f, 0.14f, 0.3f)) * Mat4::rotationY(k * 0.5f) *
                                                       Mat4::translation(Vec3(0, -0.15f + k * 0.16f, 0)) * root,
                                                   matCol[mi], 0.2f, mi == 0 ? MAT_WOOD : (mi == 1 ? MAT_STONE : MAT_METAL)));
        } else if (p.item.kind == ItemKind::Shockwave) {
            s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(0.34f, 0.34f, 0.34f)) * root, Vec4(0.35f, 0.75f, 1.0f, 1), 0.6f, MAT_METAL));
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.42f, 0.05f, 0.42f)) * root, Vec4(0.8f, 0.95f, 1.0f, 1), 0.8f));
        } else if (p.item.kind == ItemKind::MedKit) {
            s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.5f, 0.32f, 0.3f)) * root, Vec4(0.95f, 0.95f, 0.95f, 1)));
            s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.3f, 0.08f, 0.31f)) * root, Vec4(0.9f, 0.15f, 0.15f, 1), 0.5f));
            s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(0.08f, 0.24f, 0.31f)) * root, Vec4(0.9f, 0.15f, 0.15f, 1), 0.5f));
        } else {
            static const Vec4 ammoCol[] = {{0.85f, 0.75f, 0.35f, 1}, {0.4f, 0.6f, 0.35f, 1}, {0.3f, 0.4f, 0.65f, 1}, {0.75f, 0.3f, 0.25f, 1}};
            s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.3f, 0.36f, 0.3f)) * root, ammoCol[(int)p.item.ammoType], 0.3f));
        }
        Vec4 beam = rc;
        beam.w = 0.22f;
        float g0 = c.y - 0.5f;
        s.transparent[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.12f, 5.0f, 0.12f)) * Mat4::translation(Vec3(p.pos.x, g0 + 2.5f, p.pos.z)), beam, 1.0f));
    }

    // The battle bus (while it's flying) and the gliders.
    if (g.bus.flying || g.bus.t < 1.0f) {
        Vec3 bp = g.bus.pos();
        float yaw = std::atan2(g.bus.dir().x, g.bus.dir().z);
        Mat4 root = Mat4::rotationY(yaw) * Mat4::translation(bp);
        Vec4 busBlue(0.25f, 0.45f, 0.85f, 1);
        s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(3.2f, 2.8f, 9.0f)) * Mat4::translation(Vec3(0, 1.5f, 0)) * root, busBlue, 0, MAT_METAL));
        s.opaque[MESH_CUBE].push_back(inst(Mat4::scale(Vec3(3.3f, 0.9f, 9.1f)) * Mat4::translation(Vec3(0, 1.9f, 0)) * root, Vec4(0.75f, 0.85f, 1, 1), 0, MAT_METAL));  // windows
        for (int sd = -1; sd <= 1; sd += 2)
            for (int wh = -1; wh <= 1; wh += 2)
                s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(1.0f, 0.5f, 1.0f)) * Mat4::rotationZ(PI * 0.5f) *
                                                           Mat4::translation(Vec3(1.7f * sd, 0.3f, 2.8f * wh)) * root,
                                                       Vec4(0.1f, 0.1f, 0.12f, 1), 0, MAT_METAL));
        s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.3f, 6.0f, 0.3f)) * Mat4::translation(Vec3(0, 6.0f, 0)) * root, Vec4(0.3f, 0.3f, 0.32f, 1), 0, MAT_METAL));
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(9.0f, 7.0f, 9.0f)) * Mat4::translation(Vec3(0, 12.5f, 0)) * root, Vec4(0.9f, 0.3f, 0.55f, 1), 0.2f, MAT_CLOTH));
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(6.0f, 6.0f, 6.0f)) * Mat4::translation(Vec3(0, 12.5f, 0)) * root, Vec4(0.95f, 0.6f, 0.2f, 1), 0.2f, MAT_CLOTH));
    }
    auto addGlider = [&](const Vec3& feet, float yaw) {
        Mat4 root = Mat4::rotationY(yaw) * Mat4::translation(feet);
        s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.05f, 1.4f, 0.05f)) * Mat4::translation(Vec3(0, 2.6f, 0)) * root, Vec4(0.3f, 0.3f, 0.32f, 1), 0, MAT_METAL));
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(3.2f, 0.7f, 2.2f)) * Mat4::translation(Vec3(0, 3.4f, 0)) * root, Vec4(0.95f, 0.5f, 0.2f, 1), 0.1f, MAT_CLOTH));
    };

    // Other players.
    for (const Bot& b : g.bots) {
        if (!b.alive) continue;
        if (b.drop == DropPhase::OnBus) continue;  // inside the bus
        if (lengthXZ(b.body.pos - s.eye) > 420.0f) continue;  // too far to see on the big map
        FigurePose f;
        f.feet = b.body.pos;
        f.yaw = b.yaw;
        f.walkPhase = b.walkPhase;
        f.moveAmount = clampf(lengthXZ(b.body.vel) / 4.0f, 0.0f, 1.0f);
        f.shirt = skinAt(b.skin).shirt;
        f.skin = b.skin;
        f.teamTint = b.team == 0;
        f.look = b.index + 1;
        f.held = &b.gun;
        f.hurt = b.hurtTimer > 1.35f ? 0.7f : 0.0f;
        f.anim = b.drop == DropPhase::Landed ? botAnim(g, b) : (b.drop == DropPhase::Glide ? Anim::Glide : Anim::Skydive);
        if (b.drop == DropPhase::Glide) addGlider(b.body.pos, b.yaw);
        f.animT = g.time + b.index * 0.7f;
        f.animKey = b.index + 1;
        f.clock = g.time;
        f.eyeDistance = length(b.body.pos - s.eye);
        if (b.drop != DropPhase::Landed) f.held = nullptr;
        f.reloadFrac = b.gunState.reloadTimer > 0 ? 1.0f - b.gunState.reloadTimer / gunStats(b.gun.gun).reloadTime : 0.0f;
        f.shot = b.shotAnim / 0.12f;
        f.verticalVel = b.body.vel.y;
        addFigure(s, f);
        addBlobShadow(s, w, b.body.pos);
    }

    // You.
    if (g.player.alive) {
        const Player& p = g.player;
        FigurePose f;
        f.feet = p.body.pos;
        f.yaw = p.yaw;
        f.walkPhase = p.walkPhase;
        f.moveAmount = clampf(lengthXZ(p.body.vel) / 4.0f, 0.0f, 1.0f);
        f.shirt = skinAt(p.skin).shirt;
        f.skin = p.skin;
        const Item& held = g.heldItem();
        if (p.mode == Mode::Combat && (held.kind == ItemKind::Gun || held.kind == ItemKind::Pickaxe)) f.held = &held;
        f.aimPitch = held.kind == ItemKind::Gun ? p.pitch * 0.8f : 0.0f;
        f.swing = p.swingAnim > 0 ? 1.0f - p.swingAnim / 0.3f : -1.0f;
        f.hurt = p.hurtTimer > 0.2f ? 0.5f : 0.0f;
        f.anim = g.dropPhase == DropPhase::Landed ? playerAnim(g)
                                                  : (g.dropPhase == DropPhase::OnBus ? Anim::Idle : (g.dropPhase == DropPhase::Glide ? Anim::Glide : Anim::Skydive));
        if (g.dropPhase == DropPhase::Glide) addGlider(p.body.pos, p.yaw);
        f.animKey = 0;
        f.clock = g.time;
        f.eyeDistance = length(p.body.pos - s.eye);
        if (g.dropPhase != DropPhase::Landed) f.held = nullptr;
        f.animT = f.anim == Anim::Build ? p.buildAnim : p.idleTime;
        if (f.anim == Anim::Build) f.animT = 0.2f - p.buildAnim;
        f.reloadFrac = held.kind == ItemKind::Gun && p.gunStates[p.selected].reloadTimer > 0
                           ? 1.0f - p.gunStates[p.selected].reloadTimer / gunStats(held.gun).reloadTime
                           : 0.0f;
        f.shot = p.shotAnim / 0.12f;
        f.verticalVel = p.body.vel.y;
        addFigure(s, f);
        addBlobShadow(s, w, p.body.pos);
    }

    // The storm wall: a tall purple ring at the circle's edge, plus a faint one for the next circle.
    if (g.storm.radius < 400) {
        const Storm& st = g.storm;
        s.transparent[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(st.radius * 2, 120, st.radius * 2)) *
                                                        Mat4::translation(Vec3(st.center.x, 40, st.center.z)),
                                                    Vec4(0.6f, 0.25f, 0.9f, 0.28f), 0.8f));
        s.transparent[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(st.radius * 2 + 0.6f, 120, st.radius * 2 + 0.6f)) *
                                                        Mat4::translation(Vec3(st.center.x, 40, st.center.z)),
                                                    Vec4(0.75f, 0.4f, 1.0f, 0.18f), 1.0f));
    }

    // Shockwave grenades in the air and their blast rings.
    for (const Grenade& gr : g.grenades) {
        if (!gr.alive) continue;
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(0.3f, 0.3f, 0.3f)) * Mat4::translation(gr.pos), Vec4(0.35f, 0.75f, 1.0f, 1), 0.8f, MAT_METAL));
    }
    for (const ShockRing& r : g.shockRings) {
        float t = r.age / 0.6f;
        float rad = SHOCKWAVE_RADIUS * (0.2f + 0.8f * t);
        s.transparent[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(rad, rad * 0.6f, rad)) * Mat4::translation(r.pos), Vec4(0.5f, 0.85f, 1.0f, 0.45f * (1 - t)), 1.0f));
        s.transparent[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(rad * 1.3f, 0.08f, rad * 1.3f)) * Mat4::translation(r.pos + Vec3(0, 0.1f, 0)), Vec4(0.7f, 0.95f, 1.0f, 0.6f * (1 - t)), 1.0f));
    }
    for (const Tracer& t : g.tracers) {
        Vec4 c = t.color;
        c.w *= 1.0f - t.age / 0.08f;
        addBeam(s.transparent[MESH_CUBE], t.from, t.to, 0.035f, c);
    }
}

// ------------------------------------------------------------------ lobby
Vec3 lobbySpot(int i) {
    static const Vec3 spots[4] = {{0, 0, 0}, {-1.7f, 0, 0.9f}, {1.7f, 0, 0.9f}, {-3.3f, 0, 1.8f}};
    return spots[i & 3];
}

Camera buildLobbyScene(const std::vector<LobbyMember>& party, float time, float aspect, SceneList& s) {
    Camera cam;
    cam.eye = Vec3(1.2f, 1.75f, -6.2f);
    Vec3 look(0.4f, 1.05f, 0.4f);
    cam.forward = normalize(look - cam.eye);
    cam.right = normalize(cross(Vec3(0, 1, 0), cam.forward));
    cam.up = cross(cam.forward, cam.right);
    cam.fovY = deg2rad(48.0f);
    cam.aimRay = Ray{cam.eye, cam.forward};

    s.clear();
    s.drawTerrain = false;
    s.view = cameraView(cam);
    s.proj = cameraProj(cam, aspect);
    s.eye = cam.eye;
    s.sunDir = normalize(Vec3(-0.3f, -1.0f, 0.5f));
    s.sky = Vec4(0.42f, 0.62f, 0.95f, 1);

    // Ground, round stage, and a ring of trees behind.
    s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(160, 1, 160)) * Mat4::translation(Vec3(0, -0.5f, 0)), Vec4(0.33f, 0.62f, 0.28f, 1), 0, MAT_GRASS));
    s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(9, 0.4f, 9)) * Mat4::translation(Vec3(0, -0.15f, 0.8f)), Vec4(0.78f, 0.74f, 0.66f, 1), 0, MAT_STONE));
    s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(9.6f, 0.2f, 9.6f)) * Mat4::translation(Vec3(0, -0.25f, 0.8f)), Vec4(0.25f, 0.45f, 0.85f, 1), 0.3f));
    for (int i = 0; i < 14; ++i) {
        float a = -1.2f + i * 0.18f;
        float r = 14.0f + (i % 3) * 3.0f;
        Vec3 c(std::sin(a) * r, 0, std::cos(a) * r + 2.0f);
        float h = 3.5f + (i % 4) * 0.7f;
        s.opaque[MESH_CYLINDER].push_back(inst(Mat4::scale(Vec3(0.6f, h, 0.6f)) * Mat4::translation(c + Vec3(0, h * 0.5f, 0)), Vec4(0.42f, 0.28f, 0.16f, 1)));
        s.opaque[MESH_SPHERE].push_back(inst(Mat4::scale(Vec3(4.0f, 3.6f, 4.0f)) * Mat4::translation(c + Vec3(0, h + 1.2f, 0)),
                                             Vec4(0.18f + 0.04f * (i % 3), 0.50f + 0.05f * (i % 2), 0.20f, 1)));
    }
    for (int i = 0; i < 5; ++i) {
        Vec3 c(-7.0f + i * 3.6f, 0.2f, 8.0f + (i % 2) * 2.0f);
        s.opaque[MESH_ROCK].push_back(inst(Mat4::scale(Vec3(1.6f, 1.1f, 1.4f)) * Mat4::rotationY((float)i) * Mat4::translation(c), Vec4(0.55f, 0.55f, 0.57f, 1), 0, MAT_STONE));
    }

    static const Item idleGun = Item::makeGun(GunType::AssaultRifle, Rarity::Legendary);
    static const Item pick = Item::makePickaxe();
    for (size_t i = 0; i < party.size() && i < 4; ++i) {
        FigurePose f;
        f.feet = lobbySpot((int)i);
        // Face the camera, a little turned toward the centre.
        Vec3 to = cam.eye - f.feet;
        f.yaw = std::atan2(to.x, to.z) + (i == 0 ? 0.0f : (lobbySpot((int)i).x < 0 ? 0.25f : -0.25f));
        f.shirt = skinAt(party[i].skin).shirt;
        f.skin = party[i].skin;
        f.look = party[i].you ? 0 : (int)(party[i].name.size() * 3 + party[i].name[0]);
        f.held = party[i].dance ? nullptr : (party[i].you ? &pick : &idleGun);
        f.anim = party[i].you ? Anim::Pickaxe : Anim::Idle;
        f.animT = time + (float)i * 1.3f;
        f.lobby = true;
        f.dance = party[i].dance;
        f.animKey = 1000 + (int)i;
        f.clock = time;
        f.eyeDistance = length(f.feet - cam.eye);
        addFigure(s, f);
        s.transparent[MESH_CYLINDER].push_back(
            inst(Mat4::scale(Vec3(1.0f, 0.02f, 1.0f)) * Mat4::translation(f.feet + Vec3(0, 0.07f, 0)), Vec4(0, 0, 0, 0.3f)));
    }
    return cam;
}

// ------------------------------------------------------------------ meshes
namespace {
// Makes every triangle wind clockwise when seen from outside (Direct3D's default front face):
// in our left-handed space that means cross(b-a, c-a) points along the vertex normals.
void fixWinding(const std::vector<MeshVertex>& v, std::vector<uint32_t>& idx) {
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const MeshVertex &a = v[idx[t]], &b = v[idx[t + 1]], &c = v[idx[t + 2]];
        Vec3 e1(b.px - a.px, b.py - a.py, b.pz - a.pz), e2(c.px - a.px, c.py - a.py, c.pz - a.pz);
        Vec3 n(a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz);
        if (dot(cross(e1, e2), n) < 0) std::swap(idx[t + 1], idx[t + 2]);
    }
}

void makeLatLong(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int slices, int stacks, bool lumpy) {
    v.clear();
    idx.clear();
    // Ring vertices (no duplicate pole rings: poles are single vertices).
    auto radius = [&](float th, float ph) {
        if (!lumpy) return 0.5f;
        return 0.5f * (1.0f + 0.10f * std::sin(3 * th + 1.3f) * std::sin(2 * ph) + 0.07f * std::cos(5 * th) * std::sin(3 * ph + 0.5f) +
                       0.05f * std::sin(7 * th + 2.0f * ph));
    };
    v.push_back({0, radius(0, 0), 0, 0, 1, 0, 1, 1, 1});  // top pole
    for (int i = 1; i < stacks; ++i) {
        float ph = PI * (float)i / stacks;
        for (int j = 0; j < slices; ++j) {
            float th = 2 * PI * (float)j / slices;
            Vec3 d(std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th));
            float r = radius(th, ph);
            v.push_back({d.x * r, d.y * r, d.z * r, d.x, d.y, d.z, 1, 1, 1});
        }
    }
    v.push_back({0, -radius(0, PI), 0, 0, -1, 0, 1, 1, 1});  // bottom pole
    uint32_t bottom = (uint32_t)v.size() - 1;
    auto ring = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * slices + (j % slices)); };
    for (int j = 0; j < slices; ++j) {
        idx.push_back(0); idx.push_back(ring(1, j)); idx.push_back(ring(1, j + 1));
        idx.push_back(bottom); idx.push_back(ring(stacks - 1, j + 1)); idx.push_back(ring(stacks - 1, j));
    }
    for (int i = 1; i < stacks - 1; ++i)
        for (int j = 0; j < slices; ++j) {
            uint32_t a = ring(i, j), b = ring(i, j + 1), c = ring(i + 1, j), d = ring(i + 1, j + 1);
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    if (lumpy) {
        // Smooth normals from the actual lumpy surface.
        std::vector<Vec3> acc(v.size(), Vec3());
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const MeshVertex &a = v[idx[t]], &b = v[idx[t + 1]], &c = v[idx[t + 2]];
            Vec3 n = cross(Vec3(b.px - a.px, b.py - a.py, b.pz - a.pz), Vec3(c.px - a.px, c.py - a.py, c.pz - a.pz));
            Vec3 out(a.px + b.px + c.px, a.py + b.py + c.py, a.pz + b.pz + c.pz);
            if (dot(n, out) < 0) n = -n;
            for (int k = 0; k < 3; ++k) acc[idx[t + k]] += n;
        }
        for (size_t i = 0; i < v.size(); ++i) {
            Vec3 n = normalize(acc[i]);
            v[i].nx = n.x; v[i].ny = n.y; v[i].nz = n.z;
        }
    }
    fixWinding(v, idx);
}
}  // namespace

void makeCubeMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx) {
    v.clear();
    idx.clear();
    const float h = 0.5f;
    struct Face { Vec3 n, u, w; } faces[6] = {
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
    };
    for (const Face& f : faces) {
        uint32_t base = (uint32_t)v.size();
        const float su[4] = {-1, 1, 1, -1}, sw[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            Vec3 p = f.n * h + f.u * (su[i] * h) + f.w * (sw[i] * h);
            v.push_back({p.x, p.y, p.z, f.n.x, f.n.y, f.n.z, 1, 1, 1});
        }
        // Clockwise when seen from outside (Direct3D's default front face).
        uint32_t q[6] = {0, 2, 1, 0, 3, 2};
        for (uint32_t k : q) idx.push_back(base + k);
    }
}

void makeConeQuadMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx) {
    v.clear();
    idx.clear();
    Vec3 apex(0, 1, 0), a(0.5f, 0, 0), b(0.5f, 0, 0.5f), c(0, 0, 0.5f);
    auto tri = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2) {
        Vec3 n = normalize(cross(p1 - p0, p2 - p0));
        if (n.y < 0) n = -n;
        uint32_t base = (uint32_t)v.size();
        for (const Vec3* p : {&p0, &p1, &p2}) v.push_back({p->x, p->y, p->z, n.x, n.y, n.z, 1, 1, 1});
        idx.push_back(base);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
    };
    tri(apex, b, a);  // clockwise when seen from outside/above
    tri(apex, c, b);
}

void makeTriMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx) {
    v.clear();
    idx.clear();
    v.push_back({0, 0, 0, 0, 1, 0, 1, 1, 1});
    v.push_back({1, 0, 0, 0, 1, 0, 1, 1, 1});
    v.push_back({0, 0, 1, 0, 1, 0, 1, 1, 1});
    idx = {0, 2, 1};  // clockwise seen from above (+y)
    fixWinding(v, idx);
}

Mat4 triangleMatrix(const Vec3& a, const Vec3& b, const Vec3& c) {
    // Rows: x axis = b-a, y axis = unit normal, z axis = c-a, origin = a. The shader turns
    // the unit normal (0,1,0) into row 1 / |row 1|^2 = the real normal.
    Vec3 n = normalize(cross(c - a, b - a));
    Mat4 m = Mat4::identity();
    m.m[0][0] = b.x - a.x; m.m[0][1] = b.y - a.y; m.m[0][2] = b.z - a.z;
    m.m[1][0] = n.x;       m.m[1][1] = n.y;       m.m[1][2] = n.z;
    m.m[2][0] = c.x - a.x; m.m[2][1] = c.y - a.y; m.m[2][2] = c.z - a.z;
    m.m[3][0] = a.x;       m.m[3][1] = a.y;       m.m[3][2] = a.z;
    return m;
}

void makeSphereMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod) {
    static const int sl[3] = {10, 28, 64}, st[3] = {6, 18, 40};
    lod = clampf((float)lod, 0, 2) > 1.5f ? 2 : (lod < 1 ? 0 : 1);
    makeLatLong(v, idx, sl[lod], st[lod], false);
}
void makeRockMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod) {
    static const int sl[3] = {9, 18, 48}, st[3] = {6, 12, 32};
    lod = lod < 0 ? 0 : (lod > 2 ? 2 : lod);
    makeLatLong(v, idx, sl[lod], st[lod], true);
}

void makeCylinderMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod) {
    v.clear();
    idx.clear();
    static const int segs[3] = {8, 28, 64};
    const int seg = segs[lod < 0 ? 0 : (lod > 2 ? 2 : lod)];
    for (int j = 0; j < seg; ++j) {
        float th = 2 * PI * (float)j / seg;
        float x = std::cos(th), z = std::sin(th);
        v.push_back({x * 0.5f, -0.5f, z * 0.5f, x, 0, z, 1, 1, 1});
        v.push_back({x * 0.5f, 0.5f, z * 0.5f, x, 0, z, 1, 1, 1});
    }
    for (int j = 0; j < seg; ++j) {
        uint32_t a = 2 * j, b = 2 * j + 1, c = 2 * ((j + 1) % seg), d = c + 1;
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
        idx.push_back(c); idx.push_back(b); idx.push_back(d);
    }
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap ? 0.5f : -0.5f, ny = cap ? 1.0f : -1.0f;
        uint32_t center = (uint32_t)v.size();
        v.push_back({0, y, 0, 0, ny, 0, 1, 1, 1});
        for (int j = 0; j < seg; ++j) {
            float th = 2 * PI * (float)j / seg;
            v.push_back({std::cos(th) * 0.5f, y, std::sin(th) * 0.5f, 0, ny, 0, 1, 1, 1});
        }
        for (int j = 0; j < seg; ++j) {
            idx.push_back(center);
            idx.push_back(center + 1 + j);
            idx.push_back(center + 1 + (j + 1) % seg);
        }
    }
    fixWinding(v, idx);
}

void makeTerrainMesh(const World& w, std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int step) {
    v.clear();
    idx.clear();
    if (step < 1) step = 1;
    int n = (w.hmSize() - 1) / step + 1;
    for (int iz = 0; iz < n; ++iz)
        for (int ix = 0; ix < n; ++ix) {
            float x = -MAP_HALF + ix * step * HEIGHTMAP_STEP, z = -MAP_HALF + iz * step * HEIGHTMAP_STEP;
            float y = w.terrainHeight(x, z);
            float e = 1.0f;
            float hx = w.terrainHeight(x + e, z) - w.terrainHeight(x - e, z);
            float hz = w.terrainHeight(x, z + e) - w.terrainHeight(x, z - e);
            Vec3 nn = normalize(Vec3(-hx, 2 * e, -hz));
            // Grass with gentle colour variation.
            float var = 0.5f + 0.5f * std::sin(x * 0.11f) * std::cos(z * 0.13f);
            float r = 0.30f + 0.06f * var, g = 0.58f + 0.08f * var, b = 0.24f;
            if (std::fabs(x) > MAP_HALF - 3 || std::fabs(z) > MAP_HALF - 3) { r = 0.75f; g = 0.7f; b = 0.5f; }
            v.push_back({x, y, z, nn.x, nn.y, nn.z, r, g, b});
        }
    for (int iz = 0; iz < n - 1; ++iz)
        for (int ix = 0; ix < n - 1; ++ix) {
            uint32_t a = iz * n + ix, b = a + 1, c = a + n, d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
}

}  // namespace bf
