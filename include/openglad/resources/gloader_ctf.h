/* Capture-the-flag loader registration.
 *
 * Fills the loader's per-family data tables for the CTF treasure families
 * (flag, control point) after construction, so the base loader tables stay
 * untouched. Sprite files fall back to existing treasure art when absent.
 */
#pragma once

class loader;

void register_ctf_loader_entries(loader& l);

// Single-family registration, exposed so tests can exercise the
// missing-sprite fallback path with a nonexistent pix filename.
void register_ctf_treasure_entry(loader& l, int family, const char* pix_file,
                                 int fallback_family);
