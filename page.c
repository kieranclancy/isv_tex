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
  long long cumulative_penalty=0;
  float cumulative_height=0;
  
  long long best_penalty=0x7ffffff;
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

	cumulative_penalty+=penalty;
	cumulative_height+=height;

	int old_checkpoint_para=checkpoint_para;
	int old_checkpoint_line=checkpoint_line;

	determinism_test_integer(checkpoint_para);
	determinism_test_integer(checkpoint_line);

	checkpoint_para=end_para;
	checkpoint_line=end_line;
	checkpoint_piece=end_piece;

	if (!backtrace) {
	  fprintf(stderr,"    checkpoint advanced to %d %d %d (start is %d %d %d)"
		  " penalty=%lld, height=%.1fpts (after adding %.1fpts). (%.1f -- %.1f pts on page)\n",
		  checkpoint_para,checkpoint_line,checkpoint_piece,
		  start_para,start_line,start_piece,
		  cumulative_penalty,cumulative_height,height,
		  cumulative_height-height+top_margin,
		  cumulative_height+top_margin);
	  fprintf(stderr,"      Line included in checkpoint is: ");
	  if (body_paragraphs[old_checkpoint_para]
	      ->paragraph_lines[old_checkpoint_line])
	    line_dump(body_paragraphs[old_checkpoint_para]
		      ->paragraph_lines[old_checkpoint_line]);
	}
      }
    }
    
    // Stop accumulating page once it is too tall to fit.
    if (cumulative_height>(page_height-top_margin-bottom_margin)) {
      determinism_test_float(cumulative_height);
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
      penalty=0;
      height=l->line_height;
      determinism_test_integer(penalty);
      determinism_test_float(height);
    } else if (l&&l->piece_count) {
      assert(l->metrics->line_pieces==l->piece_count);
      assert(l->metrics->line_pieces>=end_piece);	  
      assert(l->metrics->line_pieces>=checkpoint_piece);
      assert(end_piece>=checkpoint_piece);
      assert(end_line==checkpoint_line);
      penalty=l->metrics->starts[checkpoint_piece][end_piece].penalty;
      height=l->metrics->starts[checkpoint_piece][end_piece].height;
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
        
    // Work out penalty for emptiness of page
    int emptiness=(100*this_height)/(page_height-top_margin-bottom_margin);
    if (emptiness<0) emptiness=100;
    else if (emptiness>100) emptiness=0;
    else emptiness=100-emptiness;
    int emptiness_penalty=16*emptiness*emptiness;
	// Over-filling the page is BAD
    if (this_height>(page_height-top_margin-bottom_margin))
      emptiness_penalty=100000000;
    else if (page_height<=(top_margin+bottom_margin+this_height+footnotes_height)) {
      emptiness_penalty=100000000;
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
    if (start_position_count>0) {
      if ((backtrace[start_position_count-1].page_count % 2) == 1) {
	// Right page, so work out balance penalty:
	// = square of difference in height of text blocks
	balance_penalty=1000*
	  (backtrace[start_position_count-1].height-this_height)
	  *(backtrace[start_position_count-1].height-this_height);	  
	  }
    }
    
    assert(penalty>=0);
    assert(cumulative_penalty>=0);
    assert(emptiness_penalty>=0);
    
    long long this_penalty=penalty+cumulative_penalty
      +emptiness_penalty+balance_penalty;
    
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
	fprintf(stderr,"  rendering para #%d line #%d : page_width=%d-%d-%d,"
		" pre-computed page height=%.1fpts, pre-computed para height=%.1fpts"
		" (ptr=%p)"
		" (pieces %d..%d)\n    ",
		start_para,start_line,page_width,left_margin,right_margin,
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
