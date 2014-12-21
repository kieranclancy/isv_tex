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

int line_recalculate_width(struct line_pieces *l)
{
  // Recalculate line width
  int i;
  l->line_width_so_far=0;
  for(i=0;i<l->piece_count;i++) l->line_width_so_far+=l->piece_widths[i];
  return 0;
}

int line_emit(struct paragraph *p,int line_num)
{
  struct line_pieces *l=p->paragraph_lines[line_num];
  int break_page=0;
  
  // Does the line itself require more space than there is?
  float baseline_y=page_y+l->line_height*line_spacing;
  if (baseline_y>(page_height-bottom_margin)) break_page=1;

  // XXX Does the line plus footnotes require more space than there is?
  // XXX - clone footnote paragraph and then append footnotes referenced in this
  // line to the clone, then measure its height.
  // XXX - deduct footnote space from remaining space.
  if (p==&body_paragraph) {
    struct paragraph temp;
    paragraph_init(&temp);
    paragraph_clone(&temp,&rendered_footnote_paragraph);
    int i;
    for(i=0;i<footnote_count;i++)
      if (l->line_uid==footnote_line_numbers[i])
	paragraph_append(&temp,&footnote_paragraphs[i]);
    current_line_flush(&temp);
    int footnotes_height=paragraph_height(&temp);
    baseline_y+=footnotes_height;
    baseline_y+=footnote_sep_vspace;
    fprintf(stderr,"Footnote block is %dpts high (%d lines).\n",
	    footnotes_height,temp.line_count);
    if (baseline_y>(page_height-bottom_margin)) {
      fprintf(stderr,"Breaking page at %.1fpts to make room for footnotes block\n",
	      page_y);
      break_page=1;
    }
  }

  // XXX Does the line plus its cross-references require more space than there is?
  // XXX - add height of cross-references for any verses in this line to height of
  // all cross-references and make sure that it can fit above the cross-references.
  if (p==&body_paragraph) {
  }
  
  if (break_page) {
    // No room on this page. Start a new page.

    // The footnotes that have been collected so far need to be output, then
    // freed.  Then any remaining footnotes need to be shuffled up the list
    // and references to them re-enumerated.

    // Cross-references also need to be output.
    
    leftRight=-leftRight;

    // Reenumerate footnotes 

    if (p==&body_paragraph) {
      output_accumulated_footnotes();
      output_accumulated_cross_references();
      reenumerate_footnotes(p->paragraph_lines[line_num]->line_uid);
      new_empty_page(leftRight);
    }
    
    page_y=top_margin;
    baseline_y=page_y+l->line_height*line_spacing;
  }

  // Add footnotes to footnote paragraph
  if (p==&body_paragraph) {
    int i;
    for(i=0;i<footnote_count;i++)
      if (l->line_uid==footnote_line_numbers[i])
	paragraph_append(&rendered_footnote_paragraph,&footnote_paragraphs[i]);
    fprintf(stderr,"Rendered footnote paragraph is now:\n");
    paragraph_dump(&rendered_footnote_paragraph);
  }
  
  // convert y to libharu coordinate system (y=0 is at the bottom,
  // and the y position is the base-line of the text to render).
  // Don't apply line_spacing to adjustment, so that extra line spacing
  // appears below the line, rather than above it.
  float y=(page_height-page_y)-l->line_height;

  int i;
  float linegap=0;

  line_remove_trailing_space(l);
  
  // Add extra spaces to justified lines, except for the last
  // line of a paragraph.
  if ((l->alignment==AL_JUSTIFIED)
      &&(p->line_count>(line_num+1))) {

    line_recalculate_width(l);
    line_remove_leading_space(l);
    
    float points_to_add=l->max_line_width-l->line_width_so_far; // -l->left_margin;
    
    fprintf(stderr,"Justification requires sharing of %.1fpts.\n",points_to_add);
    fprintf(stderr,"  used=%.1fpts, max_width=%dpts\n",
	    l->line_width_so_far,l->max_line_width);
    if (points_to_add>0) {
      int elastic_pieces=0;
      for(i=0;i<l->piece_count;i++) if (l->piece_is_elastic[i]) elastic_pieces++;
      if (elastic_pieces) {
	float slice=points_to_add/elastic_pieces;
	fprintf(stderr,
		"  There are %d elastic pieces to share this among (%.1fpts each).\n",
		elastic_pieces,slice);
	for(i=0;i<l->piece_count;i++)
	  if (l->piece_is_elastic[i]) l->piece_widths[i]+=slice;
	l->line_width_so_far=l->max_line_width;
      }
    }
  }
  
  // Now draw the pieces
  HPDF_Page_BeginText (page);
  HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
  float x=0;
  switch(l->alignment) {
  case AL_LEFT: case AL_JUSTIFIED: case AL_NONE:
    // Finally apply any left margin that has been set
    x+=l->left_margin;
    if (l->left_margin) {
      fprintf(stderr,"Applying left margin of %dpts\n",l->left_margin);
    }
    break;
  case AL_CENTRED:
    x=(l->max_line_width-l->line_width_so_far)/2;
    fprintf(stderr,"x=%.1f (centre alignment, w=%.1fpt, max w=%d)\n",
	    x,l->line_width_so_far,l->max_line_width);
    break;
  case AL_RIGHT:
    x=l->max_line_width-l->line_width_so_far;
    fprintf(stderr,"x=%.1f (right alignment)\n",x);
    break;
  default:
    fprintf(stderr,"x=%.1f (left/justified alignment)\n",x);

  }

  for(i=0;i<l->piece_count;i++) {
    HPDF_Page_SetFontAndSize(page,l->fonts[i]->font,l->actualsizes[i]);
    HPDF_Page_SetRGBFill(page,l->fonts[i]->red,l->fonts[i]->green,l->fonts[i]->blue);
    HPDF_Page_TextOut(page,left_margin+x,y-l->piece_baseline[i],
		      l->pieces[i]);
    x=x+l->piece_widths[i];
    // Don't adjust line gap for dropchars
    if (l->fonts[i]->line_count==1) {
      if (l->fonts[i]->linegap>linegap) linegap=l->fonts[i]->linegap;
    }
  }
  HPDF_Page_EndText (page);
  if (!l->piece_count) linegap=l->line_height;

  // Indicate the height of each line
  if (debug_vspace) {
    debug_vspace_x^=8;
    HPDF_Page_SetRGBFill (page, 0.0,0.0,0.0);
    HPDF_Page_Rectangle(page,
			32+debug_vspace_x, y,
			8,linegap*line_spacing);
    HPDF_Page_Fill(page);
  }

  float old_page_y=page_y;
  page_y=page_y+linegap*line_spacing;
  fprintf(stderr,"Added linegap of %.1f to page_y (= %.1fpts). Next line at %.1fpt\n",
	  linegap*line_spacing,old_page_y,page_y);
  return 0;
}

int line_remove_trailing_space(struct line_pieces *l)
{
  // Remove any trailing spaces from the line
  int i;
  for(i=l->piece_count-1;i>=0;i--) {
    fprintf(stderr,"Considering piece #%d/%d '%s'\n",i,
	    l->piece_count,
	    l->pieces[i]);
    if ((!strcmp(" ",l->pieces[i]))
	||(!strcmp("",l->pieces[i]))) {
      l->piece_count=i;
      l->line_width_so_far-=l->piece_widths[i];
      free(l->pieces[i]);
      fprintf(stderr,"  Removed trailing space from line\n");
    } else break;
  }
  return 0;
}

int line_remove_leading_space(struct line_pieces *l)
{
  // Remove any trailing spaces from the line
  int i,j;
  for(i=0;i<l->piece_count;i++) {
    fprintf(stderr,"Considering piece #%d/%d '%s'\n",i,
	    l->piece_count,
	    l->pieces[i]);
    if ((strcmp(" ",l->pieces[i]))&&(strcmp("",l->pieces[i]))) break;
    else {
      fprintf(stderr,"  removing space from start of line.\n");
      free(l->pieces[i]); l->pieces[i]=NULL;
      l->line_width_so_far-=l->piece_widths[i];
    }
  }

  if (i) {
    // Shuffle remaining pieces down
    fprintf(stderr,"Shuffling remaining pieces down.\n");
    for(j=0;j<l->piece_count-i;j++) {
      l->pieces[j]=l->pieces[j+i];
      l->fonts[j]=l->fonts[j+i];
      l->actualsizes[j]=l->actualsizes[j+i];
      l->piece_is_elastic[j]=l->piece_is_elastic[j+i];
      l->piece_baseline[j]=l->piece_baseline[j+i];
      l->piece_widths[j]=l->piece_widths[j+i];
      l->crossrefs[j]=l->crossrefs[j+i];
    }
    
    l->piece_count-=i;
    
    fprintf(stderr,"  Removed %d leading spaces from line\n",i);
  }
  return 0;
}

int line_dump(struct line_pieces *l)
{
  int i;
  fprintf(stderr,"line_uid #%d: ",l->line_uid);
  for(i=0;i<l->piece_count;i++)
    fprintf(stderr,"[%s]",l->pieces[i]);
  fprintf(stderr,"\n");
  return 0;
}
