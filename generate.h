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
#define TT_NONBREAKINGSPACE 6
#define MAX_TOKENS 1048576
extern int token_count;
extern int token_types[MAX_TOKENS];
extern char *token_strings[MAX_TOKENS];

struct piece {
  char *piece;
  struct type_face *font;
  int actualsize;
  float piece_width;  // width of piece after any fiddling
  float natural_width;  // natural width of piece
  // Used to mark spaces that can be stretched for justification
  int piece_is_elastic;
  // Where the piece sits with respect to the nominal baseline
  // (used for placing super- and sub-scripts).
  int piece_baseline;
  struct paragraph *crossrefs;
};

struct line_pieces {
#define MAX_LINE_PIECES 1024
  int line_uid;

  int tied_to_next_line;
  
  // Horizontal space available to the line
  int max_line_width;
  // Reserved space on left side, e.g., for dropchars
  int left_margin;
  // Left and right side width of hanging content.
  float left_hang;
  float right_hang;

  int alignment;
  
  int piece_count;
  float line_width_so_far;

  struct piece pieces[MAX_LINE_PIECES];
  
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

  // The position on the page where the line has been rendered.
  // (used to try to match position of cross-references)
  int on_page_y;

  // DEBUG check whether line has been freed previously
  int freed;
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

  int first_crossref_line;

  // Pointer to next paragraph in hash bin
  // (used for fast searching for cross-reference paragraphs)  
  struct paragraph *next;
  // Source info (used for identifying crossreference paragraphs)
  char *src_book;
  int src_chapter;
  int src_verse;
  int total_height;
};

#define MAX_INCLUDE_DEPTH 32

#define TYPE_FACE_STACK_DEPTH 32
extern struct type_face *type_face_stack[TYPE_FACE_STACK_DEPTH];
extern int type_face_stack_pointer;

extern int leftRightAlternates;
extern int page_width;
extern int page_height;
extern int heading_y;
extern int pagenumber_y;
extern int left_margin;
extern int right_margin;
extern int top_margin;
extern int bottom_margin;
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
extern int footnote_mode;

extern int debug_vspace;
extern int debug_vspace_x;

extern int poetry_left_margin;
extern int poetry_level_indent;
extern int poetry_wrap_indent;
extern int poetry_vspace;

extern int last_verse_on_page;
extern int last_chapter_on_page;
extern int chapter_label;
extern int verse_label;

extern float page_y;
extern struct paragraph body_paragraph;
extern struct paragraph rendered_footnote_paragraph;

#define MAX_FOOTNOTES_ON_PAGE 256
extern int footnote_line_numbers[MAX_FOOTNOTES_ON_PAGE];
extern struct paragraph footnote_paragraphs[MAX_FOOTNOTES_ON_PAGE];
#define MAX_VERSES_ON_PAGE 256
extern struct paragraph cross_reference_paragraphs[MAX_VERSES_ON_PAGE];
extern struct paragraph *target_paragraph;

extern float footnote_rule_width;
extern int footnote_rule_length;
extern int footnote_rule_ydelta;

extern char *crossreference_book;
extern char *crossreference_chapter;
extern char *crossreference_verse;
extern int crossreference_mode;
extern int crossref_min_vspace;
extern int crossref_column_width;
extern int crossref_margin_width;
#define MAX_VERSES_ON_PAGE 256
extern struct paragraph *crossrefs_queue[MAX_VERSES_ON_PAGE];
extern int crossrefs_y[MAX_VERSES_ON_PAGE];
extern int crossref_count;

extern int page_to_record;
extern char *recording_filename;
extern int current_page;
extern int line_uid_counter;

// Are we drawing a left or right face, or neither
#define LR_LEFT -1
#define LR_RIGHT 1
#define LR_NEITHER 0
extern int leftRight;

int include_show_stack();
int tokenise_file(char *filename, int crossreference_parsing);
int clear_tokens();

int new_empty_page(int page_face, int noHeading);

int paragraph_clear_style_stack();
int paragraph_push_style(struct paragraph *p, int font_alignment,int font_index);
int paragraph_append_thinspace(struct paragraph *p,int forceSpaceAtStartOfLine);
int paragraph_append_space(struct paragraph *p, int forceSpaceAtStartOfLine);
int paragraph_append_nonbreakingspace(struct paragraph *p,int forceSpaceAtStartOfLine);
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
int paragraph_insert_vspace(struct paragraph *p,int points, int tied);
int paragraph_pop_style(struct paragraph *p);
int paragraph_height(struct paragraph *p);
int paragraph_dump(struct paragraph *p);
int current_line_flush(struct paragraph *p);
int paragraph_insert_line(struct paragraph *p,int line_number, struct line_pieces *l);

int line_dump(struct line_pieces *l);
int line_dump_segment(struct line_pieces *l,int start,int end);
int line_emit(struct paragraph *p, int n, int isBodyParagraph);
int line_free(struct line_pieces *l);
int line_calculate_height(struct line_pieces *l);
struct line_pieces *line_clone(struct line_pieces *l);
int line_apply_poetry_margin(struct paragraph *p,struct line_pieces *current_line);
int dropchar_margin_check(struct paragraph *p,struct line_pieces *l);
int line_recalculate_width(struct line_pieces *l);
int line_remove_trailing_space(struct line_pieces *l);
int line_remove_leading_space(struct line_pieces *l);
int line_set_checkpoint(struct line_pieces *l);
int line_append_piece(struct line_pieces *l,struct piece *p);
float calc_left_hang(struct line_pieces *l,int left_hang_piece);
struct piece *new_line_piece(char *text,struct type_face *current_font,
			     float size,float text_width,
			     struct paragraph *crossrefs,float baseline);


int generate_footnote_mark(int footnote_count);
int begin_footnote();
int end_footnote();
int output_accumulated_footnotes();
int reenumerate_footnotes(int line_uid);
int footnotes_reset();
char *next_footnote_mark();

int output_accumulated_cross_references(struct paragraph *p,
					int last_line_to_render);
int crossreference_start();
int crossreference_end();
int crossreference_register_verse(struct paragraph *p,
				  char *book,int chapter, int verse);
int crossref_hashtable_init();
int crossref_queue(struct paragraph *p, int y);
int crossref_set_ylimit(int y);

int set_font(char *nickname);

int record_text(struct type_face *font,int text_size,char *text,
		int x,int y,int radians);
int record_fillcolour(float red,float green,float blue);
int record_text_end();
int record_newpage();
int record_rectangle(int x,int y,int width,int height);

int run_tests(char *testdir);
int run_test();
int read_profile(char *file);
int finish_job();
int setup_job();
int typeset_file(char *file);

int unicodify(char *token_text,int *token_len,int max_len,
	      unsigned char next_char);
int unicodePointIsHangable(int codepoint);
int unicodePrevCodePoint(char *text,int *offset);
char *unicodeToUTF8(int codepoint);
int unicode_replace(char *text,int *len,
		    int offset,int number_of_chars_to_replace,
		    int unicode_point);

struct paragraph *layout_paragraph(struct paragraph *p);
