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
