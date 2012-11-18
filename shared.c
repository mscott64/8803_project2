#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void send_error(int hSocket, char *error_msg, char *descrip)
{
  int len = strlen(error_msg) + strlen(descrip);
  char pBuffer[len];
  char *buf_ptr = pBuffer;
  memcpy(buf_ptr, error_msg, strlen(error_msg));
  buf_ptr += strlen(error_msg);
  memcpy(buf_ptr, descrip, strlen(descrip));
  write(hSocket, pBuffer, len);
  
  if(close(hSocket) == -1)
    printf("Could not close socket\n");
}

int parse_url(char *url, char **scheme, char **hostname, char *path)
{
  char *u = strtok(url, "/:");
  int n = 0;
  int hasPort = 0;
  int req_port = 80;
  while(u != NULL)
  {
    switch(n)
    {
    case 0:
      *scheme = u;
      break;
    case 1:
      *hostname = u;
      break;
    case 2:
      if(isdigit(u[0])) 
      {
	hasPort = 1;
	req_port = atoi(u);
      } else {
	sprintf(path, "%s", u);
      }
      break;
    case 3:
      if(hasPort == 1) {
	sprintf(path, "%s", u);
	break;
      }
    default:
      sprintf(path, "%s/%s", path, u);
    }
    u = strtok(NULL, "/:");
    n++;
  }
  return req_port;
}
