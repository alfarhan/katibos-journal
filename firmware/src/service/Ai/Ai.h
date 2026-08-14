#pragma once

#include <ArduinoJson.h>
#include <WString.h>

// On-demand spelling correction of the OPEN file through Google Gemini (Ctrl+G).
//
// Whole-document: the editor's 8000-byte buffer is only a sliding WINDOW over
// the file, so this works on the file on disk, not the buffer. The text and the
// reply are both held in RAM at once (PSRAM is enabled on both boards), capped
// by AI_MAX_BYTES.
//
// The replace is far too large for the editor's 2KB undo arena, so undo cannot
// cover it. The original file is moved to "<file>_backup.txt" first - that IS
// the undo, and it uses the same naming the delete path already does.

// Progress states, mirrored into app["ai_state"]; the editor status bar renders
// app["ai_message"] whenever a pass is running.
#define AI_IDLE 0
#define AI_RUNNING 1
#define AI_DONE 2
#define AI_ERROR 3

// Refuse a document larger than this (request + reply must both fit).
static const long AI_MAX_BYTES = 24 * 1024;

// How long to wait for the model. Generous on purpose: this call generates text,
// so it is nothing like the instant API calls the sync path makes, and the
// transport's 5s default cuts it off mid-reply (-11 READ_TIMEOUT).
static const unsigned long AI_TIMEOUT_MS = 90000;

// True when config.ai.key is present, i.e. the feature is usable at all.
bool ai_configured(JsonDocument &app);

// Reset state and ask the background task to run a pass on the open file.
// Returns false (and sets ai_state/ai_message) if it can't start.
bool ai_request(JsonDocument &app);

// Pumped from app_loop() on the background core, like sync_loop(). Picks up the
// request, runs the whole blocking pass, publishes progress as it goes.
void ai_loop();
