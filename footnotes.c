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

struct paragraph footnote_paragraph;

int footnote_stack_depth=-1;
char footnote_mark_string[4]={'a'-1,0,0,0};
int footnote_count=0;

float footnote_rule_width=0.5;
int footnote_rule_length=100;
int footnote_rule_ydelta=0;

int footnotes_reset()
{
  paragraph_clear(&footnote_paragraph);
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
  if(footnote_count>(footnote_alphabet_size*footnote_alphabet_size)) {
    fprintf(stderr,"Too many footnotes on a single page (limit is %d)\n",
	    (footnote_alphabet_size*footnote_alphabet_size));
    exit(-1);
  }
  return footnote_mark_string;
}

int footnotemarkinfootnotefont_index=-1;
int footnote_mode=0;
int begin_footnote(int token_number)
{
  //  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // footnote_count has already been incremented, so take one away when working out
  // which paragraph to access.
  target_paragraph=&footnote_paragraph;

  // Put space before each footnote.  For space at start of line, since when the
  // footnotes get appended together later the spaces may be in the middle of a line.
  // We use 4 normal spaces so that justification will scale the space appropriately.
  int i;
  for(i=0;i<4;i++) paragraph_append_space(target_paragraph,
					  FORCESPACEATSTARTOFLINE,NO_NOTBREAKABLE,
					  token_number);

  // Draw footnote mark at start of footnote
  if (footnotemarkinfootnotefont_index==-1)
    footnotemarkinfootnotefont_index=set_font_by_name("footnotemarkinfootnote");
  paragraph_push_style(target_paragraph,AL_JUSTIFIED,
		       footnotemarkinfootnotefont_index);
  generate_footnote_mark(footnote_count-1);
  // Don't allow footnote marks to appear at the end of a line
  paragraph_append_text(target_paragraph,footnote_mark_string,0,
			NO_FORCESPACEATSTARTOFLINE,NOTBREAKABLE,token_number);
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
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (!drawingPage) return 0;
  
  // Commit any partial last-line in the footnote paragraph.
  if (footnote_paragraph.current_line) {
    current_line_flush(&footnote_paragraph);
  }

  int saved_page_y=page_y;
  int saved_bottom_margin=bottom_margin;

  struct paragraph *f=layout_paragraph(&footnote_paragraph,drawingPage);
  
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
  
  paragraph_flush(&footnote_paragraph,drawingPage);
  
  if (footnotes_height&&drawingPage) {
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
    crossref_set_ylimit(page_height-saved_bottom_margin);
  }
  
  // Restore page settings
  page_y=saved_page_y;
  bottom_margin=saved_bottom_margin;

  // Clear footnote block after printing it
  paragraph_clear(&footnote_paragraph);
  
  return 0;
}
