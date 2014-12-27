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
  if (0)
    fprintf(stderr,"Calculating height of line %p (%d pieces, %.1fpts wide, align=%d, left margin=%d)\n",
	    l,l->piece_count,l->line_width_so_far,l->alignment,l->left_margin);

  // insert_vspace() works by making a line with zero pieces, and the space to skip
  // is set in l->line_height, so we don't need to do anything in that case.
  if (l->piece_count==0) {
    l->ascent=l->line_height;
    l->descent=0;
    // fprintf(stderr,"  Line is vspace(%.1fpt)\n",l->line_height);
    return 0;
  }
  
  for(i=0;i<l->piece_count;i++)
    {
      // Get ascender height of font
      int ascender_height=HPDF_Font_GetAscent(l->fonts[i]->font)*l->fonts[i]->font_size/1000;
      // Get descender depth of font
      int descender_depth=HPDF_Font_GetDescent(l->fonts[i]->font)*l->fonts[i]->font_size/1000;
      if (0) fprintf(stderr,"  '%s' is %.1fpt wide.\n",
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
  // fprintf(stderr,"  line ascends %dpts and descends %d points.\n",max,-min);
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
    if (0)
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
  if (0) fprintf(stderr,"Freeing line with uid = %d\n",l->line_uid);
  for(i=0;i<l->piece_count;i++) free(l->pieces[i]);
  free(l);
  return 0;
}

float calc_left_hang(struct line_pieces *l,int left_hang_piece)
{
  if (left_hang_piece>=l->piece_count) return 0.0;
  
  char *text=l->pieces[left_hang_piece];
  char hang_text[1024];

  int o=0;
  hang_text[0]=0;
  while(o<1024&&text[o]) {
    int codepoint=0;
    int bytes=1;
    if (text[o]&0x80) {
      // Unicode
      if (!(text[o]&0x20)) {
	// 2 byte code point
	codepoint
	  =((text[o]&0x1f)<<6)
	  |((text[o+1]&0x3f)<<0);
	bytes=2;
      } else if (!(text[o]&0x10)) {
	// 3 byte code point
	codepoint
	  =((text[o]&0x0f)<<12)
	  |((text[o+1]&0x3f)<<6)
	  |((text[o+2]&0x3f)<<0);
	bytes=3;
      } else {
	// 4 byte code point
	codepoint
	  =((text[o]&0x07)<<18)
	  |((text[o+1]&0x3f)<<12)
	  |((text[o+2]&0x3f)<<6)
	  |((text[o+3]&0x3f)<<0);
	bytes=4;
      }
    } else {
      codepoint=text[o++];
      bytes=1;
    }

    if (unicodePointIsHangable(codepoint)) {
      for(int i=0;i<bytes;i++) hang_text[o+i]=text[o+i];
      o+=bytes;
      hang_text[o]=0;
      continue;
    }
    break;
  }
  
  if (hang_text[0]) {
    set_font(l->fonts[left_hang_piece]->font_nickname);
    float hang_width=HPDF_Page_TextWidth(page,hang_text);
    fprintf(stderr,"Hanging '%s' on the left (%.1f points)\n",
	    hang_text,hang_width);
    return hang_width;
  } else return 0.0;
}

int line_recalculate_width(struct line_pieces *l)
{
  // Recalculate line width
  int i;

  // Work out basic width.
  // At the same time, look for footnotes that can be placed above punctuation
  int footnotemark_index=set_font("footnotemark");
  l->line_width_so_far=0;
  for(i=0;i<l->piece_count;i++) {
    l->piece_widths[i]=l->natural_widths[i];

    if (i  // make sure there is a previous piece
	&&l->fonts[i]==&type_faces[footnotemark_index]) {
      // This is a footnote mark.
      // Check if the preceeding piece ends in any low punctuation marks.
      // If so, then discount the width of that piece by the width of such
      // punctuation. If the width of the discount is wider than this footnotemark,
      // then increase the width of this footnotemark so that the end of text
      // position is advanced correctly.
      fprintf(stderr,"hanging footnotemark over punctuation: ");
      line_dump(l);
      char *text=l->pieces[i-1];
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
      set_font(l->fonts[i-1]->font_nickname);
      float hang_width=HPDF_Page_TextWidth(page,hang_text);
      float all_width=l->natural_widths[i-1];
      l->piece_widths[i-1]=all_width-hang_width;
      if (hang_width>l->piece_widths[i]) l->piece_widths[i]=hang_width;
      fprintf(stderr,"  This is the punctuation over which we are hanging the footnotemark: [%s] (%.1fpts)\n",hang_text,hang_width);
      fprintf(stderr,"Line after hanging footnote over punctuation: ");
      line_dump(l);
    }

    // Related to the above, we must discount the width of a dropchar if it is
    // followed by left-hangable material
    if ((i==1)&&(l->fonts[0]->line_count>1))
      {
	int piece=i;
	float discount=0;
	
	// Discount any footnote
	if (l->fonts[piece]==&type_faces[footnotemark_index]) {
	  discount+=l->natural_widths[piece];
	  piece++;
	}
	discount+=calc_left_hang(l,piece);       
	
	l->piece_widths[0]=l->natural_widths[0]-discount;
      }
  }

  for(i=0;i<l->piece_count;i++) l->line_width_so_far+=l->piece_widths[i];

  l->left_hang=0;
  l->right_hang=0;

  // Now discount for hanging verse numbers, footnotes and punctuation.
  int left_hang_piece=0;
  if (l->piece_count) {
    if (!(strcmp(l->fonts[0]->font_nickname,"versenum"))) {
      // Verse number on the left.
      // Only hang single-digit or skinny (10-19) verse numbers
      // Actually, let's just hang all verse numbers. I think it looks better.
      int vn=atoi(l->pieces[0]);      
      if (vn<999) {
	l->left_hang=l->piece_widths[0];	
	left_hang_piece=1;
	fprintf(stderr,"Hanging verse number '%s'(=%d) in left margin (%.1f points)\n",
		l->pieces[0],vn,l->left_hang);
      }
    }

    char *text=NULL;
    char *hang_text=NULL;

    // Check for hanging punctuation (including if it immediately follows a
    // verse number)
    if (left_hang_piece<l->piece_count) {
      l->left_hang+=calc_left_hang(l,left_hang_piece);
    }

    // Now check for right hanging
    hang_text=NULL;
    int right_hang_piece=l->piece_count-1;

    // Ignore white-space at the end of lines when working out hanging.
    while ((right_hang_piece>=0)
	   &&l->pieces[right_hang_piece]
	   &&strlen(l->pieces[right_hang_piece])
	   &&(l->pieces[right_hang_piece][0]==' '))
      right_hang_piece--;

    float hang_note_width=0;
    float hang_width=0;
    
    if (right_hang_piece>=0) {
      // Footnotes always hang 
      if (!(strcmp(l->fonts[right_hang_piece]->font_nickname,"footnotemark"))) {
	hang_note_width=l->natural_widths[right_hang_piece];
	l->right_hang=l->piece_widths[right_hang_piece--];
      }
    }

    if (right_hang_piece>=0&&(right_hang_piece<l->piece_count)) {
      text=l->pieces[right_hang_piece];
      int textlen=strlen(text);

      if (text) {
	// Now look for right hanging punctuation
	int o=textlen-1;
	while(o>=0) {
	  fprintf(stderr,"Requesting prev code point from \"%s\"[%d]\n",
		  text,o);
	  int codepoint=unicodePrevCodePoint(text,&o);
	  if (codepoint&&unicodePointIsHangable(codepoint)) {
	    fprintf(stderr,"Decided [%s] is hangable (o=%d)\n",
		    unicodeToUTF8(codepoint),o);
	    hang_text=&text[o+1];
	  } else
	    break;
	}
	
	if (hang_text) {
	  set_font(l->fonts[right_hang_piece]->font_nickname);
	  hang_width=HPDF_Page_TextWidth(page,hang_text);
	  // Reduce hang width by the amount of any footnote hang over
	  // the punctuation.
	  hang_width-=(l->natural_widths[right_hang_piece]
		       -l->piece_widths[right_hang_piece]);
	  // Only hang if it won't run into things on the side.
	  // XX Narrowest space is probably between body and
	  // cross-refs, so use that measure regardless of whether
	  // we are on a left or right face.
	  int max_hang_space
	    =right_margin
	    -crossref_margin_width-crossref_column_width
	    -2;  // plus a little space to ensure some white space
	  if (hang_width+hang_note_width<=max_hang_space) {
	    l->right_hang=hang_note_width+hang_width;
	    fprintf(stderr,"Hanging '%s' in right margin (%.1f points, font=%s)\n",
		    hang_text,hang_width,l->fonts[right_hang_piece]->font_nickname);
	  } else l->right_hang=hang_note_width;
	}
      }
    }
  }
  l->line_width_so_far-=l->left_hang+l->right_hang;
  return 0;
}

int line_emit(struct paragraph *p,int line_num)
{
  struct line_pieces *l=p->paragraph_lines[line_num];
  int break_page=0;

  fprintf(stderr,"Emitting line: "); line_dump(p->paragraph_lines[line_num]);
  
  // Work out maximum line number that we have to take into account for
  // page fitting, i.e., to prevent orphaned heading lines.
  int max_line_num=line_num;
  float combined_line_height=l->line_height;
  fprintf(stderr,"  line itself (#%d) is %.1fpts high\n",line_num,l->line_height);
  while ((max_line_num<(p->line_count-1))
	 &&p->paragraph_lines[max_line_num]->tied_to_next_line) {
    combined_line_height+=p->paragraph_lines[++max_line_num]->line_height;
    fprintf(stderr,"  dependent line is %.1fpts high:",
	    p->paragraph_lines[max_line_num]->line_height);
    line_dump(p->paragraph_lines[max_line_num]);
  }
  fprintf(stderr,"Treating lines %d -- %d as a unit %.1fpts high\n",
	  line_num,max_line_num,combined_line_height);
  
  // Does the line(s) require more space than there is?    
  float baseline_y=page_y+combined_line_height*line_spacing;
  if (baseline_y>(page_height-bottom_margin)) {
    fprintf(stderr,"Breaking page %d at %.1fpts to make room for body text\n",
	    current_page,page_y);
    break_page=1;
  }

  // Does the line plus footnotes require more space than there is?
  // - clone footnote paragraph and then append footnotes referenced in this
  // line to the clone, then measure its height.
  // - deduct footnote space from remaining space.
  int footnotes_total_height=0;
  if (p==&body_paragraph) {
    struct paragraph temp;
    paragraph_init(&temp);
    paragraph_clone(&temp,&rendered_footnote_paragraph);

    // Include footnote height of this line, and any lines tied to it.
    int n;
    for(n=line_num;n<=max_line_num;n++) {
      struct line_pieces *ll=p->paragraph_lines[n];
      int i;
      for(i=0;i<footnote_count;i++)
	if (ll->line_uid==footnote_line_numbers[i])
	  paragraph_append(&temp,&footnote_paragraphs[i]);      
    }
    
    current_line_flush(&temp);
    int footnotes_height=paragraph_height(&temp);
    baseline_y+=footnotes_height;
    baseline_y+=footnote_sep_vspace;
    footnotes_total_height=footnotes_height+footnote_sep_vspace;
    fprintf(stderr,"Footnote block is %dpts high (%d lines).\n",
	    footnotes_height,temp.line_count);
    if (baseline_y>(page_height-bottom_margin)) {
      fprintf(stderr,"Breaking page %d at %.1fpts to make room for %dpt footnotes block\n",
	      current_page,page_y,footnotes_height);
      break_page=1;
    }
  }

  // Does the line plus its cross-references require more space than there is?
  // - add height of cross-references for any verses in this line to height of
  // all cross-references and make sure that it can fit above the cross-references.
  if (p==&body_paragraph) {
    int crossref_height=0;
    int crossref_para_count=crossref_count;
    int n,i;

    // Total height of crossrefs from previous lines on page
    for(n=0;n<crossref_count;n++)
      crossref_height+=crossrefs_queue[n]->total_height;
    // Now add height of cross refs on the current line(s) being drawn.
    for(n=line_num;n<=max_line_num;n++) {
      struct line_pieces *ll=p->paragraph_lines[n];
      for(i=0;i<ll->piece_count;i++)
	if (ll->crossrefs[i]) {
	  crossref_height+=ll->crossrefs[i]->total_height;
	  crossref_para_count++;
	}
    }

    if ((crossref_height+((crossref_para_count+1)*crossref_min_vspace))
	>(page_height-footnotes_total_height-bottom_margin-top_margin)) {
      fprintf(stderr,"Breaking page %d at %.1fpts to avoid %dpts of cross references for %d verses (only %dpts available for crossrefs)\n",
	      current_page,page_y,
	      crossref_height+((crossref_para_count+1)*crossref_min_vspace),
	      crossref_para_count,
	      (page_height-footnotes_total_height-bottom_margin-top_margin));
      paragraph_dump(p);
      break_page=1;
    } else {
      fprintf(stderr,"%d cross reference blocks, totalling %dpts high (lines %d..%d)\n",
	      crossref_para_count,
	      crossref_height+((crossref_para_count+1)*crossref_min_vspace),
	      p->first_crossref_line,max_line_num);
    }
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
      output_accumulated_cross_references(p,line_num-1);
      reenumerate_footnotes(p->paragraph_lines[line_num]->line_uid);
      new_empty_page(leftRight,0);
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
  if (l->alignment==AL_JUSTIFIED) line_remove_leading_space(l);
  fprintf(stderr,"Final width recalculation: ");
  line_recalculate_width(l);

  fprintf(stderr,"After width recalculation: "); line_dump(p->paragraph_lines[line_num]);

  
  // Add extra spaces to justified lines, except for the last
  // line of a paragraph, and poetry lines.
  if (l->alignment==AL_JUSTIFIED) {
    if (p->line_count>(line_num+1)) {

      float points_to_add
	=l->max_line_width-l->line_width_so_far;
      
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
  }

  fprintf(stderr,"After justification: "); line_dump(p->paragraph_lines[line_num]);

  
  // Now draw the pieces
  l->on_page_y=page_y;
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
  x-=l->left_hang;

  for(i=0;i<l->piece_count;i++) {
    HPDF_Page_SetFontAndSize(page,l->fonts[i]->font,l->actualsizes[i]);
    HPDF_Page_SetRGBFill(page,l->fonts[i]->red,l->fonts[i]->green,l->fonts[i]->blue);
    HPDF_Page_TextOut(page,left_margin+x,y-l->piece_baseline[i],
		      l->pieces[i]);
    record_text(l->fonts[i],l->actualsizes[i],
		l->pieces[i],left_margin+x,y-l->piece_baseline[i],0);
    x=x+l->piece_widths[i];
    // Don't adjust line gap for dropchars
    if (l->fonts[i]->line_count==1) {
      if (l->fonts[i]->linegap>linegap) linegap=l->fonts[i]->linegap;
    }

    // Queue cross-references
    if (l->crossrefs[i]) crossref_queue(l->crossrefs[i],page_y);

    if (!strcmp(l->fonts[i]->font_nickname,"versenum"))
      last_verse_on_page=atoi(l->pieces[i]);
    if (!strcmp(l->fonts[i]->font_nickname,"chapternum"))
      last_chapter_on_page=atoi(l->pieces[i]);
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
  if (0)
    fprintf(stderr,"Added linegap of %.1f to page_y (= %.1fpts). Next line at %.1fpt\n",
	    linegap*line_spacing,old_page_y,page_y);
  return 0;
}

int line_remove_trailing_space(struct line_pieces *l)
{
  // Remove any trailing spaces from the line
  int i;
  for(i=l->piece_count-1;i>=0;i--) {
    if (0) fprintf(stderr,"Considering piece #%d/%d '%s'\n",i,
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
      l->natural_widths[j]=l->natural_widths[j+i];
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
  for(i=0;i<l->piece_count;i++) {
    if (i&&(l->piece_widths[i-1]!=l->natural_widths[i-1]))
      fprintf(stderr,"%.1f",l->piece_widths[i-1]-l->natural_widths[i-1]);
    fprintf(stderr,"[%s]",l->pieces[i]);
  }
  fprintf(stderr,"\n");
  return 0;
}

int line_set_checkpoint(struct line_pieces *l)
{
  if (!l) return 0;
  if (!l->piece_count) return 0;
  
  // Start with checkpoint at end of current line.
  l->checkpoint=l->piece_count;
  while(l->checkpoint>0) {
    // move back one if the previous word is a verse number
    if (!strcasecmp(l->fonts[l->checkpoint-1]->font_nickname,"versenum"))
      l->checkpoint--;
    // Or if we are drawing a footnote mark
    else if (!strcasecmp(current_font->font_nickname,"footnotemark"))
      l->checkpoint--;
    else if (!strcasecmp(current_font->font_nickname,"footnotemarkinfootnote"))
      l->checkpoint--;
    else if (!strcasecmp(current_font->font_nickname,"footnoteversenum"))
      l->checkpoint--;
    // Or if we see a non-breaking space
    else if (((unsigned char)l->pieces[l->checkpoint-1][0])==0xa0)
      l->checkpoint--;
    else
      break;
  }
  return 0;
}
