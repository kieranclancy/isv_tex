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

int paragraph_init(struct paragraph *p)
{
  bzero(p,sizeof(struct paragraph));
  return 0;
}

int paragraph_clear(struct paragraph *p)
{

  // Free any line structures in this paragraph
  int i;
  if (0) fprintf(stderr,"Freeing %d lines in paragraph %p\n",
		 p->line_count,p);
  for(i=0;i<p->line_count;i++) {
    line_free(p->paragraph_lines[i]);
    p->paragraph_lines[i]=NULL;
  }
  p->line_count=0;
  
  paragraph_init(p);
  return 0;
}

int paragraph_flush(struct paragraph *p)
{  
  //  fprintf(stderr,"%s():\n",__FUNCTION__);

  // First flush the current line
  current_line_flush(p);

  // XXX mark last line terminal (so that it doesn't get justified).

  /* Write lines of paragraph to PDF, generating new pages as required.
     Here the challenge is knowing that the line will fit.
     We can fairly easily measure the height of the line itself to see that
     it will fit.  The trick is making sure that there is room for the line 
     plus any inseperable lines fit, too. Also, there has to be room for the
     marginal cross-references, and also the footnotes at the bottom.

     The footnotes are merged in a single footnote paragraph, which makes
     determining the space for them dependent on what has already been
     rendered on this page.

     The marginal notes should appear next to the corresponding verses,
     unless some verses have too many, in which case we have to do some
     vertical sliding to try to make them fit, if possible.

     For now, we will ignore all of that, and just emit the lines if there is
     physical space for the line.  This requires only pre-processing each line
     to determine the maximum extents of that line.
  */  
  int i;
  for(i=0;i<p->line_count;i++) line_calculate_height(p->paragraph_lines[i]);

  for(i=0;i<p->line_count;i++) line_emit(p,i);

  // Clear out old lines
  for(i=0;i<p->line_count;i++) line_free(p->paragraph_lines[i]);
  p->line_count=0;
  
  return 0;
}

/* To build a paragraph we need to build lines, and then to know 
   which lines are inseparable and those which can be separated, i.e.,
   what the separable units of the paragraph are.
   With this information the paragraph can be written by seeing if each
   separable unit in turn can fit on the current page. If not, the page
   gets flushed, and the process continues on the next (initially empty)
   page.

   At the next lower level we need to assemble lines from the incoming words.
   This works by computing the dimenstions of each word, and seeing if it
   can fit on the current line.  If so, then good, else the previous line is
   finalised (which may involve adjusting the horizontal spacing if centring
   or justification is applied.

   Line assembly has a few corner cases to handle.  First, some smallcaps
   output is emulated using the capital characters of a regular font, in
   which case words may need to be split into two or more pieces, with no
   space in between.  Similarly foot notes and verse numbers are pieces
   which must be able to be placed without any space before or after them
   depending on the context.

*/
int paragraph_append_line(struct paragraph *p,struct line_pieces *line)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (p->line_count>=MAX_LINES_IN_PARAGRAPH) {
    fprintf(stderr,"Too many lines in paragraph.\n");
    exit(-1);
  }

  p->paragraph_lines[p->line_count++]=p->current_line;
  p->current_line=NULL;
  return 0;
}

int line_uid_counter=0;
int paragraph_setup_next_line(struct paragraph *p)
{
  //  fprintf(stderr,"%s()\n",__FUNCTION__);

  // Append any line in progress before creating fresh line
  if (p->current_line) paragraph_append_line(p,p->current_line);
  
  // Allocate structure
  p->current_line=calloc(sizeof(struct line_pieces),1); 

  p->current_line->line_uid=line_uid_counter++;
  
  // Set maximum line width
  p->current_line->max_line_width=page_width-left_margin-right_margin;

  // If there is a dropchar margin in effect, then apply it.
  if (p->drop_char_margin_line_count>0) {
    if (0) fprintf(stderr,
		   "Applying dropchar margin of %dpt (%d more lines, including this one)\n",
		   p->drop_char_left_margin,p->drop_char_margin_line_count);
    p->current_line->max_line_width
      =page_width-left_margin-right_margin-p->drop_char_left_margin;
    p->current_line->left_margin=p->drop_char_left_margin;
    p->drop_char_margin_line_count--;
  }

  line_apply_poetry_margin(p,p->current_line);
  if (0) fprintf(stderr,"New line left margin=%dpts, max_width=%dpts\n",
		 p->current_line->left_margin,p->current_line->max_line_width);
  
  return 0;
}

int paragraph_set_widow_counter(struct paragraph *p,int lines)
{
  fprintf(stderr,"%s(): STUB\n",__FUNCTION__);
  return 0;
}

int paragraph_append_characters(struct paragraph *p,char *text,int size,int baseline,
				int forceSpaceAtStartOfLine)
{
  // fprintf(stderr,"%s(\"%s\",%d)\n",__FUNCTION__,text,size);
  
  if (!p->current_line) paragraph_setup_next_line(p);

  // Don't start lines with empty space.
  if ((!strcmp(text," "))&&(p->current_line->piece_count==0))
    if (!forceSpaceAtStartOfLine) return 0;

  // Verse numbers at the start of poetry lines appear left of the poetry margin
  int is_poetry_leading_verse=0;
  if (p->poem_level&&(!p->current_line->piece_count)
      &&!strcmp("versenum",current_font->font_nickname)) {
    is_poetry_leading_verse=1;
    // fprintf(stderr,"Placing verse number in margin at start of poem line\n");
  } else {  
    // Make sure the line has enough space
    if (p->current_line->piece_count>=MAX_LINE_PIECES) {
      fprintf(stderr,"Cannot add '%s' to line, as line is too long.\n",text);
      exit(-1);
    }
  }

  // Get width of piece
  float text_width;
  float text_height;
  
  HPDF_Page_SetFontAndSize (page, current_font->font, size);
  text_height = HPDF_Font_GetCapHeight(current_font->font) * size/1000;
  text_width = HPDF_Page_TextWidth(page,text);
  if (0) fprintf(stderr,"  text_width=%.1f, height=%.1f, font=%p('%s'), size=%d, text='%s'\n",
		 text_width,text_height,current_font->font,
		 current_font->font_nickname,size,text);
  
  // Place initial verse number in margin for poetry.
  if (is_poetry_leading_verse) {
    p->current_line->left_margin-=text_width;
    p->current_line->max_line_width+=text_width;
  }

  p->current_line->pieces[p->current_line->piece_count]=strdup(text);
  p->current_line->fonts[p->current_line->piece_count]=current_font;
  p->current_line->actualsizes[p->current_line->piece_count]=size;
  p->current_line->piece_widths[p->current_line->piece_count]=text_width;
  p->current_line->crossrefs[p->current_line->piece_count]=NULL;
  if (strcmp(text," "))
    p->current_line->piece_is_elastic[p->current_line->piece_count]=0;
  else
    p->current_line->piece_is_elastic[p->current_line->piece_count]=1;
  p->current_line->piece_baseline[p->current_line->piece_count]=baseline;  
  // p->current_line->line_width_so_far+=text_width;
  p->current_line->piece_count++;
  line_recalculate_width(p->current_line);

  if (p->current_line->line_width_so_far>=p->current_line->max_line_width) {
    if (0) fprintf(stderr,"Breaking line at %1.f points wide (max width=%dpts).\n",
		   p->current_line->line_width_so_far,
		   p->current_line->max_line_width);
    // Line is too long.
    if (p->current_line->checkpoint) {
      // Rewind to checkpoint, add this line
      // to the current paragraph.  Then allocate a new line with just
      // the recently added stuff.
      int saved_piece_count=p->current_line->piece_count;
      int saved_checkpoint=p->current_line->checkpoint;
      p->current_line->piece_count=p->current_line->checkpoint;
      struct line_pieces *last_line=p->current_line;
      paragraph_append_line(p,p->current_line);
      paragraph_setup_next_line(p);
      p->current_line->alignment=last_line->alignment;
      if (0) fprintf(stderr,"  new line is indented %dpts\n",
		     p->current_line->left_margin);
      // Now populate new line with the left overs from the old line
      int i;
      for(i=saved_checkpoint;i<saved_piece_count;i++)
	{
	  // last_line->line_width_so_far-=last_line->piece_widths[i];
	  // p->current_line->line_width_so_far+=last_line->piece_widths[i];
	  p->current_line->pieces[p->current_line->piece_count]=last_line->pieces[i];
	  p->current_line->fonts[p->current_line->piece_count]=last_line->fonts[i];
	  p->current_line->actualsizes[p->current_line->piece_count]
	    =last_line->actualsizes[i];
	  p->current_line->piece_widths[p->current_line->piece_count]
	    =last_line->piece_widths[i];
	  p->current_line->piece_is_elastic[p->current_line->piece_count]
	    =last_line->piece_is_elastic[i];
	  p->current_line->piece_baseline[p->current_line->piece_count]
	    =last_line->piece_baseline[i];
	  p->current_line->crossrefs[p->current_line->piece_count]
	    =last_line->crossrefs[i];
	  p->current_line->piece_count++;	  
	}
      line_recalculate_width(p->current_line);
      line_recalculate_width(last_line);
      // fprintf(stderr,"  after breaking, the old line is %1.fpts wide\n",
      //         last_line->line_width_so_far);
      // fprintf(stderr,"  after breaking, the new line is %1.fpts wide\n",
      //         p->current_line->line_width_so_far);
      
      // Inherit alignment of previous line
      p->current_line->alignment=last_line->alignment;
      dropchar_margin_check(p,p->current_line);
    } else {
      // Line too long, but no checkpoint.  This is bad.
      // Just add this line as is to the paragraph and report a warning.
      fprintf(stderr,"Line too wide when appending '%s'\n",text);
      paragraph_append_line(p,p->current_line);
      dropchar_margin_check(p,p->current_line);
      p->current_line=NULL;
      paragraph_setup_next_line(p);
    }
  } else {
    // Fits on this line.
    dropchar_margin_check(p,p->current_line);
  }
  
  return 0;
}

int paragraph_append_text(struct paragraph *p,char *text,int baseline,
			  int forceSpaceAtStartOfLine)
{  
  fprintf(stderr,"%s(\"%s\")\n",__FUNCTION__,text);

  // Don't put verse number immediately following dropchar
  if (p->current_line&&(p->current_line->piece_count==1))
    {
      if (!strcasecmp(p->current_line->fonts[p->current_line->piece_count-1]->font_nickname,"chapternum"))
	if (!strcasecmp(current_font->font_nickname,"versenum"))
	  return 0;

    }

  
  // Keep track of whether the last character is a full stop so that we can
  // apply double spacing between sentences (if desired).
  if (text[strlen(text)-1]=='.') p->last_char_is_a_full_stop=1;
  else p->last_char_is_a_full_stop=0;

  // Checkpoint where we are up to, in case we need to split the line
  if (p->current_line) {
    // Start with checkpoint at end of current line.
    p->current_line->checkpoint=p->current_line->piece_count;
    if (p->current_line->piece_count) {
      // But move back one if the previous word is a verse number
      if (!strcasecmp(p->current_line->fonts[p->current_line->piece_count-1]->font_nickname,"versenum"))
	p->current_line->checkpoint--;
      // Or if we are drawing a footnote mark
      else if (!strcasecmp(current_font->font_nickname,"footnotemark"))
	p->current_line->checkpoint--;
    }
  }
  
  if (current_font->smallcaps) {
    // This font uses emulated small caps, so break the word down into
    // as many pieces as necessary.
    int i,j;
    char chars[strlen(text)+1];
    for(i=0;text[i];)
      {
	int islower=0;
	int count=0;
	if ((text[i]>='a')&&(text[i]<='z')) islower=1;
	for(j=i;text[i];j++)
	  {
	    int thisislower=0;
	    if ((text[j]>='a')&&(text[j]<='z')) thisislower=1;
	    if (thisislower!=islower)
	      {
		// case change
		chars[count]=0;
		if (islower)
		  paragraph_append_characters(p,chars,current_font->smallcaps,
					      baseline+current_font->baseline_delta,
					      forceSpaceAtStartOfLine);
		else
		  paragraph_append_characters(p,chars,current_font->font_size,
					      baseline+current_font->baseline_delta,
					      forceSpaceAtStartOfLine);
		i=j;
		count=0;
		break;
	      } else chars[count++]=toupper(text[j]);
	  }
	if (count) {
	  chars[count]=0;
	  if (islower)
	    paragraph_append_characters(p,chars,current_font->smallcaps,
					baseline+current_font->baseline_delta,
					forceSpaceAtStartOfLine);
	  else
	    paragraph_append_characters(p,chars,current_font->font_size,
					baseline+current_font->baseline_delta,
					forceSpaceAtStartOfLine);
	  break;
	}
      }
  } else {
    // Regular text. Render as one piece.
    paragraph_append_characters(p,text,current_font->font_size,
				baseline+current_font->baseline_delta,
				forceSpaceAtStartOfLine);
  }
    
  return 0;
}

/* Add a space to a paragraph.  Similar to appending text, but adds elastic
   space that can be expanded if required for justified text.
*/
int paragraph_append_space(struct paragraph *p,int forceSpaceAtStartOfLine)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);
  if (p->last_char_is_a_full_stop) fprintf(stderr,"  space follows a full-stop.\n");

  // Don't put spaces after dropchars
  if (p->current_line&&(p->current_line->piece_count==1))
    {
      if (!strcasecmp(p->current_line->fonts[p->current_line->piece_count-1]->font_nickname,"chapternum"))
	return 0;

    }
    
  // Checkpoint where we are up to, in case we need to split the line
  if (p->current_line) p->current_line->checkpoint=p->current_line->piece_count;
  paragraph_append_characters(p," ",current_font->font_size,0,forceSpaceAtStartOfLine);
  return 0;
}

// Thin space.  Append a normal space, then revise it's width down to 1/2
int paragraph_append_thinspace(struct paragraph *p,int forceSpaceAtStartOfLine)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);
  // Checkpoint where we are up to, in case we need to split the line
  if (p->current_line) p->current_line->checkpoint=p->current_line->piece_count;
  paragraph_append_characters(p," ",current_font->font_size,0,forceSpaceAtStartOfLine);
  p->current_line->piece_widths[p->current_line->piece_count-1]/=2;
  return 0;
}

struct type_face *type_face_stack[TYPE_FACE_STACK_DEPTH];
int type_face_stack_pointer=0;
int paragraph_push_style(struct paragraph *p, int font_alignment,int font_index)
{
  fprintf(stderr,"%s(): alignment=%d, style=%s\n",__FUNCTION__,
	  font_alignment,type_faces[font_index].font_nickname);

  if ((!p->current_line)
      ||(p->current_line->piece_count
	 &&p->current_line->alignment!=font_alignment
	 &&p->current_line->alignment!=AL_NONE))
    {
      // Change of alignment - start on new line
      fprintf(stderr,"Creating new line due to alignment change.\n");
      paragraph_setup_next_line(p);
    }    
  p->current_line->alignment=font_alignment;
  
  if (type_face_stack_pointer<TYPE_FACE_STACK_DEPTH)
    type_face_stack[type_face_stack_pointer++]=current_font;
  else {
    fprintf(stderr,"Typeface stack overflowed.\n");
    fprintf(stderr,"Typeface stack contents:\n");
    int i;
    for(i=0;i<type_face_stack_pointer;i++) {
      fprintf(stderr,"  %-2d: '%s'\n",
	      i,type_face_stack[i]->font_nickname);
    }
    
    exit(-1);
  }

  current_font=&type_faces[font_index];
  
  return 0;
}

int paragraph_insert_vspace(struct paragraph *p,int points)
{
  fprintf(stderr,"%s(%dpt)\n",__FUNCTION__,points);
  paragraph_setup_next_line(p);
  p->current_line->line_height=points;
  current_line_flush(p);
  return 0;
}

int paragraph_pop_style(struct paragraph *p)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  if (type_face_stack_pointer==footnote_stack_depth) {
    fprintf(stderr,"Ending footnote collection mode.\n");
    end_footnote();
    footnote_stack_depth=-1;
  }
  
  if (type_face_stack_pointer) {
    // Add vertical space after certain type faces
    if (!strcasecmp(current_font->font_nickname,"booktitle"))
      {
	paragraph_insert_vspace(p,type_faces[set_font("blackletter")].linegap/2);
      }
    current_font=type_face_stack[--type_face_stack_pointer];
  }
  else {
    fprintf(stderr,"Typeface stack underflowed.\n"); exit(-1);
  }

  return 0;
}

int paragraph_clear_style_stack()
{
  fprintf(stderr,"%s()\n",__FUNCTION__);
  current_font=&type_faces[set_font("blackletter")];
  type_face_stack_pointer=0;
  return 0;
}

/* Clone the lines in one paragraph into the other.
   The destination paragraph is assumed to be empty.  Memory
   will be leaked if this is not the case.
*/
int paragraph_clone(struct paragraph *dst,struct paragraph *src)
{
  fprintf(stderr,"%s():\n",__FUNCTION__);

  int i;

  // Copy main contents
  bcopy(src,dst,sizeof(struct paragraph));

  if (dst->current_line) dst->current_line=line_clone(src->current_line);
  
  for(i=0;i<src->line_count;i++)
    dst->paragraph_lines[i]=line_clone(src->paragraph_lines[i]);      
  
  return 0;
};

int paragraph_dump(struct paragraph *p)
{
  int i;
  for(i=0;i<p->line_count;i++) {
    fprintf(stderr,"  ");
    line_dump(p->paragraph_lines[i]);
  }
  if (p->current_line) {
    fprintf(stderr,"  current line: ");
    line_dump(p->current_line);
  }
  return 0;
}

int paragraph_append(struct paragraph *dst,struct paragraph *src)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);
  // paragraph_dump(dst);
  
  int i,j;

  for(i=0;i<src->line_count;i++) {
    fprintf(stderr,"  Appending ");
    line_dump(src->paragraph_lines[i]);
	  
    if (!src->paragraph_lines[i]->piece_count) {
      // Empty lines are vspace markers, so just re-add the vspace
      paragraph_insert_vspace(dst,src->paragraph_lines[i]->line_height);
    } else {
      for(j=0;j<src->paragraph_lines[i]->piece_count;j++)
	{
	  struct type_face *preserved_current_font = current_font;
	  current_font=src->paragraph_lines[i]->fonts[j];
	 
	  // Checkpoint where we are up to, in case we need to split the line.
	  if (dst->current_line) {
	    dst->current_line->checkpoint=dst->current_line->piece_count;
	    // XXX - Adjust the checkpoint so that we don't split verse numbers
	    // from text, or footnote marks from the initial verse.
	    if (dst->current_line->piece_count>1) {
	      struct type_face *face
		=dst->current_line->fonts[dst->current_line->piece_count-1];
	      if (!strcmp(face->font_nickname,"footnotemarkinfootnote"))
		dst->current_line->checkpoint--;
	    }
	  }

	  // Don't force spaces at start of lines, so that formatting comes out
	  // right with appended footnotes etc.
	  paragraph_append_characters(dst,
				      src->paragraph_lines[i]->pieces[j],
				      src->paragraph_lines[i]->actualsizes[j],
				      src->paragraph_lines[i]->piece_baseline[j],
				      0);
	  current_font = preserved_current_font;
	}
    }
  }

  // fprintf(stderr,"Paragraph after appending is:\n");
  // paragraph_dump(dst);

  
  return 0;
};

int paragraph_height(struct paragraph *p)
{
  // fprintf(stderr,"%s():\n",__FUNCTION__);

  float height=0;
  int i;
  
  for(i=0;i<p->line_count;i++) {
    line_calculate_height(p->paragraph_lines[i]);
    height+=p->paragraph_lines[i]->line_height;
  }
  if (p->current_line) {
    line_calculate_height(p->current_line);
    height+=p->current_line->line_height;
  }
  
  return height;
};
