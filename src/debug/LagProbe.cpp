#include "LagProbe.hpp"
#include "AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include <fstream>

using namespace geode::prelude;

LagProbe& LagProbe::get() {
    static LagProbe inst;
    return inst;
}

LagProbe::Scope::Scope(char const* name, std::string detail) {
    this->m_name = name;
    if (!LagProbe::get().enabled()) return;
    // Only nest when an episode is open (death/reset/checkpoint).
    if (!LagProbe::get().episodeOpen()) return;
    this->m_active = true;
    LagProbe::get().begin(name, std::move(detail));
}

LagProbe::Scope::~Scope() {
    if (!this->m_active) return;
    LagProbe::get().end(this->m_name);
}

bool LagProbe::enabled() const {
    return this->m_enabled;
}

void LagProbe::setEnabled(bool on) {
    if (this->m_enabled == on) return;
    this->m_enabled = on;
    if (on) {
        this->reset();
        this->writeLegend();
        this->flush();
        AssistLog::info(fmt::format("LagProbe ON → {}", logPath()));
        return;
    }
    if (this->m_episodeOpen) this->endEpisode();
    this->flush();
    AssistLog::info("LagProbe OFF");
}

std::string LagProbe::logPath() {
    auto* mod = Mod::get();
    if (!mod) return {};
    return (mod->getSaveDir() / "earlybuffer-lagprobe.log").string();
}

void LagProbe::reset() {
    this->m_episodeOpen = false;
    this->m_episodeName = nullptr;
    this->m_depth = 0;
    this->m_pending.clear();
    this->m_pendingLines = 0;
}

void LagProbe::writeToFile(std::string const& text) {
    if (text.empty()) return;
    auto const path = logPath();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    out << text;
    if (!text.empty() && text.back() != '\n') out << '\n';
}

void LagProbe::appendLine(std::string const& line) {
    this->m_pending += line;
    this->m_pending.push_back('\n');
    ++this->m_pendingLines;
    // Keep death episodes durable — flush often so a crash still leaves data.
    if (this->m_pendingLines >= 24) {
        this->writeToFile(this->m_pending);
        this->m_pending.clear();
        this->m_pendingLines = 0;
    }
}

void LagProbe::flush() {
    if (!this->m_pending.empty()) {
        this->writeToFile(this->m_pending);
        this->m_pending.clear();
        this->m_pendingLines = 0;
    }
}

void LagProbe::writeLegend() {
    this->appendLine("================================================================");
    this->appendLine("EarlyBuffer LagProbe — death / reset / checkpoint timings");
    this->appendLine(fmt::format("file: {}", logPath()));
    this->appendLine("----------------------------------------------------------------");
    this->appendLine("HOW TO USE");
    this->appendLine("  1. Leave Lag Probe ON. Die at the laggy spot. Restart / continue.");
    this->appendLine("  2. Open this file; jump to the latest EPISODE BEGIN death/resetLevel.");
    this->appendLine("  3. Sort by '= X.XXms' — biggest number = cost driver.");
    this->appendLine("  4. If vanilla.PlayLayer::resetLevel (or loadFromCheckpoint) wins,");
    this->appendLine("     the hitch is GD/other mods. If evacuate*/flush*/learn* wins,");
    this->appendLine("     it is EarlyBuffer — send that EPISODE block.");
    this->appendLine("----------------------------------------------------------------");
    this->appendLine("EPISODE NAMES");
    this->appendLine("  death              PlayLayer::destroyPlayer real-player path");
    this->appendLine("  resetLevel         PlayLayer::resetLevel (attempt restart)");
    this->appendLine("  loadFromCheckpoint practice checkpoint reload");
    this->appendLine("----------------------------------------------------------------");
    this->appendLine("EVENT NAMES (inside episodes)");
    this->appendLine("  eb.onRealDeath              our death bookkeeping (no physics)");
    this->appendLine("  eb.outcomeNote              DivergenceTracker on pending plan");
    this->appendLine("  eb.learnDeath               LevelProfile noteDied / earliness");
    this->appendLine("  eb.maybeAutosave            profile dirty/decay (no disk ideal)");
    this->appendLine("  eb.saveCounterClear         SaveFrameCounter::clear markers");
    this->appendLine("  eb.syncButtons              stuck-jump button sync");
    this->appendLine("  vanilla.destroyPlayer       GD PlayLayer::destroyPlayer rest");
    this->appendLine("  eb.bakeCancel               BakeScheduler::cancel");
    this->appendLine("  eb.evacuateGhosts           pull ghosts out of section vectors");
    this->appendLine("  eb.evacuate.scrub           full/targeted section scrub");
    this->appendLine("  eb.evacuate.tryRemove       index-based erase per ghost");
    this->appendLine("  eb.evacuate.park            park/hide ghost bodies");
    this->appendLine("  eb.resetAttempt             clear buffers / flush self-bake");
    this->appendLine("  eb.flushSegment             (deferred on reset/checkpoint)");
    this->appendLine("  eb.selfBake.idleMerge       amortized pending→BakeMap merge");
    this->appendLine("  eb.selfBake.idleRefine      amortized window refine (few sims/tick)");
    this->appendLine("  eb.selfBake.flushMergeOnly  quit/complete force-merge, no refine");
    this->appendLine("  eb.triggerBake.load         TriggerWorldStore::loadForLevel");
    this->appendLine("  eb.triggerBake.build        TriggerWorldBaker slice / kinematic build");
    this->appendLine("  eb.triggerBake.save         TriggerWorldStore::save (quit/complete)");
    this->appendLine("  eb.trigger.capture          snapshot transforms for bake tracks");
    this->appendLine("  eb.trigger.apply            write baked poses at abs tick (sim)");
    this->appendLine("  eb.trigger.restore          restore transforms after sim / evacuate");
    this->appendLine("  triggerWorld.checkpoint     mark: keep store, attemptTick reset");
    this->appendLine("  eb.saveFeedbackReset        SaveFeedback::reset");
    this->appendLine("  eb.clickGuardReset          ClickIntentGuard::reset");
    this->appendLine("  eb.updateOverlay            status overlay string refresh");
    this->appendLine("  vanilla.PlayLayer::resetLevel   GD attempt rebuild (often huge)");
    this->appendLine("  vanilla.loadFromCheckpoint  GD checkpoint apply");
    this->appendLine("  eb.onCheckpointLoad         our checkpoint-side flush");
    this->appendLine("================================================================");
}

double LagProbe::msSinceEpisode() const {
    if (!this->m_episodeOpen) return 0.0;
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - this->m_episodeStart
    ).count() / 1000.0;
}

std::string LagProbe::indent() const {
    // depth 0 → two spaces under episode; nested scopes add two each.
    return std::string(static_cast<size_t>(2 + this->m_depth * 2), ' ');
}

void LagProbe::beginEpisode(char const* name, std::string detail) {
    if (!this->m_enabled || !name) return;
    if (this->m_episodeOpen) this->endEpisode();

    this->m_episodeOpen = true;
    this->m_episodeName = name;
    this->m_episodeStart = std::chrono::steady_clock::now();
    this->m_depth = 0;

    if (detail.empty()) {
        this->appendLine(fmt::format("EPISODE BEGIN {}", name));
    }
    else {
        this->appendLine(fmt::format("EPISODE BEGIN {} | {}", name, detail));
    }
}

void LagProbe::endEpisode() {
    if (!this->m_enabled || !this->m_episodeOpen) return;

    while (this->m_depth > 0) {
        this->end(this->m_stack[this->m_depth - 1].name
            ? this->m_stack[this->m_depth - 1].name
            : "?");
    }

    double const total = this->msSinceEpisode();
    char const* name = this->m_episodeName ? this->m_episodeName : "?";
    this->appendLine(fmt::format(
        "EPISODE END {} total={:.2f}ms",
        name,
        total
    ));
    this->appendLine("----");
    this->m_episodeOpen = false;
    this->m_episodeName = nullptr;
    this->flush();
}

void LagProbe::begin(char const* name, std::string detail) {
    if (!this->m_enabled || !this->m_episodeOpen || !name) return;
    if (this->m_depth >= kMaxDepth) return;

    if (detail.empty()) {
        this->appendLine(fmt::format(
            "+{:.2f}ms {}> {}",
            this->msSinceEpisode(),
            this->indent(),
            name
        ));
    }
    else {
        this->appendLine(fmt::format(
            "+{:.2f}ms {}> {} | {}",
            this->msSinceEpisode(),
            this->indent(),
            name,
            detail
        ));
    }

    this->m_stack[this->m_depth].name = name;
    this->m_stack[this->m_depth].start = std::chrono::steady_clock::now();
    ++this->m_depth;
}

void LagProbe::end(char const* name) {
    if (!this->m_enabled || !this->m_episodeOpen || this->m_depth <= 0) return;

    --this->m_depth;
    auto const& fr = this->m_stack[this->m_depth];
    char const* n = name;
    if (fr.name && fr.name[0]) n = fr.name;

    double const us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - fr.start
        ).count()
    );

    this->appendLine(fmt::format(
        "+{:.2f}ms {}< {} = {:.2f}ms",
        this->msSinceEpisode(),
        this->indent(),
        n ? n : "?",
        us / 1000.0
    ));
}

void LagProbe::mark(char const* name, std::string detail) {
    if (!this->m_enabled || !this->m_episodeOpen || !name) return;
    if (detail.empty()) {
        this->appendLine(fmt::format(
            "+{:.2f}ms {}MARK {}",
            this->msSinceEpisode(),
            this->indent(),
            name
        ));
        return;
    }
    this->appendLine(fmt::format(
        "+{:.2f}ms {}MARK {} | {}",
        this->msSinceEpisode(),
        this->indent(),
        name,
        detail
    ));
}
