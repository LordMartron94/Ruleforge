#include <nexus.h>

#include "../logos.h"

/* One record per line. The line's text ends break_length bytes before the next line starts. */
typedef struct LLexerSourceIndexLine {
  uint_large start;        /* Offset of the line's first byte. */
  uint_large event_first;  /* Index of the line's first irregular byte in the event table. */
  uint8      break_length; /* 0 for the open last line, 1 for LF or CR, 2 for CRLF. */
} LLexerSourceIndexLine;

/* A byte run whose columns differ from its byte count: a tab, a multi-byte character, or a byte order mark. Bytes between two events are one
   byte and one column each, in every measure, so only these need a record. */
typedef struct LLexerSourceIndexEvent {
  uint_large offset;
  uint32     character_before;
  uint32     visual_before;
  uint32     character_after;
  uint32     visual_after;
  uint8      length;
} LLexerSourceIndexEvent;

struct LLexerSourceIndex {
  uint8 tab_size;

  LLexerSourceIndexLine *lines;
  uint_large             line_count;
  uint_large             line_capacity;

  LLexerSourceIndexEvent *events;
  uint_large              event_count;
  uint_large              event_capacity;

  uint_large length;

  /* Columns of the open last line, counting every byte resolved so far. */
  uint_large character_column;
  uint_large visual_column;

  boolean previous_was_cr; /* The last byte was CR, so an LF right after it belongs to the same break. */

  /* A UTF-8 sequence cut off by the end of a chunk: its bytes are not resolved (not counted) until it completes or breaks. */
  byte       pending[4];
  uint8      pending_count;
  uint8      pending_length; /* The length the lead byte promises. */
  uint_large pending_start;
};

LLexerSourceIndex *logos_language_tooling_lexing_source_index_create(uint8 tab_size) {
  LLexerSourceIndex *index;

  NEXUS_PROGRAM_PRECONDITION(tab_size >= 1, "the tab size must be at least 1");

  index           = (LLexerSourceIndex *)nexus_memory_allocate(NEXUS_SIZEOF(*index));
  index->tab_size = tab_size;

  index->line_capacity  = 64;
  index->lines          = (LLexerSourceIndexLine *)nexus_memory_allocate(index->line_capacity * NEXUS_SIZEOF(LLexerSourceIndexLine));
  index->event_capacity = 16;
  index->events         = (LLexerSourceIndexEvent *)nexus_memory_allocate(index->event_capacity * NEXUS_SIZEOF(LLexerSourceIndexEvent));

  index->lines[0].start        = 0;
  index->lines[0].event_first  = 0;
  index->lines[0].break_length = 0;
  index->line_count            = 1;
  index->event_count           = 0;

  index->length           = 0;
  index->character_column = 0;
  index->visual_column    = 0;
  index->previous_was_cr  = FALSE;
  index->pending_count    = 0;
  index->pending_length   = 0;
  index->pending_start    = 0;

  return index;
}

void logos_language_tooling_lexing_source_index_destroy(LLexerSourceIndex *index) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");

  nexus_memory_deallocate(index->events);
  nexus_memory_deallocate(index->lines);
  nexus_memory_deallocate(index);
}

static void l_lexing_source_index_line_add(LLexerSourceIndex *index, uint_large start) {
  if (index->line_count == index->line_capacity) {
    index->line_capacity *= 2;
    index->lines = (LLexerSourceIndexLine *)nexus_memory_reallocate(index->lines, index->line_capacity * NEXUS_SIZEOF(LLexerSourceIndexLine));
  }

  index->lines[index->line_count].start        = start;
  index->lines[index->line_count].event_first  = index->event_count;
  index->lines[index->line_count].break_length = 0;
  index->line_count++;

  index->character_column = 0;
  index->visual_column    = 0;
}

/* Records a run at offset of length bytes that is character_width code points wide and visual_after - visual_before cells, then advances the
   open line's columns past it. */
static void l_lexing_source_index_event_add(LLexerSourceIndex *index, uint_large offset, uint8 length, uint32 character_width,
                                            uint_large visual_after) {
  LLexerSourceIndexEvent *event;

  NEXUS_PROGRAM_PRECONDITION(visual_after < (uint_large)UINT32_MAX_VAL && index->character_column < (uint_large)UINT32_MAX_VAL,
                             "a line is too wide for the source index");

  if (index->event_count == index->event_capacity) {
    index->event_capacity *= 2;
    index->events = (LLexerSourceIndexEvent *)nexus_memory_reallocate(index->events, index->event_capacity * NEXUS_SIZEOF(LLexerSourceIndexEvent));
  }

  event                   = &index->events[index->event_count];
  event->offset           = offset;
  event->length           = length;
  event->character_before = (uint32)index->character_column;
  event->visual_before    = (uint32)index->visual_column;
  event->character_after  = (uint32)(index->character_column + character_width);
  event->visual_after     = (uint32)visual_after;
  index->event_count++;

  index->character_column += character_width;
  index->visual_column = visual_after;
}

/* The pending bytes are not a valid sequence: each is one column. */
static void l_lexing_source_index_pending_discard(LLexerSourceIndex *index) {
  index->character_column += index->pending_count;
  index->visual_column += index->pending_count;
  index->pending_count = 0;
}

/* Whether byte may be the next byte of the pending sequence. The second byte has tighter bounds after some leads, which rejects overlong
   encodings, surrogates and values above U+10FFFF. */
static boolean l_lexing_source_index_continues(const LLexerSourceIndex *index, byte value) {
  byte low;
  byte high;

  low  = 0x80;
  high = 0xBF;

  if (index->pending_count == 1) {
    switch (index->pending[0]) {
    case 0xE0:
      low = 0xA0;
      break;
    case 0xED:
      high = 0x9F;
      break;
    case 0xF0:
      low = 0x90;
      break;
    case 0xF4:
      high = 0x8F;
      break;
    default:
      break;
    }
  }

  return value >= low && value <= high;
}

void logos_language_tooling_lexing_source_index_chunk_add(LLexerSourceIndex *index, const byte *chunk, uint_large chunk_size) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(chunk != NULL || chunk_size == 0, "a nonempty chunk must not be NULL");

  for (i = 0; i < chunk_size; i++) {
    byte       value;
    uint_large offset;

    value  = chunk[i];
    offset = index->length + i;

    if (index->pending_count > 0) {
      if (l_lexing_source_index_continues(index, value)) {
        index->pending[index->pending_count] = value;
        index->pending_count++;

        if (index->pending_count == index->pending_length) {
          /* A complete character. The byte order mark is the one sequence that takes no column, and only at the start of the source. */
          if (index->pending_start == 0 && index->pending_length == 3 && index->pending[0] == 0xEF && index->pending[1] == 0xBB &&
              index->pending[2] == 0xBF) {
            l_lexing_source_index_event_add(index, index->pending_start, 3, 0, index->visual_column);
          } else {
            l_lexing_source_index_event_add(index, index->pending_start, index->pending_length, 1, index->visual_column + 1);
          }

          index->pending_count = 0;
        }

        index->previous_was_cr = FALSE;

        continue;
      }

      l_lexing_source_index_pending_discard(index);
    }

    if (value < 0x80) {
      if (value == '\n') {
        if (index->previous_was_cr) {
          /* The CR already began the next line at this very byte; the LF finishes the same break, so that line starts one byte later. */
          index->lines[index->line_count - 2].break_length = 2;
          index->lines[index->line_count - 1].start        = offset + 1;
        } else {
          index->lines[index->line_count - 1].break_length = 1;
          l_lexing_source_index_line_add(index, offset + 1);
        }

        index->previous_was_cr = FALSE;
      } else if (value == '\r') {
        index->lines[index->line_count - 1].break_length = 1;
        l_lexing_source_index_line_add(index, offset + 1);

        index->previous_was_cr = TRUE;
      } else if (value == '\t') {
        l_lexing_source_index_event_add(index, offset, 1, 1, ((index->visual_column / index->tab_size) + 1) * index->tab_size);

        index->previous_was_cr = FALSE;
      } else {
        index->character_column++;
        index->visual_column++;
        index->previous_was_cr = FALSE;
      }

      continue;
    }

    index->previous_was_cr = FALSE;

    if (value >= 0xC2 && value <= 0xF4) {
      /* A lead byte: its sequence is resolved once it completes or breaks, possibly in a later chunk. */
      index->pending[0]    = value;
      index->pending_count = 1;
      if (value < 0xE0) {
        index->pending_length = 2;
      } else if (value < 0xF0) {
        index->pending_length = 3;
      } else {
        index->pending_length = 4;
      }

      index->pending_start = offset;
    } else {
      /* A stray continuation byte, 0xC0, 0xC1 or 0xF5..0xFF can start nothing. */
      index->character_column++;
      index->visual_column++;
    }
  }

  index->length += chunk_size;
}

uint_large logos_language_tooling_lexing_source_index_length_get(const LLexerSourceIndex *index) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");

  return index->length;
}

uint_large logos_language_tooling_lexing_source_index_line_count_get(const LLexerSourceIndex *index) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");

  return index->line_count;
}

/* The events of one line are the table range [first, end). */
static uint_large l_lexing_source_index_event_end(const LLexerSourceIndex *index, uint_large line) {
  return line + 1 < index->line_count ? index->lines[line + 1].event_first : index->event_count;
}

void logos_language_tooling_lexing_source_index_position_get(const LLexerSourceIndex *index, uint_large offset, LLexerSourcePosition *out_position) {
  uint_large low;
  uint_large high;
  uint_large line;
  uint_large first;
  uint_large end;

  NEXUS_PROGRAM_PRECONDITION(index != NULL && out_position != NULL, "index and out_position must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(offset <= index->length, "the offset must not lie beyond the indexed source");

  /* The last line that starts at or before offset: starts increase and the first is 0, so the answer always exists. */
  low  = 0;
  high = index->line_count;

  while (high - low > 1) {
    uint_large middle;

    middle = low + ((high - low) / 2);

    if (index->lines[middle].start <= offset) {
      low = middle;
    } else {
      high = middle;
    }
  }

  line  = low;
  first = index->lines[line].event_first;
  end   = l_lexing_source_index_event_end(index, line);

  /* The last irregular run of the line that starts at or before offset. Past it the columns grow one per byte. */
  low  = first;
  high = end;

  while (low < high) {
    uint_large middle;

    middle = low + ((high - low) / 2);

    if (index->events[middle].offset <= offset) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }

  out_position->line        = line;
  out_position->column_byte = offset - index->lines[line].start;

  if (low == first) {
    out_position->column_character = out_position->column_byte;
    out_position->column_visual    = out_position->column_byte;
  } else {
    const LLexerSourceIndexEvent *event;
    uint_large                    event_end;

    event     = &index->events[low - 1];
    event_end = event->offset + event->length;

    if (offset >= event_end) {
      out_position->column_character = event->character_after + (offset - event_end);
      out_position->column_visual    = event->visual_after + (offset - event_end);
    } else {
      /* Inside a character: the character's own start. */
      out_position->column_character = event->character_before;
      out_position->column_visual    = event->visual_before;
    }
  }
}

void logos_language_tooling_lexing_source_index_span_get(const LLexerSourceIndex *index, LLexerToken token, LLexerSourceSpan *out_span) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL && out_span != NULL, "index and out_span must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(token.lexeme_offset <= index->length && token.lexeme_width <= index->length - token.lexeme_offset,
                             "the token must not reach beyond the indexed source");

  logos_language_tooling_lexing_source_index_position_get(index, token.lexeme_offset, &out_span->start);
  logos_language_tooling_lexing_source_index_position_get(index, token.lexeme_offset + token.lexeme_width, &out_span->end);
}

void logos_language_tooling_lexing_source_index_line_range_get(const LLexerSourceIndex *index, uint_large line, uint_large *out_start,
                                                               uint_large *out_end) {
  uint_large next_start;

  NEXUS_PROGRAM_PRECONDITION(index != NULL && out_start != NULL && out_end != NULL, "index, out_start and out_end must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(line < index->line_count, "the line must exist");

  next_start = line + 1 < index->line_count ? index->lines[line + 1].start : index->length;

  *out_start = index->lines[line].start;
  *out_end   = next_start - index->lines[line].break_length;
}

uint_large logos_language_tooling_lexing_source_index_offset_get(const LLexerSourceIndex *index, uint_large line, uint_large column_character) {
  uint_large low;
  uint_large high;
  uint_large first;
  uint_large offset;
  uint_large text_start;
  uint_large text_end;

  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(line < index->line_count, "the line must exist");

  logos_language_tooling_lexing_source_index_line_range_get(index, line, &text_start, &text_end);

  first = index->lines[line].event_first;

  /* The runs that end at or before the column, counted in characters: the last of them is the closest anchor, and the bytes after it are one
     character each. */
  low  = first;
  high = l_lexing_source_index_event_end(index, line);

  while (low < high) {
    uint_large middle;

    middle = low + ((high - low) / 2);

    if (index->events[middle].character_after <= column_character) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }

  if (low == first) {
    offset = text_start + column_character;
  } else {
    const LLexerSourceIndexEvent *event;

    event  = &index->events[low - 1];
    offset = event->offset + event->length + (column_character - event->character_after);
  }

  return offset < text_end ? offset : text_end;
}
