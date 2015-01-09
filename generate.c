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
  {"divine","blackletter.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"bookpretitle","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"booktitle","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"header","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"pagenumber","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"passageheader","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"passageinfo","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"booktab","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"versenum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"chapternum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotemark","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotemarkinfootnote","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnote","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnotebib","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"footnoteversenum","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"crossref","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {"crossrefmarker","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
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
      hash_configline(line);
      
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

	    else if (!strcasecmp(key,"recording_filename")) recording_filename=strdup(value);
	    else if (!strcasecmp(key,"recording_pagenumber")) page_to_record=atoi(value);


	    else if (!strcasecmp(key,"output_file")) output_file=strdup(value);


	    else if (!strcasecmp(key,"heading_y")) heading_y=atoi(value);
	    else if (!strcasecmp(key,"pagenumber_y")) pagenumber_y=atoi(value);
	    
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
	    else if (!strcasecmp(key,"crossref_column_width"))
	      crossref_column_width=atoi(value);
	    // Margin between marginpar and edge of page
	    else if (!strcasecmp(key,"crossref_margin_width"))
	      crossref_margin_width=atoi(value);

	    // Footnote horizontal rule settings
	    else if (!strcasecmp(key,"footnoterule_width")) footnote_rule_width=atof(value);
	    else if (!strcasecmp(key,"footnoterule_length")) footnote_rule_length=atoi(value);
	    else if (!strcasecmp(key,"footnoterule_ydelta")) footnote_rule_ydelta=atoi(value);

	    // Cross-reference settings
	    else if (!strcasecmp(key,"crossref_min_vspace")) crossref_min_vspace=atoi(value);
	    else if (!strcasecmp(key,"crossref_column_width")) crossref_column_width=atoi(value);

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

int leftRight=LR_NEITHER;

// Current booktab text
char *booktab_text=NULL;
// And position
int booktab_y=0;

// Current vertical position on the page
float page_y=0;

// Short name of book used for finding cross-references
char *short_book_name=NULL;
char *long_book_name=NULL;
// For page headers and cross-references
int chapter_label=0;
int verse_label=0;
int next_token_is_verse_number=0;

int on_first_page=0;

int suppress_page_header=0;
int page_leftRight;

int heading_y=400;
int pagenumber_y=20;

int first_verse_on_page=0;
int first_chapter_on_page=0;
int last_verse_on_page=0;
int last_chapter_on_page=0;

int headerfont_index=-1;
int pagenumberfont_index=-1;
int booktabfont_index=-1;
int bookpretitlefont_index=-1;
int booktitlefont_index=-1;
int passageheaderfont_index=-1;
int passageinfofont_index=-1;
int chapternumfont_index=-1;
int versenumfont_index=-1;
int footnoteversenumfont_index=-1;
int footnotefont_index=-1;
int footnotebibfont_index=-1;
int footnotemarkfont_index=-1;
int divinefont_index=-1;
int redletterfont_index=-1;
int blackletterfont_index=-1;

int finalise_page()
{
  char heading[1024];
  int y,x;
  
  if (!suppress_page_header) {
    // Draw page header.
    // Largely this is book chap:verse on the same side as the booktab

    if (headerfont_index==-1) headerfont_index=set_font_by_name("header");
    set_font(&type_faces[headerfont_index]);

    y=heading_y;

    if (page_leftRight==LR_LEFT) {
      if (first_chapter_on_page)
	sprintf(heading,"%s %d:%d",
		long_book_name,first_chapter_on_page,first_verse_on_page);
      else
	// Largely for Jude which has only one chapter
	sprintf(heading,"%s %d",long_book_name,first_verse_on_page);
      x=left_margin;
    } else {
      if (last_chapter_on_page)
	sprintf(heading,"%s %d:%d",
		long_book_name,last_chapter_on_page,last_verse_on_page);
      else
	// Largely for Jude which has only one chapter
	sprintf(heading,"%s %d",long_book_name,last_verse_on_page);
      int text_width = HPDF_Page_TextWidth(page,heading);
      x=page_width-right_margin-text_width;
    }
    
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
    HPDF_Page_SetRGBFill (page, 0.00, 0.00, 0.00);
    record_text(&type_faces[headerfont_index],
		type_faces[headerfont_index].font_size,
		heading,x,y,0);
    
    HPDF_Page_TextOut (page, x, y, heading);
    HPDF_Page_EndText (page);

  }
  
  first_chapter_on_page=last_chapter_on_page;
  first_verse_on_page=last_verse_on_page;

  // Draw page number on page
  HPDF_Page_BeginText (page);
  HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
  HPDF_Page_SetRGBFill (page, 0.00, 0.00, 0.00);
  if (pagenumberfont_index==-1) pagenumberfont_index=set_font_by_name("pagenumber");
  int index=pagenumberfont_index;
  y=pagenumber_y;
  char pagenumberstring[1024];
  snprintf(pagenumberstring,1024,"%d",current_page);
  set_font(&type_faces[pagenumberfont_index]);
  float text_width = HPDF_Page_TextWidth(page,pagenumberstring);
  x=(page_width/2)-(text_width/2);
  record_text(&type_faces[index],type_faces[index].font_size,heading,x,y,0);  
  HPDF_Page_TextOut (page, x, y, pagenumberstring);
  HPDF_Page_EndText (page);
  
  return 0;
}

// Create a new empty page
// Empty of main content, that is, a booktab will be added
int new_empty_page(int leftRight, int noHeading)
{
  // fprintf(stderr,"%s(%d,%d)\n",__FUNCTION__,leftRight,noHeading);

  //  if (!on_first_page) finalise_page();

  page_leftRight=leftRight;
  
  if (on_first_page) on_first_page=0; else {
    // Create the page
    page = HPDF_AddPage(pdf);
    record_newpage();
    
    // Set its dimensions
    HPDF_Page_SetWidth(page,page_width);
    HPDF_Page_SetHeight(page,page_height);
  }

  // XXX Draw booktab
  if (booktab_text) {

    // Draw booktab box
    int x=0;
    if (leftRight==LR_RIGHT) x = page_width-booktab_width+1;
    HPDF_Page_SetRGBFill (page, 0.25, 0.25, 0.25);
    HPDF_Page_Rectangle(page, x, page_height-booktab_y-booktab_height+1,
			booktab_width, booktab_height);
    HPDF_Page_Fill(page);
    record_fillcolour(0.25,0.25,0.25);
    record_rectangle(x, page_height-booktab_y-booktab_height+1,
		     booktab_width, booktab_height);

    // Now draw sideways text
    float text_width, text_height;

    if (booktabfont_index==-1) booktabfont_index=set_font_by_name("booktab");
    else set_font(&type_faces[booktabfont_index]);
    int index = booktabfont_index;
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
    record_fillcolour(1.00,1.00,1.00);
    HPDF_Page_SetTextMatrix (page,
			     cos(radians), sin(radians),
			     -sin(radians), cos(radians),
                x, y);
    record_text(&type_faces[index],type_faces[index].font_size,
		booktab_text,x,y,radians);
    HPDF_Page_ShowText (page, booktab_text);
    HPDF_Page_EndText (page);
    record_text_end();

  }

  page_y=top_margin;
  suppress_page_header=noHeading;
  
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

int set_font(struct type_face *f)
{
  HPDF_Page_SetFontAndSize (page, f->font, f->font_size);
  return 0;
}

int set_font_by_name(char *nickname) {
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
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (para->current_line) {
    line_remove_trailing_space(para->current_line);

    if (para->current_line->piece_count||para->current_line->line_height) {
      if (0) fprintf(stderr,"%d pieces in line uid #%d, height=%.1fpts.\n",
		     para->current_line->piece_count,
		     para->current_line->line_uid,
		     para->current_line->line_height);
      paragraph_append_current_line(para);
      paragraph_setup_next_line(para);
    } else {
      // Current line is empty and does not dictate a height.
      if (0) fprintf(stderr,"discarding line uid #%d (no pieces, zero height)\n",
		     para->current_line->line_uid);
      line_free(para->current_line);
      para->current_line=NULL;
      paragraph_setup_next_line(para);
    }
  }
 
  //  paragraph_dump(para);
  return 0;
}

int dropchar_margin_check(struct paragraph *p,struct line_pieces *l)
{
  // If the current font has drop chars (i.e, line_count > 1),
  // then set the dropchar margin line count to (line_count-1),
  // and set drop_char_left_margin to the width accumulated on the
  // line so far, plus enough space for left hanging punctation.
  
  int max_hang_space
    =right_margin
    -crossref_margin_width-crossref_column_width
    -2;  // plus a little space to ensure some white space
  
  if ((current_font->line_count-1)>p->drop_char_margin_line_count) {
    fprintf(stderr,"Font '%s' spans %d lines -- activating dropchar margin\n",
	    current_font->font_nickname,current_font->line_count);
    p->drop_char_margin_line_count=current_font->line_count-1;
    if (l->line_width_so_far+max_hang_space>p->drop_char_left_margin)
      p->drop_char_left_margin=l->line_width_so_far+max_hang_space;
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

  if (long_book_name) free(long_book_name);
  long_book_name=strdup(text);

  if (booktab_text) free(booktab_text);
  booktab_text=strdup(text);
  for(j=0;booktab_text[j];j++) booktab_text[j]=toupper(booktab_text[j]);
  return 0;
}

int render_tokens(int token_low,int token_high,int drawingPage)
{
  int i;

  if (token_low<0) token_low=0;
  if (token_high>token_count) token_high=token_count;
  
  footnotes_reset();

  if (!token_low) paragraph_clear_style_stack();
  
  // Initialise all paragraph structures.
  paragraph_init(&body_paragraph);
  for(i=0;i<MAX_VERSES_ON_PAGE;i++) paragraph_init(&cross_reference_paragraphs[i]);
  
  for(i=token_low;i<token_high;i++)
  //  for(i=0;i<50;i++)
    {
      switch(token_types[i]) {
      case TT_PARAGRAPH:
	{
	  // fprintf(stderr,"TT_PARAGRAPH\n");
	  int break_paragraph=1;
	  if (target_paragraph->current_line) {
	    if (target_paragraph->current_line->tied_to_next_line)
	      break_paragraph=0;
	  } else if (target_paragraph->line_count) {
	    if (target_paragraph->paragraph_lines[target_paragraph->line_count-1]
		->tied_to_next_line)
	      break_paragraph=0;
	  }
	  if (break_paragraph) {
	    // Flush the previous paragraph.
	    // fprintf(stderr,"Breaking paragraph.\n");
	    paragraph_flush(target_paragraph,drawingPage);
	    // Indent the paragraph.
	  }
	  paragraph_setup_next_line(target_paragraph);
	  body_paragraph.current_line->left_margin=paragraph_indent;
	  body_paragraph.current_line->max_line_width-=paragraph_indent;
	}
	break;
      case TT_SPACE:
	paragraph_append_space(target_paragraph,
			       NO_FORCESPACEATSTARTOFLINE,
			       NO_NOTBREAKABLE,i);
	break;
      case TT_NONBREAKINGSPACE:
	// fprintf(stderr,"Saw nonbreakingspace\n");
	paragraph_append_space(target_paragraph,
			       FORCESPACEATSTARTOFLINE,NOTBREAKABLE,i);
	break;
      case TT_THINSPACE:
	paragraph_append_thinspace(target_paragraph,
				   NO_FORCESPACEATSTARTOFLINE,
				   NO_NOTBREAKABLE,i);
	break;
      case TT_TAG:
	if (token_strings[i]) {
	  if (!strcasecmp(token_strings[i],"crossref")) {
	    // Crossreferences look like:
	    // \crossref{book}{chapter}{verse}{cross-references}
	    // We need to parse out a TT_TAG, TT_TEXT, TT_ENDTAG, TT_TAG,
	    // TT_TEXT, TT_ENDTAG
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
	    // Begin crossreference, unless the crossreference entry is empty.
	    if (token_types[i+1]!=TT_ENDTAG)
	      crossreference_start(i);
	    else {
	      i++;
	    }	    
	  } else if (!strcasecmp(token_strings[i],"bookheader")) {
	    // Set booktab text to upper case version of this tag and
	    // begin a new page.  We shouldn't have to do this in the middle of a
	    // page.  Instead, just ascribe a really big penalty to it.
	    page_penalty_if_not_start_of_page();
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    set_booktab_text(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
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
	    if (bookpretitlefont_index==-1)
	      bookpretitlefont_index=set_font_by_name("bookpretitle");
	    paragraph_push_style(target_paragraph,AL_CENTRED,
				 bookpretitlefont_index);
	    
	  } else if (!strcasecmp(token_strings[i],"booktitle")) {
	    // Book title header line
	    chapter_label=0; verse_label=0;
	    current_line_flush(target_paragraph);
	    if (booktitlefont_index==-1)
	      booktitlefont_index=set_font_by_name("booktitle");
	    paragraph_push_style(target_paragraph,AL_CENTRED,
				 booktitlefont_index);	    
	  } else if (!strcasecmp(token_strings[i],"passage")) {
	    // Passage header line
	    if (passageheaderfont_index==-1)
	      passageheaderfont_index=set_font_by_name("passageheader");
	    int index=passageheaderfont_index;
	    paragraph_flush(target_paragraph,drawingPage);
	    paragraph_insert_vspace(target_paragraph,passageheader_vspace,1);
	    paragraph_push_style(target_paragraph,AL_LEFT,index);
	    // Require at least one more line after this before page breaking
	    paragraph_set_widow_counter(target_paragraph,1);
	  } else if (!strcasecmp(token_strings[i],"passageinfo")) {
	    // Passage info line
	    if (passageinfofont_index==-1)
	      passageinfofont_index=set_font_by_name("passageinfo");
	    paragraph_insert_vspace(target_paragraph,passageheader_vspace,1);
	    paragraph_push_style(target_paragraph,AL_LEFT,passageinfofont_index);
	    // Require at least one more line after this before page breaking
	    paragraph_set_widow_counter(target_paragraph,1);
	  } else if (!strcasecmp(token_strings[i],"chapt")) {
	    // Chapter big number (dropchar)
	    verse_label=0;
	    current_line_flush(target_paragraph);
	    if (chapternumfont_index==-1)
	      chapternumfont_index=set_font_by_name("chapternum");
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,
				 chapternumfont_index);
	    // fprintf(stderr,"Before setting widow counter\n");
	    // paragraph_dump(target_paragraph);
	    // Require sufficient lines after this one so that the
	    // drop character can fit.
	    if (type_faces[chapternumfont_index].line_count>1) {
	      // fprintf(stderr,"Tying line due to drop-char (cl uid #%d)\n",
	      //      target_paragraph->current_line->line_uid);
	      paragraph_set_widow_counter(target_paragraph,
					  type_faces[chapternumfont_index].line_count
					  -1);
	      // paragraph_dump(target_paragraph);
	    }
	    // Don't indent lines beginning with dropchars
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->left_margin=0;
	  } else if (!strcasecmp(token_strings[i],"v")) {
	    // Verse number	    
	    if (!footnote_mode) {
	      // XXX mark line as touching this verse for building cross-reference
	      // data.
	      next_token_is_verse_number=1;
	      int alignment=AL_JUSTIFIED;
	      if (target_paragraph->poem_level) alignment=AL_LEFT;
	      if (versenumfont_index==-1)
		versenumfont_index=set_font_by_name("versenum");
	      paragraph_push_style(target_paragraph,alignment,versenumfont_index);
	    } else {
	      if (footnoteversenumfont_index==-1)
		footnoteversenumfont_index=set_font_by_name("footnoteversenum");
	      paragraph_push_style(target_paragraph,AL_JUSTIFIED,
				   footnoteversenumfont_index);
	    }

	  } else if (!strcasecmp(token_strings[i],"ldots")) {
	    // Insert an ellipsis
	    paragraph_push_style(target_paragraph,
				 target_paragraph->current_line->alignment,
				 set_font(current_font));
	    paragraph_append_text(target_paragraph,unicodeToUTF8(0x2026),0,
				  NO_FORCESPACEATSTARTOFLINE,NO_NOTBREAKABLE,i);
	  } else if (!strcasecmp(token_strings[i],"divine")) {
	    if (divinefont_index==-1)
	      divinefont_index=set_font_by_name("divine");
	    paragraph_push_style(target_paragraph,
				 target_paragraph->current_line->alignment,
				 divinefont_index);
	  } else if (!strcasecmp(token_strings[i],"red")) {
	    if (redletterfont_index==-1)
	      redletterfont_index=set_font_by_name("redletter");
	    paragraph_push_style(target_paragraph,
				 target_paragraph->current_line->alignment,
				 redletterfont_index);
	  } else if (!strcasecmp(token_strings[i],"fbackref")) {
	    if (footnotefont_index==-1)
	      footnotefont_index=set_font_by_name("footnote");
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,
				 footnotefont_index);
	  } else if (!strcasecmp(token_strings[i],"fbib")) {
	    if (footnotebibfont_index==-1)
	      footnotebibfont_index=set_font_by_name("footnotebib");
	    paragraph_push_style(target_paragraph,AL_JUSTIFIED,
				 footnotebibfont_index);
	  } else if (!strcasecmp(token_strings[i],"fnote")) {
	    // Foot note.
	    // 1. Insert a footnote mark here.
	    // 2. Redirect contents of tag to footnote block
	    // The major complication is that we don't know at this time what the
	    // foot note mark will be, because page breaks are worked out later by
	    // the page optimiser.  For now, we will use the widest double-character
	    // footnote mark, so that there will always be enough space.
	    // The trade-off is that some lines may end up being under-full when
	    // actually rendered, which may result in paragraph layouts differing
	    // during rednering compared with the costs actually calculated.  This
	    // could result in a paragraph taking one less line, or the last line of
	    // a paragraph being shorter than it otherwise would have been.  Neither
	    // are ideal, but the problem is difficult to work around otherwise.

	    // Draw the mark.
	    char *mark="mm";
	    int alignment=AL_NONE;
	    if (target_paragraph->current_line)
	      alignment=target_paragraph->current_line->alignment;
	    if (footnotemarkfont_index==-1)
	      footnotemarkfont_index=set_font_by_name("footnotemark");
	    // Write footnote mark
	    paragraph_push_style(target_paragraph,alignment,
				 footnotemarkfont_index);
	    paragraph_append_text(target_paragraph,mark,current_font->baseline_delta,
				  NO_FORCESPACEATSTARTOFLINE,NO_NOTBREAKABLE,i);
	    paragraph_pop_style(target_paragraph);

	    // Select footnote font
	    if (footnotefont_index==-1)
	      footnotefont_index=set_font_by_name("footnote");
	    paragraph_push_style(target_paragraph,alignment,footnotefont_index);

	    // XXX Redirect the foot note text itself to the footnote accumulator.
	    footnote_stack_depth=type_face_stack_pointer;
	    begin_footnote(i);
	    
	    // Poem line indenting. Set default left margin for lines.
	  } else if (!strcasecmp(token_strings[i],"poeml")) {
	    target_paragraph->poem_level=1; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->alignment=AL_LEFT;
	  } else if (!strcasecmp(token_strings[i],"poemll")) {
	    target_paragraph->poem_level=2; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->alignment=AL_LEFT;
	  } else if (!strcasecmp(token_strings[i],"poemlll")) {
	    target_paragraph->poem_level=3; target_paragraph->poem_subsequent_line=0;
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->alignment=AL_LEFT;
	  } else if (!strcasecmp(token_strings[i],"end")) {
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    if (!strcasecmp(token_strings[i],"poetry")) {
	      target_paragraph->poem_level=0;
	      paragraph_insert_vspace(target_paragraph,poetry_vspace,0);
	      // Revert to justified text after poetry
	      target_paragraph->paragraph_lines[target_paragraph->line_count-1]
		->alignment=AL_JUSTIFIED;
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
	      paragraph_insert_vspace(target_paragraph,poetry_vspace,0);
	    } else {
	      fprintf(stderr,"Warning: I don't know about \%s{%s}\n",
		      token_strings[i-1],token_strings[i]);	      
	    }
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else {	    
	    fprintf(stderr,"Warning: unknown tag \"\\%s\" (%d styles on the stack.)\n",
		    token_strings[i],type_face_stack_pointer);
	    int j=i-10;
	    fprintf(stderr,"  Context: ");
	    if (j<0) j=0;
	    for(;(j<i+10)&&(j<token_count);j++) {
	      char *s=token_strings[j]?token_strings[j]:"";
	      switch(token_types[j]) {
	      case TT_TEXT: fprintf(stderr,"%s",s); break;
	      case TT_TAG: fprintf(stderr,"\\%s",s); break;
	      case TT_SPACE: fprintf(stderr," "); break;
	      case TT_NONBREAKINGSPACE: fprintf(stderr," "); break;
	      case TT_PARAGRAPH: fprintf(stderr,"\n\n    "); break;
	      }
	    }
	    fprintf(stderr,"\n");
	    if (blackletterfont_index==-1)
	      blackletterfont_index=set_font_by_name("blackletter");
	    paragraph_push_style(target_paragraph,AL_LEFT,blackletterfont_index);
	  }	  
	}
	break;
      case TT_TEXT:
	// Append to paragraph
	if (!strcmp(token_strings[i],"\r")) {
	  current_line_flush(target_paragraph);
	} else {
	  int nobreak=NO_NOTBREAKABLE;
	  if (!strcmp(current_font->font_nickname,"versenum")) nobreak=NOTBREAKABLE;
	  paragraph_append_text(target_paragraph,token_strings[i],0,
				NO_FORCESPACEATSTARTOFLINE,nobreak,i);
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
  last_paragraph_report_time=0;
  if (target_paragraph->current_line&&target_paragraph->current_line->piece_count)
    paragraph_append_current_line(target_paragraph);
  paragraph_flush(target_paragraph,drawingPage);
  fprintf(stderr,"\n");
  
  return 0;
}

int main(int argc,char **argv)
{
  if ((argc<2)||(strcasecmp(argv[1],"typeset")&&strcasecmp(argv[1],"test")))
    {
      fprintf(stderr,"usage: generate typeset <profile> [<file.tex> ...>\n");
      fprintf(stderr,"    or generate test <testdir>\n");
      exit(-1);
    }

  if (!strcasecmp(argv[1],"test")) return(run_tests(argv[2]));

  read_profile(argv[2]);
  hash_configend();

  setup_job();

  int i;
  for(i=3;i<argc;i++)
    {
      typeset_file(argv[i]);
    }

  finish_job();
}

int setup_job()
{
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
  
  HPDF_UseUTFEncodings(pdf); 
  HPDF_SetCurrentEncoder(pdf, "UTF-8"); 
  
  fprintf(stderr,"About to load fonts\n");
  // Load all the fonts we will need
  int i;
  for(i=0;type_faces[i].font_nickname;i++) {
    fprintf(stderr,"  Loading %s font from %s\n",
	    type_faces[i].font_nickname,type_faces[i].font_filename);
    type_faces[i].font
      =HPDF_GetFont(pdf,resolve_font(type_faces[i].font_filename),"UTF-8");
    type_faces[i].linegap=get_linegap(type_faces[i].font_filename,
				      type_faces[i].font_size);
    fprintf(stderr,"%s linegap is %dpt\n",type_faces[i].font_nickname,
	    type_faces[i].linegap);
  }
  fprintf(stderr,"Loaded fonts\n");
  
  // Start with a left page so that we don't insert a blank one
  leftRight=LR_LEFT;

  // Create first page so that text widths get calculated.
  page = HPDF_AddPage(pdf); 
  record_newpage();
  HPDF_Page_SetWidth(page,page_width);
  HPDF_Page_SetHeight(page,page_height);
  on_first_page=1;
  
  fprintf(stderr,"Loading cross-reference library.\n");
  crossref_hashtable_init();
  tokenise_file("crossrefs.tex",1);
  render_tokens(0,token_count,0);
  // Tidy up output after cross-reference generation reports.
  fprintf(stderr,"\n");
  clear_tokens();

  return 0;
}

int typeset_file(char *file)
{
  tokenise_file(file,0);
  fprintf(stderr,"Parsed %s\n",file);
  page_optimal_render_tokens();
  clear_tokens();
  fprintf(stderr,"Rendered %s\n",file);
  return 0;
}

int finish_job()
{
  // finalise_page();
  
  // Write PDF to disk
  HPDF_SaveToFile(pdf,output_file);
  HPDF_Free(pdf);
  pdf=NULL;
  
  return 0;
}
