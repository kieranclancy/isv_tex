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

int debug_vspace=1;
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
  {"footnote","font.ttf",12,0,0,1,0.00,0.00,0.00,NULL,0},
  {NULL,NULL,0,0,0,0,0.00,0.00,0.00,NULL,0}
};

struct paragraph body_paragraph;

struct paragraph rendered_footnote_paragraph;

#define MAX_FOOTNOTES_ON_PAGE 256
struct paragraph footnote_paragraphs[MAX_FOOTNOTES_ON_PAGE];
int footnote_line_numbers[MAX_FOOTNOTES_ON_PAGE];
#define MAX_VERSES_ON_PAGE 256
struct paragraph cross_reference_paragraphs[MAX_VERSES_ON_PAGE];

struct paragraph *target_paragraph=&body_paragraph;


int footnote_stack_depth=-1;
char footnote_mark_string[4]={'a'-1,0,0,0};
int footnote_count=0;

int footnotes_reset()
{
  int i;
  for(i=0;i<MAX_FOOTNOTES_ON_PAGE;i++) {
    paragraph_clear(&footnote_paragraphs[i]);
    footnote_line_numbers[i]=-1;
  }

  generate_footnote_mark(-1);

  footnote_stack_depth=-1;
  footnote_count=0;

  return 0;
}

int generate_footnote_mark(int n)
{
  if (n<27) footnote_mark_string[0]='a'+(n);
  else {
    n-=26;
    footnote_mark_string[0]='a'+(n/26);
    footnote_mark_string[1]='a'+(n%26);
    footnote_mark_string[2]=0;
  }
  return 0;
}

char *next_footnote_mark()
{
  generate_footnote_mark(footnote_count++);
  if(footnote_count>MAX_FOOTNOTES_ON_PAGE) {
    fprintf(stderr,"Too many footnotes on a single page (limit is %d)\n",
	    MAX_FOOTNOTES_ON_PAGE);
    exit(-1);
  }
  return footnote_mark_string;
}

int set_font(char *nickname);
int paragraph_append_line(struct paragraph *p,struct line_pieces *line);
int paragraph_setup_next_line(struct paragraph *p);


int begin_footnote()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // footnote_count has already been incremented, so take one away when working out
  // which paragraph to access.
  target_paragraph=&footnote_paragraphs[footnote_count-1];
  footnote_line_numbers[footnote_count-1]=body_paragraph.current_line->line_uid;
  fprintf(stderr,"Footnote '%s' is in line #%d (line uid %d). There are %d foot notes.\n",
	  footnote_mark_string,body_paragraph.line_count,
	  body_paragraph.current_line->line_uid,footnote_count);
  return 0;
}

int end_footnote()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  current_line_flush(target_paragraph);
  target_paragraph=&body_paragraph;
  return 0;
}

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

// For page headers
char *chapter_label=NULL;

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
    // Remove any trailing spaces from the line
    int i;
    for(i=para->current_line->piece_count-1;i>=0;i--) {
      fprintf(stderr,"Considering piece #%d/%d '%s'\n",i,
	      para->current_line->piece_count,
	      para->current_line->pieces[i]);
      if (!strcmp(" ",para->current_line->pieces[i])) {
	para->current_line->piece_count=i;
	para->current_line->line_width_so_far-=para->current_line->piece_widths[i];
	free(para->current_line->pieces[i]);
      } else break;      
    }
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

int output_accumulated_footnotes()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // Commit any partial last-line in the footnote paragraph.
  if (rendered_footnote_paragraph.current_line) {
    current_line_flush(&rendered_footnote_paragraph);
  }

  int saved_page_y=page_y;
  int saved_bottom_margin=bottom_margin;
  
  int footnotes_height=paragraph_height(&rendered_footnote_paragraph);

  int footnotes_y=page_height-bottom_margin-footnotes_height;

  page_y=footnotes_y;
  bottom_margin=0;
  
  paragraph_flush(&rendered_footnote_paragraph);
  // XXX Draw horizontal rule

  // Restore page settings
  page_y=saved_page_y;
  bottom_margin=saved_bottom_margin;

  // Clear footnote block after printing it
  paragraph_clear(&rendered_footnote_paragraph);
  
  return 0;
}

int output_accumulated_cross_references()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  return 0;
}

int reenumerate_footnotes(int first_remaining_line_uid)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // While there are footnotes we have already output, purge them.
  while((footnote_count>0)&&(footnote_line_numbers[0]<first_remaining_line_uid))
    {
      fprintf(stderr,"%d foot notes remaining: deleting %d (is < %d)\n",
	      footnote_count,
	      footnote_line_numbers[0],first_remaining_line_uid);
      paragraph_clear(&footnote_paragraphs[0]);
      int i;
      // Copy down foot notes
      for(i=0;i<footnote_count;i++) {
	footnote_line_numbers[i]=footnote_line_numbers[i+1];
	bcopy(&footnote_paragraphs[i],&footnote_paragraphs[i+1],
	      sizeof (struct paragraph));
      }
      footnote_count--;
    }

  // Now that we have only the relevant footnotes left, update the footnote marks
  // in the footnotes, and in the lines that reference them.
  // XXX - If the footnote mark becomes wider, it might stick out into the margin.
  int i;
  int footnote_number_in_line=0;
  int footnotemark_typeface_index=set_font("footnotemark");
  for(i=0;i<footnote_count;i++)
    {

      if (i) {
	if (footnote_line_numbers[i]==footnote_line_numbers[i-1])
	  footnote_number_in_line++;
	else
	  footnote_number_in_line=0;
      }

      generate_footnote_mark(i);
      struct paragraph *p=&body_paragraph;
      int j,k;
      for(j=0;j<p->line_count;j++)
	if (p->paragraph_lines[j]->line_uid==footnote_line_numbers[i])
	{	  
	  int position_in_line=-1;
	  for(k=0;k<p->paragraph_lines[j]->piece_count;k++)
	    {
	      if (p->paragraph_lines[j]->fonts[k]
		  ==&type_faces[footnotemark_typeface_index])
		{
		  position_in_line++;
		  if (position_in_line==footnote_number_in_line) {
		    // This is the piece
		    free(p->paragraph_lines[j]->pieces[k]);
		    p->paragraph_lines[j]->pieces[k]=strdup(footnote_mark_string);
		    fprintf(stderr,"  footnotemark #%d = '%s'\n",i,footnote_mark_string);
		  }
		}
	    }
	}
    }

  // Update footnote mark based on number of footnotes remaining
  generate_footnote_mark(footnote_count-1);

  return 0;
}


int line_emit(struct paragraph *p,int line_num)
{
  struct line_pieces *l=p->paragraph_lines[line_num];
  int break_page=0;
  
  // Does the line itself require more space than there is?
  float baseline_y=page_y+l->line_height*line_spacing;
  if (baseline_y>(page_height-bottom_margin)) break_page=1;

  // XXX Does the line plus footnotes require more space than there is?
  // XXX - clone footnote paragraph and then append footnotes referenced in this
  // line to the clone, then measure its height.
  // XXX - deduct footnote space from remaining space.
  if (p==&body_paragraph) {
    struct paragraph temp;
    paragraph_init(&temp);
    paragraph_clone(&temp,&rendered_footnote_paragraph);
    int i;
    for(i=0;i<footnote_count;i++)
      if (l->line_uid==footnote_line_numbers[i])
	paragraph_append(&temp,&footnote_paragraphs[i]);
    current_line_flush(&temp);
    int footnotes_height=paragraph_height(&temp);
    baseline_y+=footnotes_height;
    baseline_y+=footnote_sep_vspace;
    fprintf(stderr,"Footnote block is %dpts high (%d lines).\n",
	    footnotes_height,temp.line_count);
    if (baseline_y>(page_height-bottom_margin)) {
      fprintf(stderr,"Breaking page at %.1fpts to make room for footnotes block\n",
	      page_y);
      break_page=1;
    }
  }

  // XXX Does the line plus its cross-references require more space than there is?
  // XXX - add height of cross-references for any verses in this line to height of
  // all cross-references and make sure that it can fit above the cross-references.
  if (p==&body_paragraph) {
  }
  
  if (break_page) {
    // No room on this page. Start a new page.

    // The footnotes that have been collected so far need to be output, then
    // freed.  Then any remaining footnotes need to be shuffled up the list
    // and references to them re-enumerated.

    // Cross-references also need to be output.
    
    leftRight=-leftRight;

    // Reenumerate footnotes 

    if (p==&body_paragraph) {
      output_accumulated_footnotes();
      output_accumulated_cross_references();
      reenumerate_footnotes(p->paragraph_lines[line_num]->line_uid);
      new_empty_page(leftRight);
    }
    
    page_y=top_margin;
    baseline_y=page_y+l->line_height*line_spacing;
  }

  // Add footnotes to footnote paragraph
  if (p==&body_paragraph) {
    int i;
    for(i=0;i<footnote_count;i++)
      if (l->line_uid==footnote_line_numbers[i])
	paragraph_append(&rendered_footnote_paragraph,&footnote_paragraphs[i]);
  }
  
  // convert y to libharu coordinate system (y=0 is at the bottom,
  // and the y position is the base-line of the text to render).
  // Don't apply line_spacing to adjustment, so that extra line spacing
  // appears below the line, rather than above it.
  float y=(page_height-page_y)-l->line_height;

  int i;
  float linegap=0;

  // Now draw the pieces
  HPDF_Page_BeginText (page);
  HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
  float x=0;
  switch(l->alignment) {
  case AL_LEFT: case AL_JUSTIFIED: case AL_NONE:
    // Finally apply any left margin that has been set
    x+=l->left_margin;
    if (l->left_margin) {
      fprintf(stderr,"Applying left margin of %dpts\n",l->left_margin);
    }
    break;
  case AL_CENTRED:
    x=(l->max_line_width-l->line_width_so_far)/2;
    fprintf(stderr,"x=%.1f (centre alignment, w=%.1fpt, max w=%d)\n",
	    x,l->line_width_so_far,l->max_line_width);
    break;
  case AL_RIGHT:
    x=l->max_line_width-l->line_width_so_far;
    fprintf(stderr,"x=%.1f (right alignment)\n",x);
    break;
  default:
    fprintf(stderr,"x=%.1f (left/justified alignment)\n",x);

  }
  
  for(i=0;i<l->piece_count;i++) {
    HPDF_Page_SetFontAndSize(page,l->fonts[i]->font,l->actualsizes[i]);
    HPDF_Page_SetRGBFill(page,l->fonts[i]->red,l->fonts[i]->green,l->fonts[i]->blue);
    HPDF_Page_TextOut(page,left_margin+x,y-l->piece_baseline[i],
		      l->pieces[i]);
    x=x+l->piece_widths[i];
    // Don't adjust line gap for dropchars
    if (l->fonts[i]->line_count==1) {
      if (l->fonts[i]->linegap>linegap) linegap=l->fonts[i]->linegap;
    }
  }
  HPDF_Page_EndText (page);
  if (!l->piece_count) linegap=l->line_height;

  // Indicate the height of each line
  if (debug_vspace) {
    debug_vspace_x^=8;
    HPDF_Page_SetRGBFill (page, 0.0,0.0,0.0);
    HPDF_Page_Rectangle(page,
			32+debug_vspace_x, y,
			8,linegap*line_spacing);
    HPDF_Page_Fill(page);
  }

  float old_page_y=page_y;
  page_y=page_y+linegap*line_spacing;
  fprintf(stderr,"Added linegap of %.1f to page_y (= %.1fpts). Next line at %.1fpt\n",
	  linegap*line_spacing,old_page_y,page_y);
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
	break;
      case TT_SPACE:
	paragraph_append_space(target_paragraph);
	break;
      case TT_THINSPACE:
	paragraph_append_thinspace(target_paragraph);
	break;
      case TT_TAG:
	if (token_strings[i]) {
	  if (!strcasecmp(token_strings[i],"bookheader")) {
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
	    if (chapter_label) free(chapter_label); chapter_label=NULL;
	    i++; if (token_types[i]!=TT_TEXT) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	    chapter_label=strdup(token_strings[i]);
	    i++; if (token_types[i]!=TT_ENDTAG) {
	      fprintf(stderr,"\%s must be followed by {value}\n",token_strings[i-1]);
	      exit(-1);
	    }
	  } else if (!strcasecmp(token_strings[i],"bookpretitle")) {
	    // Book title header line
	    paragraph_push_style(target_paragraph,AL_CENTRED,set_font("bookpretitle"));
	    
	  } else if (!strcasecmp(token_strings[i],"booktitle")) {
	    // Book title header line
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
	    current_line_flush(target_paragraph);
	    int index=set_font("chapternum");
	    paragraph_push_style(target_paragraph,AL_LEFT,index);
	    // Require sufficient lines after this one so that the
	    // drop character can fit.
	    if (type_faces[index].line_count>1)
	      paragraph_set_widow_counter(target_paragraph,type_faces[index].line_count-1);
	    // Don't indent lines beginning with dropchars
	    paragraph_setup_next_line(target_paragraph);
	    target_paragraph->current_line->left_margin=0;
	  } else if (!strcasecmp(token_strings[i],"v")) {
	    // Verse number
	    paragraph_push_style(target_paragraph,AL_LEFT,set_font("versenum"));

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
	    paragraph_append_text(target_paragraph,mark,current_font->baseline_delta);
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
	} else
	  paragraph_append_text(target_paragraph,token_strings[i],0);
	break;
      case TT_ENDTAG:
	paragraph_pop_style(target_paragraph);
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

  tokenise_file("books/01_Genesis.tex");
  fprintf(stderr,"Parsed Genesis.tex\n");
  render_tokens();
  fprintf(stderr,"Rendered Genesis.tex\n");
  
  // Write PDF to disk
  HPDF_SaveToFile(pdf,output_file);
  
  return 0;
}
