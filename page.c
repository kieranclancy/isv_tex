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

  int page_count;

  long long penalty;
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
  
  while(1) {
    // Work out cost to here.

    if (!end_piece) {
      if ((end_para!=checkpoint_para)||(end_line!=checkpoint_line)) {
	// We have advanced to a new line, so add last penalty to the
	// cumulative penalty, and also adjust the cumulative height
	cumulative_penalty+=penalty;
	cumulative_height+=height;

	int old_checkpoint_para=checkpoint_para;
	int old_checkpoint_line=checkpoint_line;
	
	checkpoint_para=end_para;
	checkpoint_line=end_line;
	checkpoint_piece=end_piece;

	if (0) {
	  fprintf(stderr,"    checkpoint advanced to %d %d %d (start is %d %d %d)"
		  " penalty=%lld, height=%.1fpts (added %.1fpts)\n",
		  checkpoint_para,checkpoint_line,checkpoint_piece,
		  start_para,start_line,start_piece,
		  cumulative_penalty,cumulative_height,height);
	  fprintf(stderr,"      Line included in checkpoint is: ");
	  if (body_paragraphs[old_checkpoint_para]
	      ->paragraph_lines[old_checkpoint_line])
	    line_dump(body_paragraphs[old_checkpoint_para]
		      ->paragraph_lines[old_checkpoint_line]);
	}
      }
    }
    
    // Stop accumulating page once it is too tall to fit.
    if (cumulative_height>(page_height-top_margin-bottom_margin)) break;
    
    // Get height and penalty of the current piece of the current line.
    struct line_pieces *l=NULL;
    if (body_paragraphs[checkpoint_para])
      l=body_paragraphs[checkpoint_para]->paragraph_lines[checkpoint_line];
    
    if (l&&!l->piece_count) {
      penalty=0;
      height=l->line_height;      
    } else if (l&&l->piece_count) {
      assert(l->metrics->line_pieces==l->piece_count);
      assert(l->metrics->line_pieces>=end_piece);	  
      assert(l->metrics->line_pieces>=checkpoint_piece);
      assert(end_piece>=checkpoint_piece);
      assert(end_line==checkpoint_line);
      penalty=l->metrics->starts[checkpoint_piece][end_piece].penalty;
      height=l->metrics->starts[checkpoint_piece][end_piece].height;

      struct piece *piece=
	piece=&body_paragraphs[end_para]->paragraph_lines[end_line]->pieces[end_piece];
      if (piece->footnote_number) {
	if (first_footnote==-1)	first_footnote=piece->footnote_number;
	last_footnote=piece->footnote_number;
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

    // XXX - Look up height and penalty of footnote paragraph so that it can be
    // taken into account.
    float footnotes_height=footnotes_paragraph_height(first_footnote,last_footnote);
    
    // Look up height of cross-references so that we can stop if they are too
    // tall.  We don't care about the position.
    if (l->piece_count)
      crossrefs_register_line(l,end_piece,end_piece+1, 0);
    if (crossrefs_height>(page_height-top_margin-bottom_margin-footnotes_height))
      break;
        
    // Work out penalty for emptiness of page
    int emptiness=(100*this_height)/(page_height-top_margin-bottom_margin);
    if (emptiness<0) emptiness=100;
    else if (emptiness>100) emptiness=0;
    else emptiness=100-emptiness;
    int emptiness_penalty=16*emptiness*emptiness;
    if (this_height>(page_height-top_margin-bottom_margin))
      emptiness_penalty=100000000;
      
    // XXX Work out penalty based on balance of columns.
    
    assert(penalty>=0);
    assert(cumulative_penalty>=0);
    assert(emptiness_penalty>=0);
    
    long long this_penalty=penalty+cumulative_penalty+emptiness_penalty;
    
    if (this_penalty<best_penalty) {
      best_penalty=this_penalty; best_height=this_height;
    }       	
    
    if (((this_penalty+backtrace[start_position_count-1].penalty)
	 <backtrace[end_position].penalty)
	||(backtrace[end_position].penalty==-1)) {
      backtrace[end_position].start_index=start_position_count-1;
      backtrace[end_position].start_para=start_para;
      backtrace[end_position].start_line=start_line;
      backtrace[end_position].start_piece=start_piece;
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
  
    // Advance to next ending point
    if (body_paragraphs[end_para]) {
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
    } else break;
    
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
	  >=body_paragraphs[start_para]->paragraph_lines[start_line]->piece_count) {
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
  
  while(position>=0) {
    page_positions[page_count++]=position;
    
    long long penalty=backtrace[position].penalty;
    int next_position=backtrace[position].start_index;
    if (next_position>=0) penalty-=backtrace[next_position].penalty;

    if (next_position>=position||next_position<-1) {
      fprintf(stderr,"Illegal step or loop in page optimisation dyanmic programming list.\n");
      exit(-1);
    }
    position=backtrace[position].start_index;
  }

  struct paragraph *out=new_paragraph();
  struct paragraph *footnotes=new_paragraph();
  
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
    
    int end_para=backtrace[position].start_para;
    int end_line=backtrace[position].start_line;
    int end_piece=backtrace[position].start_piece;

    int num_footnotes=0;
    
    fprintf(stderr,"%d (%d %d %d -- %d %d %d) : penalty=%lld, page_count=%d, page_height=%.1fpts\n",
	    next_position+1,
	    start_para,start_line,start_piece,
	    end_para,end_line,end_piece,
	    penalty,
	    backtrace[position].page_count,
	    (next_position>=0)?backtrace[next_position].height:-1
	    );

    struct line_pieces *l=NULL;

    while(start_para<end_para||start_line<end_line||start_piece<end_piece) {

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
	  if (0) {
	    fprintf(stderr,"  line #%d %d..%d: left_margin=%d, max_width=%d\n",
		    i,start,end,
		    out->paragraph_lines[i]->left_margin,
		    out->paragraph_lines[i]->max_line_width);
	    line_dump(out->paragraph_lines[i]);
	  }
	  float last_page_y=page_y;
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
      fprintf(stderr,"    actual paragraph height=%.1fpts (%.1f -- %.1fpts)\n",
	      page_y-prev_page_y,prev_page_y,page_y);
      
      paragraph_clear(out);

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

    float actual_page_height=page_y-top_margin;

    // Now layout footnotes and output.
    if (footnotes->current_line) paragraph_append_current_line(footnotes);
    struct paragraph *laid_out_footnotes=layout_paragraph(footnotes,1);
    
    float footnotes_height=paragraph_height(laid_out_footnotes);
    float saved_page_y=page_y;
    page_y=page_height-bottom_margin-footnotes_height;

    for(int i=0;i<laid_out_footnotes->line_count;i++) {
      line_dump(laid_out_footnotes->paragraph_lines[i]);
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


    finalise_page();
    
    leftRight=-leftRight;    
  }

  paragraph_free(out);
  
  return 0;
}
