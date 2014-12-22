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

char *recording_filename=NULL;
int page_to_record=-1;
int current_page=0;

FILE *r=NULL;
int record_begin_recording()
{
  if (recording_filename) r=fopen(recording_filename,"w+");
  return 0;
}

int record_end_recording()
{
  fclose(r);
  r=NULL;
  return 0;
}

int record_write_item(char *item)
{
  if (r) fprintf(r,"%s",item);
  return 0;
}

char item[1024];

int record_newpage()
{
  if (current_page==page_to_record) record_end_recording();
  current_page++;
  if (current_page==page_to_record) record_begin_recording();
  return 0;
}

int record_text(struct type_face *font,char *text,int x,int y,int radians)
{
  if (current_page==page_to_record) {
    snprintf(item,1024,"text:%d:%d:%d:%s\n",x,y,radians,text);
    record_write_item(item);
  }
  return 0;
}

int record_fillcolour(float red,float green,float blue)
{
  snprintf(item,1024,"fillcolour:%f:%f:%f\n",red,green,blue);
  record_write_item(item);
  return 0;
}

int record_text_end()
{
  return 0;
}

int record_rectangle(int x,int y,int width,int height)
{
  snprintf(item,1024,"rectangle:%d:%d:%d:%d\n",x,y,width,height);
  record_write_item(item);
  return 0;
}
