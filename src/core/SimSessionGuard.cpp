#include "SimSessionGuard.hpp"
#include "ShadowSimulator.hpp"

SimSessionGuard::SimSessionGuard(ShadowSimulator& sim, PlayLayer* layer)
    : m_sim(&sim)
    , m_layer(layer) {
    this->m_sim->beginSession(this->m_layer);
}

SimSessionGuard::~SimSessionGuard() {
    if (!this->m_sim) return;
    this->m_sim->endSession(this->m_layer);
    this->m_sim = nullptr;
    this->m_layer = nullptr;
}

SimSessionGuard::SimSessionGuard(SimSessionGuard&& other) noexcept
    : m_sim(other.m_sim)
    , m_layer(other.m_layer) {
    other.m_sim = nullptr;
    other.m_layer = nullptr;
}

SimSessionGuard& SimSessionGuard::operator=(SimSessionGuard&& other) noexcept {
    if (this == &other) return *this;
    if (this->m_sim) {
        this->m_sim->endSession(this->m_layer);
    }
    this->m_sim = other.m_sim;
    this->m_layer = other.m_layer;
    other.m_sim = nullptr;
    other.m_layer = nullptr;
    return *this;
}
