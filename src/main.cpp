#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cmath>

using namespace geode::prelude;

namespace {
    struct Stage {
        int allTicks;
        int waveTicks;
        bool enabled;
    };

    constexpr Stage stages[] = {
        {2, 2, true},
        {4, 4, true},
        {8, 8, true},
        {16, 8, true},
        {0, 0, false}
    };

    constexpr double TICKS_PER_SECOND = 240.0;

    struct PlayerState {
        bool held = false;
        bool pending = false;
        int pressTick = 0;
        int releaseAtTick = -1;
    };

    int currentStage = 0;
    bool logSaves = true;
    int tickCounter = 0;
    bool injecting = false;
    PlayerState players[2];

    int currentLimit() {
        auto const& stage = stages[currentStage];
        return stage.enabled ? stage.allTicks : 0;
    }

    void resetState() {
        tickCounter = 0;
        injecting = false;
        for (auto& p : players) p = PlayerState{};
    }

    void logStage() {
        auto const& stage = stages[currentStage];

        if (!stage.enabled) {
            log::info("[InputBuffer] Stage 5: OFF (100% Raw Input)");
            return;
        }

        log::info(
            "[InputBuffer] Stage {}: {} Ticks All | {} Ticks Wave",
            currentStage + 1,
            stage.allTicks,
            stage.waveTicks
        );
    }

    void nextStage() {
        currentStage = (currentStage + 1) % 5;
        logStage();
    }

    bool isCube(PlayerObject* p) {
        return !p->m_isShip && !p->m_isBird && !p->m_isBall &&
               !p->m_isDart && !p->m_isRobot && !p->m_isSpider &&
               !p->m_isSwing;
    }

    bool hazardNear(GJBaseGameLayer* layer, PlayerObject* p) {
        auto pos = p->getPosition();
        for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj || obj->m_objectType != GameObjectType::Hazard) continue;
            auto opos = obj->getPosition();
            if (std::fabs(opos.x - pos.x) < 60.f && std::fabs(opos.y - pos.y) < 60.f) {
                return true;
            }
        }
        return false;
    }
}

class $modify(PredictiveInputBufferKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        enumKeyCodes key,
        bool isKeyDown,
        bool isKeyRepeat,
        double timestamp
    ) {
        if (isKeyDown && !isKeyRepeat) {
            if (key == enumKeyCodes::KEY_F6) {
                nextStage();
            } else if (key == enumKeyCodes::KEY_F7) {
                logSaves = !logSaves;
                log::info("[InputBuffer] Save logging {}", logSaves ? "ON" : "OFF");
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(
            key, isKeyDown, isKeyRepeat, timestamp
        );
    }
};

class $modify(PredictiveInputBufferGame, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        PlayerObject* p = isPlayer1 ? m_player1 : m_player2;
        bool airborneCube = p && !p->m_isOnGround && isCube(p);

        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (injecting || button != 1) return;

        auto& st = players[isPlayer1 ? 0 : 1];
        st.held = down;

        if (down && airborneCube && currentLimit() > 0) {
            st.pending = true;
            st.pressTick = tickCounter;
        }
    }

        void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick) tickCounter++;
        int limit = currentLimit();

        for (int i = 0; i < 2; i++) {
            auto& st = players[i];
            PlayerObject* p = (i == 0) ? m_player1 : m_player2;

            if (st.releaseAtTick >= 0 && tickCounter >= st.releaseAtTick) {
                st.releaseAtTick = -1;
                injecting = true;
                this->handleButton(false, 1, i == 0);
                injecting = false;
            }

            if (!st.pending) continue;

            if (!p || limit == 0 || tickCounter - st.pressTick > limit) {
                st.pending = false;
                continue;
            }

            if (!p->m_isOnGround) continue;

            st.pending = false;
            if (st.held) continue; // game's own hold-to-jump handles this

            int saved = tickCounter - st.pressTick;

            injecting = true;
            this->handleButton(true, 1, i == 0);
            injecting = false;
            st.releaseAtTick = tickCounter + 1;

            if (logSaves) {
                double interval = CCDirector::get()->getAnimationInterval();
                int fps = interval > 0 ? static_cast<int>(std::round(1.0 / interval)) : 60;
                double frames = saved * fps / TICKS_PER_SECOND;
                log::info(
                    "[InputBuffer] *** SAVED +{} TICKS ({:.2f} frames @ {}fps){} (P{}) (Limit: {}) ***",
                    saved, frames, fps,
                    hazardNear(this, p) ? " HAZARD!" : "",
                    i + 1, limit
                );
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(PredictiveInputBufferPlay, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        resetState();
    }
};

$on_mod(Loaded) {
    log::info("[InputBuffer] Loaded. F6 cycles stages, F7 toggles save logging.");
    logStage();
}
