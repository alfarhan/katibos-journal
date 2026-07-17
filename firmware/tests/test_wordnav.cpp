#include "microtest.h"
#include "service/Editor/WordNav.h"
#include <cstring>

// helper: jump from pos, return new pos
static int jr(const char *s, int pos) { return editorWordJump(s, (int)strlen(s), pos, true); }
static int jl(const char *s, int pos) { return editorWordJump(s, (int)strlen(s), pos, false); }

TEST(word_right_english)
{
    const char *s = "the quick brown";
    CHECK_EQ_INT(jr(s, 0), 4);  // "the " -> start of "quick"
    CHECK_EQ_INT(jr(s, 4), 10); // "quick " -> start of "brown"
    CHECK_EQ_INT(jr(s, 10), 15); // last word -> end of buffer
}

TEST(word_left_english)
{
    const char *s = "the quick brown";
    CHECK_EQ_INT(jl(s, 15), 10); // from end -> start of "brown"
    CHECK_EQ_INT(jl(s, 10), 4);  // -> start of "quick"
    CHECK_EQ_INT(jl(s, 4), 0);   // -> start of "the"
    CHECK_EQ_INT(jl(s, 0), 0);   // already at start
}

TEST(word_right_mid_word)
{
    const char *s = "the quick brown";
    CHECK_EQ_INT(jr(s, 1), 4); // inside "the" -> next word
    CHECK_EQ_INT(jl(s, 6), 4); // inside "quick" -> its start
}

// Arabic: each letter is a 2-byte UTF-8 sequence. "سلام عليكم" =
// س(2) ل(2) ا(2) م(2) space(1) ع(2) ل(2) ي(2) ك(2) م(2).
// byte offsets: word1 "سلام" = [0,8), space at 8, word2 "عليكم" = [9,19).
TEST(word_right_arabic)
{
    const char *s = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85";
    CHECK_EQ_INT((int)strlen(s), 19);
    CHECK_EQ_INT(jr(s, 0), 9);  // start of "سلام" -> start of "عليكم" (past the space)
    CHECK_EQ_INT(jr(s, 9), 19); // last word -> end
    CHECK_EQ_INT(jr(s, 4), 9);  // mid first word (on a char boundary) -> next word
}

TEST(word_left_arabic)
{
    const char *s = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85";
    CHECK_EQ_INT(jl(s, 19), 9); // from end -> start of "عليكم"
    CHECK_EQ_INT(jl(s, 9), 0);  // -> start of "سلام"
    CHECK_EQ_INT(jl(s, 8), 0);  // sitting on the space -> start of first word
}

TEST(word_jump_lands_on_char_boundary_arabic)
{
    // every landing offset must be the start of a UTF-8 character (not a
    // continuation byte 0x80-0xBF), proving the jump never splits a glyph.
    const char *s = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85";
    int n = (int)strlen(s);
    for (int p = 0; p <= n; p++)
    {
        int r = jr(s, p), l = jl(s, p);
        CHECK(((unsigned char)s[r] & 0xC0) != 0x80);
        CHECK(((unsigned char)s[l] & 0xC0) != 0x80);
    }
}

TEST(word_jump_mixed_arabic_english)
{
    // "hi سلام yo" : "hi"=[0,2) sp ع... "سلام"=[3,11) sp "yo"=[12,14)
    const char *s = "hi \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 yo";
    CHECK_EQ_INT((int)strlen(s), 14);
    CHECK_EQ_INT(jr(s, 0), 3);   // "hi" -> arabic word
    CHECK_EQ_INT(jr(s, 3), 12);  // arabic word -> "yo"
    CHECK_EQ_INT(jl(s, 14), 12); // end -> "yo"
    CHECK_EQ_INT(jl(s, 12), 3);  // "yo" -> arabic word
    CHECK_EQ_INT(jl(s, 3), 0);   // arabic word -> "hi"
}

TEST(word_jump_bounds_safe)
{
    const char *s = "ab";
    CHECK_EQ_INT(jr(s, 5), 2);  // pos past end clamps to bufLen
    CHECK_EQ_INT(jl(s, -3), 0); // negative pos clamps to 0
}

// paragraph jump
static int pd(const char *s, int pos) { return editorParagraphJump(s, (int)strlen(s), pos, true); }
static int pu(const char *s, int pos) { return editorParagraphJump(s, (int)strlen(s), pos, false); }

TEST(para_down_single_newline)
{
    // "abc\ndef\nghi": paragraphs at 0, 4, 8
    const char *s = "abc\ndef\nghi";
    CHECK_EQ_INT(pd(s, 0), 4);  // start abc -> start def
    CHECK_EQ_INT(pd(s, 4), 8);  // start def -> start ghi
    CHECK_EQ_INT(pd(s, 8), 11); // last para -> end of buffer
    CHECK_EQ_INT(pd(s, 5), 8);  // mid def -> start ghi
}

TEST(para_up_single_newline)
{
    const char *s = "abc\ndef\nghi";
    CHECK_EQ_INT(pu(s, 11), 8); // end -> start ghi
    CHECK_EQ_INT(pu(s, 8), 4);  // start ghi -> start def
    CHECK_EQ_INT(pu(s, 4), 0);  // start def -> start abc
    CHECK_EQ_INT(pu(s, 6), 4);  // mid def -> start def
}

TEST(para_blank_line_separated)
{
    // "p1\n\np2\n\np3": blank lines between paragraphs
    const char *s = "p1\n\np2\n\np3";
    CHECK_EQ_INT(pd(s, 0), 4);  // p1 -> p2 (skip the blank line)
    CHECK_EQ_INT(pd(s, 4), 8);  // p2 -> p3
    CHECK_EQ_INT(pu(s, 8), 4);  // p3 -> p2
    CHECK_EQ_INT(pu(s, 4), 0);  // p2 -> p1
}

TEST(para_jump_arabic_char_boundary)
{
    // "سلام\nعليكم\nمرحبا": each letter 2 bytes. para1=[0,8) \n@8,
    // para2=[9,19) \n@19, para3=[20,30).
    const char *s = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85\n\xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85\n\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    int n = (int)strlen(s);
    CHECK_EQ_INT(pd(s, 0), 9);   // para1 -> para2 (after newline at 8)
    CHECK_EQ_INT(pd(s, 9), 20);  // para2 -> para3 (after newline at 19)
    CHECK_EQ_INT(pu(s, 20), 9);  // para3 -> para2
    // every landing is a UTF-8 char start, never a continuation byte
    for (int p = 0; p <= n; p++)
    {
        CHECK(((unsigned char)s[pd(s, p)] & 0xC0) != 0x80);
        CHECK(((unsigned char)s[pu(s, p)] & 0xC0) != 0x80);
    }
}

TEST(para_jump_bounds_safe)
{
    const char *s = "ab\ncd";
    CHECK_EQ_INT(pd(s, 9), 5);  // past end clamps
    CHECK_EQ_INT(pu(s, -1), 0); // negative clamps
}
