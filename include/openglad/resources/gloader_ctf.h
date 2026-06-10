/* Capture-the-flag loader registration.
 *
 * Fills the loader's per-family data tables for the CTF treasure families
 * (flag, control point) after construction, so the base loader tables stay
 * untouched. Sprite files fall back to existing treasure art when absent.
 */
#pragma once

class loader;

void register_ctf_loader_entries(loader& l);
