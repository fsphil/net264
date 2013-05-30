
/* A quick and dirty h.264 byte stream streamer
 * for the Raspberry Pi camera.
 *
 * Usage: raspivid -t 9999999 -fps 25 -o - | net264
 *
 * Copyright 2010-2012 Philip Heron <phil@sanslogic.co.uk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Disclaimer: This was written with only the most basic knowledge of the
               h.264 byte stream. It's the minimum I needed to allow
               me to stream the Raspberry Pi camera to multiple clients,
               and will almost certainly not work for anything else.
   -Phil */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define IN_SIZE (1024 * 1024) /* 1MB */
#define OUT_SIZE (8 * IN_SIZE) /* 8MB */

#define DEFAULT_PORT (5500)
#define DEFAULT_MAX_CLIENTS (10)

typedef struct {
	
	/* h.264 byte stream input */
	FILE *fin;
	
	/* Incoming buffer */
	uint8_t *in;
	size_t in_len;
	
	/* Output buffer */
	uint8_t *out;
	size_t out_len;
	size_t out_key_frame_offset;
	
	/* Listening socket */
	int port;
	int max_clients;
	int in_sock;
	int *in_client;
	
} state_t;

int send_block(int sock, uint8_t *src, size_t len)
{
	while(len)
	{
		int r = send(sock, src, len, 0);
		if(r < 0) break;
		src += r;
		len -= r;
	}
	
	return(len ? -1 : 0);
}

int send_frame(state_t *s, int sock)
{
	uint8_t marker[4] = { 0x00, 0x00, 0x00, 0x01 };
	if(send_block(sock, marker, 4) != 0) return(-1);
	return(send_block(sock, s->in, s->in_len));
}

int write_frame(state_t *s)
{
	if(s->out_len + s->in_len + 4 > OUT_SIZE)
	{
		fprintf(stderr, "warning: output buffer full\n");
		return(0);
	}
	
	s->out[s->out_len++] = 0x00;
	s->out[s->out_len++] = 0x00;
	s->out[s->out_len++] = 0x00;
	s->out[s->out_len++] = 0x01;
	
	memcpy(s->out + s->out_len, s->in, s->in_len);
	s->out_len += s->in_len;
	
	return(0);
}

/* Reads until a marker is found, or EOF */
int read_to_marker(state_t *s)
{
	uint8_t i = 0;
	int c;
	
	s->in_len = 0;
	
	while((c = fgetc(s->fin)) != EOF)
	{
		s->in[s->in_len++] = c;
		if(s->in_len == IN_SIZE)
		{
			fprintf(stderr, "warning: input buffer full\n");
			return(0);
		}
		
		/* Watch out for markers */
		if(c == 0) i++;
		else if(c == 1 && i == 3)
		{
			s->in_len -= 4;
			return(0);
		}
		else i = 0;
	}
	
	if(s->in_len > 0) return(0);
	
	return(EOF);
}

void exit_usage()
{
	fprintf(stderr,
		"Usage: net264 [-p port] [-m max_clients]\n"
		"\n"
		"Port defaults to 5500.\n"
		"Max Clients defaults to 100\n"
	);
	exit(-1);
}

int main(int argc, char *argv[])
{
	state_t s;
	struct sockaddr_in6 addr;
	int optval;
	int i;
	
	s.port = DEFAULT_PORT;
	s.max_clients = DEFAULT_MAX_CLIENTS;
	
	opterr = 0;
	while((i = getopt(argc, argv, "p:m:")) != -1)
	{
		switch(i)
		{
		case 'p': s.port = atoi(optarg); break;
		case 'm': s.max_clients = atoi(optarg); break;
		case '?': exit_usage();
		}
	}
	
	if(optind < argc) exit_usage();
	if(s.max_clients <= 0)
	{
		fprintf(stderr, "Maximum clients is invalid\n");
		exit_usage();
	}
	
	/* Ignore annoying signal */
	signal(SIGPIPE, SIG_IGN);
	
	s.fin = stdin;
	
	/* Allocate memory for the frame in buffer */
	s.in = malloc(IN_SIZE);
	if(!s.in)
	{
		perror("malloc");
		return(-1);
	}
	s.in_len = 0;
	
	/* Same for output frame buffer */
	s.out = malloc(OUT_SIZE);
	if(!s.out)
	{
		perror("malloc");
		return(-1);
	}
	s.out_len = 0;
	s.out_key_frame_offset = 0;
	
	/* Create the listener sockets */
	s.in_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if(s.in_sock < 0)
	{
		perror("socket");
		return(-1);
	}
	
	optval = 1;
	if(setsockopt(s.in_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		perror("setsockopt");
		return(-1);
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_any;
	addr.sin6_port = htons(s.port);
	
	if(bind(s.in_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		perror("bind");
		return(-1);
	}
	
	if(listen(s.in_sock, 10) < 0)
	{
		perror("listen");
		return(-1);
	}
	
	/* Allocate an empty client list */
	s.in_client = calloc(s.max_clients, sizeof(int));
	if(!s.in_client)
	{
		perror("calloc");
		return(-1);
	}
	
	/* Read the next frame */
	while(1)
	{
		fd_set rd;
		int maxfd, r;
		
		/* Check for incoming socket or H.264 data */
		FD_ZERO(&rd);
		FD_SET(fileno(s.fin), &rd);
		FD_SET(s.in_sock, &rd);
		maxfd = (fileno(s.fin) > s.in_sock ? fileno(s.fin) : s.in_sock) + 1;
		
		select(maxfd, &rd, NULL, NULL, NULL);
		
		if(FD_ISSET(s.in_sock, &rd))
		{
			struct sockaddr_in6 raddr;
			socklen_t raddrlen;
			char host[INET6_ADDRSTRLEN];
			
			/* Find a clear client slot */
			for(i = 0; i < s.max_clients; i++)
				if(s.in_client[i] == 0) break;
			
			/* No space? */
			if(i == s.max_clients)
			{
				/* Accept and close the socket */
				int sock = accept(s.in_sock, NULL, NULL);
				if(sock >= 0) close(sock);
				continue;
			}
			
			/* Get the socket */
			if((s.in_client[i] = accept(s.in_sock, NULL, NULL)) < 0)
			{
				perror("accept");
				s.in_client[i] = 0;
				continue;
			}
			
			raddrlen = sizeof(raddr);
			getpeername(s.in_client[i], (struct sockaddr *) &raddr, &raddrlen);
			if(inet_ntop(AF_INET6, &raddr.sin6_addr, host, sizeof(host)))
				fprintf(stderr, "Connection from %s\n", host);
			
			/* Write the buffered frames */
			r = send_block(s.in_client[i], s.out, s.out_len);
			if(r < 0)
			{
				/* Failed to send all data */
				close(s.in_client[i]);
				s.in_client[i] = 0;
			}
		}
		
		if(FD_ISSET(fileno(s.fin), &rd))
		{
			if(read_to_marker(&s) == EOF) break;
			
			if(s.in_len == 0) continue;
			
			switch(s.in[0])
			{
			case 0x27: /* Header frames? */
			case 0x28:
				s.out_len = s.out_key_frame_offset;
				write_frame(&s);
				s.out_key_frame_offset = s.out_len;
				break;
			
			case 0x25: /* Key frame */
				s.out_len = s.out_key_frame_offset;
				write_frame(&s);
				break;
			
			case 0x21: /* I frame */
				write_frame(&s);
				break;
			
			default:
				fprintf(stderr, "Unknown frame type 0x%02X\n", s.in[0]);
				break;
			}
			
			for(i = 0; i < s.max_clients; i++)
			{
				if(s.in_client[i] == 0) continue;
				
				/* Write the frame */
				r = send_frame(&s, s.in_client[i]);
				if(r < 0)
				{
					/* Failed to send all data */
					close(s.in_client[i]);
					s.in_client[i] = 0;
				}
			}
		}
	}
	
	close(s.in_sock);
	for(i = 0; i < s.max_clients; i++)
			if(s.in_client[i] != 0) close(s.in_client[i]);
	free(s.in_client);
	fclose(s.fin);
	free(s.in);
	
	fprintf(stderr, "EOF\n");
	
	return(0);
}

