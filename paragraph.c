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
#include <time.h>
#include "ft2build.h"
#include FT_FREETYPE_H
#include "hpdf.h"
#include "generate.h"

int paragraph_init(struct paragraph *p)
{
  bzero(p,sizeof(struct paragraph));
  return 0;
}

int _paragraph_clear(struct paragraph *p,const char *func,const char *file,int line)
{
  if (0)
    fprintf(stderr,"paragraph_clear(%p) called from %s:%d %s()\n",
	    p,file,line,func);

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

int paragraph_free(struct paragraph *p)
{
  paragraph_clear(p);
  free(p);
  return 0;
}

time_t last_paragraph_report_time=0;
int paragraph_count=0;
struct paragraph *body_paragraphs[MAX_PARAGRAPHS];

int paragraph_flush(struct paragraph *p_in,int drawingPage)
{  
  //  fprintf(stderr,"%s():\n",__FUNCTION__);

  // First flush the current line
  current_line_flush(p_in);

  if (paragraph_count>=MAX_PARAGRAPHS) {
    fprintf(stderr,"Too many paragraphs. Increase MAX_PARAGRAPHS.\n");
    exit(-1);
  }

  // Don't create a new paragraph when the current paragraph is empty
  if (!p_in->line_count) return 0;
  
  struct paragraph *p=new_paragraph();
  paragraph_clone(p,p_in);
  paragraph_clear(p_in);

  paragraph_analyse(p);
   
  //  if (last_paragraph_report_time<time(0)) {
    last_paragraph_report_time=time(0);
    fprintf(stderr,"Analysed paragraph %d:\n",paragraph_count);
    paragraph_dump(p);
    fflush(stderr);
    //  }

    body_paragraphs[paragraph_count++]=p;

  return 0;
}

/* To build a paragraph we need to build lines, and then to know 
   which lines are inseparable and those which can be separated, i.e.,
   what the separable units of the paragraph are.

   At the next lower level we need to assemble lines from the incoming words.

   Line assembly has a few corner cases to handle.  First, some smallcaps
   output is emulated using the capital characters of a regular font, in
   which case words may need to be split into two or more pieces, with no
   space in between.  Similarly foot notes and verse numbers are pieces
   which must be able to be placed without any space before or after them
   depending on the context.

*/
int paragraph_append_current_line(struct paragraph *p)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (!p->current_line) {
    fprintf(stderr,"Attempted to append NULL line to paragraph.\n");
    exit(-1);
  }
  
  if (p->line_count>=MAX_LINES_IN_PARAGRAPH) {
    fprintf(stderr,"Too many lines in paragraph.\n");
    paragraph_dump(p);
    exit(-1);
  }

  p->paragraph_lines[p->line_count++]=p->current_line;
  p->current_line=NULL;
  return 0;
}

int line_uid_counter=0;
int paragraph_setup_next_line(struct paragraph *p)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);
  
  // Append any line in progress before creating fresh line
  if (p->current_line) {
    if (p->current_line->piece_count||p->current_line->line_height)
      paragraph_append_current_line(p);
    else
      // No need to setup next line if we already have an empty one on hand.
      return 0;
    p->current_line=NULL;
  }
  
  // Allocate structure
  p->current_line=new_line();

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
    if (0) fprintf(stderr,
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
  // fprintf(stderr,"%s() ENTER\n",__FUNCTION__);
  if (!p->current_line) paragraph_setup_next_line(p);
  p->current_line->tied_to_next_line=1;
  // fprintf(stderr,"Tied line to next (line %d) (widow counter)\n",p->line_count);
  // fprintf(stderr,"%s() EXIT\n",__FUNCTION__);
  return 0;
}

int paragraph_append_characters(struct paragraph *p,char *text,int size,int baseline,
				int forceSpaceAtStartOfLine, int nobreak,
				int token_number)
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
				   nobreak,token_number));

  // Mark line as poetry if required.
  p->current_line->poem_level=p->poem_level;
  
  return 0;
}

int paragraph_append_text(struct paragraph *p,char *text,int baseline,
			  int forceSpaceAtStartOfLine,
			  int nobreak, int token_number)
{  
  // fprintf(stderr,"%s(\"%s\")\n",__FUNCTION__,text);

  // Don't put verse number immediately following dropchar
  if (p->current_line&&(p->current_line->piece_count==1))
    {
      if (!strcasecmp(p->current_line->pieces[p->current_line->piece_count-1].font->font_nickname,"chapternum"))
	if (!strcasecmp(current_font->font_nickname,"versenum"))
	  return 0;

    }

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
					      nobreak_flag, token_number);
		else
		  paragraph_append_characters(p,chars,current_font->font_size,
					      baseline+current_font->baseline_delta,
					      forceSpaceAtStartOfLine,
					      nobreak_flag, token_number);
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
					forceSpaceAtStartOfLine,nobreak,
					token_number);
	  else
	    paragraph_append_characters(p,chars,current_font->font_size,
					baseline+current_font->baseline_delta,
					forceSpaceAtStartOfLine,nobreak,
					token_number);
	  break;
	}
      }
  } else {
    // Regular text. Render as one piece.
    paragraph_append_characters(p,text,current_font->font_size,
				baseline+current_font->baseline_delta,
				forceSpaceAtStartOfLine,nobreak, token_number);
  }
    
  return 0;
}

/* Add a space to a paragraph.  Similar to appending text, but adds elastic
   space that can be expanded if required for justified text.
*/
int paragraph_append_space(struct paragraph *p,
			   int forceSpaceAtStartOfLine, int nobreak,
			   int token_number)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

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
			      forceSpaceAtStartOfLine,nobreak, token_number);
  return 0;
}

// Thin space.  Append a normal space, then revise it's width down to 1/2
int paragraph_append_thinspace(struct paragraph *p,int forceSpaceAtStartOfLine,
			       int nobreak, int token_number)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);
  // Checkpoint where we are up to, in case we need to split the line.
  // We don't want to break on a thin space, so we need to go back to the
  // previous elastic space
  line_set_checkpoint(p->current_line);
  paragraph_append_characters(p," ",current_font->font_size,0,
			      forceSpaceAtStartOfLine,nobreak, token_number);
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

struct type_face *stashed_type_face_stack0[TYPE_FACE_STACK_DEPTH];
int stashed_type_face_stack_pointer0=0;
struct type_face *stashed_current_font0=NULL;

struct type_face *stashed_type_face_stack1[TYPE_FACE_STACK_DEPTH];
int stashed_type_face_stack_pointer1=0;
struct type_face *stashed_current_font1=NULL;

int paragraph_stash_style_stack(int n)
{
  fprintf(stderr," {stash %d:%d}",type_face_stack_pointer,n);
  if (n==0) {
    stashed_type_face_stack_pointer0=type_face_stack_pointer;
    bcopy(&type_face_stack,&stashed_type_face_stack0,
	  sizeof(struct type_face *)*TYPE_FACE_STACK_DEPTH);
    stashed_current_font0=current_font;
  } else {
    stashed_type_face_stack_pointer1=type_face_stack_pointer;
    bcopy(&type_face_stack,&stashed_type_face_stack1,
	  sizeof(struct type_face *)*TYPE_FACE_STACK_DEPTH);
    stashed_current_font1=current_font;
  }
  return 0;
}

extern int blackletterfont_index;

int paragraph_fetch_style_stack(int n)
{
  if (n==0) {
    type_face_stack_pointer=stashed_type_face_stack_pointer0;
    bcopy(&stashed_type_face_stack0,&type_face_stack,
	  sizeof(struct type_face *)*TYPE_FACE_STACK_DEPTH);
    current_font=stashed_current_font0;
  } else {
    type_face_stack_pointer=stashed_type_face_stack_pointer1;
    bcopy(&stashed_type_face_stack1,&type_face_stack,
	  sizeof(struct type_face *)*TYPE_FACE_STACK_DEPTH);
    current_font=stashed_current_font1;
  }
  if (!type_face_stack_pointer)
    if (blackletterfont_index==-1)
      blackletterfont_index=set_font_by_name("blackletter");
    current_font=&type_faces[blackletterfont_index];

  fprintf(stderr," {fetch %d:%d}",type_face_stack_pointer,n);
  return 0;
}

int paragraph_push_style(struct paragraph *p, int font_alignment,int font_index)
{
  if (0) fprintf(stderr,"%s(): alignment=%d, style=%s\n",__FUNCTION__,
		 font_alignment,type_faces[font_index].font_nickname);

  if ((!p->current_line)
      ||(p->current_line->piece_count
	 &&p->current_line->alignment!=font_alignment
	 &&p->current_line->alignment!=AL_NONE))
    {
      // Change of alignment - start on new line
      // fprintf(stderr,"Creating new line due to alignment change.\n");
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
  // fprintf(stderr,"%s(%dpt)\n",__FUNCTION__,points);
  paragraph_setup_next_line(p);
  p->current_line->line_height=points;
  p->current_line->tied_to_next_line=tied;
  // if (tied) fprintf(stderr,"Tied line to next (line %d) (via vspace)\n",p->line_count);

  current_line_flush(p);
  return 0;
}

int paragraph_pop_style(struct paragraph *p)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);

  if (type_face_stack_pointer==footnote_stack_depth) {
    // fprintf(stderr,"Ending footnote collection mode.\n");
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
      if (l) {
	l->pieces[l->piece_count-1].natural_width+=max_hang_space;
	// line_recalculate_width(l);
      }
      if (0) {
	fprintf(stderr,"After closing dropchar text\n");
	paragraph_dump(target_paragraph);
      }
    }
    // Add vertical space after certain type faces
    if (!strcasecmp(current_font->font_nickname,"booktitle"))
      {
	if (blackletterfont_index==-1)
	  blackletterfont_index=set_font_by_name("blackletter");
	paragraph_insert_vspace(p,type_faces[blackletterfont_index].linegap/2,1);
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
  // fprintf(stderr,"%s()\n",__FUNCTION__);
  if (blackletterfont_index==-1)
    blackletterfont_index=set_font_by_name("blackletter");
  current_font=&type_faces[blackletterfont_index];
  type_face_stack_pointer=0;
  return 0;
}

/* Clone the lines in one paragraph into the other.
   The destination paragraph is assumed to be empty.  Memory
   will be leaked if this is not the case.
*/
int paragraph_clone(struct paragraph *dst,struct paragraph *src)
{
  //  fprintf(stderr,"%s():\n",__FUNCTION__);
  
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

int paragraph_append(struct paragraph *dst,struct paragraph *src, int skip)
{
  // fprintf(stderr,"%s()\n",__FUNCTION__);
  // paragraph_dump(dst);
  
  int i,j;

  for(i=0;i<src->line_count;i++) {
    // fprintf(stderr,"  Appending ");
    // line_dump(src->paragraph_lines[i]);
	  
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

	  if (skip) skip--;
	  else {
	    // Don't force spaces at start of lines, so that formatting comes out
	    // right with appended footnotes etc.
	    paragraph_append_characters(dst,
					src->paragraph_lines[i]->pieces[j].piece,
					src->paragraph_lines[i]->pieces[j].actualsize,
					src->paragraph_lines[i]->pieces[j].piece_baseline,
					NO_FORCESPACEATSTARTOFLINE,
					src->paragraph_lines[i]->pieces[j].nobreak,
					src->paragraph_lines[i]->pieces[j].token_number);
	  }
	  current_font = preserved_current_font;
	}
    }
  }

  // fprintf(stderr,"Paragraph after appending is:\n");
  // paragraph_dump(dst);

  
  return 0;
};

float paragraph_height(struct paragraph *p)
{
  // fprintf(stderr,"%s():\n",__FUNCTION__);

  float height=0;
  int i;
  
  for(i=0;i<p->line_count;i++) {
    line_calculate_height(p->paragraph_lines[i],
			  0,p->paragraph_lines[i]->piece_count);
    height+=p->paragraph_lines[i]->line_height;
  }
  if (p->current_line) {
    line_calculate_height(p->current_line,0,p->current_line->piece_count);
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


struct paragraph *new_paragraph()
{
  struct paragraph *p=malloc(sizeof(struct paragraph));
  p->line_count=0;
  p->current_line=0;
  p->drop_char_left_margin=0;
  p->drop_char_margin_line_count=0;
  p->poem_level=0;
  p->poem_subsequent_line=0;
  p->first_crossref_line=0;
  p->src_book=NULL;
  p->src_chapter=0;
  p->src_verse=0;
  p->total_height=0;
  p->noindent=0;
  p->crossref_heights=NULL;
  return p;
}

int paragraph_analyse(struct paragraph *p)
{
  int i;
  for(i=0;i<p->line_count;i++) line_analyse(p,i);
  return 0;
}
