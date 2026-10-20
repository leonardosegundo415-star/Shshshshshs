// Predictive Input Buffer (Mobile) v2.0 - recreación desde cero para Android.
//
// Qué hace:
//  - Release buffer: un toque más corto que el buffer se mantiene pulsado hasta completar N ticks,
//    así aterrizas y saltas aunque sueltes antes.
//  - Press buffer en orbes: si pulsas hasta N ticks antes de tocar un orbe, el click se repite al tocarlo.
//  - Modos donde la duración del hold cambia el movimiento (ship, swing, wave, robot) usan el límite pequeño.
//
// Seguridad (casos que podían hacerte morir):
//  - Cada press se usa como máximo en UN orbe (evita clicks fantasma en cadenas de orbes).
//  - Si el press era para un orbe ya en contacto, no se reutiliza en el siguiente.
//  - Watchdog por tiempo real: si un hold retrasado se quedara pegado, se suelta solo.
//  - Todo se reinicia al morir, reiniciar o salir del nivel.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <chrono>

using namespace geode::prelude;

namespace pb {

    using Clock = std::chrono::steady_clock;

    struct Slot {
        bool held = false;            // el juego cree que el botón está pulsado
        bool pendingRelease = false;  // release retrasado
        bool consumed = true;         // true = este press ya no se puede reutilizar en un orbe
        int64_t pressTick = -1000000;
        int64_t releaseTick = 0;
        int limit = 0;
        Clock::time_point pressTime{};
        void* lastRing = nullptr;
        int64_t lastRingTick = -1000000;
    };

    inline int64_t g_tick = 0;
    inline Slot g_slots[2];            // 0 = jugador 1, 1 = jugador 2
    inline bool g_bypass = false;      // true mientras inyectamos inputs propios
    inline int g_allLimit = 2;
    inline int g_waveLimit = 2;
    inline int g_inputs = 0;           // estadísticas para la pantalla de pausa
    inline int g_holds = 0;
    inline int g_orbs = 0;

    inline void applyStage(std::string const& s) {
        int a = 2, w = 2;
        if (s == "4/4") { a = 4; w = 4; }
        else if (s == "8/8") { a = 8; w = 8; }
        else if (s == "16/8") { a = 16; w = 8; }
        else if (s == "TEST") { a = 60; w = 60; }
        else if (s == "OFF") { a = 0; w = 0; }
        g_allLimit = a;
        g_waveLimit = w;
    }

    inline void loadSetting() {
        applyStage(Mod::get()->getSettingValue<std::string>("stage"));
    }

    inline std::string nextStage(std::string const& cur) {
        static const char* order[] = {"2/2", "4/4", "8/8", "16/8", "TEST", "OFF"};
        for (int i = 0; i < 6; ++i) {
            if (cur == order[i]) return order[(i + 1) % 6];
        }
        return "2/2";
    }

    inline bool enabled() { return g_allLimit > 0 || g_waveLimit > 0; }

    // Aviso en pantalla cuando el buffer actúa (se apaga en los ajustes).
    inline void toast(std::string const& text) {
        if (!Mod::get()->getSettingValue<bool>("show-saves")) return;
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto old = scene->getChildByTag(98765)) old->removeFromParent();
        auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setTag(98765);
        auto win = CCDirector::get()->getWinSize();
        lbl->setPosition({win.width / 2, win.height - 20.f});
        lbl->runAction(CCSequence::create(CCDelayTime::create(0.5f), CCFadeOut::create(0.3f), nullptr));
        scene->addChild(lbl, 99999);
    }

    inline void resetState() {
        g_tick = 0;
        for (auto& s : g_slots) s = Slot{};
        g_bypass = false;
        loadSetting();
    }

    inline void resetStats() {
        g_inputs = 0;
        g_holds = 0;
        g_orbs = 0;
    }

    // En ship, swing, wave y robot la duración del hold cambia el movimiento: usan el límite pequeño.
    inline int limitForPlayer(PlayerObject* p) {
        if (!p) return g_allLimit;
        bool sensitive = p->m_isShip || p->m_isDart || p->m_isSwing || p->m_isRobot;
        return sensitive ? g_waveLimit : g_allLimit;
    }

    inline int limitFor(bool p1) {
        auto pl = PlayLayer::get();
        if (!pl) return 0;
        return limitForPlayer(p1 ? pl->m_player1 : pl->m_player2);
    }

    inline void releaseSlot(GJBaseGameLayer* layer, int i) {
        auto& s = g_slots[i];
        s.pendingRelease = false;
        s.held = false;
        g_bypass = true;
        layer->handleButton(false, 1, i == 0);
        g_bypass = false;
    }

    // Ejecuta los releases retrasados cuyo momento ya llegó (por ticks).
    inline void flushPending(GJBaseGameLayer* layer) {
        for (int i = 0; i < 2; ++i) {
            if (g_slots[i].pendingRelease && g_tick >= g_slots[i].releaseTick) {
                releaseSlot(layer, i);
            }
        }
    }

    // Seguro por tiempo real: si por cualquier motivo el contador de ticks no avanza,
    // un hold retrasado nunca se queda pegado.
    inline void watchdog(GJBaseGameLayer* layer) {
        auto now = Clock::now();
        for (int i = 0; i < 2; ++i) {
            auto& s = g_slots[i];
            if (!s.pendingRelease) continue;
            double elapsed = std::chrono::duration<double>(now - s.pressTime).count();
            double allowed = s.limit / 240.0 + 0.15;
            if (elapsed > allowed) releaseSlot(layer, i);
        }
    }
}

class $modify(PBGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        auto pl = PlayLayer::get();
        GJBaseGameLayer* self = this;
        if (pl && static_cast<GJBaseGameLayer*>(pl) == self) {
            pb::watchdog(self);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        GJBaseGameLayer* self = this;
        if (pb::g_bypass || button != 1 || !pl || static_cast<GJBaseGameLayer*>(pl) != self) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto& s = pb::g_slots[isPlayer1 ? 0 : 1];
        int limit = pb::limitFor(isPlayer1);
        if (down) pb::g_inputs++;

        // Buffer apagado: pasa todo tal cual, soltando antes lo pendiente.
        if (limit <= 0) {
            bool wasPending = s.pendingRelease;
            s.pendingRelease = false;
            if (wasPending && down) GJBaseGameLayer::handleButton(false, 1, isPlayer1);
            s.held = down;
            GJBaseGameLayer::handleButton(down, 1, isPlayer1);
            return;
        }

        if (down) {
            if (s.pendingRelease) {
                // Había un hold virtual: ciérralo con un release real antes del nuevo press.
                s.pendingRelease = false;
                GJBaseGameLayer::handleButton(false, 1, isPlayer1);
            }
            s.held = true;
            // Si había un orbe en contacto (o recién tocado), este press es para ese orbe.
            s.consumed = (pb::g_tick - s.lastRingTick) <= 4;
            s.pressTick = pb::g_tick;
            s.pressTime = pb::Clock::now();
            s.limit = limit;
            GJBaseGameLayer::handleButton(true, 1, isPlayer1);
        } else {
            if (!s.held) {
                GJBaseGameLayer::handleButton(false, 1, isPlayer1);
                return;
            }
            int64_t age = pb::g_tick - s.pressTick;
            if (age < s.limit) {
                // Toque más corto que el buffer: mantén el hold hasta completar los ticks.
                s.pendingRelease = true;
                s.releaseTick = s.pressTick + s.limit;
                pb::g_holds++;
                pb::toast("HOLD +" + std::to_string(s.limit - age));
            } else {
                s.held = false;
                s.pendingRelease = false;
                GJBaseGameLayer::handleButton(false, 1, isPlayer1);
            }
        }
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        GJBaseGameLayer::playerTouchedRing(player, ring);

        if (pb::g_bypass || !pb::enabled()) return;
        auto pl = PlayLayer::get();
        GJBaseGameLayer* self = this;
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != self) return;

        bool p1 = (player == m_player1);
        auto& s = pb::g_slots[p1 ? 0 : 1];

        // ¿Es un contacto nuevo con este orbe?
        bool newContact = s.lastRing != static_cast<void*>(ring) || (pb::g_tick - s.lastRingTick) > 1;
        s.lastRing = ring;
        s.lastRingTick = pb::g_tick;
        if (!newContact) return;

        bool usable = s.held && !s.consumed
            && s.pressTick < pb::g_tick
            && (pb::g_tick - s.pressTick) <= s.limit;

        // Un press sirve como máximo para el primer orbe que toca después.
        s.consumed = true;
        if (!usable) return;

        pb::g_orbs++;
        pb::toast("ORB +" + std::to_string(pb::g_tick - s.pressTick));

        // El press llegó unos ticks antes de tocar el orbe: repítelo ahora.
        pb::g_bypass = true;
        GJBaseGameLayer::handleButton(false, 1, p1);
        GJBaseGameLayer::handleButton(true, 1, p1);
        pb::g_bypass = false;
    }
};

class $modify(PBPlayer, PlayerObject) {
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (pl && this == pl->m_player1) {
            pb::g_tick++;
            pb::flushPending(pl);
        }
        PlayerObject::update(dt);
    }
};

class $modify(PBPlayLayer, PlayLayer) {
    void resetLevel() {
        pb::resetState();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        pb::resetState();
        pb::resetStats();
        PlayLayer::onQuit();
    }
};

class $modify(PBPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto stage = Mod::get()->getSettingValue<std::string>("stage");
        auto spr = ButtonSprite::create(("Buffer: " + stage).c_str());
        spr->setScale(0.7f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PBPause::onBufferCycle));
        auto menu = CCMenu::create();
        menu->setID("predictive-buffer-menu"_spr);
        menu->addChild(btn);

        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 70.f, 30.f});
        this->addChild(menu, 100);

        // Estadísticas: prueban si el mod recibe tus toques y cuántas veces actuó.
        auto stats = "Inputs " + std::to_string(pb::g_inputs)
            + " | Hold " + std::to_string(pb::g_holds)
            + " | Orb " + std::to_string(pb::g_orbs);
        auto info = CCLabelBMFont::create(stats.c_str(), "bigFont.fnt");
        info->setScale(0.3f);
        info->setPosition({win.width - 70.f, 56.f});
        this->addChild(info, 100);
    }

    void onBufferCycle(CCObject* sender) {
        auto next = pb::nextStage(Mod::get()->getSettingValue<std::string>("stage"));
        Mod::get()->setSettingValue<std::string>("stage", next);
        pb::applyStage(next);

        if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
            if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
                spr->setString(("Buffer: " + next).c_str());
            }
        }
    }
};
