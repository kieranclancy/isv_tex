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
#include "hpdf.h"

void error_handler(HPDF_STATUS error_number, HPDF_STATUS detail_number,
		   void *data)
{
  fprintf(stderr,"HPDF error: %04x.%u\n",
	  (HPDF_UINT)error_number,(HPDF_UINT)detail_number);
  exit(-1);
}

// Page size in points
int page_width=72*5;
int page_height=72*7;
// Colour of "red" text
char *red_colour="#000000";
int left_margin=72;
int right_margin=72;
int top_margin=72;
int bottom_margin=72;
int marginpar_width=50;
int marginpar_margin=8;
int booktab_fontsize=12;
int booktab_width=27;
int booktab_height=115;
int booktab_upperlimit=36;
int booktab_lowerlimit=72*5.5;
char *booktab_fontfile="booktab.ttf";

char *black_fontfile="blacktext.ttf";
int black_fontsize=8;
char *red_fontfile="redtext.ttf";
int red_fontsize=8;
char *versenum_fontfile="blacktext.ttf";
int versenum_fontsize=4;
char *chapternum_fontfile="redtext.ttf";
int chapternum_fontsize=8;
int chapternum_lines=2;
char *footnotemark_fontfile="blacktext.ttf";
int footnotemark_fontsize=4;


/* Read the profile of the bible to build.
   The profile consists of a series of key=value pairs that set various 
   parameters for the typesetting.
*/
#define MAX_INCLUDE_DEPTH 32
int include_depth=0;
char *include_files[MAX_INCLUDE_DEPTH];
int include_lines[MAX_INCLUDE_DEPTH];

int include_show_stack()
{
  int i;
  for(i=include_depth-1;i>=0;i--)
    {
      fprintf(stderr,"In file included from %s:%d\n",
	      include_files[i],include_lines[i]);
    }
  return 0;
}

int include_push(char *file,int line_num)
{
  if (include_depth>=MAX_INCLUDE_DEPTH) {
    include_show_stack();
    fprintf(stderr,"%s:%d:Includes nested too deeply.\n",file,line_num);
    exit(-1);
  }
  int i;
  for(i=0;i<include_depth;i++) {
    if (!strcmp(include_files[i],file)) {
      include_show_stack();
      fprintf(stderr,"%s:%d:Include nested too deeply.\n",file,line_num);
      exit(-1);
    }
  }
  include_files[include_depth]=strdup(file);
  include_lines[include_depth++]=line_num;
  return 0;
}

int include_pop()
{
  if (include_depth>0) free(include_files[include_depth-1]);
  include_depth--;
  return 0;
}

int read_profile(char *file)
{
  FILE *f=fopen(file,"r");
  char line[1024];

  int errors=0;
  
  if (!f) {
    include_show_stack();
    fprintf(stderr,"Could not read profile file '%s'\n",file);
    exit(-1);
  }

  int line_num=0;
  
  line[0]=0; fgets(line,1024,f);
  while(line[0])
    {
      line_num++;
      char key[1024],value[1024];
      if (line[0]!='#'&&line[0]!='\r'&&line[0]!='\n') {
	if (sscanf(line,"%[^ ] %[^\r\n]",key,value)==2)
	  {
	    if (!strcasecmp(key,"include")) {
	      include_push(file,line_num);
	      read_profile(value);
	      include_pop();
	    }

	    // Size of page
	    else if (!strcasecmp(key,"page_width")) page_width=atoi(value);
	    else if (!strcasecmp(key,"page_height")) page_height=atoi(value);

	    // Margins of a left page (left_margin and right_margin get switched
	    // automatically if output is for a book
	    else if (!strcasecmp(key,"left_margin")) left_margin=atoi(value);
	    else if (!strcasecmp(key,"right_margin")) right_margin=atoi(value);
	    else if (!strcasecmp(key,"top_margin")) top_margin=atoi(value);
	    else if (!strcasecmp(key,"bottom_margin")) bottom_margin=atoi(value);

	    // Width of marginpar for holding cross-references
	    else if (!strcasecmp(key,"marginpar_width")) marginpar_width=atoi(value);
	    // Margin between marginpar and edge of page
	    else if (!strcasecmp(key,"marginpar_margin")) marginpar_margin=atoi(value);

	    // Size of solid colour book tabs
	    else if (!strcasecmp(key,"booktab_fontsize"))
	      booktab_fontsize=atoi(value);
	    else if (!strcasecmp(key,"booktab_fontfile"))
	      booktab_fontfile=strdup(value);
	    else if (!strcasecmp(key,"booktab_width")) booktab_width=atoi(value);
	    else if (!strcasecmp(key,"booktab_height")) booktab_height=atoi(value);
	    // Set vertical limit of where booktabs can be placed
	    else if (!strcasecmp(key,"booktab_upperlimit")) booktab_upperlimit=atoi(value);
	    else if (!strcasecmp(key,"booktab_lowerlimit")) booktab_lowerlimit=atoi(value);

	    // colour of red text
	    else if (!strcasecmp(key,"red")) red_colour=strdup(value);

	    else {
	      include_show_stack();
	      fprintf(stderr,"%s:%d:Syntax error (unknown key '%s')\n",
		      file,line_num,key);
	      errors++;
	    }
	  } else {
	  include_show_stack();
	  fprintf(stderr,"%s:%d:Syntax error (should be keyword value)\n",file,line_num);
	  errors++;
	}
      }
      line[0]=0; fgets(line,1024,f);
    }
  fclose(f);
  if (errors) exit(-1); else return 0;
}

int main(int argc,char **argv)
{
  if (argc==2) 
    read_profile(argv[1]);
  else
    {
      fprintf(stderr,"usage: generate <profile>\n");
      exit(-1);
    }

  // font_name = HPDF_LoadTTFontFromFile (pdf, "/usr/local/fonts/arial.ttf", HPDF_TRUE);

  return 0;
}
