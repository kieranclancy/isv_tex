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

char *skip_tokens=NULL;

int page_begin()
{
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
  
  return 0;
}

long long page_end(int drawingPage)
{
  paragraph_flush(&body_paragraph,drawingPage);
  output_accumulated_footnotes();
  output_accumulated_cross_references(target_paragraph->line_count-1,
				      drawingPage);
  fprintf(stderr,"  Page penalty = %lld\n",page_penalty);
  
  return page_penalty;
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
  int start,end;

  skip_tokens=calloc(sizeof(char),(token_count+1));

  // Generate every possible page, and record the score.
  for(start=0;start<(token_count-1);start++) {
    if (!skip_tokens[start]) {
      for(end=start+1;end<token_count;end++) {
	fprintf(stderr,"Calculating cost of page: tokens=[%d,%d)\n",
		start,end);
	page_begin();
	
	// We need to know the type-face stack at each point.
	// When start==0, it is start of the document, so no problem.
	// Else, we need to fetch the style stack.
	if (start) paragraph_fetch_style_stack();
	render_tokens(start,end,0);
	// So that we have a style stack to fetch, we need to have it stowed
	// away.
	if (end==start+1) paragraph_stash_style_stack();
	page_end(0);
	
	// Stop when page score is too bad
	if (page_penalty>(OVERFULL_PAGE_PENALTY_PER_PT*16))
	  break;
      }
    }
  }

  free(skip_tokens);
  
  return 0;
}

int page_skip_token_as_subordinate(int token_number)
{
  if (skip_tokens) skip_tokens[token_number]=1;
  return 0;
}
