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
#include <assert.h>
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

  int checkpoint_para=0;
  int checkpoint_line=0;
  int checkpoint_piece=0;
  
  int end_para=0;
  int end_line=0;
  int end_piece=0;

  int start_position_count=0;
  
  while(1) {
    start_position_count++;

    fprintf(stderr,"\rAnalysing page start position %d:                 ",
	    start_position_count);
    fflush(stderr);

    // Try this starting point, but only if it isn't
    // an empty paragraph
    if (!body_paragraphs[start_para]->line_count) {
      // Record zero cost for spanning empty paragraph
    } else {

      // Now advance through all possible ending points.
      // Note that the end point here is inclusive, to simplify the logic.
      end_para=start_para;
      end_line=start_line;
      end_piece=start_piece;

      checkpoint_para=start_para;
      checkpoint_line=start_line;
      checkpoint_piece=start_piece;

      int penalty=0;
      float height=0;
      int cumulative_penalty=0;
      float cumulative_height=0;
      
      while(1) {
	// Work out cost to here.

	if ((end_para!=checkpoint_para)||(end_line!=checkpoint_line)) {
	  // We have advanced to a new line, so add last penalty to the
	  // cumulative penalty, and also adjust the cumulative height
	  cumulative_penalty+=penalty;
	  cumulative_height+=height;

	  checkpoint_para=end_para;
	  checkpoint_line=end_line;
	  checkpoint_piece=end_piece;
	}

	// Stop accumulating page once it is too tall to fit.
	if (cumulative_height>(page_height-top_margin-bottom_margin))
	  {
	    break;
	  }

	// Get height and penalty of the current piece of the current line.
	struct line_pieces *l=body_paragraphs[checkpoint_para]->paragraph_lines[checkpoint_line];
	assert(l->metrics->line_pieces==l->piece_count);
	assert(l->metrics->line_pieces>=end_piece);	  
	assert(l->metrics->line_pieces>=checkpoint_piece);
	assert(end_piece>=checkpoint_piece);
	assert(end_line==checkpoint_line);
	penalty=l->metrics->starts[checkpoint_piece][end_piece].penalty;
	height=l->metrics->starts[checkpoint_piece][end_piece].height;	

	// XXX - Look up height and penalty of footnote paragraph so that it can be
	// taken into account.

	// XXX - Look up height of cross-references so that we can stop if they are too
	// tall.

	float this_height=height+cumulative_height;

	// Work out penalty for emptiness of page
	int emptiness=(100*this_height)/(page_height-top_margin-bottom_margin);
	if (emptiness<0) emptiness=100;
	else if (emptiness>100) emptiness=0;
	else emptiness=100-emptiness;
	int emptiness_penalty=16*emptiness*emptiness;
	if (this_height>(page_height-top_margin-bottom_margin))
	  emptiness_penalty=100000000;
	
	// XXX Work out penalty based on balance of columns.
	
	int this_penalty=penalty+cumulative_penalty+emptiness_penalty;
	
	fprintf(stderr,"Analysing page start position %d:"
		" %d:%d:%d"
		" - %d:%d:%d"
		" p=%d, h=%.1fpts\n",
		start_position_count,
		start_para,start_line,start_piece,
		end_para,end_line,end_piece,
		this_penalty,this_height);
	

	// Advance to next ending point
	if (!body_paragraphs[end_para]->line_count) {
	  end_para++;
	} else {
	  end_piece++;
	  if (end_piece
	      >=body_paragraphs[end_para]->paragraph_lines[end_line]->piece_count) {
	    end_piece=0; end_line++;
	  }
	  if (end_line>=body_paragraphs[end_para]->line_count) {
	    end_line=0;
	    end_para++;
	  }
	  if (end_para>=paragraph_count) break;
	}
	
      }
    }
    
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
