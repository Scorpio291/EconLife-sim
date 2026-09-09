#include "save_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/tick/tick_orchestrator.h"
#include "core/world_state/world_state.h"
#include "persistence_module.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace econlife {

namespace fs = std::filesystem;

namespace {

// Flush the file's bytes to the storage device, not merely to the C++ stream
// buffer. Without this the rename below can be ordered ahead of the data on a
// crash, and the save is atomically... empty.
bool sync_file(std::FILE* f) {
    if (f == nullptr)
        return false;
    if (std::fflush(f) != 0)
        return false;
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0;
#else
    return ::fsync(fileno(f)) == 0;
#endif
}

std::vector<ITickModule*> module_pointers(const TickOrchestrator& orchestrator) {
    std::vector<ITickModule*> mods;
    mods.reserve(orchestrator.modules().size());
    for (const auto& m : orchestrator.modules())
        mods.push_back(m.get());
    return mods;
}

std::vector<const ITickModule*> const_module_pointers(const TickOrchestrator& orchestrator) {
    std::vector<const ITickModule*> mods;
    mods.reserve(orchestrator.modules().size());
    for (const auto& m : orchestrator.modules())
        mods.push_back(m.get());
    return mods;
}

}  // namespace

SaveResult write_save_bytes(const std::string& path, const std::vector<uint8_t>& blob) {
    SaveResult r{};
    r.path = path;
    r.bytes = blob.size();

    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path() && !target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            r.error = "cannot create save directory: " + ec.message();
            return r;
        }
    }

    const fs::path tmp = fs::path(path + ".tmp");

    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        if (f == nullptr) {
            r.error = "cannot open " + tmp.string() + " for writing";
            return r;
        }
        if (!blob.empty()) {
            const std::size_t written = std::fwrite(blob.data(), 1, blob.size(), f);
            if (written != blob.size()) {
                std::fclose(f);
                fs::remove(tmp, ec);
                r.error = "short write to " + tmp.string();
                return r;
            }
        }
        const bool synced = sync_file(f);
        std::fclose(f);
        if (!synced) {
            fs::remove(tmp, ec);
            r.error = "cannot flush " + tmp.string() + " to disk";
            return r;
        }
    }

    // Keep the outgoing save. If anything goes wrong from here the player has
    // a complete previous game to come back to.
    if (fs::exists(target, ec)) {
        fs::rename(target, fs::path(path + ".bak"), ec);
        ec.clear();  // a failed backup is not a reason to lose the new save
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        r.error = "cannot commit save: " + ec.message();
        fs::remove(tmp, ec);
        return r;
    }

    r.ok = true;
    return r;
}

SaveResult read_save_bytes(const std::string& path, std::vector<uint8_t>& out) {
    SaveResult r{};
    r.path = path;

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        r.error = "no save at " + path;
        return r;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        r.error = "cannot open " + path;
        return r;
    }
    const std::streamsize size = in.tellg();
    if (size < 0) {
        r.error = "cannot size " + path;
        return r;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(out.data()), size)) {
        r.error = "short read from " + path;
        out.clear();
        return r;
    }

    r.ok = true;
    r.bytes = out.size();
    return r;
}

SaveResult save_game(const std::string& path, const WorldState& world,
                     const TickOrchestrator& orchestrator) {
    SaveResult r{};
    r.path = path;

    // Cross-province effects are produced in one tick and consumed in the next.
    // A save taken between the two would drop them, so refuse rather than write
    // a game that has quietly lost a tick's work.
    if (!PersistenceModule::is_save_allowed(world.cross_province_delta_buffer.entries.empty())) {
        r.error = "cross-province effects are still in flight; save at a tick boundary";
        return r;
    }

    std::vector<uint8_t> blob;
    try {
        blob = PersistenceModule::serialize(world, const_module_pointers(orchestrator));
    } catch (const std::exception& e) {
        r.error = std::string("serialize failed: ") + e.what();
        return r;
    }

    SaveResult w = write_save_bytes(path, blob);
    w.schema_version = PersistenceModule::CURRENT_SCHEMA_VERSION;
    return w;
}

SaveResult load_game(const std::string& path, WorldState& world,
                     const TickOrchestrator& orchestrator) {
    std::vector<uint8_t> blob;
    SaveResult r = read_save_bytes(path, blob);
    if (!r.ok)
        return r;

    // Restore into a scratch world first: a rejected save must not leave the
    // player holding a half-loaded game.
    WorldState restored{};
    const RestoreResult rr =
        PersistenceModule::deserialize(blob, restored, module_pointers(orchestrator));
    if (rr != RestoreResult::success) {
        r.ok = false;
        r.error = "save rejected (schema or format mismatch)";
        return r;
    }

    // Reference data is CONTENT, not state: recipes, the era ladder, the
    // occupation vocabulary, the technology tree and the per-era tech effects
    // are loaded from packages/base_game at world generation and never change
    // during play. They are deliberately not in the save — putting them there
    // would bloat every file and, worse, freeze content at save time, so a
    // corrected recipe or a new era could never reach a game already in
    // progress. That is the whole point of the data-driven doctrine.
    //
    // So they are carried across from the world that was generated for this
    // session rather than restored. Without this the loaded world had no
    // recipes at all: production could not resolve a single facility's output,
    // and the resumed game quietly diverged from the one that was saved.
    restored.loaded_recipes = std::move(world.loaded_recipes);
    restored.era_catalog = std::move(world.era_catalog);
    restored.occupation_catalog = std::move(world.occupation_catalog);
    restored.technology_catalog = world.technology_catalog;
    restored.tech_effects_by_era = std::move(world.tech_effects_by_era);
    if (!restored.goods_catalog && world.goods_catalog)
        restored.goods_catalog = std::move(world.goods_catalog);

    world = std::move(restored);
    r.ok = true;
    r.schema_version = world.current_schema_version;
    return r;
}

std::string autosave_path(const std::string& save_dir) {
    return (fs::path(save_dir) / "autosave.econsave").string();
}

}  // namespace econlife
