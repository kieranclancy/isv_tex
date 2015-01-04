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
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#define SHA1_Init CC_SHA1_Init
#define SHA1_Update CC_SHA1_Update
#define SHA1_Final CC_SHA1_Final
#define SHA_CTX CC_SHA1_CTX
#define SHA_DIGEST_LENGTH (CC_SHA1_DIGEST_LENGTH)
#else
#include <openssl/sha.h>
#endif
#include "ft2build.h"
#include FT_FREETYPE_H
#include "hpdf.h"
#include "generate.h"

/*
  Add this config line to the hash calculation for the configuration
  (after we normalise it by skipping leading spaces and trailing CR/LF).
  Thus we can build a hash of the complete configuration, including any
  included content, that is insensitive to which files the matter comes from.

  The final hash will be used as the starting value when hashing line
  structures to find the cache filename for loading or saving pre-calculated
  scores and heights.  The hash of the config is required since the page size
  and fonts used will affect the placement of the line contents.

*/

int config_sha_inited=0;
SHA_CTX config_sha;
char config_hash[SHA_DIGEST_LENGTH*2+1]={0};

int hash_configline(char *line)
{
  if (!config_sha_inited) {
    SHA1_Init(&config_sha);
    config_sha_inited=1;
  }

  // XXX Normalise before adding
  SHA1_Update(&config_sha,line,strlen(line));
  
  return 0;
}

int hash_configend()
{
  unsigned char md[SHA_DIGEST_LENGTH];
  SHA1_Final(md,&config_sha);

  int i;
  for(i=0;i<SHA_DIGEST_LENGTH;i++)
    sprintf(&config_hash[i*2],"%02x",md[i]);
  fprintf(stderr,"Config hash is %s\n",config_hash);
  return 0;
}
