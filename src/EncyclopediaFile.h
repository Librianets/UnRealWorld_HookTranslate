#pragma once
#include "TranslationCatalog.h"
#include <cstdint>
#include <string>
#include <vector>

namespace urwtranslation {
// Produces the entire GAME.NFO with CRLF and CP1251 translated bodies.
// Disabled documents render source, with strict CP437 fallback for English
// source characters unavailable in CP1251. Raw header bytes remain unchanged.
// On failure bytes is cleared; a previous published file must remain untouched.
bool BuildEncyclopedia(const std::vector<DocumentEntry>& documents,
                       std::string& bytes, std::string& error, unsigned codePage = 1251);

// Call once before the game builds its encyclopedia index. Addresses refer to
// a mapped, relocated clean 3.88 image; no executable file on disk is touched.
// Reserves zeroed process-lifetime storage for 1000 x 200-byte synopses and
// redirects the two verified native operands. Repeating for the same image is
// idempotent and never zeroes an already populated replacement table.
bool InstallSynopsisTable(std::uintptr_t imageBase, std::string& error);
}
