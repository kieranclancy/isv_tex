/*
  Log or compare a log of actions that are supposed to be deterministic
  to find where multiple runs of a program deviate from being deterministic.

  This program is copyright Paul Gardner-Stephen 2015, and is offered
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
#include <unistd.h>
#include <assert.h>

FILE *log_file=NULL;
int determinism_compare=0;
char determinism_line_of_input[1024];

int determinism_initialise()
{
  log_file=fopen("determinism.log","r");
  if (!log_file) {
    log_file=fopen("determinism.log","w");    
  } else
    determinism_compare=1;
  assert(log_file);
  
  return 0;
}

char *determinism_read_line()
{
  if (!log_file) determinism_initialise();
  determinism_line_of_input[0]=0;
  fgets(determinism_line_of_input,1024,log_file);
  return determinism_line_of_input;
}

int _determinism_event_integer(int event)
{  
  if (determinism_compare) {
    int v;
    char *l=determinism_read_line();
    int dud=0;
    int r=sscanf(l,"int:%d",&v);
    if (r!=1) { dud|=1; fprintf(stderr,"r=%d\n",r); }
    if (v!=event) dud|=2;

    if (dud) {
      fprintf(stderr,"Expected event int:%d, but saw %s (error code: %d)\n",
	      event,l,dud);
      exit(-1);
    }
    return 0;
  }
  if (!log_file) determinism_initialise();
  fprintf(log_file,"int:%d\n",event);
  return 0;
}

int _determinism_event_float(float event)
{  
  if (determinism_compare) {
    float v;
    char *l=determinism_read_line();
    if ((sscanf(l,"float:%f",&v)!=1)||(v!=event)) {
      fprintf(stderr,"Expected event float:%f, but saw %s\n",
	      event,l);
      exit(-1);
    }
    return 0;
  }
  if (!log_file) determinism_initialise();
  fprintf(log_file,"float:%f\n",event);
  return 0;
}
