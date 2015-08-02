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

struct page_option_record {
  int start_index;
  
  int start_para;
  int start_line;
  int start_piece;

  int end_para;
  int end_line;
  int end_piece;

  int page_count;

  long long penalty;
  // Height of text block only, so that we can also use it for balancing pages,
  // which don't care about the height of the foot-notes.
  float height;
};

extern float crossrefs_height;

/*
  Page scoring is now somewhat complicated by the need to support two column mode.

  First, there are some text types allowed at the top of a page which span the
  columns. For now, the only ones are the book title type faces. So we need to deduct
  their height from the available height for columns.

  Second, we want to balance column heights, so we need to be able to split columns
  at any arbitrary point after that.  We will do this by remembering the height at
  any given point on the page, and cache a page end penalty for that point that can
  be fed into the column balancer later.  Since cutting a column in the middle of a
  line can never result in it being taller than would occur if a single line was
  added, we can work this out as we go along, and calculate the lowest imbalance
  penalty by considering breaking the page at each possible point.  This does add some
  extra computation to each page, but let's hope that it is manageable.  The end
  result of optimally balanced columns on every page should be worth the inconvenience.

 */
int page_score_at_this_starting_point(int start_para,int start_line,int start_piece,
				      struct page_option_record *backtrace,
				      int start_position_count)
{
  crossrefs_reset();

  int first_footnote=-1;
  int last_footnote=-1;

  int checkpoint_para=0;
  int checkpoint_line=0;
  int checkpoint_piece=0;
  
  int end_para=0;
  int end_line=0;
  int end_piece=0;

  determinism_test_integer(start_para);
  determinism_test_integer(start_line);
  determinism_test_integer(start_piece);
  
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
  float cumulative_height=0;
  float column_span_height=0;
  float two_column_height=0;
  int two_columns=0;
  long long cumulative_penalty=0;
  long long column_penalty=0;

  long long widow_penalty=0;
  
  long long best_penalty=0x7ffffffffffffffLL;
  float best_height=-1;

  
  int end_position=start_position_count;
  
  determinism_test_integer(end_para);
  determinism_test_integer(end_line);
  determinism_test_integer(end_piece);

  while(1) {
    determinism_test_integer(end_para);
    determinism_test_integer(end_line);
    determinism_test_integer(end_piece);
    
    if (!end_piece) {
      if ((end_para!=checkpoint_para)||(end_line!=checkpoint_line)) {
	// We have advanced to a new line, so add last penalty to the
	// cumulative penalty, and also adjust the cumulative height
	determinism_test_integer(penalty);
	determinism_test_float(height);

	// Get paragraph and line to work out if the paragraph spans columns, or is
	// confined to a single column.
	if (body_paragraphs[end_para]) {
	  if (body_paragraphs[end_para]->span_columns) {
	    // Paragraph spans columns, thus reducing the amount of space available
	    // for any following two-column text
	    column_span_height+=height;
	    if (two_columns) {
	      // A page cannot switch back to single column mode after having
	      // two columns above (it would be nice to relax this later).
	      // This means that we need to lie and claim that our height is way too
	      // big to fit, so that this option doesn't get considered.
	      column_span_height=page_height+1;
	    }
	  } else {
	    // Paragraph is in two-column mode.
	    // A page should not be able to revert to single column mode,
	    two_columns=1;
	    two_column_height+=height;
	  }
	}
	
	cumulative_penalty+=penalty;
	cumulative_height+=height;

	determinism_test_integer(checkpoint_para);
	determinism_test_integer(checkpoint_line);

	checkpoint_para=end_para;
	checkpoint_line=end_line;
	checkpoint_piece=end_piece;

      }
    }
    
    // Stop accumulating page once it is too tall to fit.
    // XXX - Doesn't check two-column height.
    if (column_span_height>(page_height-top_margin-bottom_margin)) {
      break;
    }
    
    // Get height and penalty of the current piece of the current line.
    struct line_pieces *l=NULL;
    determinism_test_integer(checkpoint_para);
    determinism_test_integer(checkpoint_line);

    determinism_test_integer(body_paragraphs[checkpoint_para]?1:0);
    if (body_paragraphs[checkpoint_para]) {
      l=body_paragraphs[checkpoint_para]->paragraph_lines[checkpoint_line];    
      determinism_test_integer(checkpoint_para);
    }
    determinism_test_integer(l?1:0);
    determinism_test_integer(l?l->piece_count:-1);
    
    if (l&&!l->piece_count) {
      // don't penalise vspace
      // XXX - Should we ignore vspace at the top of a page?
      // This is probably being done somewhere already, since we don't see
      // vspace at the top of pages in the output.
      penalty=0;
      height=l->line_height;
      determinism_test_integer(penalty);
      determinism_test_float(height);
    } else if (l&&l->piece_count) {
      // Line contains text, so retrieve penalty etc for it.
      assert(l->metrics->line_pieces==l->piece_count);
      assert(l->metrics->line_pieces>=end_piece);	  
      assert(l->metrics->line_pieces>=checkpoint_piece);
      assert(end_piece>=checkpoint_piece);
      assert(end_line==checkpoint_line);
      penalty=l->metrics->starts[checkpoint_piece][end_piece].penalty;
      height=l->metrics->starts[checkpoint_piece][end_piece].height;

      // Add widow/orphan penalties for partial lines included in the page
      widow_penalty=0;
      if ((start_para==end_para)&&(start_line==end_line))
	{
	  // We are in the first line of the page.
	  // If we are including a partial section of that paragraph, so see if it
	  // is < 2 lines. If so, then add a big penalty.
	  int lines = l->metrics->starts[start_piece][end_piece].lines;
	  int total_lines = l->metrics->starts[0][l->piece_count].lines;
	  if (total_lines>1)
	    if (lines<2)
	      widow_penalty=WIDOW_PENALTY;
	}

      determinism_test_integer(penalty);
      determinism_test_float(height);

      if (end_piece<body_paragraphs[end_para]->paragraph_lines[end_line]->piece_count)
	{
	  struct piece *piece=
	    piece=
	    &body_paragraphs[end_para]->paragraph_lines[end_line]->pieces[end_piece];
	  if (piece->footnote_number) {
	    if (first_footnote==-1)	first_footnote=piece->footnote_number;
	    last_footnote=piece->footnote_number;
	  }
	}

      // Check end piece to see if it incurs a penalty, for example, from having
      // a page end on a heading line.
      if (l->pieces&&(end_piece>=0)&&l->pieces[end_piece].font) 
	penalty+=l->pieces[end_piece].font->penalty_at_end_of_page;
      if (!l->pieces) {
	// vspace should not appear at the bottom of a page -- it probably indicates
	// an orphaned/widowed heading.
	penalty+=VSPACE_AT_END_PENALTY;
      }
    }

    
    float this_height=height+cumulative_height;
    if (0) {
      fprintf(stderr,"   %d..%d : segment height=%.1fpts, cumulative_height=%.1fpts\n",
	      checkpoint_piece, end_piece,
	      height,cumulative_height);
      line_segment_dump(body_paragraphs[checkpoint_para],checkpoint_line,
			       checkpoint_piece, end_piece);
    }

    // Look up height and penalty of footnote paragraph so that it can be
    // taken into account.
    float footnotes_height=footnotes_paragraph_height(first_footnote,last_footnote);
    determinism_test_float(footnotes_height);
    
    // Look up height of cross-references so that we can stop if they are too
    // tall.  We don't care about the position.
    if (l->piece_count&&l->piece_count>end_piece)      
      crossrefs_register_line(l,end_piece,end_piece+1, 0);
    if (crossrefs_height>(page_height-top_margin-bottom_margin-footnotes_height)) {
      determinism_test_float(crossrefs_height);
      break;
    }

    /* Work out optimal split of any two-column content as well.
       This method is HORRIBLY slow, because it works out every possible split on
       every possible page. It would be more efficient to render the column once,
       work out where the cheapest split in the middle is by simple examination of
       the height, and a "what is the cost of breaking text here" function to catch
       hanging things (which we need for applying to ends of pages, anyway), and then
       we would have a pretty good solution that would be much, much faster than this,
       and any suboptimality would be limited to a single line of height difference
       between the columns. 
    */
    int best_split_para=-1,best_split_line=-1,best_split_piece=-1;
    
    if ((two_columns)&&(column_count==2)) {

      long long best_penalty=-1;
      float best_height=0;
      
      int split_para=start_para;
      int split_line=start_line;
      int split_piece=start_piece;

      // Empty old items from height and penalty cache
      column_height_and_penalty_cache_advance_to(start_para,start_line,start_piece);
      
      while (split_para<end_para||split_line<end_line||split_piece<end_piece)
	{
	  // Calculate height and penalty of each column piece.
	  long long left_penalty,right_penalty;
	  float left_height,right_height;
	  column_get_height_and_penalty(start_para,start_line,start_piece,
					split_para,split_line,split_piece,
					&left_penalty,&left_height);
	  // XXX - We count the token at the split in both columns!
	  column_get_height_and_penalty(split_para,split_line,split_piece,
					end_para,end_line,end_piece,
					&right_penalty,&right_height);
	  
	  long long imbalance_penalty
	    =COLUMN_IMBALANCE_PENALTY_PER_SQPOINT*(left_height-right_height)*(left_height-right_height);
	  
	  if (best_penalty==-1
	      ||(left_penalty+right_penalty+imbalance_penalty<best_penalty))
	    {
	      // This position is better than what we have seen before.
	      best_penalty=left_penalty+right_penalty+imbalance_penalty;
	      if (left_height<right_height)
		best_height=right_height;
	      else best_height=left_height;
	      best_split_para=split_para;
	      best_split_line=split_line;
	      best_split_piece=split_piece;
	    }
	  
	  // Advance split position
	  if (body_paragraphs[split_para]) {
	    if (!body_paragraphs[split_para]->line_count) {
	      split_para++;
	    } else {
	      split_piece++;
	      if (split_piece
		  >body_paragraphs[split_para]->paragraph_lines[split_line]
		  ->piece_count) {
		split_piece=0; split_line++;
	      }
	      if (split_line>=body_paragraphs[split_para]->line_count) {
		split_line=0;
		split_para++;
	      }	      
	    }
	  }
	}
      // Remember best split
      column_penalty=best_penalty;
      this_height=column_span_height+best_height;
    } else {
      // Single column only mode, so no splitting or imbalance penalties required,
      // and this_height is calculated the normal way from height+cumulative_height
      column_penalty=0;
    }
    
    // Work out penalty for emptiness of page
    int emptiness=(100*this_height)/(page_height-top_margin-bottom_margin);
    if (emptiness<0) emptiness=100;
    else if (emptiness>100) emptiness=0;
    else emptiness=100-emptiness;
    long long emptiness_penalty=16*emptiness*emptiness;
	// Over-filling the page is BAD
    if (this_height>(page_height-top_margin-bottom_margin))
      emptiness_penalty=
	OVERFULL_PAGE_PENALTY_PER_PT*
	(this_height-(page_height-top_margin-bottom_margin));
    else if (page_height<=(top_margin+bottom_margin+this_height+footnotes_height)) {
      emptiness_penalty=
	OVERFULL_PAGE_PENALTY_PER_PT*
	((top_margin+bottom_margin+this_height+footnotes_height)-page_height);
    }
    
    determinism_test_integer(emptiness);
    determinism_test_integer(emptiness_penalty);
    
    /* Work out penalty based on balance of columns.

       First note: we balance the text block heights, not the footnotes.

       There are two algorigthms we could apply to balancing the columns:
       1. We could take the current page, and see how feasible it is to split the
       text into the columns, and see what the penalty would be of balancing them.
       or
       2. We could take a look at the length of the previous page, and try to match
       it.

       Both have problems.  
       For (1), we need to re-calculate a pile of stuff.
       For (2), we need to consider all possible page-pairs.

       (1) is the worst though, as (2) will be somewhat mitigated by the final
       back-trace that will pick the best page series, taking into account the 
       balance cost, so we will stick with (2).

       This means we need to keep track of the page of the text on each page as it
       enters the dynamic programming grid, and then check it for right-side pages
       only as we go along.
    */

    long long balance_penalty=0;
    if (backtrace) {
      if (start_position_count>0) {
	if ((backtrace[start_position_count-1].page_count % 2) == 0) {
	  // Right page, so work out balance penalty:
	  // = square of difference in height of text blocks
	  balance_penalty=PAGE_IMBALANCE_PENALTY_PER_SQPOINT*
	    (backtrace[start_position_count-1].height-this_height)
	    *(backtrace[start_position_count-1].height-this_height);	  
	}
      }
    }

    // Now, we actually do need to do (1) as well, to balance columns on a page, as
    // compared to on facing pages. For this, we need to have used a different
    // page_width and page_height for the text, than that of the physical page, and
    // which is used for the footnotes. We also need to split the cross-references.

    // XXX - Implement column balancing on a page.
    
    assert(penalty>=0);
    assert(cumulative_penalty>=0);
    assert(emptiness_penalty>=0);
    assert(column_penalty>=0);
    assert(widow_penalty>=0);
    
    long long this_penalty=penalty+cumulative_penalty
      +emptiness_penalty+balance_penalty+column_penalty+widow_penalty;
    
    if (this_penalty<best_penalty) {
      best_penalty=this_penalty; best_height=this_height;
    }       	

    if (backtrace) {
      if (((this_penalty+backtrace[start_position_count-1].penalty)
	   <backtrace[end_position].penalty)
	  ||(backtrace[end_position].penalty==-1)) {
	assert(end_position>=0);
	backtrace[end_position].start_index=start_position_count-1;
	backtrace[end_position].start_para=start_para;
	backtrace[end_position].start_line=start_line;
	backtrace[end_position].start_piece=start_piece;
	backtrace[end_position].end_para=end_para;
	backtrace[end_position].end_line=end_line;
	backtrace[end_position].end_piece=end_piece;
	if (start_position_count>0) {
	  backtrace[end_position].penalty
	    =backtrace[start_position_count-1].penalty
	    +this_penalty;
	} else {
	  backtrace[end_position].penalty=this_penalty;
	}
	backtrace[end_position].height=this_height;
	
	if (start_position_count)
	  backtrace[end_position].page_count
	    =backtrace[start_position_count-1].page_count+1;
      }
    } 

    determinism_test_integer(end_para);
    determinism_test_integer(end_line);
    determinism_test_integer(end_piece);
    
    // Advance to next ending point
    if (body_paragraphs[end_para]) {
      if (!body_paragraphs[end_para]->line_count) {
	end_para++;
	determinism_test_integer(end_para);	
      } else {
	end_piece++;
	determinism_test_integer(end_piece);
	if (end_piece
	    >body_paragraphs[end_para]->paragraph_lines[end_line]->piece_count) {
	  determinism_test_integer(end_line);
	  determinism_test_integer(end_piece);
	  end_piece=0; end_line++;
	  // Keep widow penalty of first line on page when we advance to the next line
	  cumulative_penalty+=widow_penalty;
	}
	determinism_test_integer(end_line);
	determinism_test_integer(end_para);
	determinism_test_integer(body_paragraphs[end_para]->line_count);
	if (end_line>=body_paragraphs[end_para]->line_count) {
	  end_line=0;
	  determinism_test_integer(end_para);
	  end_para++;
	}
	if (end_para>=paragraph_count) {
	  determinism_test_integer(end_para);
	  break;
	}
      }
    } else {
      determinism_test_integer(end_para);
      break;
    }
    
    determinism_test_integer(end_position);
    end_position++;
  }

  if (0)
    fprintf(stderr,"Analysing page start position %d ("
	    "%d:%d:%d): Best result: "
	    " penalty=%lld, height=%.1fpts\n",
	    start_position_count,
	    start_para,start_line,start_piece,
	    best_penalty,best_height);      

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

  int start_position_count=0;

  struct page_option_record backtrace[token_count+1];

  for(int i=0;i<token_count;i++) {
    backtrace[i].start_index=-1;
    backtrace[i].start_para=-1;
    backtrace[i].start_line=-1;
    backtrace[i].start_piece=-1;
    backtrace[i].page_count=0;
    backtrace[i].penalty=-1;
    backtrace[i].height=-1;
  }
     
  while(1) {
    if (0) {
      fprintf(stderr,"\nAnalysing page start position %d\n",
		    start_position_count);
      fflush(stderr);
    }

    crossrefs_reset();

    // Try this starting point, but only if it isn't
    // an empty paragraph
    if (!body_paragraphs[start_para]->line_count) {
      // Record zero cost for spanning empty paragraph
    } else {

      page_score_at_this_starting_point(start_para,start_line,start_piece,
					backtrace,start_position_count);
    }    
    // Advance to next starting point
    if (!body_paragraphs[start_para]->line_count) {
      start_para++;
    } else {
      start_piece++;
      if (start_piece
	  >body_paragraphs[start_para]->paragraph_lines[start_line]->piece_count) {
	start_piece=0; start_line++;
      }
      if (start_line>=body_paragraphs[start_para]->line_count) {
	start_line=0;
	start_para++;
      }
    }
    if (start_para>=paragraph_count) break;

    start_position_count++;
  }

  fprintf(stderr,"Analysed all %d possible page starting positions.\n",
	  start_position_count);

  int position=start_position_count-1;
  int page_positions[token_count+1];
  int page_count=0;
  
  while((position>0)&&(backtrace[position].start_index<position)) {
    
    long long penalty=backtrace[position].penalty;
    int next_position=backtrace[position].start_index;
    if (next_position>=0) penalty-=backtrace[next_position].penalty;

    if (next_position>=position||next_position<-1) {
      fprintf(stderr,"Illegal step or loop in page optimisation dyanmic programming list.\n");
      exit(-1);
    }
    page_positions[page_count++]=position;
    position=backtrace[position].start_index;

    // Stop as soon as we have reached the start of the actual text
    if ((!backtrace[next_position].start_para)
	&&(!backtrace[next_position].start_line)
	&&(!backtrace[next_position].start_piece))
      break;

  }


  struct paragraph *out=new_paragraph();
  struct paragraph *footnotes=new_paragraph();
  footnotes->noindent=1;
  
  fprintf(stderr,"Optimal page path:\n");
  for(int page_number=page_count-1;page_number>=0;page_number--) {
    position=page_positions[page_number];
    long long penalty=backtrace[position].penalty;
    int next_position=backtrace[position].start_index;
    if (next_position>=0) penalty-=backtrace[next_position].penalty;

    // Setup new page and render lines onto page.
    if (page_number==(page_count-1))
      // No header on first page of a book
      new_empty_page(leftRight,1);
    else
      new_empty_page(leftRight,0);

    crossrefs_reset();
    
    if (next_position>=0) {
      start_para=backtrace[next_position].start_para;
      start_line=backtrace[next_position].start_line;
      start_piece=backtrace[next_position].start_piece;
    } else {
      start_para=0; start_line=0; start_piece=0;
    }
    if (start_para<0) {
      start_para=0; start_line=0; start_piece=0;
    }
    
    int end_para=backtrace[next_position].end_para;
    int end_line=backtrace[next_position].end_line;
    int end_piece=backtrace[next_position].end_piece;

    int num_footnotes=0;

    float predicted_page_height=(next_position>=0)?backtrace[next_position].height:-1;
    
    fprintf(stderr,"%d (%d %d %d -- %d %d %d) : penalty=%lld, page_count=%d, page_height=%.1fpts\n",
	    next_position+1,
	    start_para,start_line,start_piece,
	    end_para,end_line,end_piece,
	    penalty,
	    backtrace[position].page_count,
	    predicted_page_height);

    struct line_pieces *l=NULL;

    while(start_para<end_para||start_line<end_line||start_piece<=end_piece) {

      if (body_paragraphs[start_para])
	l=body_paragraphs[start_para]->paragraph_lines[start_line];
      else l=NULL;

      int last_piece=end_piece;
      if (end_line!=start_line||end_para!=start_para) {
	if (l) last_piece=l->piece_count;
	else last_piece=-1;
      }

      if (0) {
	fprintf(stderr,"  rendering para #%d line #%d : text_column_width=%d, page_width=%d-%d-%d,"
		" pre-computed page height=%.1fpts, pre-computed para height=%.1fpts"
		" (ptr=%p)"
		" (pieces %d..%d)\n    ",
		start_para,start_line,
		text_column_width,
		page_width,left_margin,right_margin,
		backtrace[position].height,
		(l&&l->metrics)?l->metrics->starts[start_piece][last_piece].height:-1.0,
		(l&&l->metrics)?l->metrics:NULL,
		start_piece,last_piece
		);
	if (l) line_dump(l);
      }

      // Layout line onto page
      penalty=0;
      int firstLine=1;
      float prev_page_y=page_y;
      
      int start=0;
      int end=0;
      if (body_paragraphs[start_para]->line_count) {
	// paragraph_dump(body_paragraphs[start_para]);
	struct line_pieces *l=body_paragraphs[start_para]->paragraph_lines[start_line];
	end=l->piece_count;
	line_recalculate_width(l);
	// Truncate first and last lines as required.
	if (firstLine) start=start_piece; firstLine=0;
	if (start_para==end_para&&start_line==end_line) end=end_piece;
	// Layout the line (or line segment).
	if (0) {
	  fprintf(stderr,"layout_line(line_number=%d, start=%d, end=%d) for page "
		  "(line actual range is %d..%d)\nThis is the line:\n  ",
		  start_line,start,end,0,l->piece_count);
	  line_dump(l);
	}
	penalty=layout_line(body_paragraphs[start_para],start_line,start,end,out,0);
	footnotes_build_block(footnotes,out,&num_footnotes);
	for(int i=0;i<out->line_count;i++) {
	  if (1) {
	    fprintf(stderr,"  line #%d %d..%d: left_margin=%d, max_width=%d\n",
		    i,start,end,
		    out->paragraph_lines[i]->left_margin,
		    out->paragraph_lines[i]->max_line_width);
	    line_dump(out->paragraph_lines[i]);
	  }
	  float last_page_y=page_y;

	  // Justify the last line on a page if it isn't the last line of a paragraph
	  // i.e., if the paragraph is split over the page break.
	  if (end
	      <body_paragraphs[start_para]->paragraph_lines[start_line]->piece_count)
	    out->justifylast=1;
	  else
	    out->justifylast=0;
	  
	  line_emit(out,i,1,1);
	  if (0) fprintf(stderr,"  line #%d actual height = %.1fpts\n",
			 out->paragraph_lines[i]->line_uid,page_y-last_page_y);
	  crossrefs_register_line(out->paragraph_lines[i],
				  0,out->paragraph_lines[i]->piece_count,
				  (int)last_page_y
				  -out->paragraph_lines[i]->line_height);
	}
      } else {
	// Empty paragraphs are just for vspace, so advance accordingly.
	page_y+=body_paragraphs[start_para]->total_height;
	fprintf(stderr,"  vspace of %.1fpts\n",
		body_paragraphs[start_para]->total_height);
      }
      fprintf(stderr,"    %d %d %d (to %d %d %d) actual paragraph height=%.1fpts (%.1f -- %.1fpts on page)\n",
	      start_para,start_line,start_piece,
	      end_para,end_line,end_piece,
	      page_y-prev_page_y,prev_page_y,page_y);
      
      paragraph_clear(out);

      if (end_para==start_para&&end_line==start_line) {
	if (start_piece==end_piece) break;
	start_piece=end_piece+1;
      } else {
	start_piece=0;
	start_line++;
	if (start_line>=body_paragraphs[start_para]->line_count) {
	  start_line=0; start_para++;
	  
	}
      }
    }    

    float actual_page_height=page_y-top_margin;

    // Now layout footnotes and output.
    if (footnotes->current_line) paragraph_append_current_line(footnotes);
    footnotes->noindent=1; footnotes->justifylast=1;
    struct paragraph *laid_out_footnotes=layout_paragraph(footnotes,1);
    
    float footnotes_height=paragraph_height(laid_out_footnotes);
    float saved_page_y=page_y;
    page_y=page_height-bottom_margin-footnotes_height;

    if (footnotes_height) {
      // Draw horizontal rule
      int rule_y=page_y+footnote_rule_ydelta;
      crossref_set_ylimit(rule_y);
      int y=page_height-rule_y;
      HPDF_Page_SetRGBStroke(page, 0.0, 0.0, 0.0);
      HPDF_Page_SetLineWidth(page,footnote_rule_width);
      
      HPDF_Page_MoveTo(page,left_margin,y);
      HPDF_Page_LineTo(page,left_margin+footnote_rule_length,y);
      HPDF_Page_Stroke(page);
    }
    for(int i=0;i<laid_out_footnotes->line_count;i++) {
      // line_dump(laid_out_footnotes->paragraph_lines[i]);
      line_emit(laid_out_footnotes,i,1,1);
    }    
    paragraph_clear(laid_out_footnotes); free(laid_out_footnotes);
    paragraph_clear(footnotes);          
    page_y=saved_page_y;
    
    // Cross-references can go down to to top of footnotes
    crossref_set_ylimit(page_height-bottom_margin-footnotes_height);

    int num_crossrefs=crossref_count;
    
    output_accumulated_cross_references();

    fprintf(stderr,"  actual page was %.1fpts long "
	    "(%d crossrefs, %.1fpts of footnotes).\n",
	    actual_page_height,num_crossrefs,
	    footnotes_height);
    if (fabs(actual_page_height-predicted_page_height)>0.1) {
      fprintf(stderr,"Page length does not match prediction.\n"
	      "This indicates a bug in the page cost calculation code.\n");
      fprintf(stderr,"  predicted length = %.1fpts vs actual length = %.1fpts (+%.1fpts of footnotes)\n",
	      predicted_page_height,
	      actual_page_height,
	      footnotes_height);

      fprintf(stderr,"Backtrace in the viscinity:\n");
      for(int i=next_position-5;i<next_position+5;i++) {
	fprintf(stderr,"  %d : si=%d, penalty=%lld, height=%.1fpts : %d %d %d -- %d %d %d\n",
		i,backtrace[i].start_index,
		backtrace[i].penalty,
		backtrace[i].height,
		backtrace[i].start_para,
		backtrace[i].start_line,
		backtrace[i].start_piece,
		backtrace[i].end_para,
		backtrace[i].end_line,
		backtrace[i].end_piece
		);
      }
      fprintf(stderr,"...\n");
      for(int i=position-5;i<position+5;i++) {
	fprintf(stderr,"  %d : si=%d, penalty=%lld, height=%.1fpts : %d %d %d -- %d %d %d\n",
		i,backtrace[i].start_index,
		backtrace[i].penalty,
		backtrace[i].height,
		backtrace[i].start_para,
		backtrace[i].start_line,
		backtrace[i].start_piece,
		backtrace[i].end_para,
		backtrace[i].end_line,
		backtrace[i].end_piece
		);
      }
      
      int start_position;
      if (next_position>=0) {
	start_para=backtrace[next_position].start_para;
	start_line=backtrace[next_position].start_line;
	start_piece=backtrace[next_position].start_piece;
	start_position=backtrace[next_position].start_index;
      } else {
	start_para=0; start_line=0; start_piece=0;
	start_position=0;
      }
      
      page_score_at_this_starting_point(start_para,start_line,start_piece,
					NULL /* indicates show debug output instead
						of recording backtrace */,
					start_position);
      // Show analysis results for the lines in question.
      while(start_para<end_para||start_line<end_line||start_piece<end_piece) {
	
	if (body_paragraphs[start_para])
	  l=body_paragraphs[start_para]->paragraph_lines[start_line];
	else l=NULL;
	
	int last_piece=end_piece;
	if (end_line!=start_line||end_para!=start_para) {
	  if (l) last_piece=l->piece_count;
	  else last_piece=-1;
	}

	line_analyse(body_paragraphs[start_para],start_line,1);
	
	if (end_para==start_para&&end_line==start_line) {
	  start_piece=end_piece;
	} else {
	  start_piece=0;
	  start_line++;
	  if (start_line>=body_paragraphs[start_para]->line_count) {
	    start_line=0; start_para++;
	  }
	}
      }
    }

    finalise_page();
    
    leftRight=-leftRight;

    //    return 0;
  }

  paragraph_free(out);
  
  return 0;
}
