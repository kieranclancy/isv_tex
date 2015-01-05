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

long long page_penalty=0;
float page_fullness;
int page_widow=0;

int page_begin()
{
  paragraph_clear_style_stack();
  
  footnotes_reset();
  footnote_mode=0;

  crossrefs_reset();

  new_empty_page(leftRight,0);
  
  paragraph_clear(&body_paragraph);
  target_paragraph=&body_paragraph;

  // XXX - Need to restore style stack as previously saved
  paragraph_clear_style_stack();
  
  page_y=top_margin;

  page_penalty=0;
  page_fullness=0;
  page_widow=0;
  
  return 0;
}

long long page_end(int drawingPage)
{
  paragraph_flush(&body_paragraph,drawingPage);
  output_accumulated_footnotes();
  output_accumulated_cross_references(target_paragraph->line_count-1,
				      drawingPage);

  if (page_fullness<0||page_fullness>100) {
    fprintf(stderr,"Page fullness = %.1f%%. This is bad.\n",
	    page_fullness);
    exit(-1);
  }
  
  long long fullness_penalty=(100.0-page_fullness)*(100.0-page_fullness)
    *UNDERFULL_PAGE_PENALTY_MULTIPLIER;

  long long widow_penalty=0;
  if (page_widow) widow_penalty+=WIDOW_PENALTY;

  page_penalty+=fullness_penalty+widow_penalty;
  
  fprintf(stderr," : score = -%lld(%lld,%lld)",
	  page_penalty,fullness_penalty,widow_penalty);
  
  return page_penalty;
}

int page_notify_details(float fullness,int tied_to_next_line)
{
  page_fullness=fullness;
  page_widow=tied_to_next_line;
  return 0;
}

int page_penalty_add(long long penalty)
{
  if (page_penalty+penalty>page_penalty)
    page_penalty+=penalty;
  return 0;
}

int page_penalty_if_not_start_of_page()
{
  // XXX - Apply a large penalty if we are not at the top of a page.
  return 0;
}

int page_optimal_render_tokens()
{
  /* Generate set of all paragraphs and lines that we need to optimise over.
     This automatically calculates (or loads the cached) the heights and costs
     of every possible segment of every possible line.
  */
  render_tokens(0,token_count,0);

  /* Now work out the cost of every possible page */
  int start_para=0;
  int start_line=0;
  int start_piece=0;
  
  int end_para=0;
  int end_line=0;
  int end_piece=0;

  int start_position_count=0;
  
  while(1) {
    start_position_count++;

    // Advance to next starting point
    if (!body_paragraphs[start_para]->line_count) {
      start_para++;
    } else {
      start_piece++;
      if (start_piece
	  >=body_paragraphs[start_para]->paragraph_lines[start_line]->piece_count) {
	start_piece=0; start_line++;
      }
      if (start_line>=body_paragraphs[start_para]->line_count) {
	start_line=0;
	start_para++;
      }
    }
    if (start_para>=paragraph_count) break;
  }

  fprintf(stderr,"Analysed all %d possible page starting positions.\n",
	  start_position_count);
  
  return 0;
}
