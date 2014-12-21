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

int crossreference_start()
{
  // There is actually nothing that needs to be done here.
  if (0) fprintf(stderr,"Starting to collect cross-references for %s %s:%s\n",
		 crossreference_book,
		 crossreference_chapter,
		 crossreference_verse);
  return 0;
}

int crossreference_end()
{
  free(crossreference_book); crossreference_book=NULL;
  free(crossreference_chapter); crossreference_chapter=NULL;
  free(crossreference_verse); crossreference_verse=NULL;

  // XXX Clone paragraph into list of paragraphs for cross-references
  
  current_line_flush(&crossreference_paragraph);
  paragraph_clear(&crossreference_paragraph);
  
  target_paragraph=&body_paragraph;

  crossreference_mode=0;
  
  return 0;
}

int crossreference_register_verse(struct paragraph *p,
				  char *book,int chapter, int verse)
{
  fprintf(stderr,"Saw %s %d:%d\n",book,chapter,verse);
  return 0;
}
