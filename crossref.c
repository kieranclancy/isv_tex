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

char *crossreference_book=NULL;
char *crossreference_chapter=NULL;
char *crossreference_verse=NULL;
int crossreference_mode=0;

int saved_left_margin;
int saved_right_margin;

// Minimum vertical space between crossreference paragraphs
int crossref_min_vspace=4;
// Width of crossref column
int crossref_column_width=36;
int crossref_margin_width=4;

int crossreference_start()
{
  // There is actually nothing that needs to be done here.
  fprintf(stderr,"Starting to collect cross-references for %s %s:%s\n",
	  crossreference_book,
	  crossreference_chapter,
	  crossreference_verse);

  saved_left_margin=left_margin;
  saved_right_margin=right_margin;

  left_margin=0;
  right_margin=page_width-crossref_column_width;

  target_paragraph=calloc(sizeof(struct paragraph),1);
  paragraph_clear(target_paragraph);

  paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("crossrefmarker"));

  // Begin cross-reference paragraph with marker
  if (crossreference_chapter>0) {
    paragraph_append_text(target_paragraph,crossreference_chapter,0,0); 
    paragraph_append_text(target_paragraph,":",0,0); 
  }
  paragraph_append_text(target_paragraph,crossreference_verse,0,0); 
  paragraph_append_text(target_paragraph," ",0,0); 

  paragraph_pop_style(target_paragraph);
  paragraph_push_style(target_paragraph,AL_JUSTIFIED,set_font("crossref"));

  
  crossreference_mode=1;

  return 0;
}

struct paragraph *crossref_hash_bins[0x10000];

int crossref_hashtable_init()
{
  int i;
  for(i=0;i<0x10000;i++) crossref_hash_bins[i]=NULL;
  return 0;
}

int crossref_calc_hash(char *book,int chapter,int verse)
{
  int hash=(chapter)*256+verse;
  hash&=0xffff;
  return hash;
}

int crossreference_end()
{
  // Clone paragraph, set info, and add into hash table of
  // cross-reference paragraphs
  struct paragraph *c=target_paragraph;

  current_line_flush(c);

  c->src_book=strdup(crossreference_book);
  c->src_chapter=atoi(crossreference_chapter);
  c->src_verse=atoi(crossreference_verse);
  c->total_height=paragraph_height(c);

  // Link into cross-reference hash table
  int bin=crossref_calc_hash(crossreference_book,
			     atoi(crossreference_chapter),
			     atoi(crossreference_verse));
  c->next=crossref_hash_bins[bin];
  crossref_hash_bins[bin]=c;
  fprintf(stderr,"Crossrefs for %s %d:%d in bin %d\n",
	  c->src_book,c->src_chapter,c->src_verse,bin);
  paragraph_dump(c);

  paragraph_pop_style(c);
  
  free(crossreference_book); crossreference_book=NULL;
  free(crossreference_chapter); crossreference_chapter=NULL;
  free(crossreference_verse); crossreference_verse=NULL;

  target_paragraph=&body_paragraph;
  
  crossreference_mode=0;
  left_margin=saved_left_margin;
  right_margin=saved_right_margin;

  return 0;
}

struct paragraph *crossreference_find(char *book,int chapter, int verse)
{
  struct paragraph *p=crossref_hash_bins[crossref_calc_hash(book,chapter,verse)];
  while(p) {
    if ((!strcasecmp(p->src_book,book))
	&&(chapter==p->src_chapter)
	&&(verse==p->src_verse)) {
      fprintf(stderr,"Found crossref passage for %s %d:%d (%dpts high)\n",
	      book,chapter,verse,p->total_height);
      return p;
    }
    p=p->next;
  }
  return NULL;
}

int crossreference_register_verse(struct paragraph *p,
				  char *book,int chapter, int verse)
{
  fprintf(stderr,"Saw %s %d:%d\n",book,chapter,verse);
  struct paragraph *c=crossreference_find(book,chapter,verse);
  p->current_line->pieces[p->current_line->piece_count-1].crossrefs=c;
  
  return 0;
}

struct paragraph *crossrefs_queue[MAX_VERSES_ON_PAGE];
int crossrefs_y[MAX_VERSES_ON_PAGE];
int crossref_count=0;

int crossref_queue(struct paragraph *p, int y)
{
  if (crossref_count<MAX_VERSES_ON_PAGE) {
    crossrefs_queue[crossref_count]=p;
    crossrefs_y[crossref_count++]=y;
  } else {
    fprintf(stderr,"Too many verses with cross-references on the same page.\n");
    exit(-1);
  }
  return 0;
}

int crossref_queue_dump(char *msg)
{
  fprintf(stderr,"Cross-ref paragraph list (%s):\n",msg);
  int i;
  for(i=0;i<crossref_count;i++)
    {
      fprintf(stderr,"  %-2d @ %d -- %d\n",
	      i,crossrefs_y[i],crossrefs_y[i]+crossrefs_queue[i]->total_height);
    }
  return 0;
}

/* Spread out crossreference paragraphs so that they don't overlap.
   We will use a simple approach for now:
   1. From the top to bottom of page, if a cross-ref paragraph will 
   overlap with the one above it, then shift it down so that it doesn't.
   2. Do the same from the bottom to the top.
*/
int crossref_y_limit=0;
int crossref_set_ylimit(int y)
{
  crossref_y_limit=y;
  return 0;
}

int crossrefs_reposition()
{
  int i;

  if (!crossref_count) return 0;
  
  crossref_queue_dump("initial");
  for(i=1;i<crossref_count;i++)
    {
      int prev_bottom=crossrefs_y[i-1]+crossrefs_queue[i-1]->total_height;
      int this_top=crossrefs_y[i];
      int overlap=prev_bottom-this_top+crossref_min_vspace;
      if (overlap>0) {
	fprintf(stderr,"Moving crossref %d down %dpts\n",i,overlap);
	crossrefs_y[i]+=overlap;
      }      
    }
  crossref_queue_dump("after top down");

  // Make sure that cross-refs don't appear beside the footnote paragraph
  if (crossrefs_y[crossref_count-1]+crossrefs_queue[crossref_count-1]->total_height    
      >crossref_y_limit-crossref_min_vspace)
    {
      crossrefs_y[crossref_count-1]
	=crossref_y_limit-crossrefs_queue[crossref_count-1]->total_height
	-crossref_min_vspace;
    }
  
  for(i=crossref_count-2;i>=0;i--)
    {
      int this_bottom=crossrefs_y[i]+crossrefs_queue[i]->total_height;
      int next_top=crossrefs_y[i+1];
      int overlap=this_bottom-next_top+crossref_min_vspace;
      if (overlap>0) {
	fprintf(stderr,"Moving crossref %d up %dpts (this_bottom=%d, next_top=%d)\n",
		i,overlap,this_bottom,next_top);
	crossrefs_y[i]-=overlap;
      }
    }

  crossref_queue_dump("after bottom up");
  
  return 0;
}

int output_accumulated_cross_references(struct paragraph *p,
					int max_line_to_render)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  int saved_page_y=page_y;
  int saved_bottom_margin=bottom_margin;
  bottom_margin=0;

  // Place cross-reference column in space on opposite side to the
  // booktab
  if (leftRight==LR_RIGHT) {
    left_margin=page_width-crossref_column_width-crossref_margin_width;
    right_margin=crossref_margin_width;
  } else {
    left_margin=crossref_margin_width;
    right_margin=page_width-crossref_column_width-crossref_margin_width;
  }

  crossrefs_reposition();
  
  int n,l;
  for(n=0;n<crossref_count;n++) {
    page_y=crossrefs_y[n];

    struct paragraph *cr=crossrefs_queue[n];
    {
      fprintf(stderr,"Drawing cross-references for %s %d:%d\n",
	      cr->src_book,cr->src_chapter,cr->src_verse);
      paragraph_dump(cr);
      for(l=0;l<cr->line_count;l++)
	{
	  if (leftRight==LR_RIGHT)
	    cr->paragraph_lines[l]->alignment=AL_LEFT;
	  else
	    cr->paragraph_lines[l]->alignment=AL_RIGHT;	    
	  line_emit(cr,l);
	}
    } 
  }
  crossref_count=0;

  p->first_crossref_line=max_line_to_render+1;
  fprintf(stderr,"first_crossref_line=%d\n",p->first_crossref_line);

  page_y=saved_page_y;
  left_margin=saved_left_margin;
  right_margin=saved_right_margin;
  bottom_margin=saved_bottom_margin;
  
  return 0;
}

