#include "microtest.h"
#include "service/Editor/EditorWindow.h"

// bufSize 8000 (rev_8), step = 4000.

TEST(resume_small_file_opens_at_caret)
{
    long seek, cur;
    editorResumeWindow(120, 300, 8000, seek, cur);
    CHECK_EQ_INT(seek, 0);   // whole file fits in the buffer
    CHECK_EQ_INT(cur, 120);  // caret lands exactly where it was
}

TEST(resume_caret_near_head_no_offset)
{
    long seek, cur;
    editorResumeWindow(500, 50000, 8000, seek, cur); // caret <= step
    CHECK_EQ_INT(seek, 0);
    CHECK_EQ_INT(cur, 500);
}

TEST(resume_caret_deep_keeps_context_above)
{
    long seek, cur;
    editorResumeWindow(20000, 50000, 8000, seek, cur);
    CHECK_EQ_INT(seek, 16000);        // caretAbs - step
    CHECK_EQ_INT(cur, 4000);          // caret sits half a buffer in
    CHECK(seek + cur == 20000);       // window+cursor recover the absolute offset
}

TEST(resume_caret_at_eof)
{
    long seek, cur;
    editorResumeWindow(50000, 50000, 8000, seek, cur);
    CHECK_EQ_INT(seek, 46000);
    CHECK_EQ_INT(cur, 4000);
}

TEST(resume_clamps_caret_past_eof)
{
    // file shrank since the caret was recorded (edited elsewhere / re-synced)
    long seek, cur;
    editorResumeWindow(99999, 300, 8000, seek, cur);
    CHECK_EQ_INT(seek, 0);
    CHECK_EQ_INT(cur, 300); // clamped to fileSize, still in-bounds
}

TEST(resume_negative_caret_safe)
{
    long seek, cur;
    editorResumeWindow(-5, 1000, 8000, seek, cur);
    CHECK_EQ_INT(seek, 0);
    CHECK_EQ_INT(cur, 0);
}

TEST(resume_caret_just_past_step_boundary)
{
    long seek, cur;
    editorResumeWindow(4001, 50000, 8000, seek, cur);
    CHECK_EQ_INT(seek, 1);
    CHECK_EQ_INT(cur, 4000);
}
