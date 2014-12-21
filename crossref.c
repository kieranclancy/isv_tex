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
struct paragraph crossreference_paragraph;

int saved_left_margin;
int saved_right_margin;

int crossref_column_width=36;

int crossreference_start()
{
  // There is actually nothing that needs to be done here.
  if (0) fprintf(stderr,"Starting to collect cross-references for %s %s:%s\n",
		 crossreference_book,
		 crossreference_chapter,
		 crossreference_verse);

  saved_left_margin=left_margin;
  saved_right_margin=right_margin;

  left_margin=0;
  right_margin=crossref_column_width;

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

int crossreference_register_verse(struct paragraph *p,
				  char *book,int chapter, int verse)
{
  fprintf(stderr,"Saw %s %d:%d\n",book,chapter,verse);
  return 0;
}

int crossreference_end()
{
  current_line_flush(&crossreference_paragraph);

  // Clone paragraph, set info, and add into hash table of
  // cross-reference paragraphs
  struct paragraph *c=calloc(sizeof(struct paragraph),1);
  paragraph_clone(c,&crossreference_paragraph);

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
  
  // Clear crossreference paragraph ready for the next one.
  paragraph_clear(&crossreference_paragraph);
  
  free(crossreference_book); crossreference_book=NULL;
  free(crossreference_chapter); crossreference_chapter=NULL;
  free(crossreference_verse); crossreference_verse=NULL;

  target_paragraph=&body_paragraph;

  crossreference_mode=0;
  left_margin=saved_left_margin;
  right_margin=saved_right_margin;

  return 0;
}
