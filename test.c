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
#include <dirent.h>
#include "ft2build.h"
#include FT_FREETYPE_H
#include "hpdf.h"
#include "generate.h"

int do_files_differ(char *path1,char *path2)
{
  FILE *f1 = fopen(path1, "r");
 if (!f1) {  return -1;  };
 FILE *f2 = fopen(path2, "r");
 if (!f2) {  return -1;  };
 int samefile = 1;
 int c1, c2;
 while ((samefile!=0) && (((c1 = getc(f1)) != EOF) || ((c2 = getc(f2)) != EOF)))
   if (c1 != c2) samefile = 0;
 fclose (f1), fclose (f2);

 if (samefile) return 0; else return 1;
}

int run_test()
{
  // File to write recording to
  if (recording_filename) {
    free(recording_filename);
    recording_filename=NULL;
  }
  recording_filename=strdup("run.out");
  // By default record the first page of output
  // (test.profile can change this)
  page_to_record=1;

  // Clear result for test
  unlink("PASSED");
  unlink("FAILED");
  unlink("run.out");
  
  // Typeset the test
  read_profile("test.profile");
  setup_job();
  typeset_file("test.tex");
  finish_job();

  // Compare run.out with correct.out
  FILE *f;
  if (do_files_differ("correct.out","run.out")) {
    // Test failure
    f=fopen("FAILED","w"); if (f) fclose(f);
    return -1;
  } else {
    // Test passed
    f=fopen("PASSED","w"); if (f) fclose(f);
    return 0;
  }
}

int run_tests(char *testdir)
{
  DIR *d=opendir(testdir);
  if (!d) {
    perror("Could not open test directory");
    exit(-1);
  }
  struct dirent *de=NULL;

  chdir(testdir);
  
  de=readdir(d);
  while(de) {
    if (de->d_name[0]!='.') {
      fprintf(stderr,"TEST: %s\n",de->d_name);

      // Enter individual test dir
      if (chdir(de->d_name)==0) {
	if (run_test()) fprintf(stderr,"FAIL: %s: Output differs\n",de->d_name);
	else fprintf(stderr,"PASS: %s\n",de->d_name);
      
	// step back out to directory containing list of tests
	chdir("..");
      } else {
	fprintf(stderr,"FAIL: %s: Could not enter directory\n",
		de->d_name);
      }
    }
    de=readdir(d);    
  }
  fprintf(stderr,"All done.\n");
  
  return 0;
}
