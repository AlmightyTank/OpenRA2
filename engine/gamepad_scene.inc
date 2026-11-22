// OpenRA2 Android/controller v0.1C — game-scene-only virtual cursor and camera.
// Included within GameScene (public section). No World or net protocol changes.
    ra2engine::GamepadCursor gamepad_cursor_;
    ra2engine::GamepadCommandMapper gamepad_commands_;
    int32_t gamepad_instance_ = -1;
    bool gamepad_active_ = false;   // last active pointer source was controller
    bool gamepad_injecting_ = false; // mark synthetic events for track_pointer()

    bool gamepad_modal_blocked() {
        // The base input layer must never pan during modal UI, lock input,
        // movies, chat typing, or multiplayer reconnect/end interruptions.
        return input_locked_ || ingame_movie_.active() || menu_.open_ ||
               sld_.is_open() || show_briefing_ || gf_abort_.open() ||
               net_chat_open_ || net_stalled_ || net_end_dialog_;
    }

    // Use GameScene::handle rather than SDL synthetic mouse events or
    // direct World mutations. This preserves UI, input modes and net commands.
    void gamepad_pointer(ra2engine::InputEvent::Kind kind,
                         ra2engine::InputEvent::Button button,
                         bool shift, uint32_t now_ms) {
        ra2engine::InputEvent ev;
        ev.kind = kind; ev.button = button; ev.shift = shift;
        ev.x = gamepad_cursor_.x(); ev.y = gamepad_cursor_.y();
        ev.t_ms = now_ms;
        gamepad_injecting_ = true;
        handle(ev);
        gamepad_injecting_ = false;
        gamepad_active_ = true;
    }

    void gamepad_frame(const ra2engine::GamepadState& pad, uint32_t now_ms) override {
        if (!pad.connected) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(0, -32768, -32768);
            if (gamepad_active_) mouse_present_ = false;
            gamepad_active_ = false;
            gamepad_instance_ = -1;
            gamepad_cursor_.suspend(now_ms);
            return;
        }
        if (pad.instance_id != gamepad_instance_) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            if (gamepad_active_) mouse_present_ = false;
            gamepad_active_ = false;
            gamepad_instance_ = pad.instance_id;
            gamepad_cursor_.reset(kWinW, kWinH);
            if (mouse_present_) gamepad_cursor_.sync_pointer(mx_, my_);
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            return;
        }
        gamepad_cursor_.resize(kWinW, kWinH);
        if (gamepad_modal_blocked()) {
            // Releasing an unfinished rectangle would select units, so drop
            // the Input gesture without sending a synthetic PointerUp.
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            return;
        }
        const auto motion = gamepad_cursor_.tick(now_ms,
            pad.axes[0], pad.axes[1], pad.axes[2], pad.axes[3]);
        const auto commands = gamepad_commands_.sample(
            pad.buttons, pad.axes[4], pad.axes[5],
            gamepad_cursor_.x() < playfield_w_ && !input_.placement_active());
        // Keep the projected selection rectangle anchored on-screen.
        if (!gamepad_commands_.box_active() && (motion.pan_x || motion.pan_y))
            input_.pan(-motion.pan_x, -motion.pan_y);
        if (motion.cursor_moved || (!gamepad_active_ &&
            (motion.pan_x || motion.pan_y || !commands.empty())))
            gamepad_pointer(ra2engine::InputEvent::PointerMove,
                            ra2engine::InputEvent::Left, false, now_ms);

        for (const auto& command : commands) {
            using K = ra2engine::GamepadCommand::Kind;
            using E = ra2engine::InputEvent;
            if (command.kind == K::CancelBox) {
                input_.cancel_gesture();
            } else if (command.kind == K::BeginBox) {
                gamepad_pointer(E::PointerDown, E::Left, command.shift, now_ms);
            } else if (command.kind == K::EndBox) {
                gamepad_pointer(E::PointerUp, E::Left, false, now_ms);
            } else if (command.kind == K::ClickLeft) {
                gamepad_pointer(E::PointerDown, E::Left, command.shift, now_ms);
                gamepad_pointer(E::PointerUp, E::Left, command.shift, now_ms);
            } else if (command.kind == K::ClickRight) {
                gamepad_pointer(E::PointerDown, E::Right, false, now_ms);
                gamepad_pointer(E::PointerUp, E::Right, false, now_ms);
            }
        }
    }

    void draw_gamepad_cursor(SDL_Renderer* ren) {
        if (!gamepad_active_ || gamepad_modal_blocked()) return;
        // Self-contained reticle: shows even when MOUSE.SHP art is unavailable.
        // Draw after world/UI cursors, in logical canvas coordinates.
        SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRenderDrawBlendMode(ren, &old_blend);
        SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 210);
        SDL_Rect shadow{mx_ - 11, my_ - 11, 22, 22};
        SDL_RenderDrawRect(ren, &shadow);
        SDL_SetRenderDrawColor(ren, 108, 244, 182, 235);
        SDL_Rect ring{mx_ - 9, my_ - 9, 18, 18};
        SDL_RenderDrawRect(ren, &ring);
        SDL_RenderDrawLine(ren, mx_ - 15, my_, mx_ - 6, my_);
        SDL_RenderDrawLine(ren, mx_ + 6, my_, mx_ + 15, my_);
        SDL_RenderDrawLine(ren, mx_, my_ - 15, mx_, my_ - 6);
        SDL_RenderDrawLine(ren, mx_, my_ + 6, mx_, my_ + 15);
        SDL_SetRenderDrawColor(ren, r, g, b, a);
        SDL_SetRenderDrawBlendMode(ren, old_blend);
    }
