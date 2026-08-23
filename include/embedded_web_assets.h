#pragma once

// Lookup API for the frontend build embedded directly into this binary, so
// the compiled server is a genuinely single, self-contained executable --
// no web/dist/ directory has to travel with it, unlike before. The actual
// byte data lives in src/server/embedded_web_assets.cpp, which is
// generated fresh from web/dist/ by scripts/embed_web_assets.py as part of
// every build (see build.sh) -- not committed, regenerated every time,
// same as web/dist/ itself.

#include <cstddef>
#include <string>

struct EmbeddedAsset {
  const unsigned char* data;
  size_t size;
};

// Returns the embedded asset for a URL path (e.g. "/index.html",
// "/css/index-XXX.css"), or nullptr if nothing embedded matches.
const EmbeddedAsset* find_embedded_web_asset(const std::string& path);
