#pragma once

//
#include <Arduino.h>

#ifdef BOARD_PICO
#define BUFFER_SIZE 2000
#endif

#ifdef BOARD_ESP32_S3
#define BUFFER_SIZE 8000
#endif

//
class Editor
{
public:
    // Text Buffer
    char buffer[BUFFER_SIZE + 100];

    // Saved Status
    bool saved = true;

    // Current File Name
    String fileName;

    // Current File Total Size
    size_t fileSize = 0;

    // Last File Load Position
    // This is a place where save should occurs with current buffer
    size_t seekPos = 0;

    // How many bytes of the current window came from disk at last load/save.
    // Together with seekPos this defines the on-disk region [seekPos, seekPos+loadedLength)
    // that the buffer mirrors, independent of in-memory edits that may have
    // grown or shrunk the buffer since.
    size_t loadedLength = 0;

    // Set whenever the buffer is swapped to a different window of the file
    // (paging, or the buffer filling up while typing) so display code knows
    // to do a full redraw instead of an incremental one.
    bool pageChanged = false;

    // Screen Size Definition
    int rows = 10;
    int cols = 26;

    // Optional pixel-accurate line wrapping. When wrapWidthPx > 0 and a width
    // function is provided, lines wrap at the measured pixel width instead of a
    // fixed column count (needed because Arabic/Latin glyphs aren't equal
    // width). Left unset on other revisions, which keep column wrapping.
    int wrapWidthPx = 0;
    int (*measureCharWidth)(uint16_t cp) = nullptr;
    // Context-aware variant: advance of the char at byte `i` in `buf` (length
    // `len`), so contextually-shaped Arabic joins measure exactly as rendered.
    // Preferred over measureCharWidth when set.
    int (*measureCharWidthAt)(const char *buf, int i, int len) = nullptr;

    // Each line starting point is saved in this array
    char *linePositions[BUFFER_SIZE + 2];

    // Length of each line
    int lineLengths[BUFFER_SIZE + 2];

    // Total number of lines in the buffer
    int totalLine = 0;

    // Cursor Position in terms of buffer
    int cursorPos = 0;

    // Which line the cursor is placed
    int cursorLine = 0;

    // Cursor position within the line
    int cursorLinePos = 0;

    // Word Counter File
    int wordCountFile = 0;
    int wordCountBuffer = 0;

    // Initialize the editor with the number of columns and rows
    void init(int cols, int rows);
    void loop(); // house keeping tasks

    // File Operation
    void loadFile(String fileName);
    bool saveFile();
    void clearFile();

    // Delete the current file: move it to a backup, remove it from the list,
    // and clear its cached metadata. Unlike clearFile (which empties in place),
    // the slot no longer exists afterwards.
    void deleteFile();

    // Read up to maxBytes from the start of the current file (for auto-title).
    String fileHead(size_t maxBytes);

    // Paging: slide the in-memory window to a different part of the file
    bool loadWindow(size_t offset, size_t length);
    void pageBackward();
    void pageForward();
    void advanceWindow();

    // Guards the (slow, file-I/O) window loads against re-entry: a load can
    // take long enough that the auto-repeat engine re-triggers paging mid-load,
    // overlapping two window swaps and corrupting the refresh.
    bool pagingInProgress = false;

    // Jump the caret to the very start / end of the file, sliding the window
    // there first when the target is outside the loaded region.
    void goToDocTop();
    void goToDocBottom();

    // CRASH RECOVERY
    // While there are unsaved edits, a write-ahead snapshot of the live window
    // is dumped to "<fileName>.recovery" every RECOVERY_INTERVAL ms and removed
    // on every clean save. Because autosave only fires after an idle pause, a
    // long uninterrupted burst (or a hang) can otherwise be lost; the snapshot
    // bounds that loss. Its mere presence at boot means the last session never
    // saved cleanly (crash / power loss), so the window is offered back.
    static const unsigned long RECOVERY_INTERVAL = 3000;
    unsigned long lastRecoverySnapshot = 0;
    String recoveryPath() { return fileName + ".recovery"; }
    void maybeWriteRecovery(); // time-gated snapshot, called from loop()
    void writeRecovery();      // dump window + buffer now
    void clearRecovery();      // remove the sidecar (after a clean save)
    bool recoveryPending();    // a valid sidecar exists for the current file
    bool applyRecovery();      // load the snapshot into the live buffer

    // If a previous saveFile() splice was cut off by power loss it leaves
    // "<base>.tmp" (the complete pre-save file) beside a half-written base.
    // Restore the complete one so boot never opens a truncated journal.
    void salvageInterruptedSave(const String &base);

private:
    bool readRecovery(bool intoBuffer); // shared parse/validate for the two above

public:

    //
    bool savingInProgress = false;

    // SELECTION + INTERNAL CLIPBOARD
    // selAnchor is the fixed end of the selection (-1 = no selection). The live
    // end is cursorPos. The selected byte range is [selStart(), selEnd()).
    int selAnchor = -1;
    static char clipboard[BUFFER_SIZE + 1];
    static int clipboardLength;

    bool hasSelection() { return selAnchor >= 0 && selAnchor != cursorPos; }
    void clearSelection() { selAnchor = -1; }
    int selStart() { return selAnchor < cursorPos ? selAnchor : cursorPos; }
    int selEnd() { return selAnchor < cursorPos ? cursorPos : selAnchor; }
    void deleteSelection();
    void copySelection();
    void selectAll();
    void paste();

    // UNDO / REDO  (command log, scoped to the current loaded window)
    //
    // Each record is the one contiguous change a keystroke made: the bytes it
    // removed and the bytes it inserted at `pos`, plus the buffer length before
    // and after. The lengths are the safety net - on undo/redo we refuse to
    // touch the buffer unless its current size matches what the record expects,
    // so a record that has gone stale (e.g. the window slid underneath it) can
    // never splice against the wrong buffer. The log is also cleared outright on
    // any window/file change. Consecutive typing / backspacing coalesce.
    static const int UNDO_ARENA = 2048; // bytes of edit text retained
    static const int UNDO_MAX = 64;     // max records before the oldest is dropped
    enum UndoKind : uint8_t { UNDO_OTHER = 0, UNDO_TYPE = 1, UNDO_BACK = 2 };
    struct UndoRec
    {
        int pos;          // byte offset where the change starts
        int cursorBefore; // caret position before the edit (restored on undo)
        int lenBefore;    // getBufferSize() expected before this edit (redo guard)
        int lenAfter;     // getBufferSize() expected after this edit (undo guard)
        int removedOff;   // removed text location inside undoArena
        int removedLen;
        int insertedOff;  // inserted text location inside undoArena
        int insertedLen;
        uint8_t kind;
    };
    UndoRec undoRecs[UNDO_MAX];
    int undoCount = 0;   // total records held
    int undoCurrent = 0; // split: [0,current) undoable, [current,count) redoable
    char undoArena[UNDO_ARENA];
    int undoArenaLen = 0;
    char undoBefore[BUFFER_SIZE + 100]; // pre-edit snapshot, diffed to record a step
    int undoCursorBefore = -1;          // -1 = no valid pending snapshot
    uint8_t undoKind = UNDO_OTHER;

    void clearUndo();
    void applyUndo();
    void applyRedo();
    void commitUndo(); // diff undoBefore vs buffer and push a step (coalescing)

    // Handle Keyboard Inputs. keyboard() snapshots the buffer around an editing
    // keystroke and records one undo step; keyboardImpl() is the dispatch.
    void keyboard(int key, bool pressed);
    void keyboardImpl(int key, bool pressed);
    // int, not char: key may be a command code (>= 1000) and truncating it to a
    // char can disguise it as a printable letter (COPY 1111 -> 'W').
    int lastKey = 0;
    unsigned long lastPressTime = 0;
    unsigned long repeatInterval = 80; // ms between repeats
    unsigned long repeatDelay = 300;   // ms before repeat starts
    bool backSpacePressed = false;

    //
    int getBufferSize() { return strlen(buffer); }
    void resetBuffer() { memset(buffer, '\0', sizeof(buffer)); }

    //
    void addChar(int c);
    void removeLastChar();
    void removeCharAtCursor();
    void removeLastWord();

    //
    void updateScreen();

    // Column (code-point index) on a line -> absolute byte offset in buffer.
    int colToByte(int lineIdx, int col);

    //////////////////////////////////
    // SINGLETON PATTERN
    // Static method to get the instance of the Editor
    static Editor &getInstance()
    {
        static Editor instance;
        return instance;
    }

    // Delete copy constructor and assignment operator to ensure singleton properties
    Editor(const Editor &) = delete;
    Editor &operator=(const Editor &) = delete;
    //////////////////////////////////

private:
    // Private constructor to prevent instantiation
    Editor() {}
};
