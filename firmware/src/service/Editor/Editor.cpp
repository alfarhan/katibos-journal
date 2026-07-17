#include "Editor.h"
#include "app/app.h"
#include "display/display.h"

//
#include "service/WordCounter/WordCounter.h"
#include "service/Bidi/Bidi.h"
#include "service/Tools/TextUtil.h"
#include "service/Clock/Clock.h"
#include "service/Editor/EditorWindow.h"
#include "service/Editor/WordNav.h"

// On the Arabic-capable build (rev 8) text is stored as UTF-8, so cursor
// moves, deletes and line wrapping step by whole UTF-8 characters (code
// points). Other revisions keep their original single-byte codepage behaviour
// untouched - their renderers expect it and never produce code points > 255.
#ifdef REV8
static constexpr bool UTF8_MODE = true;
#else
static constexpr bool UTF8_MODE = false;
#endif

// A byte with the top two bits 10xxxxxx is a UTF-8 continuation byte.
static inline bool isUtf8Cont(char c) { return UTF8_MODE && (((uint8_t)c & 0xC0) == 0x80); }

// Bytes occupied by the character starting at this lead byte (always 1 unless
// UTF-8 mode is on).
static inline int charLen(uint8_t lead) { return UTF8_MODE ? bidi::utf8CharLen(lead) : 1; }

// Internal clipboard, shared across the singleton editor.
char Editor::clipboard[BUFFER_SIZE + 1] = {0};
int Editor::clipboardLength = 0;

//
// EDITOR CLASS IMPLEMENTATION
//

//
// Editor Initialization with column and row setup
void Editor::init(int cols, int rows)
{
    // Define Screen Size
    this->cols = cols;
    this->rows = rows;

    //
    resetBuffer();
    updateScreen();

    //
    savingInProgress = false;

    // You can add any additional setup logic here
    _log("Editor initialized with columns: %d, rows: %d\n", cols, rows);
}

// Given the fileName, go through the loading process
// initialize FileBuffer and ScreenBuffer
void Editor::loadFile(String fileName)
{
    if (savingInProgress)
    {
        _log("Save is is progress. Load file skipped\n");
        return;
    }

    // app status
    JsonDocument &app = status();

    // Defensive: an empty path would otherwise dead-end on the (reboot-only)
    // error screen. Fall back to the configured slot so the editor still opens
    // instead of stranding the writer.
    if (fileName.length() == 0)
    {
        fileName = format("/%d.txt", app["config"]["file_index"].as<int>());
        _log("loadFile: empty filename, falling back to %s\n", fileName.c_str());
    }

    //
    _log("Editor loading file [%s]\n", fileName.c_str());

    // Step 1. Create file if necessary
    if (!gfs()->exists(fileName.c_str()))
    {
        _debug("Creating an empty file since it's new\n");
        File file = gfs()->open(fileName.c_str(), "w");
        if (!file)
        {
            //
            app["error"] = format("Failed to create a file. %s\n", fileName.c_str());
            app["screen"] = ERRORSCREEN;

            //
            _log(app["error"].as<const char *>());

            return;
        }

        //
        file.close();
        delay(100);

        //
        _debug("File created. %s\n", fileName.c_str());

        // A freshly created file is empty, so reset any cached metadata left
        // over from a previously deleted file at this slot - otherwise a stale
        // title would make the new file look like an old one reappeared.
        int idx = atoi(fileName.c_str() + 1); // "/3.txt" -> 3
        app["config"][format("title_%d", idx)] = "";
        app["config"][format("title_manual_%d", idx)] = false;
        app["config"][format("unsynced_%d", idx)] = false;
        app["config"][format("wordcount_file_%d", idx)] = 0;
        app["config"][format("wordcount_buffer_%d", idx)] = 0;
        app["config"][format("edited_%d", idx)] = 0;
        // Stats baseline starts at 0 for a brand-new file, so everything typed
        // into it counts as words written today. (A file that pre-dates this
        // feature has no baseline key and is instead seeded on first save
        // without counting its existing content - see saveFile.)
        app["config"][format("last_wc_%d", idx)] = 0;
        app["config"][format("caret_%d", idx)] = 0; // new file opens at the top
        config_save();
    }

    // Save filen name
    this->fileName = fileName;

    // Open File
    File file = gfs()->open(fileName.c_str(), "r");
    if (!file)
    {
        //
        app["error"] = format("file open failed %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;

        //
        _debug(app["error"]);

        return;
    }

    // Determine file size and set buffer accordingly
    fileSize = file.size();
    _debug("File: %s of size: %d\n", fileName.c_str(), fileSize);

    // calcualte the file offset
    seekPos = 0;
    int stepSize = BUFFER_SIZE / 2; // use half of the buffer
    if (fileSize > 0)
    {
        // this offset will offer last portion of the buffer
        seekPos = (fileSize / stepSize) * stepSize;
    }

    // when it is exactly the buffer end
    // go one buffer behind so that screen will show something
    if (fileSize == seekPos && seekPos > 0)
    {
        if (seekPos > stepSize)
            seekPos -= stepSize;
        else
            // defensive code in order for the offset not to go negative (MAX in unsigned in)
            seekPos = 0;
    }

    // RESUME AT LAST POSITION: if a caret offset was saved for this slot, load
    // the window around it instead of the tail, so the file reopens where the
    // writer left off (caret + scrollback). Pure window math (unit-tested);
    // -1 means "no saved caret", fall back to the tail window above.
    int file_index = app["config"]["file_index"].as<int>();
    int resumeCursor = -1;
    {
        String ckey = format("caret_%d", file_index);
        if (!app["config"][ckey].isNull())
        {
            long os = 0, oc = 0;
            editorResumeWindow(app["config"][ckey].as<long>(), (long)fileSize, BUFFER_SIZE, os, oc);
            seekPos = (size_t)os;
            resumeCursor = (int)oc;
        }
    }

    _log("File seekPos: %d\n", seekPos);

    // move the file position to offset
    if (!file.seek(seekPos))
    {
        //
        file.close();
        delay(100);

        //
        app["error"] = format("Failed to seek file pointer. fileSize: %d seekPos: %d\n", fileSize, seekPos);
        app["screen"] = ERRORSCREEN;
        _debug(app["error"].as<const char *>());

        return;
    }

    // reset the buffer
    resetBuffer();

    // Read file content into text buffer. Cap at BUFFER_SIZE: the tail window
    // never exceeds it, but a resume window can start far from EOF, so reading
    // to EOF unguarded could overflow the buffer.
    int bufferSize = 0;
    while (file.available() && bufferSize < BUFFER_SIZE)
    {
        buffer[bufferSize++] = file.read();
    }
    cursorPos = (resumeCursor >= 0 && resumeCursor <= bufferSize) ? resumeCursor : bufferSize;
    clearSelection();
    clearUndo();

    // this window mirrors [seekPos, seekPos+bufferSize) on disk exactly
    loadedLength = bufferSize;
    pageChanged = true;

    //
    file.close();
    delay(100);

    // log
    _debug("Editor::loadFile size: %d, seek: %d, buffer: %d, cursor: %d\n",
           fileSize, seekPos, bufferSize, cursorPos);

    // Update the Screen Buffer
    updateScreen();

    // Update the word count
    wordCountFile = wordcounter_file(fileName.c_str());
    wordCountBuffer = wordcounter_buffer(buffer);

    // update the word count in config
    app["config"][format("wordcount_file_%d", file_index)] = wordCountFile;
    app["config"][format("wordcount_buffer_%d", file_index)] = wordCountBuffer;

    //
    config_save();
}

// Copy `count` bytes from the current position of `src` to `dst`.
// Returns false if fewer than `count` bytes could be read.
static bool copyFileChunk(File &src, File &dst, size_t count)
{
    const size_t chunkSize = 512;
    uint8_t chunk[chunkSize];
    size_t remaining = count;
    while (remaining > 0)
    {
        size_t toRead = remaining < chunkSize ? remaining : chunkSize;
        size_t readSize = src.read(chunk, toRead);
        if (readSize == 0)
            return false;
        dst.write(chunk, readSize);
        remaining -= readSize;
    }
    return true;
}

bool Editor::saveFile()
{
    if (savingInProgress)
    {
        _log("Save already in progress, skipping.\n");
        return false;
    }
    savingInProgress = true;

    //
    JsonDocument &app = status();

    // Defensive: rather than dead-ending on the reboot-only error screen (and
    // losing the buffer), fall back to the configured slot so the writing is
    // still persisted somewhere recoverable.
    if (fileName.length() == 0)
    {
        fileName = format("/%d.txt", app["config"]["file_index"].as<int>());
        _log("saveFile: empty filename, falling back to %s\n", fileName.c_str());
    }

    // if already saved nothing to do
    if (this->saved)
    {
        _log("File already saved. No operation required.\n");
        savingInProgress = false;
        return true;
    }

    //
    _log("Saving file %s\n", fileName.c_str());

    // The buffer is a window onto [seekPos, seekPos+loadedLength) on disk.
    // Anything outside that window (before seekPos, after windowEnd) must
    // survive the save untouched.
    size_t newLength = getBufferSize();
    size_t windowEnd = seekPos + loadedLength;
    bool hasTrailingData = windowEnd < fileSize;

    // FAST PATH: window is the tail of the file and isn't shrinking -
    // just overwrite/extend in place, no rewrite of the rest of the file needed.
    if (!hasTrailingData && newLength >= loadedLength)
    {
        File file = gfs()->open(fileName.c_str(), "r+"); // read/write, no truncate
        if (!file)
        {
            // If file doesn't exist, create it
            file = gfs()->open(fileName.c_str(), "w+"); // create + read/write
        }

        if (!file)
        {
            //
            app["error"] = "Failed to open file for writing\n";
            app["screen"] = ERRORSCREEN;

            //
            _log(app["error"]);
            savingInProgress = false;
            return false;
        }

        // Seek to the last loaded offset
        if (!file.seek(seekPos))
        {
            _log("Failed to seek file pointer\n");
            file.close();
            delay(100);

            savingInProgress = false;
            return false;
        }
        _log("Writing file at: %d\n", seekPos);

        // writing the file content
        size_t length = file.print(buffer);
        _log("File written: %d bytes\n", length);

        file.close();

        fileSize = seekPos + length;
        loadedLength = length;
    }

    // SPLICE PATH: either the tail is shrinking, or this window sits in the
    // middle of the file and there is trailing data after it that must be
    // preserved. Rewrite the file as [prefix][new window content][suffix].
    else
    {
        String tempFileName = format("%s.tmp", fileName.c_str());
        if (gfs()->exists(tempFileName.c_str()))
        {
            gfs()->remove(tempFileName.c_str());
        }

        if (!gfs()->rename(fileName.c_str(), tempFileName.c_str()))
        {
            app["error"] = "Save failed (rename for splice)\n";
            app["screen"] = ERRORSCREEN;
            _log(app["error"].as<const char *>());
            savingInProgress = false;
            return false;
        }

        File src = gfs()->open(tempFileName.c_str(), "r");
        File dst = gfs()->open(fileName.c_str(), "w");
        bool ok = src && dst;

        if (ok)
            ok = copyFileChunk(src, dst, seekPos); // prefix, unchanged

        if (ok)
            dst.print(buffer); // this window's edited content

        if (ok && fileSize > windowEnd)
        {
            // suffix, unchanged
            ok = src.seek(windowEnd) && copyFileChunk(src, dst, fileSize - windowEnd);
        }

        if (src)
            src.close();
        if (dst)
            dst.close();

        if (!ok)
        {
            // Restore the original file so a transient I/O error never
            // leaves the journal missing or half-written.
            app["error"] = "Save failed (splice)\n";
            app["screen"] = ERRORSCREEN;
            _log(app["error"].as<const char *>());

            gfs()->remove(fileName.c_str());
            gfs()->rename(tempFileName.c_str(), fileName.c_str());

            savingInProgress = false;
            return false;
        }

        gfs()->remove(tempFileName.c_str());

        fileSize = fileSize + (newLength - loadedLength);
        loadedLength = newLength;
    }

    // flag to save
    this->saved = true;

    // the buffer now matches disk - drop the write-ahead snapshot
    clearRecovery();

    // Persist the word count alongside the file itself, instead of on a
    // fixed timer (see wordcounter_service()) - that way writing config.json
    // only ever happens at a moment a flash write was going to happen anyway,
    // and never interrupts active typing.
    wordCountBuffer = wordcounter_buffer(buffer);
    int file_index = app["config"]["file_index"].as<int>();
    app["config"][format("wordcount_buffer_%d", file_index)] = wordCountBuffer;

    // Settle any day boundary before attributing words, so writing past midnight
    // counts toward the new day (no-op until the clock is confirmed this session).
    clock_tick();

    // Writing stats: accumulate this file's net word growth into today's total
    // and the (RAM-only, resets on boot) session total. The per-file baseline
    // last_wc_N is the full-document count at the previous save; a missing
    // baseline (first save after the feature shipped, or a brand-new file just
    // opened) is seeded WITHOUT counting pre-existing content as written today.
    {
        int full = app["config"][format("wordcount_file_%d", file_index)].as<int>() + wordCountBuffer;
        String bkey = format("last_wc_%d", file_index);
        if (app["config"][bkey].is<int>())
        {
            int delta = full - app["config"][bkey].as<int>();
            if (delta > 0)
            {
                app["config"]["today_words"] = app["config"]["today_words"].as<int>() + delta;
                app["session_words"] = app["session_words"].as<int>() + delta;
            }
        }
        app["config"][bkey] = full;
    }

    // Auto-title: cache the first non-empty line so menus / status bar can label
    // the file without re-reading it. A manual title (set via Rename) wins, so
    // only refresh the auto-title when no manual one is set. Read from disk (not
    // the in-memory buffer) since the buffer is only a window over the file.
    if (!app["config"][format("title_manual_%d", file_index)].as<bool>())
        app["config"][format("title_%d", file_index)] = deriveTitle(fileHead(512));

    // Mark this file dirty-since-sync (cleared on a successful sync). Drives the
    // editor footer sync chip; no clock/RTC needed.
    app["config"][format("unsynced_%d", file_index)] = true;

    // Resume support: remember the caret's absolute byte offset in the file so
    // the file reopens here (caret + scrollback). The buffer mirrors
    // [seekPos, seekPos+loadedLength) on disk, so seekPos + cursorPos is it.
    app["config"][format("caret_%d", file_index)] = (long)(seekPos + cursorPos);

    // Bump a monotonic edit counter so the file list can order by most-recently
    // edited without needing a clock/RTC. Larger = edited more recently.
    int editSeq = (app["config"]["edit_seq"].as<int>()) + 1;
    app["config"]["edit_seq"] = editSeq;
    app["config"][format("edited_%d", file_index)] = editSeq;

    config_save();

    //
    savingInProgress = false;

#if defined(DEBUG) && defined(BOARD_PICO)
    printMemoryUsage();
#endif

    return true;
}

// Read `length` bytes starting at `offset` from disk into the buffer,
// replacing whatever window was loaded before. Used by paging so the
// in-memory buffer can slide to any part of the file, not just the tail.
bool Editor::loadWindow(size_t offset, size_t length)
{
    JsonDocument &app = status();

    File file = gfs()->open(fileName.c_str(), "r");
    if (!file)
    {
        app["error"] = format("file open failed %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;
        _debug(app["error"]);
        return false;
    }

    if (!file.seek(offset))
    {
        file.close();
        delay(100);

        app["error"] = format("Failed to seek file pointer. offset: %d\n", offset);
        app["screen"] = ERRORSCREEN;
        _debug(app["error"].as<const char *>());
        return false;
    }

    resetBuffer();
    size_t bytesRead = 0;
    while (bytesRead < length && file.available())
    {
        buffer[bytesRead++] = file.read();
    }

    file.close();
    delay(100);

    seekPos = offset;
    loadedLength = bytesRead;
    pageChanged = true;
    clearSelection();
    clearUndo(); // window positions are about to change - any history is now stale

    _debug("Editor::loadWindow offset: %d length: %d read: %d\n", offset, length, bytesRead);

    return true;
}

// Load the chunk of the file that ends exactly where the current window
// starts, so the writer can keep scrolling back through earlier text.
void Editor::pageBackward()
{
    if (seekPos == 0)
    {
        _log("pageBackward: already at the start of the file\n");
        return;
    }

    if (!saved && !saveFile())
    {
        _log("pageBackward: flush failed, staying on current page\n");
        return;
    }

    size_t windowEnd = seekPos;
    size_t stepSize = BUFFER_SIZE / 2;
    size_t newSeekPos = (windowEnd > stepSize) ? windowEnd - stepSize : 0;

    if (!loadWindow(newSeekPos, windowEnd - newSeekPos))
        return;

    // land at the end, continuing the upward motion seamlessly
    cursorPos = getBufferSize();
    updateScreen();
}

// Load the chunk of the file that starts exactly where the current window
// ends, so the writer can scroll forward again after paging backward.
void Editor::pageForward()
{
    if (!saved && !saveFile())
    {
        _log("pageForward: flush failed, staying on current page\n");
        return;
    }

    size_t windowEnd = seekPos + loadedLength;
    if (windowEnd >= fileSize)
    {
        // already at the live tail - nothing further on disk
        return;
    }

    size_t stepSize = BUFFER_SIZE / 2;
    size_t remaining = fileSize - windowEnd;
    size_t toLoad = (remaining <= stepSize) ? remaining : stepSize;

    if (!loadWindow(windowEnd, toLoad))
        return;

    // land at the start, continuing the downward motion seamlessly
    cursorPos = 0;
    updateScreen();
}

// The buffer filled up while typing. Flush it, then keep going from
// wherever this window ended - either a fresh empty tail window, or the
// next chunk of on-disk content if this wasn't the tail. Replaces the old
// behaviour of unconditionally jumping back to the tail, which would have
// discarded the writer's place (and unsaved on-disk content past it) when
// triggered on a non-tail window.
void Editor::advanceWindow()
{
    if (!saveFile())
    {
        _log("advanceWindow: flush failed, buffer is full and can't advance\n");
        return;
    }

    size_t windowEnd = seekPos + loadedLength;
    if (windowEnd >= fileSize)
    {
        // was at the tail - open a fresh empty window to keep typing into

        // The buffer saveFile() just flushed is now permanently on disk,
        // outside the window we're about to load. Fold its count into the
        // running file total now, before resetBuffer() below wipes it --
        // otherwise it's silently dropped the moment wordcounter_service()
        // next recomputes wordCountBuffer against the new, much smaller
        // buffer, undercounting every page after the first.
        wordCountFile += wordCountBuffer;
        wordCountBuffer = 0;

        JsonDocument &app = status();
        int file_index = app["config"]["file_index"].as<int>();
        app["config"][format("wordcount_file_%d", file_index)] = wordCountFile;
        app["config"][format("wordcount_buffer_%d", file_index)] = wordCountBuffer;

        seekPos = windowEnd;
        loadedLength = 0;
        resetBuffer();
        cursorPos = 0;
        pageChanged = true;
        clearUndo(); // buffer just got replaced; old records now point nowhere
    }
    else
    {
        size_t stepSize = BUFFER_SIZE / 2;
        size_t remaining = fileSize - windowEnd;
        size_t toLoad = (remaining <= stepSize) ? remaining : stepSize;

        if (!loadWindow(windowEnd, toLoad))
            return;

        cursorPos = 0;
    }

    updateScreen();
}

// Ctrl+Home: caret to the very first byte of the file. Already on the first
// window means just move the caret; otherwise flush and load the head window.
void Editor::goToDocTop()
{
    if (seekPos == 0)
    {
        cursorPos = 0;
        clearSelection();
        updateScreen();
        return;
    }

    if (!saved && !saveFile())
    {
        _log("goToDocTop: flush failed, staying on current page\n");
        return;
    }

    if (!loadWindow(0, BUFFER_SIZE / 2))
        return;

    cursorPos = 0;
    updateScreen();
}

// Ctrl+End: caret to the very end of the file. Flush first so fileSize is
// current, then load the tail window (the same offset loadFile picks) unless
// we are already on it, in which case just move the caret.
void Editor::goToDocBottom()
{
    if (!saved && !saveFile())
    {
        _log("goToDocBottom: flush failed, staying on current page\n");
        return;
    }

    size_t stepSize = BUFFER_SIZE / 2;
    size_t tailSeek = (fileSize > 0) ? (fileSize / stepSize) * stepSize : 0;
    if (fileSize == tailSeek && tailSeek > 0)
        tailSeek = (tailSeek > stepSize) ? tailSeek - stepSize : 0;

    if (tailSeek != seekPos)
    {
        if (!loadWindow(tailSeek, fileSize - tailSeek))
            return;
    }

    cursorPos = getBufferSize();
    clearSelection();
    updateScreen();
}

//////////////////////////////////
// CRASH RECOVERY
//////////////////////////////////

// Dump the live window to "<fileName>.recovery": a one-line text header
// (magic + window offsets + caret + buffer length) followed by the raw buffer
// bytes. The header lets the reader reject a half-written snapshot.
void Editor::writeRecovery()
{
    if (fileName.length() == 0)
        return;

    String path = recoveryPath();
    File f = gfs()->open(path.c_str(), "w");
    if (!f)
        return;

    int bufLen = getBufferSize();
    f.print(format("MJREC1 %u %u %u %d %d\n",
                   (unsigned)seekPos, (unsigned)loadedLength, (unsigned)fileSize,
                   cursorPos, bufLen));
    f.write((const uint8_t *)buffer, (size_t)bufLen);
    f.close();
    delay(10);
}

// Snapshot at most once per RECOVERY_INTERVAL, and only while there are unsaved
// edits - a clean save already removed any sidecar and resets the timer.
void Editor::maybeWriteRecovery()
{
    if (saved)
        return;
    unsigned long now = millis();
    if (now - lastRecoverySnapshot < RECOVERY_INTERVAL)
        return;
    lastRecoverySnapshot = now;
    writeRecovery();
}

void Editor::clearRecovery()
{
    if (fileName.length() > 0)
    {
        String path = recoveryPath();
        if (gfs()->exists(path.c_str()))
            gfs()->remove(path.c_str());
    }
    lastRecoverySnapshot = millis();
}

// Parse "<fileName>.recovery". With intoBuffer=false this only validates the
// header + body length (used to decide whether to offer recovery at boot); with
// intoBuffer=true it loads the snapshot into the live buffer and window state.
bool Editor::readRecovery(bool intoBuffer)
{
    if (fileName.length() == 0)
        return false;

    String path = recoveryPath();
    if (!gfs()->exists(path.c_str()))
        return false;

    File f = gfs()->open(path.c_str(), "r");
    if (!f)
        return false;

    // header line
    char line[96];
    int li = 0;
    while (f.available() && li < (int)sizeof(line) - 1)
    {
        char c = (char)f.read();
        if (c == '\n')
            break;
        line[li++] = c;
    }
    line[li] = 0;

    unsigned sp = 0, ll = 0, fsz = 0;
    int cp = 0, bl = -1;
    if (sscanf(line, "MJREC1 %u %u %u %d %d", &sp, &ll, &fsz, &cp, &bl) != 5)
    {
        f.close();
        return false;
    }
    if (bl < 0 || bl > BUFFER_SIZE)
    {
        f.close();
        return false;
    }
    // The window must still sit inside the file on disk (which boot has already
    // loaded, so fileSize == the on-disk size here). A mismatch means the
    // snapshot belongs to a different version - refuse it rather than risk a
    // bad splice on the next save.
    if (sp > fileSize)
    {
        f.close();
        return false;
    }

    if (!intoBuffer)
    {
        // verify the body actually holds bl bytes
        int n = 0;
        while (f.available() && n < bl)
        {
            f.read();
            n++;
        }
        f.close();
        return n == bl;
    }

    resetBuffer();
    int n = 0;
    while (f.available() && n < bl)
        buffer[n++] = (char)f.read();
    buffer[n] = 0;
    f.close();

    if (n != bl)
    {
        // truncated snapshot - drop it, keep the cleanly loaded file
        resetBuffer();
        return false;
    }

    seekPos = sp;
    loadedLength = ll;
    fileSize = (size_t)fsz;
    cursorPos = (cp < 0) ? 0 : cp;
    if (cursorPos > n)
        cursorPos = n;
    clearSelection();
    clearUndo();
    pageChanged = true;
    updateScreen();
    saved = false; // restored edits are unsaved until the next flush
    return true;
}

bool Editor::recoveryPending() { return readRecovery(false); }
bool Editor::applyRecovery() { return readRecovery(true); }

void Editor::salvageInterruptedSave(const String &base)
{
    String tmp = base + ".tmp";
    if (!gfs()->exists(tmp.c_str()))
        return;

    // A leftover .tmp means saveFile()'s splice was interrupted after it
    // renamed the original to .tmp but before the rewrite finished. The .tmp is
    // the last complete pre-save file; the base (if present) is half-written.
    // Restore the complete one. Any edits the interrupted save carried are also
    // in the .recovery snapshot, which boot offers separately.
    _log("salvage: leftover %s from interrupted save - restoring\n", tmp.c_str());
    if (gfs()->exists(base.c_str()))
        gfs()->remove(base.c_str());
    gfs()->rename(tmp.c_str(), base.c_str());
}

// Make the current file empty
String Editor::fileHead(size_t maxBytes)
{
    File f = gfs()->open(fileName.c_str(), "r");
    if (!f)
        return String("");

    if (maxBytes > 512)
        maxBytes = 512;
    size_t sz = f.size();
    if (sz > maxBytes)
        sz = maxBytes;

    char tmp[513];
    size_t n = 0;
    while (n < sz)
        tmp[n++] = (char)f.read();
    tmp[n] = 0;
    f.close();
    return String(tmp);
}

void Editor::clearFile()
{
    if (savingInProgress)
    {
        _log("Save is is progress. Clear file skipped\n");
        return;
    }

    //
    JsonDocument &app = status();

    // if no current file is specified then skip
    // Nothing loaded: safe no-op (don't dead-end on the reboot-only error screen).
    if (fileName.length() == 0)
    {
        _log("clearFile: no file loaded, nothing to clear\n");
        return;
    }

    // Step 1. Check if the backup file exists
    // remove it if already exists
    String backupFileName = format("/%s_backup.txt", fileName.c_str());
    _debug("backupFilename: %s\n", backupFileName);
    if (gfs()->exists(backupFileName.c_str()))
    {
        // remove the backup file
        gfs()->remove(backupFileName.c_str());
    }

    // Step 2. Rename the current file to the backup.txt
    if (gfs()->rename(fileName.c_str(), backupFileName.c_str()))
    {
        _debug("File renamed successfully: %s -> %s.\n", fileName.c_str(), backupFileName.c_str());
    }
    else
    {
        //
        app["error"] = format("Error making a backup file. %s\n", backupFileName);
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return;
    }

    // Step 3. Empty the current file by opening it with FILE_WRITE
    File file = gfs()->open(fileName.c_str(), "w");
    if (!file)
    {
        //
        app["error"] = format("Failed to create an empty file %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return;
    }

    // clean up file
    file.close();
    delay(100);

    // Drop this slot's cached metadata so a cleared file doesn't keep a stale
    // title or unsynced flag (an empty file derives an empty title anyway).
    {
        int file_index = app["config"]["file_index"].as<int>();
        app["config"][format("title_%d", file_index)] = "";
        app["config"][format("title_manual_%d", file_index)] = false;
        app["config"][format("unsynced_%d", file_index)] = false;
        app["config"][format("caret_%d", file_index)] = 0; // cleared file opens at the top
        app["config"].remove(format("synced_day_%d", file_index));
    }

    // Go through the loading process of the empty file
    loadFile(fileName);
}

void Editor::deleteFile()
{
    if (savingInProgress)
    {
        _log("Save is in progress. Delete file skipped\n");
        return;
    }

    JsonDocument &app = status();

    // Nothing loaded: safe no-op (don't dead-end on the reboot-only error screen).
    if (fileName.length() == 0)
    {
        _log("deleteFile: no file loaded, nothing to delete\n");
        return;
    }

    // Keep a recoverable backup, then move the live file out of the way so it
    // disappears from the list (same backup naming as clearFile).
    String backupFileName = format("/%s_backup.txt", fileName.c_str());
    if (gfs()->exists(backupFileName.c_str()))
        gfs()->remove(backupFileName.c_str());
    gfs()->rename(fileName.c_str(), backupFileName.c_str());

    // drop any write-ahead recovery sidecar for this file
    String rec = recoveryPath();
    if (gfs()->exists(rec.c_str()))
        gfs()->remove(rec.c_str());

    // clear this slot's cached metadata
    int file_index = app["config"]["file_index"].as<int>();
    app["config"][format("title_%d", file_index)] = "";
    app["config"][format("title_manual_%d", file_index)] = false;
    app["config"][format("unsynced_%d", file_index)] = false;
    app["config"][format("wordcount_file_%d", file_index)] = 0;
    app["config"][format("wordcount_buffer_%d", file_index)] = 0;
    app["config"][format("edited_%d", file_index)] = 0;
    // drop the stats baseline so a file later created in this slot has its first
    // save counted as words written today (re-seeded fresh), not measured against
    // the deleted file's count.
    app["config"].remove(format("last_wc_%d", file_index));
    app["config"].remove(format("caret_%d", file_index)); // no resume into a deleted file
    app["config"].remove(format("synced_day_%d", file_index));
    config_save();
}

// House Keeping Tasks
void Editor::loop()
{
    unsigned long now = millis();

    // write-ahead crash-recovery snapshot (time-gated, only while unsaved)
    maybeWriteRecovery();

    // Auto Repeat is activated
    if (lastKey != 0)
    {
        // check if past point of repeatDelay
        if (now - lastPressTime > repeatDelay)
        {
            // Check if past repeatInterval
            if (now - lastPressTime - repeatDelay >= repeatInterval)
            {
                keyboard(lastKey, true);
                lastPressTime = now - repeatDelay;
            }
        }
    }
}

// True for keystrokes that can change the buffer (so the wrapper snapshots and
// records an undo step). Excludes UNDO/REDO themselves (applying undo must never
// record a new step), navigation, selection and copy/select-all.
static bool isEditKey(int key)
{
    if (key == '\b' || key == 127 || key == CUT || key == PASTE)
        return true;
    // printable: ASCII text or non-Latin code points (Arabic >= 1536), never the
    // 1000-1199 command range.
    return (key >= 32 && key < 1000) || key >= 1536;
}

// Handle Keyboard Input. Snapshot the buffer around an editing keystroke and
// turn the resulting change into one undo step; everything else is dispatch.
void Editor::keyboard(int key, bool pressed)
{
    bool edit = pressed && isEditKey(key);
    if (edit)
    {
        memcpy(undoBefore, buffer, getBufferSize() + 1);
        undoCursorBefore = cursorPos;
        undoKind = (key == '\b') ? UNDO_BACK
                   : (key == 127 || key == CUT || key == PASTE) ? UNDO_OTHER
                                                                 : UNDO_TYPE;
    }

    keyboardImpl(key, pressed);

    if (edit)
        commitUndo();
}

void Editor::keyboardImpl(int key, bool pressed)
{
    // ignore non printable character
    if (key == 0 || key == 27 || key == MENU)
        return;

    //
    _debug("Editor::keyboard:: %c [%d] pressed: %d cursorPos: %d\n", key, key, pressed, cursorPos);

#ifdef DEBUG
    _debug("Buffer: %d\n", getBufferSize());
#endif

    // when any key is pressed track the last key pressed and if they don't release
    // keep issueing press events so that it keeps on typing on the screen
    if (pressed)
    {
        // Only ASCII text and control keys (< 1000) auto-repeat. Everything >=
        // 1000 fires once: command/action codes (1000-1199: SAVE, STATUSBAR,
        // SEL_*, clipboard, file switch) carry no matching release to clear
        // lastKey, so repeating would re-inject the action every loop tick (the
        // COPY->'W' leak), and non-Latin code points (Arabic >= 1536) are letters
        // nobody holds to repeat - not worth the same stuck-key risk.
        if (key >= 1000)
        {
            lastKey = 0;
            lastPressTime = 0;
        }
        else if (key != lastKey)
        {
            // New key or new press: process immediately
            lastKey = key;
            lastPressTime = millis();
            _debug("Auto Repeat %d %d\n", lastKey, lastPressTime);
        }
    }
    else
    {
        _debug("Auto Repeat Release %d\n", lastKey);
        // Key released: reset state
        lastKey = 0;
        lastPressTime = 0;
    }

    // below is only for when the key is pressed
    if (pressed)
    {
        //////////////////////////
        // SELECTION CLIPBOARD ACTIONS
        //////////////////////////
        if (key == SELECTALL)
        {
            selectAll();
            updateScreen();
            return;
        }
        if (key == COPY)
        {
            copySelection();
            return;
        }
        if (key == CUT)
        {
            if (hasSelection())
            {
                copySelection();
                deleteSelection();
                this->saved = false;
            }
            updateScreen();
            return;
        }
        if (key == PASTE)
        {
            paste();
            this->saved = false;
            updateScreen();
            return;
        }
        if (key == UNDO)
        {
            applyUndo();
            return;
        }
        if (key == REDO)
        {
            applyRedo();
            return;
        }
        if (key == DOC_TOP)
        {
            goToDocTop();
            return;
        }
        if (key == DOC_BOTTOM)
        {
            goToDocBottom();
            return;
        }

        // WORD JUMP (Ctrl+Left / Ctrl+Right), no selection. Word boundaries are
        // ASCII space/newline, which never appear inside a UTF-8 multi-byte
        // character, so byte-stepping lands on a character boundary for both
        // Arabic and English. Mirrors the SEL_WORD_* stepping below.
        if (key == WORD_LEFT || key == WORD_RIGHT)
        {
            clearSelection();
            cursorPos = editorWordJump(buffer, getBufferSize(), cursorPos, key == WORD_RIGHT);
            updateScreen();
            return;
        }

        // PARAGRAPH JUMP (Ctrl+Up / Ctrl+Down), no selection. Moves to the next /
        // previous paragraph (blank-line boundary), within the loaded window.
        if (key == PARA_UP || key == PARA_DOWN)
        {
            clearSelection();
            cursorPos = editorParagraphJump(buffer, getBufferSize(), cursorPos, key == PARA_DOWN);
            updateScreen();
            return;
        }

        //////////////////////////
        // SELECTION EXTENSION (Shift + nav)
        // Anchor the selection on first extend, then move the caret with the
        // matching plain-nav logic without clearing the anchor.
        //////////////////////////
        bool extending = (key >= SEL_LEFT && key <= SEL_WORD_RIGHT);
        if (extending)
        {
            if (selAnchor < 0)
                selAnchor = cursorPos;

            switch (key)
            {
            case SEL_LEFT: key = 18; break;
            case SEL_RIGHT: key = 19; break;
            case SEL_UP: key = 20; break;
            case SEL_DOWN: key = 21; break;
            case SEL_HOME: key = 2; break;
            case SEL_END: key = 3; break;
            case SEL_WORD_LEFT:
            {
                cursorPos = editorWordJump(buffer, getBufferSize(), cursorPos, false);
                updateScreen();
                return;
            }
            case SEL_WORD_RIGHT:
            {
                cursorPos = editorWordJump(buffer, getBufferSize(), cursorPos, true);
                updateScreen();
                return;
            }
            }
            // fall through into the cursor block below with the translated key,
            // but skip the page-load behaviour so selection stays in-window.
        }
        // A plain (unshifted) nav key while a selection exists collapses it to
        // the appropriate edge instead of moving further (PC behaviour).
        else if (hasSelection() &&
                 ((key >= 18 && key <= 23) || key == 2 || key == 3))
        {
            if (key == 18 || key == 20 || key == 2 || key == 22)
                cursorPos = selStart();
            else
                cursorPos = selEnd();
            clearSelection();
            updateScreen();
            return;
        }

        //////////////////////////
        // BACKWARD EDITING
        //////////////////////////
        if (key == '\b')
        {
            if (hasSelection())
            {
                deleteSelection();
                this->saved = false;
                this->backSpacePressed = true;
                updateScreen();
                return;
            }

            // buffer has more than 1 character
            if (getBufferSize() > 0)
            {
                //
                removeLastChar();

                // set saved flag to false
                this->saved = false;

                // set flag
                this->backSpacePressed = true;
            }
            // buffer emptied
            else
            {
                // Load previous contents from the file if at the beginning of the buffer
                _log("Backspace reached the beginning of the buffer\n");
            }
        }

        // DEL key deletes the word
        else if (key == 127)
        {
            if (hasSelection())
            {
                deleteSelection();
                this->saved = false;
                this->backSpacePressed = true;
                updateScreen();
                return;
            }
            if (getBufferSize() > 0)
            {
                // if editing at the end of the line then remove word
                if (cursorPos == getBufferSize())
                {
                    removeLastWord();
                }

                else
                {
                    // remove word in front
                    removeCharAtCursor();
                }

                // set saved flag to false
                this->saved = false;

                // set flag
                this->backSpacePressed = true;
            }
            // buffer emptied
            else
            {
                // Load previous contents from the file if at the beginning of the buffer
                _log("Delete word reached the beginning of the buffer\n");
            }
        }

        //////////////////////////
        // CURSORS
        //////////////////////////
        else if (key >= 18 && key <= 23 || key == 2 || key == 3)
        {
            // arrow keys
            // 18 - Left, 19 - Right, 20 - Up, 21 - Down
            // 22 - Page Up, 23 - Page Down
            // 2 - Home 3 - End
            // All horizontal/vertical moves step by whole UTF-8 characters and
            // express line columns in code points, never raw bytes.
            if (key == 18)
            {
                if (cursorPos == 0)
                {
                    // already at the start of the buffer - load the previous page
                    // (suppressed while extending a selection: stay in-window)
                    if (!extending)
                        pageBackward();
                }
                else
                {
                    // left: step back to the start of the previous character
                    --cursorPos;
                    while (cursorPos > 0 && isUtf8Cont(buffer[cursorPos]))
                        --cursorPos;
                }
            }
            else if (key == 19)
            {
                // cursor can't move outside the last text
                if (cursorPos < getBufferSize())
                {
                    // right: skip the whole current character
                    cursorPos += charLen((uint8_t)buffer[cursorPos]);
                    if (cursorPos > getBufferSize())
                        cursorPos = getBufferSize();
                }
                else if (!extending)
                    // already at the end of the buffer - load the next page
                    pageForward();
            }

            // UP
            else if (key == 20)
            {
                if (cursorLine == 0)
                {
                    // already at the top line - load the previous page
                    if (!extending)
                        pageBackward();
                }
                else
                {
                    int prevLen = lineLengths[cursorLine - 1];
                    int targetCol = (prevLen < cursorLinePos) ? (prevLen - 1) : cursorLinePos;
                    if (targetCol < 0)
                        targetCol = 0;
                    cursorPos = colToByte(cursorLine - 1, targetCol);
                }
            }

            // DOWN
            else if (key == 21)
            {
                if (cursorLine < totalLine)
                {
                    int nextLen = max(lineLengths[cursorLine + 1], 1);
                    int targetCol = (cursorLinePos > nextLen) ? (nextLen - 1) : cursorLinePos;
                    if (targetCol < 0)
                        targetCol = 0;
                    cursorPos = colToByte(cursorLine + 1, targetCol);
                    if (cursorPos > getBufferSize())
                        cursorPos = getBufferSize();
                }
                else if (cursorLine == totalLine)
                {
                    // already at the end of the buffer - load the next page
                    if (cursorPos == getBufferSize())
                    {
                        if (!extending)
                            pageForward();
                    }
                    else
                        cursorPos = getBufferSize();
                }
            }

            // HOME
            else if (key == 2)
            {
                cursorPos = colToByte(cursorLine, 0);
            }

            // END
            else if (key == 3)
            {
                if (cursorLine == totalLine)
                    cursorPos = getBufferSize();
                else
                {
                    int lineLength = max(lineLengths[cursorLine], 1);
                    cursorPos = colToByte(cursorLine, lineLength - 1);
                }
            }

            // PAGE UP
            else if (key == 22)
            {
                if (cursorLine == 0)
                    pageBackward();
                else
                    cursorPos = colToByte(max(cursorLine - rows, 0), 0);
            }

            // PAGE DOWN
            else if (key == 23)
            {
                if (cursorLine == totalLine && cursorPos == getBufferSize())
                    pageForward();
                else
                {
                    int newCursorLine = min(cursorLine + rows, totalLine);
                    if (newCursorLine == totalLine)
                        cursorPos = getBufferSize();
                    else
                    {
                        int lineLength = max(lineLengths[newCursorLine], 1);
                        cursorPos = colToByte(newCursorLine, lineLength - 1);
                    }
                }
            }
        }

        //////////////////////////
        // FORWARD EDITING
        //////////////////////////
        else
        {
            // Reserved action/sentinel codes (1000-1199: file switch, SAVE,
            // STATUSBAR, SEL_*/COPY/CUT/PASTE) are commands handled above, never
            // text. Guard so one can never slip through and get inserted as a
            // stray character (COPY=1111 would truncate to 'W', etc.). Real
            // characters never reach this branch (ASCII < 1000, Arabic >= 1536).
            if (key >= 1000 && key <= 1199)
                return;

            // typing over a selection replaces it
            if (hasSelection())
                deleteSelection();

            // add to the edit buffer new character
            if (getBufferSize() >= BUFFER_SIZE)
            {
                _log("Text buffer full\n");

                //
                advanceWindow();
            }

            //
            addChar(key);

            // set saved flag to false
            this->saved = false;
        }

        // update the screen buffer
        updateScreen();
    }
}

//
void Editor::updateScreen()
{
    // Loop through the text buffer
    // and product the data structure that is splitted in each line
    _debug("Editor::updateScreen called\n");

    // Handle empty buffer
    if (buffer[0] == '\0')
    {
        totalLine = 0;
        linePositions[0] = buffer;
        lineLengths[0] = 0;
        cursorLine = 0;
        cursorLinePos = 0;
        return;
    }

    // first line is the first of the buffer
    linePositions[0] = &buffer[0];
    lineLengths[0] = 0;

    //
    this->totalLine = 0;
    int line_count = 0;

    // remember the last space position to use it for the word wrap
    int last_space_index = -1;
    int last_space_position = -1;

    //
    // BUFFER -> SPLIT IN LINES
    //
    // Step one whole UTF-8 character at a time. `line_count` counts columns
    // (code points); linePositions stay byte pointers and lineLengths hold the
    // column count of each line. When a width function is supplied, the line
    // break is decided by accumulated pixel width instead of a column count,
    // so proportional Arabic/Latin lines fill the screen without clipping.
    bool pixelWrap = (measureCharWidth != nullptr && wrapWidthPx > 0);
    int line_px = 0;        // accumulated pixel width of the current line
    int last_space_px = 0;  // line_px at the last space seen

    int i = 0;
    while (i < BUFFER_SIZE)
    {
        char lead = buffer[i];

        // When reaching the end of text, break
        if (lead == '\0')
        {
            lineLengths[totalLine] = line_count;
            break;
        }

        // decode the character (need the code point for width measurement)
        uint16_t cp = (uint8_t)lead;
        int clen = charLen((uint8_t)lead);
        if (pixelWrap && UTF8_MODE)
            clen = bidi::utf8DecodeOne(&buffer[i], BUFFER_SIZE - i, &cp);

        // Count this character as one column, and add its pixel width
        line_count++;
        if (pixelWrap)
            line_px += measureCharWidthAt ? measureCharWidthAt(buffer, i, BUFFER_SIZE)
                                          : measureCharWidth(cp);

        // Track the last space (space is a single ASCII byte)
        if (lead == ' ')
        {
            last_space_index = i;
            last_space_position = line_count;
            last_space_px = line_px;
        }

        // has the line reached its limit (pixels or columns)?
        bool wrapNow = pixelWrap ? (line_px > wrapWidthPx) : (line_count == cols);

        // Handle words longer than one line (no space to break on)
        if (wrapNow && last_space_index == -1)
        {
            lineLengths[totalLine] = line_count;
            linePositions[++totalLine] = &buffer[i + clen];
            line_count = 0;
            line_px = 0;
            last_space_index = -1;
            last_space_position = -1;
            i += clen;
            continue;
        }

        // When receiving a newline or the line is full, start a new line
        if (lead == '\n' || wrapNow)
        {
            if (lead == '\n')
            {
                lineLengths[totalLine] = line_count;
                linePositions[++totalLine] = &buffer[i + clen];
                line_count = 0;
                line_px = 0;
            }
            // This line requires word-wrap
            else if (last_space_index != -1)
            {
                lineLengths[totalLine] = last_space_position;
                linePositions[++totalLine] = &buffer[last_space_index + 1];
                line_count -= last_space_position;
                line_px -= last_space_px;
            }
            // This line doesn't require word wrap
            else
            {
                lineLengths[totalLine] = line_count;
                linePositions[++totalLine] = &buffer[i + clen];
                line_count = 0;
                line_px = 0;
            }

            last_space_index = -1;
            last_space_position = -1;
        }

        i += clen;
    }

    // Handle cursor position beyond buffer
    if (cursorPos >= BUFFER_SIZE)
    {
        cursorPos = BUFFER_SIZE - 1;
    }

    // safety: never leave the cursor in the middle of a UTF-8 character
    while (cursorPos > 0 && isUtf8Cont(buffer[cursorPos]))
        cursorPos--;

    //
    // CALCULATE CURSOR INFORMATION
    //
    char *pCursorPos = &buffer[cursorPos];

    //
    cursorLine = 0;
    cursorLinePos = 0;

    // which line the cursor is on, and its column (in code points)
    for (int li = totalLine; li >= 0; li--)
    {
        if (pCursorPos >= linePositions[li])
        {
            cursorLine = li;
            int col = 0;
            char *p = linePositions[li];
            while (p < pCursorPos)
            {
                p += charLen((uint8_t)*p);
                col++;
            }
            cursorLinePos = col;
            break;
        }
    }

    //
    _debug("Editor::updateScreen cursorPos: %d\n", cursorPos);
}

// Convert a column (code-point index) on a given line into an absolute byte
// offset in the buffer, walking whole UTF-8 characters. Used by vertical
// cursor moves so they land on a character boundary, not mid-byte.
int Editor::colToByte(int lineIdx, int col)
{
    if (lineIdx < 0)
        lineIdx = 0;
    if (lineIdx > totalLine)
        lineIdx = totalLine;

    char *start = linePositions[lineIdx];
    int base = start - linePositions[0];
    int limit = (lineIdx < totalLine)
                    ? (int)(linePositions[lineIdx + 1] - start)
                    : (getBufferSize() - base);

    int b = 0, c = 0;
    while (c < col && b < limit)
    {
        b += charLen((uint8_t)start[b]);
        c++;
    }
    if (b > limit)
        b = limit;
    return base + b;
}

void Editor::addChar(int c)
{
    // Encode the code point. ASCII (and, on legacy builds, all input) is a
    // single byte; in UTF-8 mode Arabic/Latin-1 become 2-byte sequences.
    char enc[4];
    int len;
    if (!UTF8_MODE || c <= 0x7F)
    {
        enc[0] = (char)c;
        len = 1;
    }
    else
    {
        len = bidi::utf8Encode((uint16_t)c, enc);
    }

    int bufferSize = getBufferSize();
    if (bufferSize + len > BUFFER_SIZE)
        return;

    // make room at the cursor and splice in the encoded bytes
    if (bufferSize > cursorPos)
        memmove(buffer + cursorPos + len, buffer + cursorPos, bufferSize - cursorPos);
    for (int i = 0; i < len; i++)
        buffer[cursorPos + i] = enc[i];
    cursorPos += len;
    buffer[bufferSize + len] = '\0';

    _debug("FileBuffer::addChar::cursorPos %d cp %d len %d\n", cursorPos, c, len);
}

void Editor::removeLastChar()
{
    int bufferSize = getBufferSize();
    if (cursorPos <= 0 || bufferSize <= 0)
        return;

    // find the start of the whole UTF-8 character before the cursor
    int start = cursorPos - 1;
    while (start > 0 && isUtf8Cont(buffer[start]))
        start--;
    int len = cursorPos - start;

    memmove(buffer + start, buffer + cursorPos, bufferSize - cursorPos);
    buffer[bufferSize - len] = '\0';
    cursorPos = start;

    _debug("FileBuffer::removeLastChar cursorPos: %d len: %d\n", cursorPos, len);
}

void Editor::removeCharAtCursor()
{
    int bufferSize = getBufferSize();
    if (bufferSize <= 0 || cursorPos >= bufferSize)
        return;

    // delete the whole UTF-8 character starting at the cursor
    int len = charLen((uint8_t)buffer[cursorPos]);
    if (cursorPos + len > bufferSize)
        len = bufferSize - cursorPos;

    memmove(buffer + cursorPos, buffer + cursorPos + len, bufferSize - cursorPos - len);
    buffer[bufferSize - len] = '\0';
}

void Editor::removeLastWord()
{
    int length = getBufferSize();
    if (length == 0)
        return;

    int end = length - 1;
    while (end >= 0 && buffer[end] == ' ')
        end--;

    if (end < 0)
        return;

    int start = end;
    while (start >= 0 && buffer[start] != ' ' && buffer[start] != '\n')
        start--;

    if (start <= 0)
    {
        start = 0;
        buffer[0] = '\0';
    }
    else
    {
        buffer[start] = ' ';
        buffer[start + 1] = '\0';
    }

    cursorPos = getBufferSize();

    //
    _debug("FileBuffer::removeLastWord %d\n", cursorPos);
}

//
// SELECTION + CLIPBOARD
//

// Erase the selected byte range from the buffer, leaving the caret at its start.
void Editor::deleteSelection()
{
    if (!hasSelection())
        return;

    int s = selStart();
    int e = selEnd();
    int bufferSize = getBufferSize();
    if (e > bufferSize)
        e = bufferSize;

    memmove(buffer + s, buffer + e, bufferSize - e);
    buffer[bufferSize - (e - s)] = '\0';

    cursorPos = s;
    clearSelection();
    updateScreen();
}

// Copy the selected range into the internal clipboard (no buffer change).
void Editor::copySelection()
{
    if (!hasSelection())
        return;

    int s = selStart();
    int len = selEnd() - s;
    if (len > BUFFER_SIZE)
        len = BUFFER_SIZE;

    memcpy(clipboard, buffer + s, len);
    clipboard[len] = '\0';
    clipboardLength = len;
}

// Select the whole loaded buffer window.
void Editor::selectAll()
{
    selAnchor = 0;
    cursorPos = getBufferSize();
}

// Insert the clipboard at the caret, replacing any active selection first.
void Editor::paste()
{
    if (hasSelection())
        deleteSelection();

    if (clipboardLength <= 0)
        return;

    int bufferSize = getBufferSize();
    int len = clipboardLength;
    if (bufferSize + len > BUFFER_SIZE)
        len = BUFFER_SIZE - bufferSize;
    if (len <= 0)
        return;

    if (bufferSize > cursorPos)
        memmove(buffer + cursorPos + len, buffer + cursorPos, bufferSize - cursorPos);
    memcpy(buffer + cursorPos, clipboard, len);
    cursorPos += len;
    buffer[bufferSize + len] = '\0';

    updateScreen();
}

//
// UNDO / REDO
//

void Editor::clearUndo()
{
    undoCount = 0;
    undoCurrent = 0;
    undoArenaLen = 0;
    undoCursorBefore = -1; // invalidate any in-flight pre-edit snapshot
}

// Drop the oldest record and reclaim its arena bytes, keeping the arena packed
// in record order. Returns false when the log is already empty.
static bool undoDropOldest(Editor &ed)
{
    if (ed.undoCount == 0)
        return false;

    int block = ed.undoRecs[0].insertedOff + ed.undoRecs[0].insertedLen; // rec0 spans [0, block)
    memmove(ed.undoArena, ed.undoArena + block, ed.undoArenaLen - block);
    ed.undoArenaLen -= block;

    for (int i = 1; i < ed.undoCount; i++)
    {
        ed.undoRecs[i - 1] = ed.undoRecs[i];
        ed.undoRecs[i - 1].removedOff -= block;
        ed.undoRecs[i - 1].insertedOff -= block;
    }
    ed.undoCount--;
    if (ed.undoCurrent > 0)
        ed.undoCurrent--;
    return true;
}

// Diff the pre-edit snapshot against the live buffer and record the single
// contiguous change as one undo step, coalescing runs of typing / backspacing.
void Editor::commitUndo()
{
    if (undoCursorBefore < 0)
        return; // snapshot invalidated (window changed under the edit)

    int oldLen = (int)strlen(undoBefore);
    int newLen = getBufferSize();

    // longest common prefix and suffix bound the changed region
    int p = 0;
    while (p < oldLen && p < newLen && undoBefore[p] == buffer[p])
        p++;
    int s = 0;
    while (s < oldLen - p && s < newLen - p &&
           undoBefore[oldLen - 1 - s] == buffer[newLen - 1 - s])
        s++;

    int removedLen = oldLen - p - s;
    int insertedLen = newLen - p - s;
    if (removedLen == 0 && insertedLen == 0)
        return; // nothing actually changed

    int pos = p;
    const char *removed = undoBefore + p;
    const char *inserted = buffer + p;

    // discard the redo branch we are about to overwrite
    if (undoCurrent < undoCount)
    {
        undoCount = undoCurrent;
        undoArenaLen = undoCount == 0
                           ? 0
                           : undoRecs[undoCount - 1].insertedOff + undoRecs[undoCount - 1].insertedLen;
    }

    // COALESCE into the most recent step where it reads as one user action.
    if (undoCount > 0)
    {
        UndoRec &prev = undoRecs[undoCount - 1];

        // typing: contiguous pure inserts, broken at whitespace so undo is
        // word-granular (a run accumulates until a space/newline ends it).
        bool prevEndsBreak = prev.insertedLen > 0 &&
                             (undoArena[prev.insertedOff + prev.insertedLen - 1] == ' ' ||
                              undoArena[prev.insertedOff + prev.insertedLen - 1] == '\n');
        if (undoKind == UNDO_TYPE && prev.kind == UNDO_TYPE &&
            prev.removedLen == 0 && removedLen == 0 && insertedLen > 0 &&
            pos == prev.pos + prev.insertedLen && !prevEndsBreak &&
            undoArenaLen + insertedLen <= UNDO_ARENA)
        {
            memcpy(undoArena + undoArenaLen, inserted, insertedLen);
            prev.insertedLen += insertedLen;
            prev.lenAfter = newLen;
            undoArenaLen += insertedLen;
            return;
        }

        // backspacing: contiguous pure deletes extending leftward. Prepend the
        // freshly removed bytes to the front of the previous removed text.
        if (undoKind == UNDO_BACK && prev.kind == UNDO_BACK &&
            prev.insertedLen == 0 && insertedLen == 0 && removedLen > 0 &&
            pos + removedLen == prev.pos &&
            undoArenaLen + removedLen <= UNDO_ARENA)
        {
            memmove(undoArena + prev.removedOff + removedLen, undoArena + prev.removedOff, prev.removedLen);
            memcpy(undoArena + prev.removedOff, removed, removedLen);
            prev.removedLen += removedLen;
            prev.insertedOff = prev.removedOff + prev.removedLen;
            prev.pos = pos;
            prev.lenAfter = newLen;
            undoArenaLen += removedLen;
            return;
        }
    }

    // NEW record. A change whose text can't fit the arena at all isn't undoable;
    // drop the whole history rather than store it half.
    int need = removedLen + insertedLen;
    if (need > UNDO_ARENA)
    {
        clearUndo();
        return;
    }
    while (undoCount >= UNDO_MAX || undoArenaLen + need > UNDO_ARENA)
    {
        if (!undoDropOldest(*this))
            break;
    }

    UndoRec &rec = undoRecs[undoCount];
    rec.pos = pos;
    rec.cursorBefore = undoCursorBefore;
    rec.lenBefore = oldLen;
    rec.lenAfter = newLen;
    rec.removedOff = undoArenaLen;
    rec.removedLen = removedLen;
    memcpy(undoArena + rec.removedOff, removed, removedLen);
    rec.insertedOff = undoArenaLen + removedLen;
    rec.insertedLen = insertedLen;
    memcpy(undoArena + rec.insertedOff, inserted, insertedLen);
    rec.kind = undoKind;
    undoArenaLen += need;
    undoCount++;
    undoCurrent = undoCount;
}

// Splice the buffer: replace [pos, pos+oldLen) with `len` bytes from `src`.
// Returns false (no change) if the record doesn't fit the current buffer. This
// is the hard guarantee against memory corruption: a stale/garbage record can
// only make us refuse the edit, never run an out-of-bounds move.
static bool undoSplice(char *buffer, int bufLen, int pos, int oldLen, const char *src, int len)
{
    if (pos < 0 || oldLen < 0 || len < 0 || pos + oldLen > bufLen)
        return false;
    int newLen = bufLen - oldLen + len;
    if (newLen < 0 || newLen > BUFFER_SIZE)
        return false;
    memmove(buffer + pos + len, buffer + pos + oldLen, bufLen - (pos + oldLen));
    memcpy(buffer + pos, src, len);
    buffer[newLen] = '\0';
    return true;
}

void Editor::applyUndo()
{
    if (undoCurrent <= 0)
        return;

    UndoRec &rec = undoRecs[undoCurrent - 1];

    // The buffer must be in this record's post-edit state, or the log has
    // desynced from the buffer (stale window etc.) - bail safely, don't splice.
    if (getBufferSize() != rec.lenAfter)
    {
        clearUndo();
        return;
    }

    // reverse the edit: where insertedLen bytes were added, put removedLen back
    if (!undoSplice(buffer, getBufferSize(), rec.pos, rec.insertedLen,
                    undoArena + rec.removedOff, rec.removedLen))
    {
        clearUndo();
        return;
    }

    cursorPos = rec.cursorBefore;
    if (cursorPos > getBufferSize())
        cursorPos = getBufferSize();
    if (cursorPos < 0)
        cursorPos = 0;
    undoCurrent--;

    this->saved = false;
    clearSelection();
    updateScreen();
}

void Editor::applyRedo()
{
    if (undoCurrent >= undoCount)
        return;

    UndoRec &rec = undoRecs[undoCurrent];

    // The buffer must be in this record's pre-edit state, or the log desynced.
    if (getBufferSize() != rec.lenBefore)
    {
        clearUndo();
        return;
    }

    // re-apply the edit: where removedLen bytes sit, put insertedLen back
    if (!undoSplice(buffer, getBufferSize(), rec.pos, rec.removedLen,
                    undoArena + rec.insertedOff, rec.insertedLen))
    {
        clearUndo();
        return;
    }

    cursorPos = rec.pos + rec.insertedLen;
    if (cursorPos > getBufferSize())
        cursorPos = getBufferSize();
    undoCurrent++;

    this->saved = false;
    clearSelection();
    updateScreen();
}
