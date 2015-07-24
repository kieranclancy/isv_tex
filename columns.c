/*
  Generate a PDF of the ISV bible (or another translation).

  Note that the text of the ISV is copyright, and is not part of this
  program, even it comes bundled together, and thus is not touched by
  the GPL.  

  This program is copyright Paul Gardner-Stephen 2015, and is offered
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

struct column_cache_entry {
  int start_para;
  int start_line;
  int start_piece;
  int split_para;
  int split_line;
  int split_piece;
  long long penalty;
  float height;
};

struct column_cache_entry column_cache[0x100000];

int count=0;
int hits=0;
int misses=0;

int column_get_height_and_penalty(int start_para,int start_line,int start_piece,
				  int split_para,int split_line,int split_piece,
				  long long *penalty,float *height)
{
  // Layout the slab of text and see how high it is, and what the penalty it incurs is.
  *penalty=0; *height=0;

  int cache_index =
    (((start_para+9)<<14)
     ^
     ((start_line+23)<<16)
     ^
     ((start_piece+91)<<0)
     ^
     ((split_para+1)<<17)
     ^
     ((split_line+89)<<15)
     ^
     ((split_piece+73)<<6))
    &0xfffff;
    ;

    count++;
    if (count==65536) {
      printf("Column balancing %d:%d.%d : %d hits, %d misses.\n",
	     start_para,start_line,start_piece,hits,misses); fflush(stdout);
      hits=0; misses=0; count=0;
    }
    
    if ((column_cache[cache_index].start_piece==start_piece)
	&&(column_cache[cache_index].split_piece==split_piece)
	&&(column_cache[cache_index].start_line==start_line)
	&&(column_cache[cache_index].split_line==split_line)
	&&(column_cache[cache_index].start_para==start_para)
	&&(column_cache[cache_index].split_para==split_para))
      {
	*penalty=column_cache[cache_index].penalty;
	*height=column_cache[cache_index].height;
	hits++;
	return 0;
      } else misses++;
    
  
  int first_line,first_piece;
  int last_line,last_piece;
  
  for(int para = start_para;para<=split_para;para++)
    {
      if (para==start_para) {
	// Make partial paragraph from the start point.
	first_line=start_line;
	first_piece=start_piece;
	last_line=body_paragraphs[para]->line_count-1;
	struct line_pieces *l=body_paragraphs[para]->paragraph_lines[last_line];
	last_piece=l->piece_count;
      } else if (para==split_para) {
	// Make partial paragraph to the end point.
	last_line=split_line;
	last_piece=split_piece;
      } else {
	// Entire paragraph
	first_line=0;
	first_piece=0;
	last_line=body_paragraphs[para]->line_count-1;
	struct line_pieces *l=body_paragraphs[para]->paragraph_lines[last_line];
	last_piece=l->piece_count;
      }
      if (last_line>body_paragraphs[para]->line_count-1)
	last_line=body_paragraphs[para]->line_count-1;

      struct paragraph *out=new_paragraph();
      for(int line=first_line;line<=last_line;line++)
	{
	  struct paragraph *p=body_paragraphs[para];
	  struct line_pieces *l=p->paragraph_lines[line];
	  int start=0;
	  int end=l->piece_count-1;
	  if (line==first_line) start=first_piece;
	  if (line==last_line) {
	    end=last_piece;
	  }
	  if (end>=l->piece_count) end=l->piece_count-1;

	  // layout_line calls page_penalty_add() to update the page penalty.
	  // But here we are subjugating it to calculate our column layout, so
	  // we need to save page_penalty, and then put it back after, taking the
	  // penalty we want from page_penalty

	  long long saved_page_penalty=page_penalty;
	  page_penalty=0;
	  
	  layout_line(p,line,start,end,out,0);
	  // fprintf(stderr,"  laying out %d:%d.%d-%d\n",para,line,start,end);
	  
	  (*penalty)+=page_penalty;

	  page_penalty=saved_page_penalty;
	}
      (*height)+=paragraph_height(out);
      paragraph_free(out);

    }
  
  if (0)
    fprintf(stderr,"column %d:%d.%d - %d:%d.%d : cost=%lld, height=%.1fpts\n",
	    start_para,start_line,start_piece,
	    split_para,split_line,split_piece,
	    *penalty,*height);

  /* XXX - Record column rendering information so that we can recall it later.
     The challenge here is that the para, line and piece positions cannot be
     trivially converted to a single scalar that can be used as an index in a
     cache.  That said, on the basis that a page should not contain more than 64K
     pieces, and we only need to cache a page at a time, and the occassional cache
     collision is not a big deal, we can calculate a quick and dirty cache index
     based on the six values and use that.
   */

  column_cache[cache_index].start_para=start_para;
  column_cache[cache_index].start_line=start_line;
  column_cache[cache_index].start_piece=start_piece;
  column_cache[cache_index].split_para=split_para;
  column_cache[cache_index].split_line=split_line;
  column_cache[cache_index].split_piece=split_piece;
  column_cache[cache_index].penalty=*penalty;
  column_cache[cache_index].height=*height;
  
  return 0;
}

int column_height_and_penalty_cache_advance_to(int start_para,int start_line,
					       int start_piece)
{
  return 0;
}
