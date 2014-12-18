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
#define MAX_TOKENS 1048576
extern int token_count;
extern int token_types[MAX_TOKENS];
extern char *token_strings[MAX_TOKENS];


#define MAX_INCLUDE_DEPTH 32

int include_show_stack();
int tokenise_file(char *filename);


