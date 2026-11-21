#include "neural_session.hpp"
#include "../../app/class_manager.hpp"

EbNeuralSession& EbNeuralSession::get() {
    return EbClassManager::get().service<EbNeuralSession>();
}
