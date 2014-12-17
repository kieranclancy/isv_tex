/*
  Generate a PDF of the ISV bible (or another translation).

  Note that the text of the ISV is copyright, and is not part of this
  program, even it comes bundled together, and thus is not touched by
  the GPL.  

  This program is copyright Paul Gardner-Stephen 2014, and is offered
  on the following basis:
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include "hpdf.h"
#include "generate.h"


HPDF_Font current_font=NULL;
int current_font_size=0;
int current_font_smallcaps=0;
int last_char_is_a_full_stop=0;

struct line_pieces {
#define MAX_LINE_PIECES 256
  // Horizontal space available to the line
  int max_line_width;

  int piece_count;
  int line_width_so_far;
  char *pieces[MAX_LINE_PIECES];
  HPDF_Font fonts[MAX_LINE_PIECES];
  int fontsizes[MAX_LINE_PIECES];
  int piece_widths[MAX_LINE_PIECES];
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
  int line_height;
  int ascent;
  int descent;
};

// The line currently being assembled
struct line_pieces *current_line=NULL;

// Current paragraph
int paragraph_line_count=0;
#define MAX_LINES_IN_PARAGRAPH 256
struct line_pieces *paragraph_lines[MAX_LINES_IN_PARAGRAPH];

int paragraph_append_line(struct line_pieces *line);

void error_handler(HPDF_STATUS error_number, HPDF_STATUS detail_number,
		   void *data)
{
  fprintf(stderr,"HPDF error: %04x.%u\n",
	  (HPDF_UINT)error_number,(HPDF_UINT)detail_number);
  exit(-1);
}

char *output_file="output.pdf";

int leftRightAlternates=1;

// Page size in points
int page_width=72*5;
int page_height=72*7;
// Colour of "red" text
char *red_colour="#000000";
int left_margin=72;
int right_margin=72;
int top_margin=72;
int bottom_margin=72;
int marginpar_width=50;
int marginpar_margin=8;
int booktab_width=27;
int booktab_height=115;
int booktab_upperlimit=36;
int booktab_lowerlimit=72*5.5;

char *bookpretitle_fontfile="bookpretitle.ttf";
int bookpretitle_fontsize=12;
int bookpretitle_smallcaps=10;
char *booktitle_fontfile="booktitle.ttf";
int booktitle_fontsize=25;
int booktitle_smallcaps=20;
char *header_fontfile="header.ttf";
int header_fontsize=12;
char *booktab_fontfile="booktab.ttf";
int booktab_fontsize=12;
char *blackletter_fontfile="blacktext.ttf";
int blackletter_fontsize=8;
char *redletter_fontfile="redtext.ttf";
int redletter_fontsize=8;
char *versenum_fontfile="blacktext.ttf";
int versenum_fontsize=4;
char *chapternum_fontfile="redtext.ttf";
int chapternum_fontsize=8;
int chapternum_lines=2;
char *footnotemark_fontfile="blacktext.ttf";
int footnotemark_fontsize=4;


/* Read the profile of the bible to build.
   The profile consists of a series of key=value pairs that set various 
   parameters for the typesetting.
*/
int include_depth=0;
char *include_files[MAX_INCLUDE_DEPTH];
int include_lines[MAX_INCLUDE_DEPTH];

int include_show_stack()
{
  int i;
  for(i=include_depth-1;i>=0;i--)
    {
      fprintf(stderr,"In file included from %s:%d\n",
	      include_files[i],include_lines[i]);
    }
  return 0;
}

int include_push(char *file,int line_num)
{
  if (include_depth>=MAX_INCLUDE_DEPTH) {
    include_show_stack();
    fprintf(stderr,"%s:%d:Includes nested too deeply.\n",file,line_num);
    exit(-1);
  }
  int i;
  for(i=0;i<include_depth;i++) {
    if (!strcmp(include_files[i],file)) {
      include_show_stack();
      fprintf(stderr,"%s:%d:Include nested too deeply.\n",file,line_num);
      exit(-1);
    }
  }
  include_files[include_depth]=strdup(file);
  include_lines[include_depth++]=line_num;
  return 0;
}

int include_pop()
{
  if (include_depth>0) free(include_files[include_depth-1]);
  include_depth--;
  return 0;
}

int read_profile(char *file)
{
  FILE *f=fopen(file,"r");
  char line[1024];

  int errors=0;
  
  if (!f) {
    include_show_stack();
    fprintf(stderr,"Could not read profile file '%s'\n",file);
    exit(-1);
  }

  int line_num=0;
  
  line[0]=0; fgets(line,1024,f);
  while(line[0])
    {
      line_num++;
      char key[1024],value[1024];
      if (line[0]!='#'&&line[0]!='\r'&&line[0]!='\n') {
	if (sscanf(line,"%[^ ] %[^\r\n]",key,value)==2)
	  {
	    if (!strcasecmp(key,"include")) {
	      include_push(file,line_num);
	      read_profile(value);
	      include_pop();
	    }

	    else if (!strcasecmp(key,"output_file")) output_file=strdup(value);

	    // Does the output have left and right faces?
	    else if (!strcasecmp(key,"left_and_right"))
	      leftRightAlternates=atoi(value);

	    // Size of page
	    else if (!strcasecmp(key,"page_width")) page_width=atoi(value);
	    else if (!strcasecmp(key,"page_height")) page_height=atoi(value);

	    // Margins of a left page (left_margin and right_margin get switched
	    // automatically if output is for a book
	    else if (!strcasecmp(key,"left_margin")) left_margin=atoi(value);
	    else if (!strcasecmp(key,"right_margin")) right_margin=atoi(value);
	    else if (!strcasecmp(key,"top_margin")) top_margin=atoi(value);
	    else if (!strcasecmp(key,"bottom_margin")) bottom_margin=atoi(value);

	    // Width of marginpar for holding cross-references
	    else if (!strcasecmp(key,"marginpar_width")) marginpar_width=atoi(value);
	    // Margin between marginpar and edge of page
	    else if (!strcasecmp(key,"marginpar_margin")) marginpar_margin=atoi(value);

	    // Font selection
	    else if (!strcasecmp(key,"booktab_fontsize"))
	      booktab_fontsize=atoi(value);
	    else if (!strcasecmp(key,"booktab_fontfile"))
	      booktab_fontfile=strdup(value);	    
	    else if (!strcasecmp(key,"header_fontsize"))
	      header_fontsize=atoi(value);
	    else if (!strcasecmp(key,"header_fontfile"))
	      header_fontfile=strdup(value);	    
	    else if (!strcasecmp(key,"bookpretitle_fontfile"))
	      bookpretitle_fontfile=strdup(value);	    
	    else if (!strcasecmp(key,"bookpretitle_fontsize"))
	      bookpretitle_fontsize=atoi(value);
	    else if (!strcasecmp(key,"bookpretitle_smallcaps"))
	      bookpretitle_smallcaps=atoi(value);
	    else if (!strcasecmp(key,"booktitle_fontfile"))
	      booktitle_fontfile=strdup(value);	    
	    else if (!strcasecmp(key,"booktitle_fontsize"))
	      booktitle_fontsize=atoi(value);
	    else if (!strcasecmp(key,"booktitle_smallcaps"))
	      booktitle_smallcaps=atoi(value);
	    else if (!strcasecmp(key,"redletter_fontsize"))
	      redletter_fontsize=atoi(value);
	    else if (!strcasecmp(key,"redletter_fontfile"))
	      redletter_fontfile=strdup(value);	    
	    else if (!strcasecmp(key,"blackletter_fontsize"))
	      blackletter_fontsize=atoi(value);
	    else if (!strcasecmp(key,"blackletter_fontfile"))
	      blackletter_fontfile=strdup(value);	    

	    // Size of solid colour book tabs
	    else if (!strcasecmp(key,"booktab_width")) booktab_width=atoi(value);
	    else if (!strcasecmp(key,"booktab_height")) booktab_height=atoi(value);
	    // Set vertical limit of where booktabs can be placed
	    else if (!strcasecmp(key,"booktab_upperlimit")) booktab_upperlimit=atoi(value);
	    else if (!strcasecmp(key,"booktab_lowerlimit")) booktab_lowerlimit=atoi(value);

	    // colour of red text
	    else if (!strcasecmp(key,"red")) red_colour=strdup(value);

	    else {
	      include_show_stack();
	      fprintf(stderr,"%s:%d:Syntax error (unknown key '%s')\n",
		      file,line_num,key);
	      errors++;
	    }
	  } else {
	  include_show_stack();
	  fprintf(stderr,"%s:%d:Syntax error (should be keyword value)\n",file,line_num);
	  errors++;
	}
      }
      line[0]=0; fgets(line,1024,f);
    }
  fclose(f);
  if (errors) exit(-1); else return 0;
}

HPDF_Doc pdf;

HPDF_Page page;


HPDF_Font bookpretitle_font;
HPDF_Font booktitle_font;
HPDF_Font header_font;
HPDF_Font booktab_font;
HPDF_Font blackletter_font;
HPDF_Font redletter_font;
HPDF_Font blackletter_font;

// Are we drawing a left or right face, or neither
#define LR_LEFT -1
#define LR_RIGHT 1
#define LR_NEITHER 0
int leftRight=LR_LEFT;

// Current booktab text
char *booktab_text=NULL;
// And position
int booktab_y=0;

// Current vertical position on the page
int page_y=0;

// Short name of book used for finding cross-references
char *short_book_name=NULL;

// Create a new empty page
// Empty of main content, that is, a booktab will be added
int new_empty_page(int leftRight)
{
  // Create the page
  page = HPDF_AddPage(pdf);

  // Set its dimensions
  HPDF_Page_SetWidth(page,page_width);
  HPDF_Page_SetHeight(page,page_height);

  // XXX Draw booktab
  if (booktab_text) {
    // Work out vertical position of book tab
    if ((booktab_y<booktab_upperlimit)|| 
	(booktab_y+booktab_height*2>booktab_lowerlimit))
      booktab_y=booktab_upperlimit;
    else
      booktab_y=booktab_y+booktab_height;

    // Draw booktab box
    int x=0;
    if (leftRight==LR_RIGHT) x = page_width-booktab_width+1;
    HPDF_Page_SetRGBFill (page, 0.25, 0.25, 0.25);
    HPDF_Page_Rectangle(page, x, page_height-booktab_y-booktab_height+1,
			booktab_width, booktab_height);
    HPDF_Page_Fill(page);

    // Now draw sideways text
    float text_width, text_height;

    HPDF_Page_SetFontAndSize (page, booktab_font, booktab_fontsize);
    text_width = HPDF_Page_TextWidth(page,booktab_text);
    text_height = HPDF_Font_GetCapHeight(booktab_font) * booktab_fontsize/1000;
    
    int y;
    float angle_degrees=0;
    if (leftRight==LR_LEFT) {
      // Left page
      angle_degrees=90;
      x = text_height + (booktab_width-text_height)/2;
      y = text_width + booktab_y + (booktab_height-text_width)/2;
    } else {
      // Right page
      angle_degrees=-90;
      x = page_width - text_height - (booktab_width-text_height)/2;
      y = booktab_y + (booktab_height-text_width)/2;
    }
    y = page_height-y;
    float radians = angle_degrees / 180 * 3.141592;
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
    HPDF_Page_SetRGBFill (page, 1.00, 1.00, 1.00);
    HPDF_Page_SetTextMatrix (page,
			     cos(radians), sin(radians),
			     -sin(radians), cos(radians),
                x, y);
    HPDF_Page_ShowText (page, booktab_text);
    HPDF_Page_EndText (page);

  }
  
  return 0;
}

#define MAX_FONTS 64
int font_count=0;
char *font_filenames[MAX_FONTS];
const char *font_names[MAX_FONTS];

const char *resolve_font(char *font_filename)
{
  int i;
  for(i=0;i<font_count;i++)
    if (!strcmp(font_filename,font_filenames[i]))
      return font_names[i];

  if (i>=MAX_FONTS) {
    fprintf(stderr,"Too many fonts.\n"); exit(-2);
  }
  font_filenames[font_count]=strdup(font_filename);
  font_names[font_count++]=HPDF_LoadTTFontFromFile (pdf, font_filename, HPDF_TRUE);
  return font_names[font_count-1];
}

int current_line_flush()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  if (current_line)
    paragraph_append_line(current_line);
  current_line=NULL;
  return 0;
}

int line_free(struct line_pieces *l)
{
  int i;
  if (!l) return 0;
  for(i=0;i<l->piece_count;i++) free(l->pieces[i]);
  free(l);
  return 0;
}

int line_emit(struct line_pieces *l)
{
  int baseline_y=page_y+l->ascent;
  // convert y to libharu coordinate system
  int y=page_height-baseline_y-l->ascent;

  int i;

  // Now draw the pieces
  HPDF_Page_BeginText (page);
  HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
  int x=0;
  for(i=0;i<l->piece_count;i++) {
    HPDF_Page_SetFontAndSize(page,l->fonts[i],l->fontsizes[i]);
    HPDF_Page_SetRGBFill(page,0.00,0.00,0.00);
    HPDF_Page_TextOut(page,left_margin+x,y-l->piece_baseline[i],
		      l->pieces[i]);
    x=x+l->piece_widths[i];
  }
  HPDF_Page_EndText (page);

  page_y=page_y+l->ascent+l->descent;
  return 0;
}


int line_calculate_height(struct line_pieces *l)
{
  int max=-1; int min=0;
  int i;
  fprintf(stderr,"Calculating height of line %p\n",l);
  for(i=0;i<l->piece_count;i++)
    {
      // Get ascender height of font
      int ascender_height=HPDF_Font_GetAscent(l->fonts[i])*l->fontsizes[i]/1000;
      // Get descender depth of font
      int descender_depth=HPDF_Font_GetDescent(l->fonts[i])*l->fontsizes[i]/1000;
      if (ascender_height+l->piece_baseline[i]>max)
	max=ascender_height+l->piece_baseline[i];
      if (l->piece_baseline[i]-descender_depth<min)
	min=l->piece_baseline[i]-descender_depth;
    }

  l->line_height=max-min+1;
  l->ascent=max; l->descent=-min;
  fprintf(stderr,"  line ascends %dpts and descends %d points.\n",max,-min);
  return 0;
}


int paragraph_flush()
{  
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // First flush the current line
  if (current_line) current_line_flush();

  // XXX mark last line terminal (so that it doesn't get justified).

  /* Write lines of paragraph to PDF, generating new pages as required.
     Here the challenge is knowing that the line will fit.
     We can fairly easily measure the height of the line itself to see that
     it will fit.  The trick is making sure that there is room for the line 
     plus any inseperable lines fit, too. Also, there has to be room for the
     marginal cross-references, and also the footnotes at the bottom.

     The footnotes are merged in a single footnote paragraph, which makes
     determining the space for them dependent on what has already been
     rendered on this page.

     The marginal notes should appear next to the corresponding verses,
     unless some verses have too many, in which case we have to do some
     vertical sliding to try to make them fit, if possible.

     For now, we will ignore all of that, and just emit the lines if there is
     physical space for the line.  This requires only pre-processing each line
     to determine the maximum extents of that line.
  */  
  int i;
  for(i=0;i<paragraph_line_count;i++) line_calculate_height(paragraph_lines[i]);

  for(i=0;i<paragraph_line_count;i++) line_emit(paragraph_lines[i]);
  
  // Clear out old lines
  for(i=0;i<paragraph_line_count;i++) line_free(paragraph_lines[i]);
  paragraph_line_count=0;
  
  return 0;
}

/* To build a paragraph we need to build lines, and then to know 
   which lines are inseparable and those which can be separated, i.e.,
   what the separable units of the paragraph are.
   With this information the paragraph can be written by seeing if each
   separable unit in turn can fit on the current page. If not, the page
   gets flushed, and the process continues on the next (initially empty)
   page.

   At the next lower level we need to assemble lines from the incoming words.
   This works by computing the dimenstions of each word, and seeing if it
   can fit on the current line.  If so, then good, else the previous line is
   finalised (which may involve adjusting the horizontal spacing if centring
   or justification is applied.

   Line assembly has a few corner cases to handle.  First, some smallcaps
   output is emulated using the capital characters of a regular font, in
   which case words may need to be split into two or more pieces, with no
   space in between.  Similarly foot notes and verse numbers are pieces
   which must be able to be placed without any space before or after them
   depending on the context.

*/
int paragraph_append_line(struct line_pieces *line)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  if (paragraph_line_count>=MAX_LINES_IN_PARAGRAPH) {
    fprintf(stderr,"Too many lines in paragraph.\n");
    exit(-1);
  }

  paragraph_lines[paragraph_line_count++]=current_line;
  current_line=NULL;
  return 0;
}

// Setup a line 
int paragraph_setup_next_line()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // Allocate structure
  current_line=calloc(sizeof(struct line_pieces),1); 

  // Set maximum line width
  current_line->max_line_width=page_width-left_margin-right_margin;

  return 0;
}

int paragraph_append_characters(char *text,int size,int baseline)
{
  fprintf(stderr,"%s(\"%s\",%d): STUB\n",__FUNCTION__,text,size);

  if (!current_line) paragraph_setup_next_line();

  // Make sure the line has enough space
  if (current_line->piece_count>=MAX_LINE_PIECES) {
    fprintf(stderr,"Cannot add '%s' to line, as line is too long.\n",text);
    exit(-1);
  }

  // Get width of piece
  float text_width, text_height;
  
  HPDF_Page_SetFontAndSize (page, current_font, size);
  text_width = HPDF_Page_TextWidth(page,text);
  text_height = HPDF_Font_GetCapHeight(current_font) * size/1000;

  current_line->pieces[current_line->piece_count]=strdup(text);
  current_line->fonts[current_line->piece_count]=current_font;
  current_line->fontsizes[current_line->piece_count]=size;
  current_line->piece_widths[current_line->piece_count]=text_width;
  if (strcmp(text," "))
    current_line->piece_is_elastic[current_line->piece_count]=0;
  else
    current_line->piece_is_elastic[current_line->piece_count]=1;
  current_line->piece_baseline[current_line->piece_count]=baseline;  
  current_line->line_width_so_far+=text_width;
  current_line->piece_count++;

  if (current_line->line_width_so_far>current_line->max_line_width) {
    fprintf(stderr,"Breaking line at %d points wide.\n",
	    current_line->line_width_so_far);
    // Line is too long.
    if (current_line->checkpoint) {
      // Rewind to checkpoint, add this line
      // to the current paragraph.  Then allocate a new line with just
      // the recently added stuff.
      int saved_piece_count=current_line->piece_count;
      int saved_checkpoint=current_line->checkpoint;
      current_line->piece_count=current_line->checkpoint;
      struct line_pieces *last_line=current_line;
      paragraph_append_line(current_line);
      paragraph_setup_next_line();
      // Now populate new line with the left overs from the old line
      int i;
      for(i=saved_checkpoint;i<saved_piece_count;i++)
	{
	  current_line->pieces[current_line->piece_count]=last_line->pieces[i];
	  current_line->fonts[current_line->piece_count]=last_line->fonts[i];
	  current_line->fontsizes[current_line->piece_count]=last_line->fontsizes[i];
	  current_line->piece_widths[current_line->piece_count]
	    =last_line->piece_widths[i];
	  current_line->piece_is_elastic[current_line->piece_count]
	    =last_line->piece_is_elastic[i];
	  current_line->piece_baseline[current_line->piece_count]
	    =last_line->piece_baseline[i];
	  current_line->line_width_so_far+=last_line->piece_widths[i];
	  current_line->piece_count++;	  
	}
    } else {
      // Line too long, but no checkpoint.  This is bad.
      // Just add this line as is to the paragraph and report a warning.
      fprintf(stderr,"Line too wide when appending '%s'\n",text);
      paragraph_append_line(current_line);
      current_line=NULL;
      paragraph_setup_next_line();
    }
  }
  
  return 0;
}

int paragraph_append_text(char *text,int baseline)
{  
  fprintf(stderr,"%s(\"%s\"): STUB\n",__FUNCTION__,text);

  // Keep track of whether the last character is a full stop so that we can
  // apply double spacing between sentences (if desired).
  if (text[strlen(text)-1]=='.') last_char_is_a_full_stop=1;
  else last_char_is_a_full_stop=0;

  // Checkpoint where we are up to, in case we need to split the line
  if (current_line) current_line->checkpoint=current_line->piece_count;
  
  if (current_font_smallcaps) {
    // This font uses emulated small caps, so break the word down into
    // as many pieces as necessary.
    int i,j;
    char chars[strlen(text)+1];
    for(i=0;text[i];)
      {
	int islower=0;
	int count=0;
	if ((text[i]>='a')&&(text[i]<='z')) islower=1;
	for(j=i;text[i];j++)
	  {
	    int thisislower=0;
	    if ((text[j]>='a')&&(text[j]<='z')) thisislower=1;
	    if (thisislower!=islower)
	      {
		// case change
		chars[count]=0;
		if (islower)
		  paragraph_append_characters(chars,current_font_smallcaps,baseline);
		else
		  paragraph_append_characters(chars,current_font_size,baseline);
		i=j;
		count=0;
		break;
	      } else chars[count++]=toupper(text[j]);
	  }
	if (count) {
	  chars[count]=0;
	  if (islower)
	    paragraph_append_characters(chars,current_font_smallcaps,baseline);
	  else
	    paragraph_append_characters(chars,current_font_size,baseline);
	  break;
	}
      }
  } else {
    // Regular text. Render as one piece.
    paragraph_append_characters(text,current_font_size,baseline);
  }
    
  return 0;
}

/* Add a space to a paragraph.  Similar to appending text, but adds elastic
   space that can be expanded if required for justified text.
*/
int paragraph_append_space()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  if (last_char_is_a_full_stop) fprintf(stderr,"  space follows a full-stop.\n");
  paragraph_append_characters(" ",current_font_size,0);
  return 0;
}

#define AL_CENTRED 0
#define AL_LEFT -1
#define AL_RIGHT 1
#define AL_JUSTIFIED -2
int paragraph_push_style(int font_alignment,
			 HPDF_Font font,
			 int font_size,
			 int font_smallcaps)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  current_font=font;
  current_font_size=font_size;
  current_font_smallcaps=font_smallcaps;
  
  return 0;
}

int paragraph_pop_style()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  return 0;
}

int paragraph_clear_style_stack()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  current_font = blackletter_font;
  current_font_size = blackletter_fontsize;
  current_font_smallcaps = 0;
  return 0;
}

int render_tokens()
{
  int i,j;

  paragraph_clear_style_stack();
  
  for(i=0;i<token_count;i++)
    {
      switch(token_types[i]) {
      case TT_PARAGRAPH:
	paragraph_flush();
	break;
      case TT_SPACE:
	paragraph_append_space();
	break;
      case TT_TAG:
	if (token_strings[i]) {
	  if (!strcasecmp(token_strings[i],"bookheader")) {
	    // Set booktab text to upper case version of this tag and
	    // begin a new page
	    paragraph_flush();
	    paragraph_clear_style_stack();
	    if (booktab_text) free(booktab_text); booktab_text=NULL;
	    // If we are on a left page, add a blank right page so that
	    // the book starts on a left page
	    if (leftRight==LR_LEFT) new_empty_page(LR_RIGHT);
	    leftRight=LR_LEFT;
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    booktab_text=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    for(j=0;booktab_text[j];j++) booktab_text[j]=toupper(booktab_text[j]);
	    // Start new empty page
	    new_empty_page(leftRight);
	  } else if (!strcasecmp(token_strings[i],"labelbook")) {
	    // Remember short name of book for inserting entries from the
	    // cross-reference database.
	    if (short_book_name) free(short_book_name); short_book_name=NULL;
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    short_book_name=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else if (!strcasecmp(token_strings[i],"bookpretitle")) {
	    // Book title header line
	    paragraph_push_style(AL_CENTRED,
				 bookpretitle_font,
				 bookpretitle_fontsize,
				 bookpretitle_smallcaps);
	    
	  } else {
	    
	    fprintf(stderr,"Warning: unknown tag \%s\n",token_strings[i]);
	  }	  
	}
	break;
      case TT_TEXT:
	// Append to paragraph
	// XXX - Adjust baseline for verse numbers, footnote references and
	// large chapter numbers.
	paragraph_append_text(token_strings[i],0);
	break;
      case TT_ENDTAG:
	paragraph_pop_style();
	break;
      }
    }

  // Flush out any queued content.
  if (current_line&&current_line->piece_count) paragraph_append_line(current_line);
  paragraph_flush();

  
  return 0;
}

int main(int argc,char **argv)
{
  if (argc==2) 
    read_profile(argv[1]);
  else
    {
      fprintf(stderr,"usage: generate <profile>\n");
      exit(-1);
    }

  // Create empty PDF
  pdf = HPDF_New(error_handler,NULL);
  if (!pdf) {
    fprintf(stderr,"Call to HPDF_New() failed.\n"); exit(-1); 
  }
  HPDF_SetPageLayout(pdf,HPDF_PAGE_LAYOUT_TWO_COLUMN_LEFT);

  fprintf(stderr,"About to load fonts\n");
  // Load all the fonts we will need
  fprintf(stderr,"  Loading bookpretitle font from %s\n",bookpretitle_fontfile);
  bookpretitle_font=HPDF_GetFont(pdf,resolve_font(bookpretitle_fontfile),NULL);
  fprintf(stderr,"  Loading booktitle font from %s\n",booktitle_fontfile);
  booktitle_font=HPDF_GetFont(pdf,resolve_font(booktitle_fontfile),NULL);
  fprintf(stderr,"  Loading header font from %s\n",header_fontfile);
  header_font=HPDF_GetFont(pdf,resolve_font(header_fontfile),NULL);
  fprintf(stderr,"  Loading booktab font from %s\n",booktab_fontfile);
  booktab_font=HPDF_GetFont(pdf,resolve_font(booktab_fontfile),NULL);
  fprintf(stderr,"  Loading black-letter font from %s\n",blackletter_fontfile);
  blackletter_font=HPDF_GetFont(pdf,resolve_font(blackletter_fontfile),NULL);
  fprintf(stderr,"  Loading red-letter font from %s\n",redletter_fontfile);
  redletter_font=HPDF_GetFont(pdf,resolve_font(redletter_fontfile),NULL);
  fprintf(stderr,"Loaded fonts\n");
  
  // Start with a left page
  leftRight=LR_LEFT;

  tokenise_file("books/01_Genesis.tex");
  fprintf(stderr,"Parsed Genesis.tex\n");
  render_tokens();
  fprintf(stderr,"Rendered Genesis.tex\n");
  
  // Write PDF to disk
  HPDF_SaveToFile(pdf,output_file);
  
  return 0;
}
