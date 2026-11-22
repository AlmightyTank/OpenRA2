// OpenRA2 Android/controller v0.1C — game-scene-only virtual cursor and camera.
// Included within GameScene (public section). No World or net protocol changes.
    ra2engine::GamepadCursor gamepad_cursor_;
    ra2engine::GamepadCommandMapper gamepad_commands_;
    ra2engine::GamepadNav gamepad_nav_;
    bool gamepad_sidebar_focus_ = false;
    bool gamepad_sidebar_tab_ = true;
    int gamepad_cameo_ix_ = 0;
    int gamepad_saved_x_ = 0, gamepad_saved_y_ = 0;
    int gamepad_menu_ix_ = -1;
    int gamepad_menu_page_ = -1;
    bool gamepad_menu_was_open_ = false;
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

    // All controller commands go through the SAME existing GameScene::handle
    // path as touch/mouse; production and network events remain untouched.
    void gamepad_pointer_at(ra2engine::InputEvent::Kind kind,
                            ra2engine::InputEvent::Button button, bool shift,
                            int x, int y, uint32_t now_ms) {
        ra2engine::InputEvent ev;
        ev.kind = kind; ev.button = button; ev.shift = shift;
        ev.x = x; ev.y = y; ev.t_ms = now_ms;
        gamepad_injecting_ = true;
        handle(ev);
        gamepad_injecting_ = false;
        gamepad_active_ = true;
    }
    void gamepad_pointer(ra2engine::InputEvent::Kind kind,
                         ra2engine::InputEvent::Button button,
                         bool shift, uint32_t now_ms) {
        gamepad_pointer_at(kind, button, shift,
                           gamepad_cursor_.x(), gamepad_cursor_.y(), now_ms);
    }
    void gamepad_key_escape(uint32_t now_ms) {
        ra2engine::InputEvent ev;
        ev.kind = ra2engine::InputEvent::Key;
        ev.key = ra2engine::kKeyEscape;
        ev.t_ms = now_ms;
        handle(ev); // Esc is the established menu open / Back / Resume path
    }
    void gamepad_sidebar_highlight(uint32_t now_ms) {
        if (!sidebar_up_) return;
        ra2engine::SbRect r;
        if (gamepad_sidebar_tab_ || lay_.cameos.empty()) {
            gamepad_sidebar_tab_ = true;
            r = lay_.tabs[active_tab_];
        } else {
            gamepad_cameo_ix_ = std::max(0, std::min(gamepad_cameo_ix_,
                                            static_cast<int>(lay_.cameos.size()) - 1));
            r = lay_.cameos[static_cast<std::size_t>(gamepad_cameo_ix_)].rect;
        }
        gamepad_cursor_.sync_pointer(r.x + r.w / 2, r.y + r.h / 2);
        gamepad_pointer(ra2engine::InputEvent::PointerMove,
                         ra2engine::InputEvent::Left, false, now_ms);
    }
    void gamepad_sidebar_step(const ra2engine::GamepadNavEdges& e, uint32_t now_ms) {
        if (!sidebar_up_ || !ba_.sidebar_ok) {
            gamepad_sidebar_focus_ = false;
            return;
        }
        if (e.cancel || e.toggle) {
            gamepad_sidebar_focus_ = false;
            gamepad_cursor_.sync_pointer(gamepad_saved_x_, gamepad_saved_y_);
            gamepad_pointer(ra2engine::InputEvent::PointerMove,
                            ra2engine::InputEvent::Left, false, now_ms);
            return;
        }
        if (e.start) {
            gamepad_sidebar_focus_ = false;
            gamepad_key_escape(now_ms);
            return;
        }
        if (gamepad_sidebar_tab_) {
            if (e.left || e.right) {
                const int n = (active_tab_ + (e.right ? 1 : 3)) % 4;
                const ra2engine::SbRect r = lay_.tabs[n];
                // Tabs switch on PointerDown; a release here can leak into
                // tactical input after sidebar state changes.
                gamepad_pointer_at(ra2engine::InputEvent::PointerDown,
                                   ra2engine::InputEvent::Left, false,
                                   r.x + r.w / 2, r.y + r.h / 2, now_ms);
                gamepad_cameo_ix_ = 0;
            }
            if (e.down && !lay_.cameos.empty()) {
                gamepad_sidebar_tab_ = false;
                gamepad_cameo_ix_ = 0;
            }
        } else {
            if (e.left && (gamepad_cameo_ix_ % 2) == 1) --gamepad_cameo_ix_;
            if (e.right && (gamepad_cameo_ix_ % 2) == 0 &&
                gamepad_cameo_ix_ + 1 < static_cast<int>(lay_.cameos.size()))
                ++gamepad_cameo_ix_;
            if (e.down) {
                if (gamepad_cameo_ix_ + 2 < static_cast<int>(lay_.cameos.size()))
                    gamepad_cameo_ix_ += 2;
                else if (lay_.top_row < lay_.max_top_row(static_cast<int>(buildable_.size())))
                    strip_scroll(1);  // existing sidebar scroll/rebuild
            }
            if (e.up) {
                if (gamepad_cameo_ix_ >= 2) gamepad_cameo_ix_ -= 2;
                else if (lay_.top_row > 0) strip_scroll(-1);
                else gamepad_sidebar_tab_ = true;
            }
        }
        gamepad_sidebar_highlight(now_ms);
        if (!gamepad_sidebar_tab_ && gamepad_cameo_ix_ <
                static_cast<int>(lay_.cameos.size())) {
            const auto r = lay_.cameos[static_cast<std::size_t>(gamepad_cameo_ix_)].rect;
            const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
            if (e.accept) {
                gamepad_pointer_at(ra2engine::InputEvent::PointerDown,
                                    ra2engine::InputEvent::Left, false, cx, cy, now_ms);
                // A ready structure/defense enters normal Input placement.
                // Return focus to the tactical cursor rather than keeping A
                // trapped over the sidebar's cameo.
                if (input_.placement_active()) {
                    gamepad_sidebar_focus_ = false;
                    gamepad_cursor_.sync_pointer(gamepad_saved_x_, gamepad_saved_y_);
                    gamepad_pointer(ra2engine::InputEvent::PointerMove,
                                    ra2engine::InputEvent::Left, false, now_ms);
                    return;
                }
            }
            // X is the existing right-click queue action: running -> hold,
            // held/ready -> cancel; never bypasses queue/credit validation.
            if (e.alternate) {
                gamepad_pointer_at(ra2engine::InputEvent::PointerDown,
                                    ra2engine::InputEvent::Right, false, cx, cy, now_ms);
            }
        }
    }
    void gamepad_ingame_menu(const ra2engine::GamepadNavEdges& e, uint32_t now_ms) {
        if (e.cancel || e.start) {
            gamepad_key_escape(now_ms);
            gamepad_menu_ix_ = -1;
            return;
        }
        const auto rects = menu_.gamepad_button_rects();
        if (rects.empty()) return;
        if (gamepad_menu_page_ != static_cast<int>(menu_.page())) {
            gamepad_menu_page_ = static_cast<int>(menu_.page());
            gamepad_menu_ix_ = -1;
        }
        std::vector<bool> enabled(rects.size(), true);
        const int direction = e.down || e.right ? 1 : e.up || e.left ? -1 : 0;
        bool moved = false;
        if (gamepad_menu_ix_ < 0) {
            gamepad_menu_ix_ = ra2engine::gamepad_menu_step(enabled, -1, 1);
            moved = true;
        } else if (direction) {
            gamepad_menu_ix_ = ra2engine::gamepad_menu_step(enabled, gamepad_menu_ix_, direction);
            moved = true;
        }
        if (gamepad_menu_ix_ < 0) return;
        const ra2engine::SbRect r = rects[static_cast<std::size_t>(gamepad_menu_ix_)];
        const int x = r.x + r.w / 2, y = r.y + r.h / 2;
        if (moved || e.accept)
            gamepad_pointer_at(ra2engine::InputEvent::PointerMove,
                                ra2engine::InputEvent::Left, false, x, y, now_ms);
        if (e.accept) {
            gamepad_pointer_at(ra2engine::InputEvent::PointerDown,
                                ra2engine::InputEvent::Left, false, x, y, now_ms);
            // Only release while the modal still owns the pointer. If a Resume
            // action closed it, the release must not enter battlefield input.
            if (menu_.open_)
                gamepad_pointer_at(ra2engine::InputEvent::PointerUp,
                                    ra2engine::InputEvent::Left, false, x, y, now_ms);
            gamepad_menu_ix_ = -1; // new sub-page can have different enabled buttons
        }
    }

    void gamepad_frame(const ra2engine::GamepadState& pad, uint32_t now_ms) override {
        if (!pad.connected) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(0, -32768, -32768);
            gamepad_nav_.reset(0);
            gamepad_sidebar_focus_ = false;
            gamepad_menu_ix_ = -1;
            gamepad_menu_was_open_ = false;
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
            gamepad_sidebar_focus_ = false;
            gamepad_menu_ix_ = -1;
            gamepad_menu_was_open_ = false;
            gamepad_nav_.reset(pad.buttons);
            gamepad_cursor_.reset(kWinW, kWinH);
            if (mouse_present_) gamepad_cursor_.sync_pointer(mx_, my_);
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            return;
        }
        const auto nav = gamepad_nav_.sample(pad.buttons);
        gamepad_cursor_.resize(kWinW, kWinH);
        if (menu_.open_) {
            gamepad_menu_was_open_ = true;
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            gamepad_sidebar_focus_ = false;
            gamepad_ingame_menu(nav, now_ms);
            return;
        }
        gamepad_menu_ix_ = -1;
        if (gamepad_menu_was_open_) {
            gamepad_menu_was_open_ = false;
            gamepad_cursor_.suspend(now_ms);
            gamepad_pointer(ra2engine::InputEvent::PointerMove,
                            ra2engine::InputEvent::Left, false, now_ms);
        }
        if (gamepad_modal_blocked()) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            gamepad_sidebar_focus_ = false;
            if (nav.cancel && (sld_.is_open() || show_briefing_ || gf_abort_.open()))
                gamepad_key_escape(now_ms);
            return;
        }
        if (nav.start) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_sidebar_focus_ = false;
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            gamepad_key_escape(now_ms);
            return;
        }
        if (gamepad_sidebar_focus_) {
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            gamepad_sidebar_step(nav, now_ms);
            return;
        }
        if (nav.toggle && sidebar_up_ && ba_.sidebar_ok) {
            if (gamepad_commands_.cancel_box()) input_.cancel_gesture();
            gamepad_commands_.reset(pad.buttons, pad.axes[4], pad.axes[5]);
            gamepad_cursor_.suspend(now_ms);
            gamepad_saved_x_ = gamepad_cursor_.x();
            gamepad_saved_y_ = gamepad_cursor_.y();
            gamepad_sidebar_focus_ = true;
            gamepad_sidebar_tab_ = true;
            gamepad_cameo_ix_ = 0;
            gamepad_sidebar_highlight(now_ms);
            return;
        }
        const auto motion = gamepad_cursor_.tick(now_ms,
            pad.axes[0], pad.axes[1], pad.axes[2], pad.axes[3]);
        const auto commands = gamepad_commands_.sample(
            pad.buttons, pad.axes[4], pad.axes[5],
            gamepad_cursor_.x() < playfield_w_ && !input_.placement_active());
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
        // Explicit controller sidebar focus: a thin outline only. Keep the
        // underlying cameo/tab art, original font, and click geometry intact.
        if (gamepad_sidebar_focus_ && sidebar_up_) {
            ra2engine::SbRect focus = lay_.tabs[active_tab_];
            if (!gamepad_sidebar_tab_ && gamepad_cameo_ix_ >= 0 &&
                gamepad_cameo_ix_ < static_cast<int>(lay_.cameos.size()))
                focus = lay_.cameos[static_cast<std::size_t>(gamepad_cameo_ix_)].rect;
            SDL_SetRenderDrawColor(ren, 250, 230, 100, 255);
            SDL_Rect outline{focus.x - 1, focus.y - 1, focus.w + 2, focus.h + 2};
            SDL_RenderDrawRect(ren, &outline);
        }
        SDL_SetRenderDrawColor(ren, r, g, b, a);
        SDL_SetRenderDrawBlendMode(ren, old_blend);
    }
