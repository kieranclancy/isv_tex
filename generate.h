#define AL_NONE 0
#define AL_CENTRED 1
#define AL_LEFT 2
#define AL_RIGHT 3
#define AL_JUSTIFIED 4

struct parsed_text {
  int type;
#define PT_ROOT 1
#define PT_TAG 2
#define PT_WORD 3
#define PT_INFO 4

  // List of footnotes (if PT_ROOT)
  struct parsed_text *footnotelist;
  
  // next node in list
  struct parsed_text *next;
  // child (must be a PT_TAG)
  struct parsed_text *child;
  // parent of the tag we are in, so that we can easily
  // and efficiently exit a tag when we find a }
  struct parsed_text *tag_parent;
  // Information contained in this tag (for PT_INFO only).
  // Used to set book titles etc
  char *key;
  char *value;

  // The actual text of this word if a PT_WORD
  // XXX - need to define list structure for here, as well as record
  // size of rendered word

  
};

struct type_face {
  // Name of font as referenced by us
  char *font_nickname;

  // Information about which font to use and at what size
  char *font_filename;
  int font_size;
  int smallcaps;
  int baseline_delta;
  int line_count;

  // colour of text
  float red;
  float green;
  float blue;

  // -----------------
  // Fields below here are populated by us, whereas fields above are set by the
  // user in .profile files.

  HPDF_Font font;

  // points between lines (read from libfreetype)
  int linegap;
};

#define TT_TEXT 0
#define TT_TAG 1
#define TT_ENDTAG 2
#define TT_PARAGRAPH 3
#define TT_SPACE 4
#define TT_THINSPACE 5
#define MAX_TOKENS 1048576
extern int token_count;
extern int token_types[MAX_TOKENS];
extern char *token_strings[MAX_TOKENS];

struct line_pieces {
#define MAX_LINE_PIECES 256
  int line_uid;
  
  // Horizontal space available to the line
  int max_line_width;
  // Reserved space on left side, e.g., for dropchars
  int left_margin;

  int alignment;
  
  int piece_count;
  float line_width_so_far;
  char *pieces[MAX_LINE_PIECES];
  struct type_face *fonts[MAX_LINE_PIECES];
  int actualsizes[MAX_LINE_PIECES];
  float piece_widths[MAX_LINE_PIECES];
  // Used to mark spaces that can be stretched for justification
  int piece_is_elastic[MAX_LINE_PIECES];
  // Where the piece sits with respect to the nominal baseline
  // (used for placing super- and sub-scripts).
  int piece_baseline[MAX_LINE_PIECES];
  
 
  // We try adding a word first, and if it doesn't fit,
  // then we re-wind to the last checkpoint, flush that
  // line out, then purge out the flushed pieces, leaving
  // only the non-emitted ones in the line.  This approach
  // makes it fairly easy to add 
  int checkpoint;

  // Vertical height data
  float line_height;
  int ascent;
  int descent;
};

// Current paragraph
struct paragraph {
  int line_count;
#define MAX_LINES_IN_PARAGRAPH 256
  struct line_pieces *paragraph_lines[MAX_LINES_IN_PARAGRAPH];

  // The line currently being assembled
  struct line_pieces *current_line;

  int drop_char_left_margin;
  int drop_char_margin_line_count;
  
  int poem_level;
  int poem_subsequent_line;

  int last_char_is_a_full_stop;
};

#define MAX_INCLUDE_DEPTH 32

#define TYPE_FACE_STACK_DEPTH 32
extern struct type_face *type_face_stack[TYPE_FACE_STACK_DEPTH];
extern int type_face_stack_pointer;

extern int leftRightAlternates;
extern int page_width;
extern int page_height;
extern int left_margin;
extern int right_margin;
extern int top_margin;
extern int bottom_margin;
extern int marginpar_width;
extern int marginpar_margin;
extern int booktab_width;
extern int booktab_height;
extern int booktab_upperlimit;
extern int booktab_lowerlimit;
extern int paragraph_indent;
extern int passageheader_vspace;
extern int footnote_sep_vspace;
extern float line_spacing;
extern int include_depth;
extern char *include_files[MAX_INCLUDE_DEPTH];
extern int include_lines[MAX_INCLUDE_DEPTH];
extern struct type_face *current_font;
extern HPDF_Doc pdf;
extern HPDF_Page page;
extern struct type_face type_faces[];

extern int footnote_stack_depth;
extern char footnote_mark_string[4];
extern int footnote_count;

extern int debug_vspace;
extern int debug_vspace_x;

extern int poetry_left_margin;
extern int poetry_level_indent;
extern int poetry_wrap_indent;
extern int poetry_vspace;

extern float page_y;
extern struct paragraph body_paragraph;
extern struct paragraph rendered_footnote_paragraph;

#define MAX_FOOTNOTES_ON_PAGE 256
extern int footnote_line_numbers[MAX_FOOTNOTES_ON_PAGE];
extern struct paragraph footnote_paragraphs[MAX_FOOTNOTES_ON_PAGE];
#define MAX_VERSES_ON_PAGE 256
extern struct paragraph cross_reference_paragraphs[MAX_VERSES_ON_PAGE];
extern struct paragraph *target_paragraph;

extern int leftRight;


int include_show_stack();
int tokenise_file(char *filename);

int new_empty_page(int page_face);

int paragraph_clear_style_stack();
int paragraph_push_style(struct paragraph *p, int font_alignment,int font_index);
int paragraph_append_thinspace(struct paragraph *p,int forceSpaceAtStartOfLine);
int paragraph_append_space(struct paragraph *p, int forceSpaceAtStartOfLine);
int paragraph_append_text(struct paragraph *p,char *text,int baseline,int forceSpaceAtStartOfLine);
int paragraph_append_characters(struct paragraph *p,char *text,int size,int baseline,int forceSpaceAtStartOfLine);
int paragraph_set_widow_counter(struct paragraph *p,int lines);
int paragraph_setup_next_line(struct paragraph *p);
int paragraph_append_line(struct paragraph *p,struct line_pieces *line);
int paragraph_flush(struct paragraph *p);
int paragraph_init(struct paragraph *p);
int paragraph_clear(struct paragraph *p);
int paragraph_clone(struct paragraph *dst,struct paragraph *src);
int paragraph_append(struct paragraph *dst,struct paragraph *src);
int paragraph_insert_vspace(struct paragraph *p,int points);
int paragraph_pop_style(struct paragraph *p);
int paragraph_height(struct paragraph *p);
int paragraph_dump(struct paragraph *p);
int current_line_flush(struct paragraph *p);

int line_dump(struct line_pieces *l);
int line_emit(struct paragraph *p, int n);
int line_free(struct line_pieces *l);
int line_calculate_height(struct line_pieces *l);
struct line_pieces *line_clone(struct line_pieces *l);
int line_apply_poetry_margin(struct paragraph *p,struct line_pieces *current_line);
int dropchar_margin_check(struct paragraph *p,struct line_pieces *l);
int line_recalculate_width(struct line_pieces *l);
int line_remove_trailing_space(struct line_pieces *l);
int line_remove_leading_space(struct line_pieces *l);

int generate_footnote_mark(int footnote_count);
int begin_footnote();
int end_footnote();
int output_accumulated_footnotes();
int reenumerate_footnotes(int line_uid);

int output_accumulated_cross_references();


int set_font(char *nickname);
