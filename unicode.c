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

int unicode_replace(char *text,int *len,
		    int offset,int number_of_chars_to_replace,
		    int unicode_point)
{
  int bytes;
  if (unicode_point<0) return -1;
  else if (unicode_point<0x80) bytes=1;
  else if (unicode_point<0x800) bytes=2;
  else if (unicode_point<0x10000) bytes=3;
  else if (unicode_point<0x20000) bytes=4;
  else return -1;
  
  // Make space for the unicode point.
  bcopy(&text[offset+number_of_chars_to_replace],
	&text[offset+bytes],
	strlen(text)-offset-number_of_chars_to_replace);
  (*len)+=bytes-number_of_chars_to_replace;

  // We can now encode the point at &text[offset] using bytes bytes
  switch(bytes) {
  case 1:
    text[offset]=unicode_point&0x7f;
    return 0;
  case 2:
    text[offset]=0xc0+((unicode_point>>6)&0x1f);
    text[offset+1]=0x80+((unicode_point>>0)&0x3f);
    return 0;
  case 3:
    text[offset]=0xe0+((unicode_point>>12)&0x0f);
    text[offset+1]=0x80+((unicode_point>>6)&0x3f);
    text[offset+2]=0x80+((unicode_point>>0)&0x3f);
    return 0;
  case 4:
    text[offset]=0xf0+((unicode_point>>12)&0x07);
    text[offset+1]=0x80+((unicode_point>>12)&0x3f);
    text[offset+2]=0x80+((unicode_point>>6)&0x3f);
    text[offset+3]=0x80+((unicode_point>>0)&0x3f);
    return 0;
  default:
    return -1;
  }
  
}

int dump_bytes(char *text,int len)
{
  int i;
  for(i=0;i<len;i++) fprintf(stderr," 0x%02x",(unsigned char)text[i]);
  fprintf(stderr,"\n");
  return 0;
}

int unicodify(char *text,int *token_len,int max_len,
	      unsigned char next_char)
{
  int len=*token_len;

  // em- and en- dashes
  if (len>=3) {
    if ((text[len-2]=='-')
	&&(text[len-1]=='-')) {
      
      if (text[len-3]=='-') {
	// Replace --- with em-dash, which conveniently takes the same number
	// of characters to encode in UTF8
	unicode_replace(text,token_len,len-3,3,0x2014);
      } else {
	if (next_char!='-') {
	  // Replace -- with en-dash
	  unicode_replace(text,token_len,len-2,2,0x2013);
	}
      }
    }
  }

  // Quotation marks and apostrophes
  if (text[len-1]=='\'') {
    if (((len==1)||(text[len-2]!='\''))&&(next_char!='\'')) {
      // Lone closing quote.
      // If surrounded by letters, then it must be an apostrophe,
      // else we assume it to be a single closing book quote
      if ((len>1)&&(isalpha(text[len-2]))&&(isalpha(next_char))) {
	// Surrounded by letters, so is an apostrophe
	return unicode_replace(text,token_len,len-1,1,0x0027);
      } else {
	// Lacks a letter one side or the other, so is a closing quote
	return unicode_replace(text,token_len,len-1,1,0x2019);
      }
    }
    if ((len>1)&&(text[len-2]=='\'')&&(text[len-1]=='\'')) {
      // Two closing quotes
      return unicode_replace(text,token_len,len-2,2,0x201D);
    }
  }
  if (text[len-1]=='`') {
    if ((len>1)&&(text[len-2]=='`')&&(text[len-1]=='`')) {
      // Two opening quotes
      return unicode_replace(text,token_len,len-2,2,0x201C);
    }
    if (((len==1)||(text[len-2]!='`'))&&(next_char!='`')) {
      // Lone opening quote
      return unicode_replace(text,token_len,len-1,1,0x2018);
    }
  }
  
  return 0;
}

int unicodePointIsHangable(int codepoint)
{
  switch(codepoint)
    {
    case '\"': case '`': case '\'':
    case '.': case ';': case ':': case ',': case ' ':
    case 0x00a0: // non-breaking space
    case 0x2018: // left single book quote mark
    case 0x2019: // right single book quote mark
    case 0x201c: // left double book quote mark
    case 0x201d: // right double book quote mark
      return 1;
    default:
      return 0;
    }
}
