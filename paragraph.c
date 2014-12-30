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

int paragraph_flush(struct paragraph *p_in)
{  
  //  fprintf(stderr,"%s():\n",__FUNCTION__);

  // First flush the current line
  current_line_flush(p_in);

  // Put line-breaks into paragraph to fit column width as required.
  struct paragraph *p=layout_paragraph(p_in);
  
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

  // Keep first two and last two lines together to stop orphans and widows.
  if (p->line_count>1) {
    p->paragraph_lines[0]->tied_to_next_line=1;
    p->paragraph_lines[p->line_count-2]->tied_to_next_line=1;    
  }
  // Also keep the first two lines of text following a passage header or passage
  // info header.
  int countdown=0;
  for(i=0;i<p->line_count;i++) {
    int isHeading=0;
    if (p->paragraph_lines[i]->piece_count) {
      if (!strcasecmp(p->paragraph_lines[i]->pieces[0].font->font_nickname,"passageheader"))
	isHeading=1;
      if (!strcasecmp(p->paragraph_lines[i]->pieces[0].font->font_nickname,"passageinfo"))
	isHeading=1;
      if (countdown) p->paragraph_lines[i]->tied_to_next_line=1;
      if (isHeading) countdown=2; else if (countdown>0) countdown--;
    }
  }

  // Actually draw the lines
  int isBodyParagraph=0;
  if (p_in==&body_paragraph) isBodyParagraph=1;
  for(i=0;i<p->line_count;i++) line_emit(p,i,isBodyParagraph);

  // Clear out old lines in input
  for(i=0;i<p_in->line_count;i++) line_free(p_in->paragraph_lines[i]);
  p_in->line_count=0;

  // ... and also in the laid-out version of the paragraph
  for(i=0;i<p->line_count;i++) line_free(p->paragraph_lines[i]);
  p->line_count=0;
  free(p);
  
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
  fprintf(stderr,"%s()\n",__FUNCTION__);
  
  // Append any line in progress before creating fresh line
  if (p->current_line) paragraph_append_line(p,p->current_line);
  
  // Allocate structure
  p->current_line=calloc(sizeof(struct line_pieces),1); 

  p->current_line->line_uid=line_uid_counter++;

  if (p->line_count)
    p->current_line->alignment
      =p->paragraph_lines[p->line_count-1]->alignment;
  else {
    // XXX - Make default alignment configurable
    p->current_line->alignment=AL_JUSTIFIED;
  }
  
  // Set maximum line width
  p->current_line->max_line_width=page_width-left_margin-right_margin;

#ifdef NOTDEFINED
  // If there is a dropchar margin in effect, then apply it.
  if (p->drop_char_margin_line_count>0) {
    if (1) fprintf(stderr,
		   "Applying dropchar margin of %dpt (%d more lines, including this one)\n",
		   p->drop_char_left_margin,p->drop_char_margin_line_count);
    p->current_line->max_line_width
      =page_width-left_margin-right_margin-p->drop_char_left_margin;
    p->current_line->left_margin=p->drop_char_left_margin;
    p->drop_char_margin_line_count--;
    if (p->drop_char_margin_line_count) p->current_line->tied_to_next_line=1;
  }
#endif
  
  line_apply_poetry_margin(p,p->current_line);
  if (0) fprintf(stderr,"New line left margin=%dpts, max_width=%dpts\n",
		 p->current_line->left_margin,p->current_line->max_line_width);
  
  return 0;
}

int paragraph_set_widow_counter(struct paragraph *p,int lines)
{
  fprintf(stderr,"%s() ENTER\n",__FUNCTION__);
  if (!p->current_line) paragraph_setup_next_line(p);
  p->current_line->tied_to_next_line=1;
  fprintf(stderr,"Tied line to next (line %d) (widow counter)\n",p->line_count);
  fprintf(stderr,"%s() EXIT\n",__FUNCTION__);
  return 0;
}

int paragraph_append_characters(struct paragraph *p,char *text,int size,int baseline,
				int forceSpaceAtStartOfLine, int nobreak)
{
  // fprintf(stderr,"%s(\"%s\",%d)\n",__FUNCTION__,text,size);
  
  if (!p->current_line) paragraph_setup_next_line(p);

  // Don't start lines with empty space.
  if ((!strcmp(text," "))&&(p->current_line->piece_count==0))
    if (!forceSpaceAtStartOfLine) return 0;

  // Verse numbers at the start of paragraphs appear left of the margin
  // (some at the start of lines do too, but that happens in line_recalculate_width()
  int is_hanging_verse=0;  
  if ((p->line_count==0)
      &&(!p->current_line->piece_count)
      &&!strcmp("versenum",current_font->font_nickname)) {
    is_hanging_verse=1;
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

  // 0xa0 for non-breaking space has zero width as measured by HPDF_Page_TextWidth,
  // so measure it as a real space instead.
  text_width = HPDF_Page_TextWidth(page,
				   (((unsigned char)text[0])!=0xa0)?text:" ");
  if (0) fprintf(stderr,"  text_width=%.1f, height=%.1f, font=%p('%s'), size=%d, text='%s'\n",
		 text_width,text_height,current_font->font,
		 current_font->font_nickname,size,text);
  
  line_append_piece(p->current_line,
		    new_line_piece(text,current_font,size,text_width,NULL,baseline,
				   nobreak));

  // Mark line as poetry if required.
  p->current_line->poem_level=p->poem_level;
  
  // Don't waste time recalculating width after every word.  Requires O(n^2) time
  // with respect to line length.
  // line_recalculate_width(p->current_line);

  // Keep dropchars and their associated lines together
  // XXX - This is now done in paragraph_layout().  It does mean that dropchars cannot
  // span multiple paragraphs any longer, however.
  //  if (current_font->line_count>1)
  //    paragraph_set_widow_counter(p,current_font->line_count-1);

  // Don't break lines as we gather input.  Line breaks should happen once a paragraph
  // has been fully collected. At that point a tex-like dynamic programming scheme
  // should be used to identify the optimal layout using an arbitrary set of
  // constraints.
  if (0) {
  //  if (p->current_line->line_width_so_far>=p->current_line->max_line_width) {
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
	  p->current_line->pieces[p->current_line->piece_count]
	    =last_line->pieces[i];
	  p->current_line->piece_count++;	  
	}
      line_remove_leading_space(p->current_line);
      line_recalculate_width(p->current_line);
      line_recalculate_width(last_line);
      
      // Inherit alignment of previous line
      if (p->poem_level) last_line->alignment=AL_LEFT;
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
			  int forceSpaceAtStartOfLine,
			  int nobreak)
{  
  fprintf(stderr,"%s(\"%s\")\n",__FUNCTION__,text);

  // Don't put verse number immediately following dropchar
  if (p->current_line&&(p->current_line->piece_count==1))
    {
      if (!strcasecmp(p->current_line->pieces[p->current_line->piece_count-1].font->font_nickname,"chapternum"))
	if (!strcasecmp(current_font->font_nickname,"versenum"))
	  return 0;

    }

  
  // Keep track of whether the last character is a full stop so that we can
  // apply double spacing between sentences (if desired).
  if (text[strlen(text)-1]=='.') p->last_char_is_a_full_stop=1;
  else p->last_char_is_a_full_stop=0;

  // Checkpoint where we are up to, in case we need to split the line
  line_set_checkpoint(p->current_line);
  
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
		int nobreak_flag=nobreak;
		if (j<strlen(text)) nobreak_flag=1;
		// case change
		chars[count]=0;
		if (islower)
		  paragraph_append_characters(p,chars,current_font->smallcaps,
					      baseline+current_font->baseline_delta,
					      forceSpaceAtStartOfLine,
					      nobreak_flag);
		else
		  paragraph_append_characters(p,chars,current_font->font_size,
					      baseline+current_font->baseline_delta,
					      forceSpaceAtStartOfLine,
					      nobreak_flag);
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
					forceSpaceAtStartOfLine,nobreak);
	  else
	    paragraph_append_characters(p,chars,current_font->font_size,
					baseline+current_font->baseline_delta,
					forceSpaceAtStartOfLine,nobreak);
	  break;
	}
      }
  } else {
    // Regular text. Render as one piece.
    paragraph_append_characters(p,text,current_font->font_size,
				baseline+current_font->baseline_delta,
				forceSpaceAtStartOfLine,nobreak);
  }
    
  return 0;
}

/* Add a space to a paragraph.  Similar to appending text, but adds elastic
   space that can be expanded if required for justified text.
*/
int paragraph_append_space(struct paragraph *p,
			   int forceSpaceAtStartOfLine, int nobreak)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);

  // Don't put spaces after dropchars
  if (p->current_line&&(p->current_line->piece_count==1))
    {
      if (!strcasecmp(p->current_line->pieces[p->current_line->piece_count-1]
		      .font->font_nickname,"chapternum"))
	return 0;

    }
  
  // Checkpoint where we are up to, in case we need to split the line
  line_set_checkpoint(p->current_line);
  paragraph_append_characters(p," ",current_font->font_size,0,
			      forceSpaceAtStartOfLine,nobreak);
  return 0;
}

// Thin space.  Append a normal space, then revise it's width down to 1/2
int paragraph_append_thinspace(struct paragraph *p,int forceSpaceAtStartOfLine,
			       int nobreak)
{
  fprintf(stderr,"%s()\n",__FUNCTION__);
  // Checkpoint where we are up to, in case we need to split the line.
  // We don't want to break on a thin space, so we need to go back to the
  // previous elastic space
  line_set_checkpoint(p->current_line);
  paragraph_append_characters(p," ",current_font->font_size,0,
			      forceSpaceAtStartOfLine,nobreak);
  // If the thin-space isn't added to the line, don't go causing a segfault.
  if (p->current_line->piece_count) {
    p->current_line->pieces[p->current_line->piece_count-1].piece_width/=2;
    p->current_line->pieces[p->current_line->piece_count-1].natural_width/=2;

    // Mark thinspace non-elastic
    p->current_line->pieces[p->current_line->piece_count-1].piece_is_elastic=0;
  }

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

int paragraph_insert_vspace(struct paragraph *p,int points,int tied)
{
  fprintf(stderr,"%s(%dpt)\n",__FUNCTION__,points);
  paragraph_setup_next_line(p);
  p->current_line->line_height=points;
  p->current_line->tied_to_next_line=tied;
  if (tied) fprintf(stderr,"Tied line to next (line %d) (via vspace)\n",p->line_count);

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
    // Adjust line position following dropchars
    if (current_font->line_count>1) {
      struct line_pieces *l=target_paragraph->current_line;
      int max_hang_space
	=right_margin
	-crossref_margin_width-crossref_column_width
	-2;  // plus a little space to ensure some white space
      l->pieces[l->piece_count-1].natural_width+=max_hang_space;
      line_recalculate_width(l);
      fprintf(stderr,"After closing dropchar text\n");
      paragraph_dump(target_paragraph);
    }
    // Add vertical space after certain type faces
    if (!strcasecmp(current_font->font_nickname,"booktitle"))
      {
	paragraph_insert_vspace(p,type_faces[set_font("blackletter")].linegap/2,1);
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
    fprintf(stderr,"  %c ",
	    p->paragraph_lines[i]->tied_to_next_line?'*':' ');
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
      paragraph_insert_vspace(dst,src->paragraph_lines[i]->line_height,
			      src->paragraph_lines[i]->tied_to_next_line);
    } else {
      for(j=0;j<src->paragraph_lines[i]->piece_count;j++)
	{
	  struct type_face *preserved_current_font = current_font;
	  current_font=src->paragraph_lines[i]->pieces[j].font;
	 
	  // Checkpoint where we are up to, in case we need to split the line.
	  if (dst->current_line) {
	    dst->current_line->checkpoint=dst->current_line->piece_count;
	    // XXX - Adjust the checkpoint so that we don't split verse numbers
	    // from text, or footnote marks from the initial verse etc.
	    line_set_checkpoint(dst->current_line);
	  }

	  // Don't force spaces at start of lines, so that formatting comes out
	  // right with appended footnotes etc.
	  paragraph_append_characters(dst,
				      src->paragraph_lines[i]->pieces[j].piece,
				      src->paragraph_lines[i]->pieces[j].actualsize,
				      src->paragraph_lines[i]->pieces[j].piece_baseline,
				      NO_FORCESPACEATSTARTOFLINE,
				      src->paragraph_lines[i]->pieces[j].nobreak);
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

int paragraph_insert_line(struct paragraph *p,int line_number, struct line_pieces *l)
{
  int i;

  if (p->line_count>=MAX_LINES_IN_PARAGRAPH) {
    fprintf(stderr,"Too many lines in paragraph.\n");
    exit(-1);
  }
  
  // Copy lines up one
  for(i=p->line_count;i>line_number;i--)
    p->paragraph_lines[i]=p->paragraph_lines[i-1];

  p->paragraph_lines[line_number]=l;
  p->line_count++;
  
  return 0;
}
