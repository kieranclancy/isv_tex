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
long long determinism_line_number=0;

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
  determinism_line_number++;
  return determinism_line_of_input;
}

int _determinism_event_integer(int event,const char *file,int line,const char *func)
{  
  if (determinism_compare) {
    int v;
    int li;
    char fi[1024];
    char *l=determinism_read_line();
    int dud=0;
    int r=sscanf(l,"int:%d:%[^:]:%d",&v,fi,&li);
    if (r!=3) { dud|=1; fprintf(stderr,"r=%d\n",r); }
    if (v!=event) dud|=2;

    if (dud) {
      fprintf(stderr,"Event #%lld: Expected event int:%d:%s:%d, but this time we saw %s\n",
	      determinism_line_number,event,file,line,l);
      exit(-1);
    }
    fprintf(stderr,"determinism event ok: %s\n",l);
    return 0;
  }
  if (!log_file) determinism_initialise();
  fprintf(log_file,"int:%d:%s:%d\n",event,file,line);
  if (determinism_line_number<10)
    fprintf(stderr,"determinism log event: int:%d:%s:%d\n",
	    event,file,line);
  determinism_line_number++;

  return 0;
}

int _determinism_event_float(float event,const char *file,int line,const char *func)
{  
  if (determinism_compare) {
    float v;
    char *l=determinism_read_line();
    int li;
    char fi[1024];
    int r=sscanf(l,"float:%f:%[^:]:%d",&v,fi,&li);
    if ((r!=3)||(v!=event)) {
      fprintf(stderr,"Event #%lld: Expected event float:%f:%s:%d, but this time we saw %s\n",
	      determinism_line_number,event,file,line,l);
      exit(-1);
    }
    fprintf(stderr,"determinism event ok: %s\n",l);
    return 0;
  }
  if (!log_file) determinism_initialise();
  fprintf(log_file,"float:%f:%s:%d\n",event,file,line);
  if (determinism_line_number<10)
    fprintf(stderr,"determinism log event: float:%f:%s:%d\n",
	    event,file,line);
  determinism_line_number++;
  return 0;
}
