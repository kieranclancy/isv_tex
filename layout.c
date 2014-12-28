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
      float all_width=l->pieces[i-1].natural_width;
      piece_width=all_width-hang_width;
      if (hang_width>piece_width) piece_width=hang_width;
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
      &&(line_count<(l->pieces[0].font->line_count-1)))
    {
      float discount=0;
      
      // Discount any footnote
      if (l->pieces[0].font==&type_faces[footnotemark_index]) {
	discount+=l->pieces[1].natural_width;
      }
      discount+=calc_left_hang(l,1);
      
      line_width-=discount;

      // Add the width of the drop char margin
      line_width+=p->drop_char_left_margin;
    }

  // fprintf(stderr,"  segment width is %.1fpts\n",line_width);
  
  // Fail if line is too wide
  float column_width=page_width-left_margin-right_margin;
  if (line_width>column_width) return -1;
  
  // Else work out penalty based on fullness of line
  float fullness=line_width*100.0/column_width;
  int penalty=(100-fullness)*(100-fullness);  

  // Then adjust penalty for bad things, like starting the line with punctuation
  // or a non-breaking space.
  
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

  int costs[MAX_LINE_PIECES];
  int next_steps[MAX_LINE_PIECES];
  int line_counts[MAX_LINE_PIECES];
  
  // Start out with infinite costs and no steps
  for(a=0;a<l->piece_count;a++) {
    costs[a]=0x7fffffff;
    next_steps[a]=-1;
    line_counts[a]=0;
  }

  // Calculate costs of every possible segment
  for(a=0;a<l->piece_count;a++) {    
    for(b=a+1;b<=l->piece_count;b++) {
      int line_count=0;
      line_count=line_counts[a];
      int segment_cost=layout_calculate_segment_cost(p,l,a,b,line_count);
      if (0) fprintf(stderr,"  segment cost of %d..%d is %d\n",
		     a,b,segment_cost);
      // Stop looking when line segment is too long
      if (segment_cost==-1) break;  
      if (segment_cost+costs[a]<costs[b]) {
	costs[b]=segment_cost+costs[a];
	next_steps[b]=a;
      }
    }
  }

  // Build list of lines by working backwards through the paragraph.
  fprintf(stderr,">> Optimal long-line layout is:\n");
  int position=l->piece_count;
  int out_line_number=out->line_count;
  int line_count=0;
  while(position>0) {
    fprintf(stderr,"Segment at position %d..%d: ",next_steps[position],position);
    line_dump_segment(l,next_steps[position],position);
    if (next_steps[position]>=position) {
      fprintf(stderr,"Circular path in %s()\n",__FUNCTION__);
      exit(-1);
    }

    // Build line
    struct line_pieces *lout=calloc(sizeof(struct line_pieces),1);
    lout->alignment=l->alignment;
    lout->line_uid=line_uid_counter++;
    lout->max_line_width=l->max_line_width;
    for(int i=next_steps[position];i<position;i++) {
      line_append_piece(lout,&l->pieces[i]);
      if (!strcasecmp(l->pieces[i].font->font_nickname,"footnotemark")) {
	// update footnote entry to say it is attached to this line UID
	for(int i=0;i<footnote_count;i++) {
	  if (!strcmp(l->pieces[i].piece,
		      footnote_paragraphs[i]
		      .paragraph_lines[0]
		      ->pieces[0].piece)) {
	    footnote_line_numbers[i]=lout->line_uid;
	    break;
	  }	  
	}
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
