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

/* Clone a line */
struct line_pieces *line_clone(struct line_pieces *l)
{
  struct line_pieces *clone=calloc(sizeof(struct line_pieces),1);

  // Copy contents of original line to clone
  bcopy(l,clone,sizeof(struct line_pieces));

  // strdup all strings
  int i;
  for(i=0;i<clone->piece_count;i++) clone->pieces[i]=strdup(clone->pieces[i]);

  return clone;
}

int line_calculate_height(struct line_pieces *l)
{
  int max=-1; int min=0;
  int linegap=0;
  int i;
  fprintf(stderr,"Calculating height of line %p (%d pieces, %.1fpts wide, align=%d, left margin=%d)\n",
	  l,l->piece_count,l->line_width_so_far,l->alignment,l->left_margin);

  // insert_vspace() works by making a line with zero pieces, and the space to skip
  // is set in l->line_height, so we don't need to do anything in that case.
  if (l->piece_count==0) {
    l->ascent=l->line_height;
    l->descent=0;
    fprintf(stderr,"  Line is vspace(%.1fpt)\n",l->line_height);
    return 0;
  }
  
  for(i=0;i<l->piece_count;i++)
    {
      // Get ascender height of font
      int ascender_height=HPDF_Font_GetAscent(l->fonts[i]->font)*l->fonts[i]->font_size/1000;
      // Get descender depth of font
      int descender_depth=HPDF_Font_GetDescent(l->fonts[i]->font)*l->fonts[i]->font_size/1000;
      fprintf(stderr,"  '%s' is %.1fpt wide.\n",
	      l->pieces[i],l->piece_widths[i]);
      if (descender_depth<0) descender_depth=-descender_depth;
      // Don't count the space used by dropchars, since it gets covered by
      // the extra line(s) of the dropchar.
      if (l->fonts[i]->line_count==1) {
	if (ascender_height-l->piece_baseline[i]>max)
	  max=ascender_height-l->piece_baseline[i];
	if (l->piece_baseline[i]-descender_depth<min)
	  min=l->piece_baseline[i]-descender_depth;
      }
      if (l->fonts[i]->line_count==1) {
	if (l->fonts[i]->linegap>linegap) linegap=l->fonts[i]->linegap;
    }

    }

  // l->line_height=max-min+1;
  l->line_height=linegap*line_spacing;
  l->ascent=max; l->descent=-min;
  fprintf(stderr,"  line ascends %dpts and descends %d points.\n",max,-min);
  return 0;
}

// Setup a line

int line_apply_poetry_margin(struct paragraph *p,struct line_pieces *current_line)
{
  // If we are in poetry mode, then apply margin.
  if (p->poem_level) {
    current_line->left_margin
      =poetry_left_margin
      +(p->poem_level-1)*poetry_level_indent
      +p->poem_subsequent_line*poetry_wrap_indent;
    current_line->max_line_width
      =page_width-left_margin-right_margin-current_line->left_margin;
    fprintf(stderr,"Applying indent of %dpts in poetry mode (level=%d, subs=%d).\n",
	    current_line->left_margin,p->poem_level,p->poem_subsequent_line);
    p->poem_subsequent_line=1;
  }
  return 0;
}

int line_free(struct line_pieces *l)
{
  int i;
  if (!l) return 0;
  fprintf(stderr,"Freeing line with uid = %d\n",l->line_uid);
  for(i=0;i<l->piece_count;i++) free(l->pieces[i]);
  free(l);
  return 0;
}
