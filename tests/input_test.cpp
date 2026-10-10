/**
 * @file input_test.cpp
 * @brief coopa::input::Input frame state: press/release edges cleared by begin_frame(), held
 *        time, sub-frame clicks registering both edges, cursor-delta suppression on the first
 *        sample and after a cursor-mode change, and releasing everything on focus loss.
 */
#include <coopa/testing/test.h>

#include <coopa/input/input.h>
#include <coopa/input/keys.h>

#include <glm/glm.hpp>

COOPA_TEST_SUITE("input");

using namespace coopa::input;

COOPA_TEST(press_and_release_edges_last_one_frame_while_down_persists) {
    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::A, 0, KeyAction::Press, Mods::None);
    EXPECT_TRUE(input.key_pressed(Key::A));
    EXPECT_TRUE(input.key_down(Key::A));

    // Next frame, no new events -- the press edge is gone, but "down" persists.
    input.begin_frame(0.016f);
    EXPECT_FALSE(input.key_pressed(Key::A));
    EXPECT_TRUE(input.key_down(Key::A));

    input.push_key(Key::A, 0, KeyAction::Release, Mods::None);
    EXPECT_TRUE(input.key_released(Key::A));
    EXPECT_FALSE(input.key_down(Key::A));
}

COOPA_TEST(held_time_accumulates_frame_deltas_and_resets_on_release) {
    Input input;
    input.begin_frame(0.0f);
    input.push_key(Key::W, 0, KeyAction::Press, Mods::None);
    EXPECT_NEAR(input.key_held_time(Key::W), 0.0f, 1e-6f);

    input.begin_frame(0.5f); // W still down -- accumulates the elapsed 0.5s
    EXPECT_NEAR(input.key_held_time(Key::W), 0.5f, 1e-6f);

    input.begin_frame(0.25f);
    EXPECT_NEAR(input.key_held_time(Key::W), 0.75f, 1e-6f);

    input.push_key(Key::W, 0, KeyAction::Release, Mods::None);
    EXPECT_NEAR(input.key_held_time(Key::W), 0.0f, 1e-6f);
}

COOPA_TEST(press_and_release_within_one_frame_sets_both_edges) {
    Input input;
    input.begin_frame(0.016f);
    // Event-driven (unlike level-triggered polling), so a click-and-release
    // faster than one frame still registers both edges -- polling would miss it.
    input.push_mouse_button(MouseButton::Left, KeyAction::Press, Mods::None);
    input.push_mouse_button(MouseButton::Left, KeyAction::Release, Mods::None);
    EXPECT_TRUE(input.button_pressed(MouseButton::Left));
    EXPECT_TRUE(input.button_released(MouseButton::Left));
    EXPECT_FALSE(input.button_down(MouseButton::Left));
}

COOPA_TEST(cursor_delta_is_suppressed_on_first_sample_and_after_mode_change) {
    Input input;
    input.begin_frame(0.016f);
    input.push_cursor_position(100.0, 100.0); // first ever -- suppressed to zero delta
    EXPECT_VEC_NEAR(input.cursor_delta(), glm::vec2(0.0f), 0.0f);

    input.begin_frame(0.016f);
    input.push_cursor_position(110.0, 105.0);
    EXPECT_VEC_NEAR(input.cursor_delta(), glm::vec2(10.0f, 5.0f), 1e-6f);

    // A mode change re-arms suppression -- the backend's next reported
    // position may jump arbitrarily (e.g. entering CursorMode::Disabled).
    input.set_cursor_mode(CursorMode::Disabled);
    input.begin_frame(0.016f);
    input.push_cursor_position(500.0, 500.0);
    EXPECT_VEC_NEAR(input.cursor_delta(), glm::vec2(0.0f), 0.0f);

    input.begin_frame(0.016f);
    input.push_cursor_position(505.0, 502.0);
    EXPECT_VEC_NEAR(input.cursor_delta(), glm::vec2(5.0f, 2.0f), 1e-6f);
}

COOPA_TEST(focus_loss_releases_every_held_key) {
    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::W, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(input.key_down(Key::W));

    // The OS is not guaranteed to deliver W's release once focus is lost
    // (e.g. alt-tabbing away while holding it) -- push_focus(false) must
    // release it anyway, or it would read as stuck down indefinitely.
    input.push_focus(false);
    EXPECT_FALSE(input.key_down(Key::W));
    EXPECT_TRUE(input.key_released(Key::W));
    EXPECT_FALSE(input.focused());
}
