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

int layout_calculate_segment_cost(struct paragraph *p,
				  struct line_pieces *l,
				  int start,int end, int line_count)
{
  float line_width=0;
  float piece_width=0;
  
  int i;
  int footnotemark_index=set_font("footnotemark");
  
  // Calculate width of the segment.
  for(i=start;i<end;i++) {
    piece_width=l->pieces[i].natural_width;
    
    if ((i>start)  // make sure there is a previous piece
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
      set_font(l->pieces[i-1].font->font_nickname);
      float hang_width=0;
      if (hang_text) hang_width=HPDF_Page_TextWidth(page,hang_text);
      // Discount the width of this piece based on hang_width
      if (hang_width>piece_width) piece_width=0;
      else piece_width=piece_width-hang_width;

      // fprintf(stderr,"  This is the punctuation over which we are hanging the footnotemark: [%s] (%.1fpts)\n",hang_text,hang_width);
      // fprintf(stderr,"Line after hanging footnote over punctuation: ");
      // line_dump_segment(l,start,end);
    }

    // XXX - Ignore width of any leading or trailing spaces
    
    line_width+=piece_width;

  }

  // Related to the above, we must discount the width of a dropchar if it is
  // followed by left-hangable material.  This only applies to absolute 2nd
  // piece.
  if ((l->pieces[0].font->line_count>1)
      &&(i==1)
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

  // Fail if line is too wide for column
  if (line_width>column_width) return -1;
  
  // Else work out penalty based on fullness of line
  float fullness=line_width*100.0/column_width;
  if (0) fprintf(stderr,"    line_width=%.1fpts, column_width=%.1fpts, fullness=%.1f%%\n",
	  line_width,column_width,fullness);
  int penalty=(100-fullness)*(100-fullness);

  // No penalty for short lines in the last line of a paragraph
  if (p->line_count&&l==p->paragraph_lines[p->line_count-1])
    if (end==l->piece_count) penalty=0;

  // Then adjust penalty for bad things, like starting the line with punctuation
  // or a non-breaking space.

  // Skip leading spaces
  i=start;
  while((i<end)&&(l->pieces[i].piece[0]==' ')) i++;

  if (i<end) {
    // Don't allow breaking of non-breakable spaces
    if (((unsigned char)l->pieces[i].piece[0])==0xa0) penalty+=1000000;
    // Or starting lines with various sorts of punctuation
    
    if (((unsigned char)l->pieces[i].piece[0])==',') penalty+=1000000;
    if (((unsigned char)l->pieces[i].piece[0])=='.') penalty+=1000000;
    if (((unsigned char)l->pieces[i].piece[0])=='\'') penalty+=1000000;
    
    if (!strcasecmp(l->pieces[i].font->font_nickname,"footnotemark"))
      penalty+=1000000;

    // Don't allow line breaks following non-breakable pieces
    if (i&&l->pieces[i-1].nobreak) penalty+=1000000;
  }

  
  return penalty;
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
int layout_line(struct paragraph *p,int line_number,struct paragraph *out)
{
  struct line_pieces *l=p->paragraph_lines[line_number];
  
  int a,b;

  int costs[MAX_LINE_PIECES+1];
  int next_steps[MAX_LINE_PIECES+1];
  int line_counts[MAX_LINE_PIECES+1];
  
  // Start out with infinite costs and no steps
  for(a=0;a<=l->piece_count;a++) {
    // make costs very hight by default, but not so high as to cause over-flows
    costs[a]=0x70000000;
    next_steps[a]=-1;
    line_counts[a]=0;
  }
  // Cost from start to start is 0
  costs[0]=0; next_steps[0]=1;

  // Calculate costs of every possible segment
  for(a=0;a<l->piece_count;a++) {    
    for(b=a+1;b<=l->piece_count;b++) {
      int line_count=0;
      line_count=line_counts[a];
      int segment_cost=layout_calculate_segment_cost(p,l,a,b,line_count);
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
  int position=l->piece_count;
  while(position>0) {
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

  
  // Build list of lines by working backwards through the paragraph.
  fprintf(stderr,">> Optimal long-line layout is:\n");
  int out_line_number=out->line_count;
  int line_count=0;
  position=l->piece_count;
  while(position>0) {
    fprintf(stderr,"Segment at position %d..%d (cost %d): ",
	    next_steps[position],position,costs[position]);
    line_dump_segment(l,next_steps[position],position);

    // Build line
    struct line_pieces *lout=calloc(sizeof(struct line_pieces),1);
    lout->alignment=l->alignment;
    lout->line_uid=line_uid_counter++;
    lout->max_line_width=page_width-left_margin-right_margin;
    for(int i=next_steps[position];i<position;i++) {
      line_append_piece(lout,&l->pieces[i]);
      if (!strcasecmp(l->pieces[i].font->font_nickname,"footnotemark")) {
	// update footnote entry to say it is attached to this line UID
	for(int j=0;j<footnote_count;j++) {
	  if (!strcmp(l->pieces[i].piece,
		      footnote_paragraphs[j]
		      .paragraph_lines[0]
		      ->pieces[0].piece)) {
	    footnote_line_numbers[j]=lout->line_uid;
	    break;
	  }	  
	}
      }
      // Add left margin for start of paragraph
      if ((line_number==0)&&(next_steps[position]<1)) {
	lout->left_margin=paragraph_indent;
	    lout->max_line_width
	      =page_width-left_margin-right_margin-paragraph_indent;
      }
      // Add left margin for any dropchar
      if (l->pieces[0].font->line_count>1)
	if (line_count<(num_lines-1))
	  if ((num_lines-line_count)<=l->pieces[0].font->line_count) {
	    lout->left_margin=l->pieces[0].natural_width;
	    lout->max_line_width
	      =page_width-left_margin-right_margin-l->pieces[0].natural_width;
	  }
    }

    // Insert it into the output paragraph
    paragraph_insert_line(out,out_line_number,lout);
    fprintf(stderr,"Laid out line: ");
    line_dump(lout);
    
    position=next_steps[position];
    line_count++;
  }  
  
  fprintf(stderr,"<<\n");
  
  return 0;
}

struct paragraph *layout_paragraph(struct paragraph *p)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  fprintf(stderr,"  page_width=%d, left_margin=%d, right_margin=%d\n",
	  page_width,left_margin,right_margin);
  
  int i;

  // Each line in the paragraph now corresponds to a "long line", which may
  // be either the entire paragraph, or a block of text in the paragraph
  // that can all be flowed together.

  struct paragraph *out=calloc(sizeof(struct paragraph),1);

  if (p->src_book) out->src_book=strdup(p->src_book);
  out->src_chapter=p->src_chapter;
  out->src_verse=p->src_verse;
  
  for(i=0;i<p->line_count;i++) layout_line(p,i,out);

  return out;
}
