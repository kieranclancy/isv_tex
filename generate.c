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
#include "ft2build.h"
#include FT_FREETYPE_H
#include "hpdf.h"
#include "generate.h"

int debug_vspace=0;    // set to 1 to show line placement
int debug_vspace_x=0;

int poetry_left_margin=35;
int poetry_level_indent=10;
int poetry_wrap_indent=30;
int poetry_vspace=5;

FT_Library  library;

struct type_face *current_font = NULL;

// List of typefaces we have available
struct type_face type_faces[] = {
  {"blackletter","blackletter.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"redletter","redletter.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"bookpretitle","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"booktitle","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"header","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"passageheader","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"booktab","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"versenum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"chapternum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotemark","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotemarkinfootnote","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnote","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotebib","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnoteversenum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"crossref","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {NULL,NULL,0,0,0,0,0.00,0.00,0.00,NULL,0}
};

struct paragraph body_paragraph;

struct paragraph cross_reference_paragraphs[MAX_VERSES_ON_PAGE];

struct paragraph *target_paragraph=&body_paragraph;


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
int paragraph_indent=18;
int passageheader_vspace=4;

int footnote_sep_vspace=10;

float line_spacing=1.2;

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
	    int i;
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

	    else if (!strcasecmp(key,"paragraph_indent")) paragraph_indent=atoi(value);
	    else if (!strcasecmp(key,"line_spacing")) line_spacing=atof(value);
	    
	    // Width of marginpar for holding cross-references
	    else if (!strcasecmp(key,"marginpar_width")) marginpar_width=atoi(value);
	    // Margin between marginpar and edge of page
	    else if (!strcasecmp(key,"marginpar_margin")) marginpar_margin=atoi(value);

	    // Footnote horizontal rule settings
	    else if (!strcasecmp(key,"footnoterule_width")) footnote_rule_width=atof(value);
	    else if (!strcasecmp(key,"footnoterule_length")) footnote_rule_length=atoi(value);
	    else if (!strcasecmp(key,"footnoterule_ydelta")) footnote_rule_ydelta=atoi(value);


	    // Font selection
	    else if (!strcasecmp(&key[strlen(key)-strlen("_fontsize")],"_fontsize")) {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_fontsize")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_fontsize")))
		    { type_faces[i].font_size=atoi(value); break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_smallcaps")],"_smallcaps"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_smallcaps")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_smallcaps")))
		    { type_faces[i].smallcaps=atoi(value); break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_ydelta")],"_ydelta"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_ydelta")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_ydelta")))
		    { type_faces[i].baseline_delta=atoi(value); break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_linecount")],"_linecount"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_linecount")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_linecount")))
		    { type_faces[i].line_count=atoi(value); break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_red")],"_red"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_red")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_red")))
		    { type_faces[i].red=atoi(value)*1.0/255.0; break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown font in text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_green")],"_green"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_green")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_green")))
		    { type_faces[i].green=atoi(value)*1.0/255.0; break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown font in text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_blue")],"_blue"))
	      {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_blue")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_blue")))
		    { type_faces[i].blue=atoi(value)*1.0/255.0; break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown font in text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }
	    else if (!strcasecmp(&key[strlen(key)-strlen("_filename")],"_filename")) {
	      for(i=0;type_faces[i].font_nickname;i++)
		if (strlen(key)
		    ==strlen(type_faces[i].font_nickname)+strlen("_filename")) {
		  if (!strncasecmp(key,type_faces[i].font_nickname,
				   strlen(key)-strlen("_filename")))
		    { type_faces[i].font_filename=strdup(value); break; }
		}
	      if (!type_faces[i].font_nickname) {
		include_show_stack();
		fprintf(stderr,"%s:%d:Unknown text style in attribute '%s'\n",
			file,line_num,key);
		errors++;
	      }
	    }

	    // Size of solid colour book tabs
	    else if (!strcasecmp(key,"booktab_width")) booktab_width=atoi(value);
	    else if (!strcasecmp(key,"booktab_height")) booktab_height=atoi(value);
	    // Set vertical limit of where booktabs can be placed
	    else if (!strcasecmp(key,"booktab_upperlimit")) booktab_upperlimit=atoi(value);
	    else if (!strcasecmp(key,"booktab_lowerlimit")) booktab_lowerlimit=atoi(value);
	    else if (!strcasecmp(key,"passageheader_vspace")) passageheader_vspace=atoi(value);
	    else if (!strcasecmp(key,"poetry_left_margin")) poetry_left_margin=atoi(value);
	    else if (!strcasecmp(key,"poetry_level_indent")) poetry_level_indent=atoi(value);
	    else if (!strcasecmp(key,"poetry_wrap_indent")) poetry_wrap_indent=atoi(value);
	    else if (!strcasecmp(key,"poetry_vspace")) poetry_vspace=atoi(value);

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

// Are we drawing a left or right face, or neither
#define LR_LEFT -1
#define LR_RIGHT 1
#define LR_NEITHER 0
int leftRight=LR_NEITHER;

// Current booktab text
char *booktab_text=NULL;
// And position
int booktab_y=0;

// Current vertical position on the page
float page_y=0;

// Short name of book used for finding cross-references
char *short_book_name=NULL;
// For page headers and cross-references
int chapter_label=0;
int verse_label=0;
int next_token_is_verse_number=0;

// Create a new empty page
// Empty of main content, that is, a booktab will be added
int new_empty_page(int leftRight)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  // Create the page
  page = HPDF_AddPage(pdf);

  // Set its dimensions
  HPDF_Page_SetWidth(page,page_width);
  HPDF_Page_SetHeight(page,page_height);

  // XXX Draw booktab
  if (booktab_text) {

    // Draw booktab box
    int x=0;
    if (leftRight==LR_RIGHT) x = page_width-booktab_width+1;
    HPDF_Page_SetRGBFill (page, 0.25, 0.25, 0.25);
    HPDF_Page_Rectangle(page, x, page_height-booktab_y-booktab_height+1,
			booktab_width, booktab_height);
    HPDF_Page_Fill(page);

    // Now draw sideways text
    float text_width, text_height;

    int index = set_font("booktab");
    text_width = HPDF_Page_TextWidth(page,booktab_text);
    int ascender_height=HPDF_Font_GetAscent(type_faces[index].font)*type_faces[index].font_size/1000;
    // int descender_depth=HPDF_Font_GetDescent(type_faces[index].font)*type_faces[index].font_size/1000;
    text_height = ascender_height; // -descender_depth;
    
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

  page_y=top_margin;
  
  return 0;
}

#define MAX_FONTS 64
int font_count=0;
char *font_filenames[MAX_FONTS];
const char *font_names[MAX_FONTS];

int get_linegap(char *font_filename, int size)
{
  FT_Face face;
  if (FT_New_Face( library,font_filename,0,&face))
    {
      fprintf(stderr,"Could not open font '%s'\n",font_filename);
      exit(-1);
    }
  if (FT_Set_Char_Size(face,0,size*64,72,72))
    {
      fprintf(stderr,"Could not set font '%s' to size %d\n",font_filename,size);
      exit(-1);
    }
  if (!face->size) {
      fprintf(stderr,"Could not get size structure of font '%s' to size %d\n",font_filename,size);
      exit(-1);
  }
  int linegap=face->size->metrics.height/64;
  fprintf(stderr,"Line gap is %dpt for %dpt text\n",linegap,size);
  
  FT_Done_Face(face);
  return linegap;
}

int set_font(char *nickname) {
  int i;
  for(i=0;type_faces[i].font_nickname;i++)
    if (!strcasecmp(nickname,type_faces[i].font_nickname)) break;
  if (!type_faces[i].font_nickname) {
    fprintf(stderr,"Asked to select non-existent font '%s'\n",nickname);
    exit(-1);
  }
  HPDF_Page_SetFontAndSize (page, type_faces[i].font, type_faces[i].font_size);
  return i;
};


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

int current_line_flush(struct paragraph *para)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);


  if (para->current_line) {
    line_remove_trailing_space(para->current_line);

    if (para->current_line->piece_count||para->current_line->line_height) {
      fprintf(stderr,"%d pieces left in %p.\n",
	      para->current_line->piece_count,para->current_line);
      paragraph_append_line(para,para->current_line);
      paragraph_setup_next_line(para);
    }
  }
 
  para->current_line=NULL;
  return 0;
}

int output_accumulated_cross_references()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  return 0;
}

int dropchar_margin_check(struct paragraph *p,struct line_pieces *l)
{
  // If the current font has drop chars (i.e, line_count > 1),
  // then set the dropchar margin line count to (line_count-1),
  // and set drop_char_left_margin to the width accumulated on the
  // line so far.
  if ((current_font->line_count-1)>p->drop_char_margin_line_count) {
    fprintf(stderr,"Font '%s' spans %d lines -- activating dropchar margin\n",
	    current_font->font_nickname,current_font->line_count);
    p->drop_char_margin_line_count=current_font->line_count-1;
    if (l->line_width_so_far>p->drop_char_left_margin)
      p->drop_char_left_margin=l->line_width_so_far;
  }
  return 0;
}

int set_booktab_text(char *text)
{
  int j;
  // Work out vertical position of book tab
  if ((booktab_y<booktab_upperlimit)|| 
      (booktab_y+booktab_height*2>booktab_lowerlimit))
    booktab_y=booktab_upperlimit;
  else
    booktab_y=booktab_y+booktab_height;

  if (booktab_text) free(booktab_text);
  booktab_text=strdup(text);
  for(j=0;booktab_text[j];j++) booktab_text[j]=toupper(booktab_text[j]);
  return 0;
}

int render_tokens()
{
  int i;

  footnotes_reset();
  
  // Initialise all paragraph structures.
  paragraph_init(&body_paragraph);
  paragraph_init(&rendered_footnote_paragraph);
  for(i=0;i<MAX_FOOTNOTES_ON_PAGE;i++) paragraph_init(&footnote_paragraphs[i]);
  for(i=0;i<MAX_VERSES_ON_PAGE;i++) paragraph_init(&cross_reference_paragraphs[i]);
  
  paragraph_clear_style_stack();
  
  for(i=0;i<token_count;i++)
  //  for(i=0;i<50;i++)
    {
      switch(token_types[i]) {
      case TT_PARAGRAPH:
	// Flush the previous paragraph.
	paragraph_flush(target_paragraph);
	// Indent the paragraph.  
	paragraph_setup_next_line(target_paragraph);
	body_paragraph.current_line->left_margin=paragraph_indent;
	body_paragraph.current_line->max_line_width-=paragraph_indent;
	break;
      case TT_SPACE:
	paragraph_append_space(target_paragraph,0);
	break;
      case TT_THINSPACE:
	paragraph_append_thinspace(target_paragraph,0);
	break;
      case TT_TAG:
	if (token_strings[i]) {
	  if (!strcasecmp(token_strings[i],"crossref")) {
	    // Crossreferences look like:
	    // \crossref{book}{chapter}{verse}{cross-references}
	    // The token stream doesn't have the {'s, so we need to
	    // parse out a TT_TEXT, TT_ENDTAG, TT_TEXT, TT_ENDTAG
	    // and then the cross-reference text follows normally.
	    // cross-reference text is not permitted to have any tags inside
	    // to simplify the state control machinery
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    crossreference_book=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    i++; if (token_types[i]!=TT_TAG||token_strings[i]) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    crossreference_chapter=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    i++; if (token_types[i]!=TT_TAG||token_strings[i]) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    crossreference_verse=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    i++; if (token_types[i]!=TT_TAG||token_strings[i]) {
	      fprintf(stderr,"\\crossref must be followed by {book}{chapter}{verse}{cross-reference list} (step %d, saw token type %d)\n",__LINE__,token_types[i]); exit(-1); }
	    crossreference_start();
	  } else if (!strcasecmp(token_strings[i],"bookheader")) {
	    // Set booktab text to upper case version of this tag and
	    // begin a new page
	    paragraph_flush(target_paragraph);
	    output_accumulated_footnotes();
	    output_accumulated_cross_references();
	    footnotes_reset();
	    paragraph_clear_style_stack();
	    // If we are on a left page, add a blank right page so that
	    // the book starts on a left page
	    if (leftRight==LR_LEFT) {
	      fprintf(stderr,"Inserting blank page so that book starts on left.\n");
	      new_empty_page(LR_RIGHT);	      
	    }
	    leftRight=LR_LEFT;
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    set_booktab_text(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
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
	  } else if (!strcasecmp(token_strings[i],"labelchapt")) {
	    // Remember short name of book for inserting entries from the
	    // cross-reference database.
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    chapter_label=atoi(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else if (!strcasecmp(token_strings[i],"bookpretitle")) {
	    // Book title header line
	    paragraph_push_style(target_paragraph,AL_CENTRED,set_font("bookpretitle"));
	    
	  } else if (!strcasecmp(token_strings[i],"booktitle")) {
	    // Book title header line
	    chapter_label=0; verse_label=0;
	    current_line_flush(target_paragraph);
	    paragraph_push_style(target_paragraph,AL_CENTRED,set_font("booktitle"));
	    
	  } else if (!strcasecmp(token_strings[i],"passage")) {
	    // Passage header line	    
	    int index=set_font("passageheader");
	    paragraph_insert_vspace(target_paragraph,passageheader_vspace);
	    paragraph_push_style(target_paragraph,AL_LEFT,index);
	    // Require at least one more line after this before page breaking
	    paragraph_set_widow_counter(target_paragraph,1);
	  } else if (!strcasecmp(token_strings[i],"chapt")) {
	    // Chapter big number (dropchar)
	    verse_label=0;
	    current_line_flush(target_paragraph);
	    int index=set_font("chapternum");
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,index);
	    // Require sufficient lines after this one so that the
	    // drop character can fit.
	    if (type_faces[index].line_count>1)
	      paragraph_set_widow_counter(target_paragraph,type_faces[index].line_count-1);
	    // Don't indent lines beginning with dropchars
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->left_margin=0;
	  } else if (!strcasecmp(token_strings[i],"v")) {
	    // Verse number
	    if (!footnote_mode) {
	      // XXX mark line as touching this verse for building cross-reference
	      // data.
	      next_token_is_verse_number=1;
	      paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("versenum"));
	    } else
	      paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("footnoteversenum"));

	  } else if (!strcasecmp(token_strings[i],"fbackref")) {
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("footnote"));
	  } else if (!strcasecmp(token_strings[i],"fbib")) {
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("footnotebib"));
	  } else if (!strcasecmp(token_strings[i],"fnote")) {
	    // Foot note.
	    // 1. Insert a footnote mark here.
	    // 2. Redirect contents of tag to footnote block
	    // The major complication is that we don't know at this time what the
	    // foot note mark will be, because we may defer this line to the next
	    // page.  One solution to this is to just go through a-z continuously,
	    // and don't reset to a on each page. This is simple, and effective.

	    // Draw the mark.
	    char *mark=next_footnote_mark();
	    paragraph_push_style(target_paragraph,target_paragraph->current_line->alignment,set_font("footnotemark"));
	    fprintf(stderr,"Footnote mark is '%s'\n",mark);
	    paragraph_append_text(target_paragraph,mark,current_font->baseline_delta,0);
	    paragraph_pop_style(target_paragraph);

	    // Select footnote font
	    paragraph_push_style(target_paragraph,target_paragraph->current_line->alignment,set_font("footnote"));

	    // XXX Redirect the foot note text itself to the footnote accumulator.
	    footnote_stack_depth=type_face_stack_pointer;
	    begin_footnote();	    
	    
	    // Poem line indenting. Set default left margin for lines.
	  } else if (!strcasecmp(token_strings[i],"poeml")) {
	    target_paragraph->poem_level=1; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	  } else if (!strcasecmp(token_strings[i],"poemll")) {
	    target_paragraph->poem_level=2; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	  } else if (!strcasecmp(token_strings[i],"poemlll")) {
	    target_paragraph->poem_level=3; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	  } else if (!strcasecmp(token_strings[i],"end")) {
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    if (!strcasecmp(token_strings[i],"poetry")) {
	      target_paragraph->poem_level=0;
	      paragraph_insert_vspace(target_paragraph,poetry_vspace);
	    } else {
	      fprintf(stderr,"Warning: I don't know about \%s{%s}\n",
		      token_strings[i-1],token_strings[i]);	      
	    }
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else if (!strcasecmp(token_strings[i],"begin")) {
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    if (!strcasecmp(token_strings[i],"poetry")) {
	      target_paragraph->poem_level=0;
	      paragraph_insert_vspace(target_paragraph,poetry_vspace);
	    } else {
	      fprintf(stderr,"Warning: I don't know about \%s{%s}\n",
		      token_strings[i-1],token_strings[i]);	      
	    }
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else {	    
	    fprintf(stderr,"Warning: unknown tag \%s (%d styles on the stack.)\n",
		    token_strings[i],type_face_stack_pointer);
	    paragraph_push_style(target_paragraph,AL_LEFT,set_font("blackletter"));
	  }	  
	}
	break;
      case TT_TEXT:
	// Append to paragraph
	if (!strcmp(token_strings[i],"\r")) {
	  current_line_flush(target_paragraph);
	} else {
	  paragraph_append_text(target_paragraph,token_strings[i],0,0);
	  // Attach verse number to this line if necessary.
	  if (next_token_is_verse_number) {
	    next_token_is_verse_number=0;
	    verse_label=atoi(token_strings[i]);
	    if (target_paragraph==&body_paragraph)
	      crossreference_register_verse(&body_paragraph,
					    short_book_name,chapter_label,verse_label);
	  }

	}
	break;
      case TT_ENDTAG:
	if (crossreference_mode) {
	  crossreference_end();
	} else paragraph_pop_style(target_paragraph);
	break;
      }
    }

  // Flush out any queued content.
  if (target_paragraph->current_line&&target_paragraph->current_line->piece_count)
    paragraph_append_line(target_paragraph,target_paragraph->current_line);
  paragraph_flush(target_paragraph);

  
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

  int error = FT_Init_FreeType( &library );
  if ( error )
    {
      fprintf(stderr,"Could not initialise libfreetype\n");
      exit(-1);
    }
  
  // Create empty PDF
  pdf = HPDF_New(error_handler,NULL);
  if (!pdf) {
    fprintf(stderr,"Call to HPDF_New() failed.\n"); exit(-1); 
  }
  HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);
  HPDF_SetPageLayout(pdf,HPDF_PAGE_LAYOUT_TWO_COLUMN_LEFT);

  fprintf(stderr,"About to load fonts\n");
  // Load all the fonts we will need
  int i;
  for(i=0;type_faces[i].font_nickname;i++) {
    fprintf(stderr,"  Loading %s font from %s\n",
	    type_faces[i].font_nickname,type_faces[i].font_filename);
    type_faces[i].font
      =HPDF_GetFont(pdf,resolve_font(type_faces[i].font_filename),NULL);
    type_faces[i].linegap=get_linegap(type_faces[i].font_filename,
				      type_faces[i].font_size);
    fprintf(stderr,"%s linegap is %dpt\n",type_faces[i].font_nickname,
	    type_faces[i].linegap);
  }
  fprintf(stderr,"Loaded fonts\n");
  
  // Start with a right page so that we don't insert a blank one
  leftRight=LR_RIGHT;

  // Create first page so that text widths get calculated.
  page = HPDF_AddPage(pdf);
  
  fprintf(stderr,"Loading cross-reference library.\n");
  crossref_hashtable_init();
  tokenise_file("crossrefs.tex");
  render_tokens();
  clear_tokens();
  
  tokenise_file("books/01_Genesis.tex");
  fprintf(stderr,"Parsed Genesis.tex\n");
  render_tokens();
  clear_tokens();
  fprintf(stderr,"Rendered Genesis.tex\n");

  // Write PDF to disk
  HPDF_SaveToFile(pdf,output_file);
  
  return 0;
}
