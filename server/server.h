#ifndef SERVER_H
#define SERVER_H

void server_create(void);

void send_error(int hSocket, char *error_msg, char *descrip);

#endif /* SERVER_H */
