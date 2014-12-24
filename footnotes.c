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
  fprintf(stderr,"%s(%d)\n",__FUNCTION__,n);
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
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  // footnote_count has already been incremented, so take one away when working out
  // which paragraph to access.
  target_paragraph=&footnote_paragraphs[footnote_count-1];
  footnote_line_numbers[footnote_count-1]=body_paragraph.current_line->line_uid;
  fprintf(stderr,"Footnote '%s' is in line #%d (line uid %d). There are %d foot notes.\n",
	  footnote_mark_string,body_paragraph.line_count,
	  body_paragraph.current_line->line_uid,footnote_count);

  // Put space before each footnote.  For space at start of line, since when the
  // footnotes get appended together later the spaces may be in the middle of a line.
  // We use 4 normal spaces so that justification will scale the space appropriately.
  int i;
  for(i=0;i<4;i++) paragraph_append_space(target_paragraph,1);

  // Draw footnote mark at start of footnote
  paragraph_push_style(target_paragraph,AL_JUSTIFIED,
		       set_font("footnotemarkinfootnote"));
  generate_footnote_mark(footnote_count-1);
  paragraph_append_text(target_paragraph,footnote_mark_string,0,0);
  paragraph_pop_style(target_paragraph);

  footnote_mode=1;
  return 0;
}

int end_footnote()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  fprintf(stderr,"Footnote paragraph is:\n");
  paragraph_dump(target_paragraph);
  line_dump(target_paragraph->current_line);
  current_line_flush(target_paragraph);
  fprintf(stderr,"Footnote paragraph is:\n");
  paragraph_dump(target_paragraph);

  target_paragraph=&body_paragraph;
  footnote_mode=0;

  fprintf(stderr,"There are %d footnotes.\n",footnote_count);
  int i;
  for(i=0;i<footnote_count;i++)
    {
      paragraph_dump(&footnote_paragraphs[i]);
    }
  
  return 0;
}


int reenumerate_footnotes(int first_remaining_line_uid)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  // While there are footnotes we have already output, purge them.
  fprintf(stderr,"There are %d footnotes.\n",footnote_count);
  int i;
  for(i=0;i<footnote_count;i++)
    {
      paragraph_dump(&footnote_paragraphs[i]);
    }
  
  while((footnote_count>0)&&(footnote_line_numbers[0]<first_remaining_line_uid))
    {
      fprintf(stderr,"%d foot notes remaining: deleting %d (is < %d)\n",
	      footnote_count,
	      footnote_line_numbers[0],first_remaining_line_uid);
      paragraph_dump(&footnote_paragraphs[0]);
      fprintf(stderr,"  clearing footnote\n");
      paragraph_clear(&footnote_paragraphs[0]);
      int i;
      // Copy down foot notes
      for(i=0;i<(footnote_count-1);i++) {
	footnote_line_numbers[i]=footnote_line_numbers[i+1];
	bcopy(&footnote_paragraphs[i+1],&footnote_paragraphs[i],
	      sizeof (struct paragraph));
      }
      footnote_count--;
    }
  
  // Now that we have only the relevant footnotes left, update the footnote marks
  // in the footnotes, and in the lines that reference them.
  // XXX - If the footnote mark becomes wider, it might stick out into the margin.
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

      // Update footnote marks in body text
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

      // Update footnote marks in footnotes
      int footnotemarkinfootnote_typeface_index=set_font("footnotemarkinfootnote");
      p=&footnote_paragraphs[i];
      for(j=0;j<p->line_count;j++)
	{	  
	  for(k=0;k<p->paragraph_lines[j]->piece_count;k++)
	    {
	      if (p->paragraph_lines[j]->fonts[k]
		  ==&type_faces[footnotemarkinfootnote_typeface_index])
		{
		  // This is the piece
		  free(p->paragraph_lines[j]->pieces[k]);
		  p->paragraph_lines[j]->pieces[k]=strdup(footnote_mark_string);
		  fprintf(stderr,"  footnotemark #%d = '%s' (piece %d/%d in footnote)\n",
			  i,footnote_mark_string,
			  k,p->paragraph_lines[j]->piece_count);
		  break;
		}
	    }
	}
      
    }


  fprintf(stderr,"There are %d footnotes left:\n",footnote_count);
  for(i=0;i<footnote_count;i++)
    {
      paragraph_dump(&footnote_paragraphs[i]);
    }
  
  // Update footnote mark based on number of footnotes remaining
  generate_footnote_mark(footnote_count-1);

  return 0;
}

int output_accumulated_footnotes()
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);

  fprintf(stderr,"Before flushing last line:\n");
  paragraph_dump(&rendered_footnote_paragraph);
  
  // Commit any partial last-line in the footnote paragraph.
  if (rendered_footnote_paragraph.current_line) {
    current_line_flush(&rendered_footnote_paragraph);
  }

  fprintf(stderr,"After flushing last line:\n");
  paragraph_dump(&rendered_footnote_paragraph);

  int saved_page_y=page_y;
  int saved_bottom_margin=bottom_margin;
  
  int footnotes_height=paragraph_height(&rendered_footnote_paragraph);

  int footnotes_y=page_height-bottom_margin-footnotes_height;

  page_y=footnotes_y;
  bottom_margin=0;

  // Mark all footnote block lines as justified, and strip leading space from
  // lines
  int i;
  for(i=0;i<rendered_footnote_paragraph.line_count;i++) {
    rendered_footnote_paragraph.paragraph_lines[i]->alignment=AL_JUSTIFIED;
    line_remove_leading_space(rendered_footnote_paragraph.paragraph_lines[i]);
  }
  
  paragraph_flush(&rendered_footnote_paragraph);
  
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
  } else crossref_set_ylimit(page_height-bottom_margin);
  
  // Restore page settings
  page_y=saved_page_y;
  bottom_margin=saved_bottom_margin;

  // Clear footnote block after printing it
  paragraph_clear(&rendered_footnote_paragraph);
  
  return 0;
}
