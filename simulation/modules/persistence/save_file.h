#pragma once

// save_file — the bridge between the serializer and the disk.
//
// PersistenceModule has produced a complete, deterministic, byte-stable save
// image since schema v7, and nothing ever wrote one to a file: its execute()
// was a stub whose body was a comment saying the game loop would do it, and no
// game loop did. A session could not survive being closed, which makes a
// two-session playtest impossible and — per the GDD — hollows out the
// consequence architecture, since "no reload" only means something if there is
// a save to come back to.
//
// Writes are crash-safe by construction: the image goes to a temporary file in
// the same directory, is flushed to the platform's satisfaction, and is then
// renamed over the target. Rename within a directory is atomic, so a reader
// sees either the whole previous save or the whole new one, never a torn file.
// The previous save is kept alongside as .bak before the rename, so a crash
// during the flush still leaves a playable game.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

struct WorldState;
class ITickModule;
class TickOrchestrator;

struct SaveResult {
    bool ok = false;
    std::string error;      // human-readable reason when !ok
    std::string path;       // the file written or read
    std::size_t bytes = 0;  // image size
    uint32_t schema_version = 0;
};

// Write `blob` to `path` atomically, keeping the previous contents at
// `path`.bak. Creates parent directories as needed.
SaveResult write_save_bytes(const std::string& path, const std::vector<uint8_t>& blob);

// Read a save image. Fails cleanly (never throws) on a missing or unreadable
// file.
SaveResult read_save_bytes(const std::string& path, std::vector<uint8_t>& out);

// Serialize the world (including every module's private state, in the
// orchestrator's registration order) and write it atomically.
//
// Refuses while the cross-province delta buffer holds entries: those are
// effects produced in one tick and consumed in the next, so a save taken
// between the two would lose them. This is PersistenceModule::is_save_allowed,
// enforced at the only place that can actually observe it.
SaveResult save_game(const std::string& path, const WorldState& world,
                     const TickOrchestrator& orchestrator);

// Read a save image and restore it into `world`, including module-private
// state. On failure `world` is left untouched.
SaveResult load_game(const std::string& path, WorldState& world,
                     const TickOrchestrator& orchestrator);

// The conventional file name for the rolling autosave inside a save directory.
std::string autosave_path(const std::string& save_dir);

}  // namespace econlife
