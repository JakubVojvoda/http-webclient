/**
 * 
 *  Simple HTTP client
 *  by Jakub Vojvoda [vojvoda@swdeveloper.sk]
 *  2013
 * 
 *  file: webclient.c
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PATTERN 1024
#define BUFFER_LEN 1024
#define SUCCESS 0
#define FAILED -1

#define BEGIN_OF_STRING 0
#define NOTHING -1
#define END_OF_STRING -2

#define CHUNKED 1
#define NORM 0

typedef struct {
  int status;
  char message[BUFFER_LEN];
} tConn;

typedef struct {
  int status;
  char domain[256];
  int port;
  char path[256];
} tUrl;

tUrl decodeUrl (char *string);
void createRequest (char *request, tUrl url);
int codeUnsafeChar (char *string);
tConn selectStatus(char *string);
int connectToServer(tUrl url, int *vsocket);
int findChunk (char *string, int noread);
int selectEnconding (char *string);
void exctractFileName(char *path, char *filename);
int selectLocation (char *string, char *redir);
int readHeader(int vsocket, char *answer);

int main (int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: webclient URL\n");
    return EXIT_FAILURE;
  }

  char msg[BUFFER_LEN];
  int vsocket;

  tUrl url = decodeUrl(argv[1]);
  if (url.status != SUCCESS) {
    fprintf(stderr, "Wrong format of URL.\n");
    return EXIT_FAILURE;
  }

  char filename[BUFFER_LEN];
  char filechar[BUFFER_LEN];

  exctractFileName(url.path, filechar);
  strcpy(filename, filechar);

  codeUnsafeChar(url.domain);
  codeUnsafeChar(url.path);

  createRequest(msg, url);

  if (connectToServer(url, &vsocket) == FAILED) {
    fprintf(stderr, "Failed: connect to server\n");
    return EXIT_FAILURE;
  }

  if (write(vsocket, msg, strlen(msg)) == FAILED) {
    fprintf(stderr, "Failed: write request\n");
    return EXIT_FAILURE;
  }

  char answer[BUFFER_LEN];
  if (readHeader(vsocket, answer) == FAILED) {
    fprintf(stderr, "Failed: read header\n");
    return EXIT_FAILURE;
  }

  int enconding = selectEnconding(answer);
  tConn connection;
  connection = selectStatus(answer);

  if (connection.status >= 400) {
    fprintf(stderr, "%d\n",connection.status);
    return EXIT_FAILURE;
  }

  char msg_redir[BUFFER_LEN];
  int connect_try = 1;
  tUrl url_redir;

  // Redirection
  while (connection.status == 301 || connection.status == 302) {
    if (selectLocation(answer, msg_redir) == FAILED) {
      fprintf(stderr, "Failed: redirection\n");
      return EXIT_FAILURE;
    }

    url_redir = decodeUrl(msg_redir);
    if (url_redir.status != SUCCESS) {
      fprintf(stderr, "Failed: decode URL (redirection %d)\n",connect_try);
      return EXIT_FAILURE;
    }

    exctractFileName(url_redir.path, filename);
    codeUnsafeChar(url_redir.domain);
    codeUnsafeChar(url_redir.path);

    createRequest(msg_redir, url_redir);

    if (connectToServer(url_redir, &vsocket) == FAILED) {
      fprintf(stderr, "Failed: connect to server\n");
      return EXIT_FAILURE;
    }

    if (write(vsocket, msg_redir, strlen(msg_redir)) == FAILED) {
      fprintf(stderr, "Failed: write request (redirection %d)\n",connect_try);
      return EXIT_FAILURE;
    }

    if (readHeader(vsocket, answer) == FAILED) {
      fprintf(stderr, "Failed: read header\n");
      return EXIT_FAILURE;
    }

    enconding = selectEnconding(answer);
    connection = selectStatus(answer);

    if (connection.status == FAILED) {
      fprintf(stderr, "%s\n", connection.message);
      return EXIT_FAILURE;
    }

    if (connection.status >= 400) {
      fprintf(stderr, "%d\n",connection.status);
      return EXIT_FAILURE;
    }

    connect_try++;

    if (connect_try > 5) {
      fprintf(stderr, "Multiple (5) redirection.");
      return EXIT_FAILURE;
    }
  }

  FILE *fw;
  remove(filename);
  if ((fw = fopen(filename,"a")) == NULL) {
    fprintf(stderr, "Failed: open dest file\n");
    return EXIT_FAILURE;
  }

  // Non-chunked coding
  if (enconding != CHUNKED) {
    char ch;
    while (read(vsocket, &ch, sizeof(char)) > 0)
      fprintf(fw,"%c",ch);
    fclose(fw);
    return SUCCESS;
  }

  // Chunked coding
  char chr1 = '#';
  char chr2 = '#';
  int i = 0;
  int chunk = 0;

  char ch[BUFFER_LEN];

  while (read(vsocket, &chr2, sizeof(char)) > 0) {
    ch[i] = chr2;
    i++;
    if (chr1 == '\r' && chr2 == '\n') {
      ch[i-2] = '\0';
      chunk = (int) strtol(ch, NULL, 16);
      i = 0;
      break;
    }
    chr1 = chr2;
  }

  int w = chunk;
  char p;

  while (w > 0) {
    if (read(vsocket, &p, sizeof(char)) == FAILED) {
      fprintf(stderr, "Failed: read data\n");
      return EXIT_FAILURE;
    }
    w--;
    fprintf(fw, "%c", p);
  }

  char ch1 = '#', ch2 = '#';
  chunk = -1;
  char chr[BUFFER_LEN];
  int n = 0;

  while (chunk != 0) {
    if (read(vsocket, &ch2, sizeof(char)) == FAILED) {
      fprintf(stderr, "Failed: read chunk or \\r\\n\n");
      return EXIT_FAILURE;
    }

    if (ch1 == '\r' && ch2 == '\n') {

      while (ch2 != '\r') {
        if (read(vsocket, &ch2, sizeof(char)) == FAILED) {
          fprintf(stderr, "Failed: read chunk\n");
          return EXIT_FAILURE;
        }
        chr[i] = ch2;
        i++;
      }

      ch1 = ch2;
      chr[i] = '\0';
      i = 0;
      if (read(vsocket, &ch2, sizeof(char)) == FAILED) {
        fprintf(stderr, "Failed: read chunked data\n");
        return EXIT_FAILURE;
      }

      if (ch1 == '\r' && ch2 == '\n') {
        chunk = (int) strtol(chr, NULL, 16);
        if (chunk == 0)
          break;
        n = chunk;

        while (n > 0) {
          if (read(vsocket, &p, sizeof(char)) == FAILED) {
            fprintf(stderr, "Failed: read chunked data\n");
            return EXIT_FAILURE;
          }
          n--;
          fprintf(fw, "%c", p);
        }

        ch2 = '#';
      }
    }
    ch1 = ch2;
  }
  fclose(fw);
  return EXIT_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////////

/* Header reading
*/
int readHeader(int vsocket, char *answer)
{
  char chr1 = '#', chr2 = '#';
  int i = 0;

  while (read(vsocket, &chr2, sizeof(char)) > 0) {
    answer[i] = chr2;
    i++;
    if (chr1 == '\r' && chr2 == '\n') {
      if (read(vsocket, &chr1, sizeof(char)) == FAILED)
        return FAILED;

      answer[i] = chr1;
      i++;

      if (read(vsocket, &chr2, sizeof(char)) == FAILED)
        return FAILED;

      answer[i] = chr2;
      i++;
      if (chr1 == '\r' && chr2 == '\n') {
        break;
      }
    }
    chr1 = chr2;
  }
  answer[i] = '\0';
  return SUCCESS;
}

/* Obtaining the URL of redirection
*/
int selectLocation (char *string, char *redir)
{
  char *pattern = "Location: ([^\n]+)\r\n";
  regex_t re;
  regmatch_t pmatch[PATTERN];

  if(regcomp( &re, pattern, REG_EXTENDED) != 0)
    return FAILED;

  int status = regexec( &re, string, PATTERN, pmatch, 0);

  if (status != SUCCESS)
    return FAILED;

  char r[BUFFER_LEN];

  if (pmatch[1].rm_so >= 0 && pmatch[1].rm_eo >= 0 ) {
      strncpy(r, string+pmatch[1].rm_so, pmatch[1].rm_eo-pmatch[1].rm_so);
      r[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
    }

  strcpy(redir, r);

  return SUCCESS;
}

/* Output file name extraction
*/
void exctractFileName(char *path, char *filename)
{
  char *p = strrchr(path, '/');

  if (p == NULL || strcmp(p, "/") == 0) {
    strcpy(filename, "index.html");
    return;
  }

  int len = strlen(p);
  int i;

  for (i = 1; i < len; i++) {
    if (p[i] != '?' && p[i] != '#')
      filename[i-1] = p[i];
    else {
      filename[i-1] = '\0';
      break;
    }
  }
  filename[i-1] = '\0';

  if (i == 1)
    strcpy(filename, "index.html");

  return;
}

/* Connecting to the server, obtaining a socket descriptor
*/
int connectToServer(tUrl url, int *vsocket)
{
  struct sockaddr_in sin = {0};
  struct hostent *hptr;
  char msg[BUFFER_LEN];

  createRequest(msg, url);

  if ((*vsocket = socket(PF_INET, SOCK_STREAM, 0 ) ) < 0)
    return FAILED;

  sin.sin_family = PF_INET;
  sin.sin_port = htons(url.port);

  if ((hptr =  gethostbyname(url.domain)) == NULL) {
    //fprintf(stderr, "Check network connection.\n");
    return FAILED;
  }

  memcpy( &sin.sin_addr, hptr->h_addr, hptr->h_length);

  if (connect (*vsocket, (struct sockaddr *)&sin, sizeof(sin) ) != 0 )
    return FAILED;

  return EXIT_SUCCESS;
}

/* Status code and string from the header extraction
*/
tConn selectStatus(char *string)
{
  tConn stat;

  char *pattern = "^HTTP/1.1 ([0-9]+) ([a-zA-Z ]+)";
  regex_t re;
  regmatch_t pmatch[PATTERN];

  if(regcomp( &re, pattern, REG_EXTENDED) != 0) {
    stat.status = FAILED;
    strcpy(stat.message, "Failed: regcomp" );
    return stat;
  }

  int status = regexec( &re, string, PATTERN, pmatch, 0);
  stat.status = status;

  if (status == SUCCESS) {
    if (pmatch[1].rm_so >= 0 && pmatch[1].rm_eo >= 0 ) {
      char nu1[BUFFER_LEN];
      int ff;
      for (ff = 0; ff < BUFFER_LEN; ff++)
        nu1[ff] = '\0';

      strncpy(nu1, string+pmatch[1].rm_so, pmatch[1].rm_eo-pmatch[1].rm_so);
      stat.status = atoi(nu1);
    }
    if (pmatch[2].rm_so >= 0 && pmatch[2].rm_eo >= 0 ) {
      strncpy(stat.message, string+pmatch[2].rm_so, pmatch[2].rm_eo-pmatch[2].rm_so+2);
      stat.message[pmatch[2].rm_eo-pmatch[2].rm_so] = '\0';
    }
  }
  else {
    strcpy(stat.message, "Failed: Status no match\n");
  }
  return stat;
}

/* Determination of content coding
*/
int selectEnconding (char *string)
{
  char *pattern = "t?T?ransfer-E?e?ncoding: ([^\n]+)\r\n";
  regex_t re;
  regmatch_t pmatch[PATTERN];

  if(regcomp( &re, pattern, REG_EXTENDED) != 0)
    return FAILED;

  int status = regexec( &re, string, PATTERN, pmatch, 0);

  if (status != SUCCESS)
    return NORM;

  char enconding[BUFFER_LEN];

  if (pmatch[1].rm_so >= 0 && pmatch[1].rm_eo >= 0 ) {
      strncpy(enconding, string+pmatch[1].rm_so, pmatch[1].rm_eo-pmatch[1].rm_so);
      enconding[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
    }

  if (strcmp(enconding, "chunked") == 0 || strcmp(enconding, "Chunked") == 0)
    return CHUNKED;

  return NORM;
}

/* Obtaining the information from the URL address
*/
tUrl decodeUrl (char *string)
{
  tUrl url;
  url.path[0] = '\0';
  url.domain[0] = '\0';
  url.port = 80;
  url.status = 0;

  regmatch_t pmatch[PATTERN];
  char *pattern = "^(http://)?([^/:]+)(:[0-9]+)?(/.*)?$";


  regex_t re;
  char buf[BUFFER_LEN];
  if(regcomp( &re, pattern, REG_EXTENDED)!= 0) {
    url.status = FAILED;
    return url;
  }

  url.status = regexec( &re, string, PATTERN, pmatch, 0);

  if(url.status == SUCCESS){
    if (pmatch[2].rm_so >= 0 && pmatch[2].rm_eo >= 0 ) {
      memcpy(url.domain, string+pmatch[2].rm_so, pmatch[2].rm_eo-pmatch[2].rm_so);
      url.domain[pmatch[2].rm_eo-pmatch[2].rm_so] = '\0';
    }

    if (pmatch[3].rm_so >= 0 && pmatch[3].rm_eo >= 0) {
      char num[BUFFER_LEN];
      strncpy(num, string+pmatch[3].rm_so+1, pmatch[3].rm_eo-pmatch[3].rm_so-1);
      url.port = atoi(num);
    }
    else
      url.port = 80;

    if (pmatch[4].rm_so >= 0 && pmatch[4].rm_eo >= 0)
      strncpy(url.path, string+pmatch[4].rm_so, pmatch[4].rm_eo-pmatch[4].rm_so+1);
    else {

      if (strcmp(url.path, "") == 0 && strchr(url.domain, '?') != NULL) {
        char *ptr = strchr(url.domain, '?');
        int index = ptr - url.domain;
        strcpy(url.path, "/");
        strncat(url.path, url.domain + index, strlen(url.domain) - index);
        url.domain[index] = '\0';
      }
      else
        strcpy(url.path, "/");
    }
    regerror(url.status, &re, buf, BUFFER_LEN);
  }
  else {
    return url;
  }

  regfree( &re);
  return url;
}

/* Creating the request to the server
*/
void createRequest(char *request, tUrl url)
{
  strcpy(request, "GET ");
  strcat(request, url.path);
  strcat(request, " HTTP/1.1\r\nHost: ");
  strcat(request, url.domain);
  strcat(request, "\r\nConnection: close\r\n\r\n");
}

/* Unsafe characters encoding 
   - CTL, SP, DQUOTE, "'", "%", "<", ">", "\", "^", "`", "{", "|", "}"
*/
int codeUnsafeChar (char *string)
{
  char hstr[BUFFER_LEN];

  int i;
  for (i = 0; i < BUFFER_LEN; i++)
    hstr[i] = '\0';

  int len = strlen(string);

  i = 0;
  int j = 0;
  for (i = 0; i < len; i++ ,j++) {
    switch (string[i]) {
      case ' ' :
        hstr[j++] = '%';
        hstr[j++] = '2';
        hstr[j] = '0';
        break;
      case '"' :
        hstr[j++] = '%';
        hstr[j++] = '2';
        hstr[j] = '2';
        break;
      case '%' :
        hstr[j++] = '%';
        hstr[j++] = '2';
        hstr[j] = '5';
        break;
      case '\\' :
        hstr[j++] = '%';
        hstr[j++] = '5';
        hstr[j] = 'C';
        break;
      case '^' :
        hstr[j++] = '%';
        hstr[j++] = '5';
        hstr[j] = 'E';
        break;
      case ';' :
        hstr[j++] = '%';
        hstr[j++] = '3';
        hstr[j] = 'B';
        break;
      case '@' :
        hstr[j++] = '%';
        hstr[j++] = '4';
        hstr[j] = '0';
        break;
      case '&' :
        hstr[j++] = '%';
        hstr[j++] = '2';
        hstr[j] = '6';
        break;
      case '$' :
        hstr[j++] = '%';
        hstr[j++] = '2';
        hstr[j] = '4';
        break;
      default:
        hstr[j] = string[i];
        break;
    }
  }
  strcpy(string, hstr);
  string[j] = '\0';
  return EXIT_SUCCESS;
}
