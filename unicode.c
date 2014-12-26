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
#include <ctype.h>
#include "hpdf.h"
#include "generate.h"

int unicodify(char *text,int *token_len,int max_len,
	      unsigned char next_char)
{
  int len=*token_len;
  
  if (len>=3) {
    if ((text[len-2]=='-')
	&&(text[len-1]=='-')) {
      
      if (text[len-3]=='-') {
	// Replace --- with em-dash, which conveniently takes the same number
	// of characters to encode in UTF8
	text[len-3]=0xE2;
	text[len-2]=0x80;
	text[len-1]=0x94;
	fprintf(stderr,"Replaced --- with em-dash\n");
      } else {
	if (next_char!='-') {
	  // Replace -- with en-dash
	  text[len-2]=0xE2;
	  text[len-1]=0x80;
	  text[len]=0x93;
	  (*token_len)++;
	  fprintf(stderr,"Replaced --- with en-dash near %d:%d\n",
		  chapter_label,verse_label);
	}
      }
    }
  }
  return 0;
}
