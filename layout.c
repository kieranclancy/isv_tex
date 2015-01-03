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

int footnotemark_index=-1;

float cumulative_widths[MAX_LINE_PIECES+1];

int precalc_cumulative_widths(struct line_pieces *l)
{
  cumulative_widths[0]=l->pieces[0].piece_width;
  for(int i=1;i<l->piece_count;i++)
    cumulative_widths[i]=cumulative_widths[i-1]+l->pieces[i].piece_width;
  return 0;
}

float get_basic_width_of_line_segment(struct line_pieces *l,int start,int end,
				      float *cumulative_widths)
{
  // for(i=start;i<end;i++) line_width+=l->pieces[i].piece_width;
  if (start)
    return cumulative_widths[end-1]-cumulative_widths[start-1];
  else
    return cumulative_widths[end-1];
}

#define MAX_TOTAL 1024
int *emptiness_lists[MAX_TOTAL]={NULL};

int calc_emptiness(int used,int total)
{
  if (total<0||total>=MAX_TOTAL) {
    fprintf(stderr,"calc_emptiness() called with invalid arg(s): %d,%d\n",
	    used,total);
    exit(-1);
  }
  
  if(!emptiness_lists[total]) {
    emptiness_lists[total]=malloc(sizeof(int)*(total+1));
    for(int i=0;i<=total;i++)
      emptiness_lists[total][i]=100-(100.0*i/total);
  }

  if (used>=total) used=total;
  if (used<0) used=0;
  return emptiness_lists[total][used];
}

int emptiness_penalty[101];

int layout_precalc_emptiness_penalties()
{
  int i;
  for(i=0;i<101;i++) emptiness_penalty[i]=i*i;
  return 0;
}

int layout_calculate_segment_cost(struct paragraph *p,
				  struct line_pieces *l,
				  int start,int end, int line_count,
				  float *cumulative_widths)
{
  float line_width=0;
  
  if (footnotemark_index==-1)
    footnotemark_index=set_font_by_name("footnotemark");

  // Skip leading spaces
  while((start<end)&&(l->pieces[start].piece_is_elastic)) start++;
  
  // Calculate width of the segment.
  // (.piece_width already incorporates any hanging)
  line_width=get_basic_width_of_line_segment(l,start,end,cumulative_widths);

  // Related to the above, we must discount the width of a dropchar if it is
  // followed by left-hangable material.  This only applies to absolute 2nd
  // piece.
  if ((l->pieces[0].font->line_count>1)
      &&(start==1)
      &&(line_count<=(l->pieces[0].font->line_count-1)))
    {
      float discount=0;
      
      // Discount any footnote
      if (l->pieces[0].font==&type_faces[footnotemark_index]) {
	discount+=l->pieces[1].natural_width;
      }
      discount+=calc_left_hang(l,1);
      
      line_width-=discount;

    }

  // fprintf(stderr,"  segment width is %.1fpts\n",line_width);

  // Work out column width
  float column_width=page_width-left_margin-right_margin;

  if (start==0&&l==p->paragraph_lines[0]) {
    // First line of a paragraph is indented.
    column_width-=paragraph_indent;
  }
  
  // Deduct drop char margin from line width if required.
  if (l->pieces[0].font->line_count>1) {
    // Drop char at beginning of chapter
    if (line_count&&line_count<=(l->pieces[0].font->line_count-1)) {
      int max_hang_space
	=right_margin
	-crossref_margin_width-crossref_column_width
	-2;  // plus a little space to ensure some white space
      column_width-=l->pieces[0].natural_width+max_hang_space;
    }
  }

  // Similarly adjust margin for poetry
  if (l->poem_level) {
    int poem_indent=0;
    poem_indent+=poetry_left_margin;
    poem_indent+=l->poem_level*poetry_level_indent;
    // Apply poetry wrap for 2nd and subsequent lines
    if (start) poem_indent+=poetry_wrap_indent;
    column_width-=poem_indent;
  }
  
  // Fail if line is too wide for column
  int penalty=0;
  if (line_width>column_width) penalty+=1000000;
  else {
    // Else work out penalty based on fullness of line
    int emptiness=calc_emptiness(line_width,column_width);
    if (0) fprintf(stderr,"    line_width=%.1fpts, column_width=%.1fpts, emptiness=%d%%\n",
		   line_width,column_width,emptiness);
    penalty=emptiness_penalty[emptiness];
  }

  // Then adjust penalty for bad things, like starting the line with punctuation
  // or a non-breaking space.

  if (start<end) {

    // line_append_piece sets the nobreak flag on the preceeding piece
    // if it shouldn't be split for any reason.  That way we don't need
    // to keep recalculating stuff in here.
#ifdef NOT_DEFINED
    // Don't allow breaking of non-breakable spaces
    // Or starting lines with various sorts of punctuation
    switch((unsigned char)l->pieces[start].piece[0]) {
    case 0xa0: case ',': case '.': case '\'':
      penalty+=1000000;
      break;
    }
    // Or with footnote marks
    if (l->pieces[start].font==&type_faces[footnotemark_index])
      penalty+=1000000;
#endif
    // Don't allow line breaks following non-breakable pieces
    if (start&&l->pieces[start-1].nobreak) penalty+=1000000;
  }

  return penalty;
}


int precalc_hang_width(struct line_pieces *l,int i)
{
  int piece_width=l->pieces[i].natural_width;
    
    if (i  // make sure there is a previous piece
	&&l->pieces[i].font==&type_faces[footnotemark_index]) {
      // This is a footnote mark.
      // Check if the preceeding piece ends in any low punctuation marks.
      // If so, then discount the width of that piece by the width of such
      // punctuation. If the width of the discount is wider than this footnotemark,
      // then increase the width of this footnotemark so that the end of text
      // position is advanced correctly.
      //      fprintf(stderr,"hanging footnotemark over punctuation: ");
      //      line_dump(l);
      char *text=l->pieces[i-1].piece;
      int o=strlen(text);
      char *hang_text=NULL;
      while(o>0) {
	switch(text[o-1]) {	    
	case '.': case ',':
	case '-': case ' ': 
	  hang_text=&text[--o];
	  continue;
	}
	break;
      }
      set_font(l->pieces[i-1].font);
      float hang_width=0;
      if (hang_text) hang_width=HPDF_Page_TextWidth(page,hang_text);
      // Discount the width of this piece based on hang_width
      if (hang_width>piece_width) piece_width=0;
      else piece_width=piece_width-hang_width;

      // fprintf(stderr,"  This is the punctuation over which we are hanging the footnotemark: [%s] (%.1fpts)\n",hang_text,hang_width);
      // fprintf(stderr,"Line after hanging footnote over punctuation: ");
      // line_dump_segment(l,start,end);
    }
    l->pieces[i].piece_width=piece_width;
    return 0;
}

/*
  To layout a long line, we need to know the cost of every
  possible section of the line, and populate a dynamic programming
  list with the cost of each such line, keeping the lowest cost
  options.

  The information we need to keep for each token position in the line is:
  1. Cost from this point to the start of the long line.
  2. The next token position towards the start of the line that was used
     to generate this cost.
  3. Number of lines already output, so that we can work out the text 
     width when drop-characters are involved.

*/
int layout_line(struct paragraph *p, int line_number,
		int start, int end,
		struct paragraph *out, int drawingPage)
{
  struct line_pieces *l=p->paragraph_lines[line_number];
  
  int a,b;

  int costs[MAX_LINE_PIECES+1];
  int next_steps[MAX_LINE_PIECES+1];
  int line_counts[MAX_LINE_PIECES+1];

  // Copy empty lines verbatim (mostly present only for vspace)
  if (l&&(!l->piece_count)) {
    l=line_clone(l);
    if (!l||(out->line_count>=MAX_LINE_PIECES)) {
      fprintf(stderr,"line_clone() returned NULL or too many lines in paragraph\n");
      exit(-1);
    }
    out->paragraph_lines[out->line_count++]=l;
    return 0;
  }
  
  // Start out with infinite costs and no steps
  for(a=0;a<=l->piece_count;a++) {
    // make costs very hight by default, but not so high as to cause over-flows
    costs[a]=0x70000000;
    next_steps[a]=-1;
    line_counts[a]=0;
  }
  // Cost from start to start is 0
  for(a=0;a<=start;a++) {
    costs[a]=0; next_steps[a]=a-1;
  }

  // pre-calculate hang width of every piece
  for(a=0;a<l->piece_count;a++) precalc_hang_width(l,a);
  precalc_cumulative_widths(l);

  layout_precalc_emptiness_penalties();
  
  // Calculate costs of every possible segment
  for(a=start;a<l->piece_count;a++) {    
    for(b=a+1;b<=end;b++) {
      int line_count=0;
      line_count=line_counts[a];
      int segment_cost=layout_calculate_segment_cost(p,l,a,b,line_count,
						     cumulative_widths);
      if (segment_cost==-1) break;  
      if (0) fprintf(stderr,"  segment cost of %d..%d is %d (combined cost = %d)\n",
		     a,b,segment_cost,segment_cost+costs[a]);
      // Stop looking when line segment is too long
      if (segment_cost+costs[a]<costs[b]) {
	// fprintf(stderr,"    this beats the old cost of %d\n",costs[b]);
	costs[b]=segment_cost+costs[a];
	next_steps[b]=a;
	line_counts[b]=line_counts[a]+1;
      }
    }
  }

  // Count number of lines in optimal layout.
  int num_lines=0;
  int position=end;
  while(position>start) {
    if (next_steps[position]>=position) {
      fprintf(stderr,"Circular path in %s(): next_steps[%d]=%d\n",
	      __FUNCTION__,position,next_steps[position]);
      for(int i=0;i<=l->piece_count;i++) {
	fprintf(stderr,"%d..%d : cost %d (next step=0x%08x)\n",
		next_steps[i],i,costs[i],next_steps[i]);
      }
      exit(-1);
    }

    num_lines++;
    position=next_steps[position];
  }

  // Add cost of paragraph to page penalty.
  page_penalty_add(costs[end]);
  
  
  // Build list of lines by working backwards through the paragraph.
  // Build the list of lines first, then go through them forwards, so that
  // line numbers are allocated forwards, and avoid confusing the footnote
  // number handling (this is only an interim problem, because once we go
  // to optimal page boundary selection only the footnotes for the current
  // page will be rendered on the current page).

  int positions[end+1];
  int pcount=0;
  position=end;
  while(position>start) {
    if (position<start||next_steps[position]<start||costs[position]==0x70000000) {
      // Illegal step.
      // Dump path
      fprintf(stderr,"Path contains illegal step at #%d\n",position);
      for(int i=start;i<=end;i++) {
	fprintf(stderr,"%d..%d : cost %d (next step=0x%08x)\n",
		next_steps[i],i,costs[i],next_steps[i]);
      }
      exit(-1);   
    }
    positions[pcount++]=position;
    position=next_steps[position];
  }

  {
    // fprintf(stderr,">> Optimal long-line layout is:\n");
    int line_count=0;
    int k;
    for(k=pcount-1;k>=0;k--) {
      position=positions[k];
      
      if (0) {
	fprintf(stderr,"Segment at position %d..%d (cost %d): ",
		next_steps[position],position,costs[position]);
	line_dump_segment(l,next_steps[position],position);
      }
      
      // Build line
      struct line_pieces *lout=new_line();
      lout->alignment=l->alignment;
      lout->tied_to_next_line=l->tied_to_next_line;
      lout->line_uid=line_uid_counter++;
      lout->max_line_width=page_width-left_margin-right_margin;
      for(int i=next_steps[position];i<position;i++) {
	line_append_piece(lout,&l->pieces[i]);
	
	// XXX - Use the largest indent specified by any of these
	
	// Add left margin for start of paragraph
	if ((line_number==0)&&(next_steps[position]<1)) {
	  lout->left_margin=paragraph_indent;
	  lout->max_line_width
	    =page_width-left_margin-right_margin-paragraph_indent;
	}
	// Add left margin for any dropchar
	// (but not for the line with the dropchar)
	if (l->pieces[0].font->line_count>1)
	  if (line_count&&(line_count<l->pieces[0].font->line_count)) {
	    lout->left_margin=l->pieces[0].natural_width;
	    lout->max_line_width
	      =page_width-left_margin-right_margin-l->pieces[0].natural_width;
	  }
	// Similarly adjust margin for poetry
	if (l->poem_level) {
	  int poem_indent=0;
	  poem_indent+=poetry_left_margin;
	  poem_indent+=l->poem_level*poetry_level_indent;
	  // Apply poetry wrap for 2nd and subsequent lines
	  if (line_count) poem_indent+=poetry_wrap_indent;
	  lout->left_margin=poem_indent;
	  lout->max_line_width=page_width-left_margin-poem_indent;
	}
      }
      
      // Insert it into the output paragraph
      paragraph_insert_line(out,out->line_count,lout);
      if (0) {
	fprintf(stderr,"Laid out line: ");
	line_dump(lout);
      }
      
      line_count++;
    }
  }
  
  // fprintf(stderr,"<<\n");
  
  return costs[end];
}

struct paragraph *layout_paragraph(struct paragraph *p, int drawingPage)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (0) fprintf(stderr,"  page_width=%d, left_margin=%d, right_margin=%d\n",
		 page_width,left_margin,right_margin);
  
  int i;

  // Each line in the paragraph now corresponds to a "long line", which may
  // be either the entire paragraph, or a block of text in the paragraph
  // that can all be flowed together.

  struct paragraph *out=new_paragraph();

  if (p->src_book) out->src_book=strdup(p->src_book);
  out->src_chapter=p->src_chapter;
  out->src_verse=p->src_verse;

  // Lay out each line
  for(i=0;i<p->line_count;i++)
    layout_line(p,i,
		0,p->paragraph_lines[i]->piece_count,
		out,drawingPage);

  return out;
}
