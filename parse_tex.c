/*
  Generate a PDF of the ISV bible (or another translation).
  Routines to parse the LaTeX formatted version of the ISV books to
  generate the text units that need to be rendered.

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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include "generate.h"

/* Basically we want to build a tree structure where we parse out the
   different LaTeX tags and attach the text present in each tag in the
   appropriate branch of the tree.  The text itself should be parsed into
   "words", i.e., things that are separated from one another by space.

   Complications are introduced by the need to keep track of verses, and
   their accumulated foot notes, so that pages can be filled, taking into
   account the space required for the footnotes.  Because the page fill
   decision process happens late, for now we will just accumulate the
   footnotes into a list.
   
*/

int next_file_token(struct parsed_text *p,
		    int token_type,int token_len,char *token_text)
{
  token_text[token_len]=0;
  printf("Token: type=%d, len=%d,text=%s\n",token_type,token_len,
	 token_text);
  return 0;
}

struct parsed_text *parse_tree(char *filename)
{
  FILE *f=fopen(filename,"r");
  if (!f) {
    fprintf(stderr,"Could not read latex file '%s'\n",filename);
    exit(-1);
  }
  fseek(f,0,SEEK_END);
  off_t fileLength=ftello(f);
  
  struct parsed_text *p=calloc(sizeof(struct parsed_text),1);
  p->type=PT_ROOT;

  // Memory map the file for efficient processing
  unsigned char *file=mmap(NULL, fileLength, PROT_READ, MAP_SHARED,
			   fileno(f), 0);
  if (file==MAP_FAILED) {
    fprintf(stderr,"Could not memory map file '%s'\n",filename);
    exit(-1);    
  }

  // File is memory mapped, so we can tokenise
  int i;
#define PS_NORMAL 0
#define PS_SLASH 1
  int parse_state=PS_NORMAL;

  int token_len=0;
  char token_text[1024];
  int token_type=0;
  
  for(i=0;i<fileLength;i++) {
    switch(parse_state) {
    case PS_NORMAL:
      switch(file[i]) {
      case '\\':
	parse_state=PS_SLASH; break;
      case ' ': case '\r': case '\n': case '\t':
	// white space, so end token
	// XXX - skip any number of space and tab as a single token.
	if (token_len) {
	  // Got a token, so pass it up, and reset token status
	  next_file_token(p,token_type,token_len,token_text);
	  token_len=0;
	  token_type=TT_TEXT;
	}

	// merge any number of spaces together
	while (file[i]==' '&&file[i+1]==' ') i++;
	
	// Is this an end of paragraph?
	if ((file[i]=='\r'||file[i]=='\n')
	    &&(file[i+1]=='\r'||file[i+1]=='\n'))
	  {
	    while (file[i+1]=='\r'||file[i+1]=='\n') i++;
	    next_file_token(p,TT_PARAGRAPH,0,token_text);
	  }
	break;
      case '{':
	// start of tag token
	token_type=TT_TAG;
	next_file_token(p,token_type,token_len,token_text);
	token_len=0;
	token_type=TT_TEXT;
	break;
      case '}':
	// end of tag token
	if (token_len) {
	  // Got a token, so pass it up, and reset token status
	  next_file_token(p,token_type,token_len,token_text);
	  token_len=0;
	  token_type=TT_TEXT;
	}
	// and also report the end of tag
	next_file_token(p,TT_ENDTAG,0,token_text);
	break;
      default:
	break;
      }
      break;
    case PS_SLASH:
      // Check for latex escape characters
      break;
    default:
      parse_state=PS_NORMAL;
      break;
    } 
  }
  
  munmap(file,fileLength);
  fclose(f);
  return p;
}
