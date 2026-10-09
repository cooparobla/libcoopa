/**
 * @file ik_system.h
 * @brief IkSystem: solves every TwoBoneIK / LookAtIK in the scene each frame, between the
 *        Animation phase (300, which writes the animated pose) and TransformResolve (350, which
 *        resolves world matrices for rendering).
 *
 * Order within a frame:
 *   1. IkDriver::ik_pre_solve() on every driver (e.g. FootIK: ground probes, pelvis offset,
 *      leg targets),
 *   2. every TwoBoneIK, in scene pre-order,
 *   3. every LookAtIK, in scene pre-order (so a head turn rides on any arm/spine solve above it),
 *   4. IkDriver::ik_post_solve() (e.g. FootIK aligning the feet to the ground normal).
 *
 * Solvers read world positions through TransformComponent::get_world_matrix(), which re-walks
 * any parent chain dirtied by the Animator or an earlier solve -- so each chain sees the pose as
 * it stands at that moment, before TransformResolve has run.
 *
 * The component lists are gathered with one scene walk per frame -- one dynamic_cast per
 * component, to their common IkComponent base -- so spawned rigs are picked up without a refresh
 * call; a scene with no IK pays only that walk (and only while simulating: not in edit mode).
 */

#ifndef COOPA_ANIMATION_IK_SYSTEM_H
#define COOPA_ANIMATION_IK_SYSTEM_H

#include <coopa/animation/ik_components.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_system.h>

#include <memory>
#include <vector>

namespace coopa {
namespace anim {

/** @brief IkSystem's update order: between Animation (300) and TransformResolve (350). */
constexpr int k_ik_update_order = 320;

/**
 * @class IkSystem
 * @brief Runs IK drivers and solvers once per simulated frame (see file doc).
 */
class IkSystem : public coopa::scene::ISceneSystem {
public:
    const char* system_name() const override { return "IK"; }

    void execute(coopa::scene::Scene& scene, const coopa::scene::FrameContext& ctx) override {
        drivers_.clear();
        two_bone_.clear();
        look_at_.clear();
        for (const auto& root : scene.root_objects()) {
            root->for_each_recursive([this](const coopa::scene::SceneObject& obj) {
                if (!obj.active()) return;
                for (const auto& comp : obj.components()) {
                    auto* ik = dynamic_cast<IkComponent*>(comp.get());
                    if (!ik) continue;
                    switch (ik->ik_kind()) {
                        case IkComponent::Kind::TwoBone: two_bone_.push_back(static_cast<TwoBoneIK*>(ik)); break;
                        case IkComponent::Kind::LookAt: look_at_.push_back(static_cast<LookAtIK*>(ik)); break;
                        case IkComponent::Kind::Driver: drivers_.push_back(static_cast<IkDriver*>(ik)); break;
                    }
                }
            });
        }
        const float dt = ctx.delta_time;
        for (IkDriver* d : drivers_) d->ik_pre_solve(dt);
        for (TwoBoneIK* s : two_bone_) s->solve(dt);
        for (LookAtIK* s : look_at_) s->solve(dt);
        for (IkDriver* d : drivers_) d->ik_post_solve(dt);
    }

    size_t two_bone_count() const { return two_bone_.size(); }
    size_t look_at_count() const { return look_at_.size(); }
    size_t driver_count() const { return drivers_.size(); }

private:
    std::vector<IkDriver*>  drivers_;
    std::vector<TwoBoneIK*> two_bone_;
    std::vector<LookAtIK*>  look_at_;
};

/**
 * @brief Constructs an IkSystem and registers it at k_ik_update_order (320).
 * @return Non-owning pointer to the installed system.
 */
inline IkSystem* install_ik_system(coopa::scene::Scene& scene) {
    auto sys = std::make_unique<IkSystem>();
    IkSystem* raw = sys.get();
    scene.add_system(std::move(sys), k_ik_update_order);
    return raw;
}

} // namespace anim
} // namespace coopa

#endif // COOPA_ANIMATION_IK_SYSTEM_H
