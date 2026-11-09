#include "sim_session_guard.hpp"
#include "shadow_simulator.hpp"

EbSimSessionGuard::EbSimSessionGuard(EbShadowSimulator& sim, PlayLayer* layer)
    : m_sim(&sim), m_layer(layer) {
    this->m_sim->beginSession(this->m_layer);
}

EbSimSessionGuard::~EbSimSessionGuard() {
    if (!this->m_sim)
        return;
    this->m_sim->endSession(this->m_layer);
    this->m_sim = nullptr;
    this->m_layer = nullptr;
}

EbSimSessionGuard::EbSimSessionGuard(EbSimSessionGuard&& other) noexcept
    : m_sim(other.m_sim), m_layer(other.m_layer) {
    other.m_sim = nullptr;
    other.m_layer = nullptr;
}

EbSimSessionGuard& EbSimSessionGuard::operator=(EbSimSessionGuard&& other) noexcept {
    if (this == &other)
        return *this;
    if (this->m_sim) {
        this->m_sim->endSession(this->m_layer);
    }
    this->m_sim = other.m_sim;
    this->m_layer = other.m_layer;
    other.m_sim = nullptr;
    other.m_layer = nullptr;
    return *this;
}
