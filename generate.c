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

int paragraph_flush()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  return 0;
}

int paragraph_append_text(char *text)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
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
  return 0;
}

int render_tokens()
{
  int i,j;

  paragraph_clear_style_stack();
  
  for(i=0;i<token_count;i++)
    {
      switch(token_types[i]) {
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
	  } else {
	    
	    fprintf(stderr,"Warning: unknown tag \%s\n",token_strings[i]);
	  }	  
	}
	break;
      case TT_TEXT:
	// Append to paragraph
	paragraph_append_text(token_strings[i]);
	break;
      case TT_ENDTAG:
	paragraph_pop_style();
	break;
      }
    }
  
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
