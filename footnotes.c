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

#define NONDECENDING_LETTERS "abcdefhiklmnorstuvwxz"
char *footnote_alphabet=NONDECENDING_LETTERS;
int footnote_alphabet_size=strlen(NONDECENDING_LETTERS);

struct paragraph rendered_footnote_paragraph;

struct paragraph footnote_paragraphs[MAX_FOOTNOTES_ON_PAGE];
int footnote_line_numbers[MAX_FOOTNOTES_ON_PAGE];

int footnote_stack_depth=-1;
char footnote_mark_string[4]={'a'-1,0,0,0};
int footnote_count=0;

float footnote_rule_width=0.5;
int footnote_rule_length=100;
int footnote_rule_ydelta=0;

int footnotes_reset()
{
  int i;
  for(i=0;i<MAX_FOOTNOTES_ON_PAGE;i++) {
    paragraph_clear(&footnote_paragraphs[i]);
    footnote_line_numbers[i]=-1;
  }

  generate_footnote_mark(0);

  footnote_stack_depth=-1;
  footnote_count=0;

  return 0;
}

int generate_footnote_mark(int n)
{
  //  fprintf(stderr,"%s(%d)\n",__FUNCTION__,n);
  if (n<footnote_alphabet_size) {
    footnote_mark_string[0]=footnote_alphabet[n];
    footnote_mark_string[1]=0;
  }
  else {
    n-=footnote_alphabet_size;
    footnote_mark_string[0]=footnote_alphabet[n/footnote_alphabet_size];
    footnote_mark_string[1]=footnote_alphabet[n%footnote_alphabet_size];
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

int footnote_mode=0;
int begin_footnote()
{
  //  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // footnote_count has already been incremented, so take one away when working out
  // which paragraph to access.
  target_paragraph=&footnote_paragraphs[footnote_count-1];
  footnote_line_numbers[footnote_count-1]=body_paragraph.current_line->line_uid;
  if (0)
    fprintf(stderr,"Footnote '%s' is in line #%d (line uid %d). There are %d foot notes.\n",
	    footnote_mark_string,body_paragraph.line_count,
	    body_paragraph.current_line->line_uid,footnote_count);

  // Put space before each footnote.  For space at start of line, since when the
  // footnotes get appended together later the spaces may be in the middle of a line.
  // We use 4 normal spaces so that justification will scale the space appropriately.
  int i;
  for(i=0;i<4;i++) paragraph_append_space(target_paragraph,
					  FORCESPACEATSTARTOFLINE,NO_NOTBREAKABLE);

  // Draw footnote mark at start of footnote
  paragraph_push_style(target_paragraph,AL_JUSTIFIED,
		       set_font("footnotemarkinfootnote"));
  generate_footnote_mark(footnote_count-1);
  // Don't allow footnote marks to appear at the end of a line
  paragraph_append_text(target_paragraph,footnote_mark_string,0,
			NO_FORCESPACEATSTARTOFLINE,NOTBREAKABLE);
  paragraph_pop_style(target_paragraph);

  footnote_mode=1;
  return 0;
}

int end_footnote()
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if(0) {
    fprintf(stderr,"Footnote paragraph (%p) is:\n",target_paragraph);
    paragraph_dump(target_paragraph);
    line_dump(target_paragraph->current_line);
    current_line_flush(target_paragraph);
    fprintf(stderr,"Footnote paragraph is:\n");
    paragraph_dump(target_paragraph);
  }

  target_paragraph=&body_paragraph;
  footnote_mode=0;

  return 0;
}

int output_accumulated_footnotes(int drawingPage)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  // Commit any partial last-line in the footnote paragraph.
  if (rendered_footnote_paragraph.current_line) {
    current_line_flush(&rendered_footnote_paragraph);
  }

  int saved_page_y=page_y;
  int saved_bottom_margin=bottom_margin;

  struct paragraph *f=layout_paragraph(&rendered_footnote_paragraph);
  
  int footnotes_height=paragraph_height(f);

  int footnotes_y=page_height-bottom_margin-footnotes_height;

  page_y=footnotes_y;
  bottom_margin=0;

  // Mark all footnote block lines as justified, and strip leading space from
  // lines
  int i;
  for(i=0;i<f->line_count;i++) {
    f->paragraph_lines[i]->alignment=AL_JUSTIFIED;
    line_remove_leading_space(f->paragraph_lines[i]);
  }

  paragraph_clear(f); free(f);
  
  paragraph_flush(&rendered_footnote_paragraph,drawingPage);
  
  if (footnotes_height) {
    // Draw horizontal rule
    int rule_y=footnotes_y+footnote_rule_ydelta;
    crossref_set_ylimit(rule_y);
    int y=page_height-rule_y;
    HPDF_Page_SetRGBStroke(page, 0.0, 0.0, 0.0);
    HPDF_Page_SetLineWidth(page,footnote_rule_width);
    
    HPDF_Page_MoveTo(page,left_margin,y);
    HPDF_Page_LineTo(page,left_margin+footnote_rule_length,y);
    HPDF_Page_Stroke(page);
  } else {
    fprintf(stderr,"crossref_set_ylimit(%d-%d)\n",
	    page_height,saved_bottom_margin);
    crossref_set_ylimit(page_height-saved_bottom_margin);
  }
  
  // Restore page settings
  page_y=saved_page_y;
  bottom_margin=saved_bottom_margin;

  // Clear footnote block after printing it
  paragraph_clear(&rendered_footnote_paragraph);
  
  return 0;
}
