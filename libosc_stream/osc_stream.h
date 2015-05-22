/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _OSC_STREAM_H_
#define _OSC_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

/*****************************************************************************
 * API START
 *****************************************************************************/

typedef struct _osc_stream_t osc_stream_t;
typedef struct _osc_stream_driver_t osc_stream_driver_t;

typedef void *(*osc_stream_recv_req_t)(size_t size, void *data);
typedef void (*osc_stream_recv_adv_t)(size_t written, void *data);

typedef const void *(*osc_stream_send_req_t)(size_t *len, void *data);
typedef void (*osc_stream_send_adv_t)(void *data);

struct _osc_stream_driver_t {
	osc_stream_recv_req_t recv_req;
	osc_stream_recv_adv_t recv_adv;
	osc_stream_send_req_t send_req;
	osc_stream_send_adv_t send_adv;
};

static inline osc_stream_t *
osc_stream_new(uv_loop_t *loop, const char *addr,
	const osc_stream_driver_t *driver, void *data);

static inline void
osc_stream_free(osc_stream_t *stream);

static inline void
osc_stream_flush(osc_stream_t *stream);

/*****************************************************************************
 * API END
 *****************************************************************************/

/*****************************************************************************
 * private
 *****************************************************************************/

#ifndef OSC_STREAM_BUF_SIZE
#	define OSC_STREAM_BUF_SIZE 2048 //TODO how big?
#endif

typedef enum _osc_stream_type_t osc_stream_type_t;
typedef enum _osc_stream_ip_version_t osc_stream_ip_version_t;

typedef struct _osc_stream_udp_t osc_stream_udp_t;
typedef struct _osc_stream_udp_tx_t osc_stream_udp_tx_t;
typedef struct _osc_stream_tcp_t osc_stream_tcp_t;
typedef struct _osc_stream_tcp_tx_t osc_stream_tcp_tx_t;
typedef struct _osc_stream_pipe_t osc_stream_pipe_t;

typedef union _osc_stream_addr_t osc_stream_addr_t;

enum _osc_stream_type_t {
	OSC_STREAM_TYPE_UDP,
	OSC_STREAM_TYPE_TCP,
	OSC_STREAM_TYPE_PIPE
};

enum _osc_stream_ip_version_t {
	OSC_STREAM_IP_VERSION_4,
	OSC_STREAM_IP_VERSION_6,
};

union _osc_stream_addr_t {
	const struct sockaddr ip;
	struct sockaddr_in ip4;
	struct sockaddr_in6 ip6;
};
	
struct _osc_stream_udp_tx_t {
	osc_stream_addr_t addr;
	uv_udp_send_t req;
	uv_buf_t msg;
};

struct _osc_stream_udp_t {
	osc_stream_ip_version_t version;
	int server;
	uv_getaddrinfo_t req;
	uv_udp_t socket;
	osc_stream_udp_tx_t tx;
};

struct _osc_stream_tcp_tx_t {
	uv_tcp_t socket;
	uv_write_t req;
};

struct _osc_stream_tcp_t {
	osc_stream_ip_version_t version;
	int slip;
	int server;
	uv_getaddrinfo_t req;

	int connected;
	osc_stream_tcp_tx_t tx;
	size_t nchunk;

	// responder only
	uv_tcp_t socket;

	//sender only
	uv_connect_t conn;

	int32_t prefix;
	uv_buf_t msg[2];
	char buf_rx [OSC_STREAM_BUF_SIZE];
	char buf_tx [OSC_STREAM_BUF_SIZE];
};

struct _osc_stream_pipe_t {
	uv_pipe_t socket;
	uv_write_t req;
	uv_buf_t msg;
	int fd;
	size_t nchunk;
	char buf_rx [OSC_STREAM_BUF_SIZE];
	char buf_tx [OSC_STREAM_BUF_SIZE];
};

struct _osc_stream_t {
	osc_stream_type_t type;
	const osc_stream_driver_t *driver;
	void *data;
	int flushing;

	union {
		osc_stream_udp_t udp;
		osc_stream_tcp_t tcp;
		osc_stream_pipe_t pipe;
	};
};

/*****************************************************************************
 * SLIP (de)encoding
 *****************************************************************************/

#define SLIP_END					(char)0300	// indicates end of packet
#define SLIP_ESC					(char)0333	// indicates byte stuffing
#define SLIP_END_REPLACE	(char)0334	// ESC ESC_END means END data byte
#define SLIP_ESC_REPLACE	(char)0335	// ESC ESC_ESC means ESC data byte

// SLIP encoding
static inline size_t
slip_encode(char *buf, uv_buf_t *bufs, int nbufs)
{
	char *dst = buf;

	int i;
	for(i=0; i<nbufs; i++)
	{
		uv_buf_t *ptr = &bufs[i];
		char *base = (char *)ptr->base;
		char *end = base + ptr->len;

		char *src;
		for(src=base; src<end; src++)
			switch(*src)
			{
				case SLIP_END:
					*dst++ = SLIP_ESC;
					*dst++ = SLIP_END_REPLACE;
					break;
				case SLIP_ESC:
					*dst++ = SLIP_ESC;
					*dst++ = SLIP_ESC_REPLACE;
					break;
				default:
					*dst++ = *src;
					break;
			}
	}
	*dst++ = SLIP_END;

	return dst - buf;
}

// inline SLIP decoding
static inline size_t
slip_decode(char *buf, size_t len, size_t *size)
{
	char *src = buf;
	char *end = buf + len;
	char *dst = buf;

	while(src < end)
	{
		if(*src == SLIP_ESC)
		{
			src++;
			if(*src == SLIP_END_REPLACE)
				*dst++ = SLIP_END;
			else if(*src == SLIP_ESC_REPLACE)
				*dst++ = SLIP_ESC;
			else
				; //TODO error
			src++;
		}
		else if(*src == SLIP_END)
		{
			src++;
			*size = dst - buf;
			return src - buf;
		}
		else
		{
			if(src != dst)
				*dst = *src;
			src++;
			dst++;
		}
	}
	*size = 0;
	return 0;
}

#undef SLIP_END
#undef SLIP_ESC
#undef SLIP_END_REPLACE
#undef SLIP_ESC_REPLACE

/*****************************************************************************
 * helpers
 *****************************************************************************/

typedef enum _osc_stream_message_t {
	OSC_STREAM_MESSAGE_RESOLVE,
	OSC_STREAM_MESSAGE_TIMEOUT,
	OSC_STREAM_MESSAGE_CONNECT,
	OSC_STREAM_MESSAGE_DISCONNECT
} osc_stream_message_t;

#if defined(__WINDOWS__)
static inline char *
strndup(const char *s, size_t n)
{
    char *result;
    size_t len = strlen (s);
    if (n < len) len = n;
    result = (char *) malloc (len + 1);
    if (!result) return 0;
    result[len] = '\0';
    return (char *) strncpy (result, s, len);
}
#endif

static inline void
_instant_msg(osc_stream_t *stream, osc_stream_message_t type)
{
	const osc_stream_driver_t *driver = stream->driver;

	const char resolve_msg [20]			= "/stream/resolve\0,\0\0\0";
	const char timeout_msg [20]			= "/stream/timeout\0,\0\0\0";
	const char connect_msg [20]			= "/stream/connect\0,\0\0\0";
	const char disconnect_msg [24]	= "/stream/disconnect\0\0,\0\0\0";

	const char *buf = NULL;
	size_t size = 0;
	switch(type)
	{
		case OSC_STREAM_MESSAGE_RESOLVE:
			buf = resolve_msg;
			size = sizeof(resolve_msg);
			break;
		case OSC_STREAM_MESSAGE_TIMEOUT:
			buf = timeout_msg;
			size = sizeof(timeout_msg);
			break;
		case OSC_STREAM_MESSAGE_CONNECT:
			buf = connect_msg;
			size = sizeof(connect_msg);
			break;
		case OSC_STREAM_MESSAGE_DISCONNECT:
			buf = disconnect_msg;
			size = sizeof(disconnect_msg);
			break;
	}

	void *tar;
	if((tar = driver->recv_req(size, stream->data)))
	{
		memcpy(tar, buf, size);
		driver->recv_adv(size, stream->data);
	}
}

static inline void
_instant_err(osc_stream_t *stream, const char *where, int what)
{
	const osc_stream_driver_t *driver = stream->driver;

	const char error_msg [20] = "/stream/error\0\0\0,ss\0";

	const char *err = uv_err_name(what);
	size_t msglen = sizeof(error_msg);
	size_t wherelen = (strlen(where) + 4) & (~3);
	size_t errlen = (strlen(err) + 4) & (~3);
	size_t size = msglen + wherelen + errlen;

	void *tar;
	if((tar = driver->recv_req(size, stream->data)))
	{
		memcpy(tar, error_msg, msglen);
		tar += msglen;

		memcpy(tar, where, wherelen);
		tar += wherelen;

		memcpy(tar, err, errlen);
		//tar += errlen;

		driver->recv_adv(size, stream->data);
	}
}

/*****************************************************************************
 * UDP implementation
 *****************************************************************************/

static inline void
_udp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_udp_t *socket = (uv_udp_t *)handle;
	osc_stream_udp_t *udp = (void *)socket - offsetof(osc_stream_udp_t, socket);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);
	const osc_stream_driver_t *driver = stream->driver;

	if(suggested_size > OSC_STREAM_BUF_SIZE)
		suggested_size = OSC_STREAM_BUF_SIZE;

	buf->base = driver->recv_req(suggested_size, stream->data);
	buf->len = buf->base ? suggested_size : 0;
}

static inline void
_udp_recv_cb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned int flags)
{
	uv_udp_t *socket = (uv_udp_t *)handle;
	osc_stream_udp_t *udp = (void *)socket - offsetof(osc_stream_udp_t, socket);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);
	const osc_stream_driver_t *driver = stream->driver;
	
	if(nread > 0)
	{
		if(udp->server)
		{
			// store client IP for potential replies
			if(addr->sa_family == PF_INET)
				memcpy(&udp->tx.addr.ip4, addr, sizeof(struct sockaddr_in));
			else if(addr->sa_family == PF_INET6)
				memcpy(&udp->tx.addr.ip6, addr, sizeof(struct sockaddr_in6));
		}

		driver->recv_adv(nread, stream->data);
	}
	else if (nread < 0)
	{
		//uv_close((uv_handle_t *)handle, NULL); //TODO
		_instant_err(stream, "_udp_recv_cb", nread);
	}
}

static inline void
_getaddrinfo_udp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_udp_t *udp = (void *)req - offsetof(osc_stream_udp_t, req);
	osc_stream_udp_tx_t *tx = &udp->tx;
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);
	const osc_stream_driver_t *driver = stream->driver;
	int err;

	if( (status >= 0) && res)
	{
		osc_stream_addr_t src;
		char remote [128] = {'\0'};
		unsigned int flags = 0;

		switch(udp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				struct sockaddr_in *ptr4 = (struct sockaddr_in *)res->ai_addr;

				if((err = uv_ip4_name(ptr4, remote, 127)))
					goto fail;
				if((err = uv_udp_init(loop, &udp->socket)))
					goto fail;
				if((err = uv_ip4_addr(remote, ntohs(ptr4->sin_port), &tx->addr.ip4)))
					goto fail;
				if((err = uv_ip4_addr("0.0.0.0", 0, &src.ip4)))
					goto fail;

				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				struct sockaddr_in6 *ptr6 = (struct sockaddr_in6 *)res->ai_addr;

				if((err = uv_ip6_name(ptr6, remote, 127)))
					goto fail;
				if((err = uv_udp_init(loop, &udp->socket)))
					goto fail;
				if((err = uv_ip6_addr(remote, ntohs(ptr6->sin6_port), &tx->addr.ip6)))
					goto fail;
				if((err = uv_ip6_addr("::", 0, &src.ip6)))
					goto fail;

				flags |= UV_UDP_IPV6ONLY; //TODO make this configurable?

				break;
			}
		}
		
		if((err = uv_udp_bind(&udp->socket, &src.ip, flags)))
			goto fail;

		if(  (udp->version == OSC_STREAM_IP_VERSION_4)
			&& !strcmp(remote, "255.255.255.255") )
		{
			if((err = uv_udp_set_broadcast(&udp->socket, 1)))
				goto fail;
		}

		if((err = uv_udp_recv_start(&udp->socket, _udp_alloc, _udp_recv_cb)))
			goto fail;

		_instant_msg(stream, OSC_STREAM_MESSAGE_RESOLVE);
	}
	else
	{
		_instant_msg(stream, OSC_STREAM_MESSAGE_TIMEOUT);
	}

	if(res)
		uv_freeaddrinfo(res);

	return;

fail:
	_instant_err(stream, "_getaddrinfo_udp_tx_cb", err);

	if(res)
		uv_freeaddrinfo(res);
}

static inline void
_getaddrinfo_udp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_udp_t *udp = (void *)req - offsetof(osc_stream_udp_t, req);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);
	const osc_stream_driver_t *driver = stream->driver;
	int err;

	if( (status >= 0) && res)
	{
		osc_stream_addr_t src;
		unsigned int flags = 0;

		if((err = uv_udp_init(loop, &udp->socket)))
			goto fail;

		switch(udp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				struct sockaddr_in *ptr4 = (struct sockaddr_in *)res->ai_addr;

				if((err = uv_ip4_addr("0.0.0.0", ntohs(ptr4->sin_port), &src.ip4)))
					goto fail;

				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				struct sockaddr_in6 *ptr6 = (struct sockaddr_in6 *)res->ai_addr;

				if((err = uv_ip6_addr("::", ntohs(ptr6->sin6_port), &src.ip6)))
					goto fail;

				flags |= UV_UDP_IPV6ONLY; //TODO make this configurable?

				break;
			}
		}
		
		if((err = uv_udp_bind(&udp->socket, &src.ip, flags)))
			goto fail;

		if((err = uv_udp_recv_start(&udp->socket, _udp_alloc, _udp_recv_cb)))
			goto fail;

		_instant_msg(stream, OSC_STREAM_MESSAGE_RESOLVE);
	}
	else
	{
		_instant_msg(stream, OSC_STREAM_MESSAGE_TIMEOUT);
	}

	if(res)
		uv_freeaddrinfo(res);

	return;

fail:
	_instant_err(stream, "_getaddrinfo_udp_rx_cb", err);
	
	if(res)
		uv_freeaddrinfo(res);
}

static inline void
_udp_flush(osc_stream_t *stream);

static inline void
_udp_send_cb(uv_udp_send_t *req, int status)
{
	osc_stream_udp_tx_t *tx = (void *)req - offsetof(osc_stream_udp_tx_t, req);
	osc_stream_udp_t *udp = (void *)tx - offsetof(osc_stream_udp_t, tx);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);
	const osc_stream_driver_t *driver = stream->driver;

	if(!status)
	{
		driver->send_adv(stream->data);
		
		stream->flushing = 0; // reset flushing flag
		_udp_flush(stream); // look for more data to flush
	}
	else
		_instant_err(stream, "_udp_send_cb", status);
}

static inline void
_udp_flush(osc_stream_t *stream)
{
	osc_stream_udp_t *udp = &stream->udp;
	const osc_stream_driver_t *driver = stream->driver;

	if(stream->flushing) // already flushing?
		return;

	if(!uv_is_active((uv_handle_t *)&udp->socket))
		return; //TODO

	uv_buf_t *msg = &udp->tx.msg;
	msg->base = (char *)driver->send_req(&msg->len, stream->data);
	
	if(msg->base && (msg->len > 0))
	{
		stream->flushing = 1; // set flushing flag

		int err;
		if((err = uv_udp_send(&udp->tx.req, &udp->socket, msg, 1, &udp->tx.addr.ip, _udp_send_cb)))
		{
			_instant_err(stream, "_udp_flush", err);
			stream->flushing = 0;
		}
	}
}

static inline void
_udp_close_cb(uv_handle_t *handle)
{
	uv_udp_t *socket = (uv_udp_t *)handle;
	osc_stream_udp_t *udp = (void *)socket - offsetof(osc_stream_udp_t, socket);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, udp);

	free(stream);
}

static inline void
_udp_free(osc_stream_t *stream)
{
	int err;
	osc_stream_udp_t *udp = &stream->udp;

	// cancel resolve
	uv_cancel((uv_req_t *)&udp->req);

	if(uv_is_active((uv_handle_t *)&udp->socket))
	{
		if((err =	uv_udp_recv_stop(&udp->socket)))
			_instant_err(stream, "_udp_free", err);
		uv_close((uv_handle_t *)&udp->socket, _udp_close_cb);
	}
}

/*****************************************************************************
 * TCP implementation
 *****************************************************************************/

static inline void
_tcp_close_done(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);
	osc_stream_tcp_t *tcp = (void *)tx - offsetof(osc_stream_tcp_t, tx);

	tcp->connected = 0;
}

static inline void
_tcp_prefix_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_tcp_t *socket = (uv_tcp_t *)handle;
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;

	if(suggested_size > OSC_STREAM_BUF_SIZE)
		suggested_size = OSC_STREAM_BUF_SIZE;

	if(tcp->nchunk == sizeof(int32_t))
	{
		buf->base = (char *)&tcp->prefix;
		buf->len = tcp->nchunk;
	}
	else
	{
		buf->base = driver->recv_req(suggested_size, stream->data);
		buf->len = buf->base ? suggested_size : 0;
	}
}

static inline void
_tcp_prefix_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);

	if(nread > 0)
	{
		if(nread == sizeof(int32_t))
		{
			tcp->nchunk = ntohl(*(int32_t *)buf->base);
		}
		else if(nread == tcp->nchunk)
		{
			driver->recv_adv(nread, stream->data);
			tcp->nchunk = sizeof(int32_t);
		}
		else // nread != sizeof(int32_t) && nread != nchunk
			tcp->nchunk = sizeof(int32_t);
	}
	else if (nread < 0)
	{
		if( (nread == UV_EOF) || (nread == UV_ETIMEDOUT) )
			_instant_msg(stream, OSC_STREAM_MESSAGE_DISCONNECT);
		else
			_instant_err(stream, "_tcp_prefix_recv_cb", nread);

		int err;
		if((err = uv_read_stop(socket)))
			_instant_err(stream, "_tcp_prefix_recv_cb", err);
		uv_close((uv_handle_t *)socket, _tcp_close_done);
		uv_cancel((uv_req_t *)&tx->req);

		//TODO try to reconnect?
	}
}

static inline void
_tcp_slip_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_tcp_t *socket = (uv_tcp_t *)handle;
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;

	buf->base = tcp->buf_rx;
	buf->base += tcp->nchunk; // is there remaining chunk from last call?
	buf->len = OSC_STREAM_BUF_SIZE - tcp->nchunk;
}

static inline void
_tcp_slip_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);
	
	if(nread > 0)
	{
		char *ptr = tcp->buf_rx;
		nread += tcp->nchunk; // is there remaining chunk from last call?
		size_t size;
		size_t parsed;
		while( (parsed = slip_decode(ptr, nread, &size)) && (nread > 0) )
		{
			void *tar;
			if((tar = driver->recv_req(size, stream->data)))
			{
				memcpy(tar, ptr, size);
				driver->recv_adv(size, stream->data);
			}
			ptr += parsed;
			nread -= parsed;
		}
		if(nread > 0) // is there remaining chunk for next call?
		{
			memmove(tcp->buf_rx, ptr, nread);
			tcp->nchunk = nread;
		}
		else
			tcp->nchunk = 0;
	}
	else if (nread < 0)
	{
		if( (nread == UV_EOF) || (nread == UV_ETIMEDOUT) )
			_instant_msg(stream, OSC_STREAM_MESSAGE_DISCONNECT);
		else
			_instant_err(stream, "_tcp_slip_recv_cb", nread);

		int err;
		if((err = uv_read_stop(socket)))
			_instant_err(stream, "_tcp_slip_recv_cb", err);
		uv_close((uv_handle_t *)socket, _tcp_close_done);
		uv_cancel((uv_req_t *)&tx->req);
	}
}

static inline void
_sender_connect(uv_connect_t *conn, int status)
{
	uv_tcp_t *socket = (uv_tcp_t *)conn->handle;
	osc_stream_tcp_t *tcp = socket->data;

	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = &tcp->tx;
	int err;

	if(status)
	{
		err = status;
		goto fail;
	}

	if(!tcp->slip)
	{
		tcp->nchunk = sizeof(int32_t); // packet size as TCP preamble
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_prefix_alloc, _tcp_prefix_recv_cb)))
			goto fail;
	}
	else // tcp->slip
	{
		tcp->nchunk = 0;
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_slip_alloc, _tcp_slip_recv_cb)))
			goto fail;
	}

	_instant_msg(stream, OSC_STREAM_MESSAGE_CONNECT);
	tcp->connected = 1;

	return;

fail:
	_instant_err(stream, "_sender_connect", err);
}

static inline void
_getaddrinfo_tcp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_tcp_t *tcp = (void *)req - offsetof(osc_stream_tcp_t, req);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	int err;

	if( (status >= 0) && res)
	{
		osc_stream_tcp_tx_t *tx = &tcp->tx;
		tx->socket.data = tcp;
		tx->req.data = tcp;

		union {
			const struct sockaddr *ip;
			const struct sockaddr_in *ip4;
			const struct sockaddr_in6 *ip6;
		} src;
		char remote [128] = {'\0'};
		
		src.ip = res->ai_addr;;
		
		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				if((err = uv_ip4_name(src.ip4, remote, 127)))
					goto fail;
				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				if((err = uv_ip6_name(src.ip6, remote, 127)))
					goto fail;
				break;
			}
		}
		
		if((err = uv_tcp_init(loop, &tx->socket)))
			goto fail;

		union {
			const struct sockaddr ip;
			struct sockaddr_in ip4;
			struct sockaddr_in6 ip6;
		} addr;

		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				if((err = uv_ip4_addr(remote, ntohs(src.ip4->sin_port), &addr.ip4)))
					goto fail;
				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				if((err = uv_ip6_addr(remote, ntohs(src.ip6->sin6_port), &addr.ip6)))
					goto fail;
				break;
			}
		}
		
		if((err = uv_tcp_connect(&tcp->conn, &tx->socket, &addr.ip, _sender_connect)))
			goto fail;
		if((err = uv_tcp_nodelay(&tx->socket, 1))) // disable Nagle's algo
			goto fail;
		if((err = uv_tcp_keepalive(&tx->socket, 1, 5))) // keepalive after 5 seconds
			goto fail;

		_instant_msg(stream, OSC_STREAM_MESSAGE_RESOLVE);
	}
	else
		_instant_msg(stream, OSC_STREAM_MESSAGE_TIMEOUT);
	
	if(res)
		uv_freeaddrinfo(res);

	return;

fail:
	_instant_err(stream, "_getaddrinfo_tcp_tx_cb", err);

	if(res)
		uv_freeaddrinfo(res);
}

static inline void
_responder_connect(uv_stream_t *socket, int status)
{
	uv_loop_t *loop = socket->loop;
	osc_stream_tcp_t *tcp = (void *)socket - offsetof(osc_stream_tcp_t, socket);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	int err;

	if(status)
	{
		err = status;
		goto fail;
	}

	if(tcp->connected)
	{
		err = UV_EALREADY;
		goto fail;
	}
	else
	{
		_instant_msg(stream, OSC_STREAM_MESSAGE_CONNECT);

		osc_stream_tcp_tx_t *tx = &tcp->tx;
		tx->socket.data = tcp;
		tx->req.data = tcp;

		if((err = uv_tcp_init(loop, &tx->socket)))
			goto fail;
		if((err = uv_accept((uv_stream_t *)socket, (uv_stream_t *)&tx->socket)))
			goto fail;

		if(!tcp->slip)
		{
			tcp->nchunk = sizeof(int32_t); // packet size as TCP preamble
			if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_prefix_alloc, _tcp_prefix_recv_cb)))
				goto fail;
		}
		else // tcp->slip
		{
			tcp->nchunk = 0;
			if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_slip_alloc, _tcp_slip_recv_cb)))
				goto fail;
		}

		if((err = uv_tcp_nodelay(&tx->socket, 1))) // disable Nagle's algo
			goto fail;
		if((err = uv_tcp_keepalive(&tx->socket, 1, 5))) // keepalive after 5 seconds
			goto fail;
		
		tcp->connected = 1;
	}

	return;

fail:
	_instant_err(stream, "_responder_connect", err);
}

static inline void
_getaddrinfo_tcp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_tcp_t *tcp = (void *)req - offsetof(osc_stream_tcp_t, req);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;
	int err;

	if(status >= 0)
	{
		osc_stream_addr_t addr;
		char remote [128] = {'\0'};
		unsigned int flags = 0;

		if((err = uv_tcp_init(loop, &tcp->socket)))
			goto fail;

		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				struct sockaddr_in *ptr4 = (struct sockaddr_in *)res->ai_addr;

				if((err = uv_ip4_name(ptr4, remote, 127)))
					goto fail;
				if((err = uv_ip4_addr(remote, ntohs(ptr4->sin_port), &addr.ip4)))
					goto fail;

				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				struct sockaddr_in6 *ptr6 = (struct sockaddr_in6 *)res->ai_addr;

				if((err = uv_ip6_name(ptr6, remote, 127)))
					goto fail;
				if((err = uv_ip6_addr(remote, ntohs(ptr6->sin6_port), &addr.ip6)))
					goto fail;

				flags |= UV_TCP_IPV6ONLY; //TODO make this configurable

				break;
			}
		}

		if((err = uv_tcp_bind(&tcp->socket, &addr.ip, flags)))
			goto fail;
		if((err = uv_listen((uv_stream_t *)&tcp->socket, 128, _responder_connect)))
			goto fail;

		_instant_msg(stream, OSC_STREAM_MESSAGE_RESOLVE);
	}
	else
		_instant_msg(stream, OSC_STREAM_MESSAGE_TIMEOUT);

	if(res)
		uv_freeaddrinfo(res);

	return;

fail:
	_instant_err(stream, "_getaddrinfo_tcp_rx_cb", err);

	if(res)
		uv_freeaddrinfo(res);
}

static inline void
_tcp_flush(osc_stream_t *stream);

static inline void
_tcp_send_cb(uv_write_t *req, int status)
{
	osc_stream_tcp_t *tcp = req->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);
	const osc_stream_driver_t *driver = stream->driver;

	if(!status)
	{
		driver->send_adv(stream->data);

		stream->flushing = 0; // reset flushing flag
		_tcp_flush(stream); // look for more data to flush
	}
	else
		_instant_err(stream, "_tcp_send_cb", status);
}

static inline void
_tcp_flush(osc_stream_t *stream)
{
	osc_stream_tcp_t *tcp = &stream->tcp;
	const osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = &tcp->tx;

	if(stream->flushing) // already flushing?
		return;

	if(!uv_is_active((uv_handle_t *)&tx->socket))
		return; //TODO

	uv_buf_t *msg = tcp->msg;

	msg[1].base = (char *)driver->send_req(&msg[1].len, stream->data);

	if(msg[1].base && (msg[1].len > 0) )
	{
		tcp->prefix = htobe32(msg[1].len);
		msg[0].base = (char *)&tcp->prefix;
		msg[0].len = sizeof(int32_t);

		if(tcp->slip)
		{
			msg[1].len = slip_encode(tcp->buf_tx, &msg[1], 1); // discard prefix size
			msg[1].base = tcp->buf_tx;
		}

		int err;
		stream->flushing = 1; // set flushing flag

		if((err = uv_write(&tx->req, (uv_stream_t *)&tx->socket, &msg[tcp->slip], 2-tcp->slip, _tcp_send_cb)))
		{
			_instant_err(stream, "_tcp_flush", err);
			stream->flushing = 0;
		}
	}
}

static inline void
_tcp_close_client_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);
	osc_stream_tcp_t *tcp = (void *)tx - offsetof(osc_stream_tcp_t, tx);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);

	if(!tcp->server) // is client
		free(stream);
}

static inline void
_tcp_close_server_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_tcp_t *tcp = (void *)socket - offsetof(osc_stream_tcp_t, socket);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, tcp);

	free(stream);
}

static inline void
_tcp_free(osc_stream_t *stream)
{
	int err;
	osc_stream_tcp_t *tcp = &stream->tcp;

	// cancel resolve
	uv_cancel((uv_req_t *)&tcp->req);

	// close clients
	osc_stream_tcp_tx_t *tx = &stream->tcp.tx;
	if(uv_is_active((uv_handle_t *)&tx->req))
		uv_cancel((uv_req_t *)&tx->req);

	if(tcp->connected)	
	{
		if(uv_is_active((uv_handle_t *)&tx->socket))
		{
			if((err = uv_read_stop((uv_stream_t *)&tx->socket)))
				_instant_err(stream, "_tcp_free", err);
		}
		uv_close((uv_handle_t *)&tx->socket, _tcp_close_client_cb);
	}

	// close server
	if(tcp->server)
	{
		if(uv_is_active((uv_handle_t *)&tcp->socket))
			uv_close((uv_handle_t *)&tcp->socket, _tcp_close_server_cb);
		else
			free(stream);
	}
	else
	{
		if(uv_is_active((uv_handle_t *)&tcp->conn))
			uv_cancel((uv_req_t *)&tcp->conn);

		if(!tcp->connected)
			free(stream);
	}
}

/*****************************************************************************
 * Serial implementation
 *****************************************************************************/

static inline void
_pipe_slip_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_pipe_t *socket = (uv_pipe_t *)handle;
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, pipe);
	const osc_stream_driver_t *driver = stream->driver;

	buf->base = pipe->buf_rx;
	buf->base += pipe->nchunk; // is there remaining chunk from last call?
	buf->len = OSC_STREAM_BUF_SIZE - pipe->nchunk;
}

static inline void
_pipe_slip_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, pipe);
	const osc_stream_driver_t *driver = stream->driver;

	if(nread > 0)
	{
		char *ptr = pipe->buf_rx;
		nread += pipe->nchunk; // is there remaining chunk from last call?
		size_t size;
		size_t parsed;
		while( (parsed = slip_decode(ptr, nread, &size)) && (nread > 0) )
		{
			void *tar;
			if((tar = driver->recv_req(size, stream->data)))
			{
				memcpy(tar, ptr, size);
				driver->recv_adv(size, stream->data);
			}
			ptr += parsed;
			nread -= parsed;
		}
		if(nread > 0) // is there remaining chunk for next call?
		{
			memmove(pipe->buf_rx, ptr, nread);
			pipe->nchunk = nread;
		}
		else
			pipe->nchunk = 0;
	}
	else if (nread < 0)
	{
		if(nread == UV_EOF)
			_instant_msg(stream, OSC_STREAM_MESSAGE_DISCONNECT);
		else
			_instant_err(stream, "_pipe_slip_recv_cb", nread);

		int err;
		if((err = uv_read_stop(socket)))
			_instant_err(stream, "_pipe_slip_recv_cb", err);
		//uv_close((uv_handle_t *)socket, NULL); //TODO
	}
}

static inline void
_pipe_init(uv_loop_t *loop, osc_stream_t *stream, int fd)
{
	osc_stream_pipe_t *pipe = &stream->pipe;
	const osc_stream_driver_t *driver = stream->driver;
	pipe->nchunk = 0;
	pipe->fd = 0;

	int err;
	if((err = uv_pipe_init(loop, &pipe->socket, 0)))
		goto fail;
	if((err = uv_pipe_open(&pipe->socket, pipe->fd)))
		goto fail;
	if((err = uv_read_start((uv_stream_t *)&pipe->socket, _pipe_slip_alloc, _pipe_slip_recv_cb)))
		goto fail;
			
	_instant_msg(stream, OSC_STREAM_MESSAGE_CONNECT);

	return;

fail:
	_instant_err(stream, "_pipe_init", err);
}

static inline void
_pipe_flush(osc_stream_t *stream);

static inline void
_pipe_send_cb(uv_write_t *req, int status)
{
	osc_stream_pipe_t *pipe = (void *)req - offsetof(osc_stream_pipe_t, req);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, pipe);
	const osc_stream_driver_t *driver = stream->driver;

	if(!status)
	{
		stream->flushing = 0; // reset flushing flag
		_pipe_flush(stream); // look for more data to flush
	}
	else
		_instant_err(stream, "_pipe_send_cb", status);
}

static inline void
_pipe_flush(osc_stream_t *stream)
{
	osc_stream_pipe_t *pipe = &stream->pipe;
	const osc_stream_driver_t *driver = stream->driver;

	if(stream->flushing) // already flushing?
		return;

	if(!uv_is_active((uv_handle_t *)&pipe->socket))
		return; //TODO

	uv_buf_t src = {
		.base = NULL,
		.len = 0
	};

	src.base = (char *)driver->send_req(&src.len, stream->data);

	if(src.base && (src.len > 0))
	{
		stream->flushing = 1; // set flushing flag

		// slip encode it to temporary buffer
		uv_buf_t *msg = &pipe->msg;
		msg->base = pipe->buf_tx;
		msg->len = slip_encode(msg->base, &src, 1);
	
		driver->send_adv(stream->data);

		int err;
		if((err =	uv_write(&pipe->req, (uv_stream_t *)&pipe->socket, msg, 1, _pipe_send_cb)))
		{
			_instant_err(stream, "_pipe_flush", err);
			stream->flushing = 0;
		}
	}
}

static inline void
_pipe_close_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, pipe);

	free(stream);
}

static inline void
_pipe_free(osc_stream_t *stream)
{
	int err;
	osc_stream_pipe_t *pipe = &stream->pipe;

	if(uv_is_active((uv_handle_t *)&pipe->socket))
	{
		if((err = uv_read_stop((uv_stream_t *)&pipe->socket)))
			_instant_err(stream, "_pipe_free", err);
	}
	uv_close((uv_handle_t *)&pipe->socket, _pipe_close_cb);

	//TODO close(pipe->fd)?
}

/*****************************************************************************
 * API implementation
 *****************************************************************************/

static inline int
_parse_protocol(osc_stream_t *stream, const char *addr, const char **url)
{
	if(!strncmp(addr, "osc.udp", 7))
	{
		addr += 7;
			
		stream->type = OSC_STREAM_TYPE_UDP;

		if(!strncmp(addr, "://", 3))
		{
			stream->udp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->udp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->udp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return UV_EPROTO;
	}
	else if(!strncmp(addr, "osc.tcp", 7))
	{
		addr += 7;
			
		stream->type = OSC_STREAM_TYPE_TCP;
		stream->tcp.slip = 0;

		if(!strncmp(addr, "://", 3))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return UV_EPROTO;
	}
	else if(!strncmp(addr, "osc.slip.tcp", 12))
	{
		addr += 12;
		
		stream->type = OSC_STREAM_TYPE_TCP;
		stream->tcp.slip = 1;

		if(!strncmp(addr, "://", 3))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->tcp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return UV_EPROTO;
	}
	else if(!strncmp(addr, "osc.pipe://", 11))
	{
		stream->type = OSC_STREAM_TYPE_PIPE;
		addr += 11;
	}
	else
		return UV_EPROTO;

	*url = addr;

	return 0;
}

static inline int
_parse_url(uv_loop_t *loop, osc_stream_t *stream, const char *url)
{
	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_t* udp = &stream->udp;
			const char *service = strchr(url, ':');
				
			if(!service)
			{
				return UV_ENXIO;
			}

			if( (udp->server = (url == service)) )
			{
				// resolve destination address
				const char *node = udp->version == OSC_STREAM_IP_VERSION_4 ? "0.0.0.0" : "::";

				const struct addrinfo hints = {
					.ai_family = udp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_DGRAM,
					.ai_protocol = IPPROTO_UDP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &udp->req, _getaddrinfo_udp_rx_cb, node, service+1, &hints);
			}
			else // !udp->server
			{
				// resolve destination address
				char *node = strndup(url, service-url);

				const struct addrinfo hints = {
					.ai_family = udp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_DGRAM,
					.ai_protocol = IPPROTO_UDP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &udp->req, _getaddrinfo_udp_tx_cb, node, service+1, &hints);
				free(node);
			}

			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_t *tcp = &stream->tcp;
			char *service = strchr(url, ':');

			if(!service)
			{
				return UV_ENXIO;
			}

			if( (tcp->server = (url == service)) )
			{
				// resolve destination address
				const char *node = tcp->version == OSC_STREAM_IP_VERSION_4 ? "0.0.0.0" : "::";

				const struct addrinfo hints = {
					.ai_family = tcp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_STREAM,
					.ai_protocol = IPPROTO_TCP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &tcp->req, _getaddrinfo_tcp_rx_cb, node, service+1, &hints);
			}
			else // !tcp->server
			{
				// resolve destination address
				char *node = strndup(url, service-url);

				const struct addrinfo hints = {
					.ai_family = tcp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_STREAM,
					.ai_protocol = IPPROTO_TCP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &tcp->req, _getaddrinfo_tcp_tx_cb, node, service+1, &hints);
				free(node);
			}

			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			// check if file exists
			uv_fs_t req;
			uv_fs_stat(loop, &req, url, NULL); // synchronous
			if(req.result)
			{
				return UV_ENXIO;
			}
			
			//TODO
			// check if file can be used as pipe
		
			int fd;
#if defined(__WINDOWS__)
			if( (fd = open(url, 0, 0)) < 0)
#else
			if( (fd = open(url, O_RDWR | O_NOCTTY | O_NONBLOCK, 0)) < 0)
#endif
			{
				return UV_ENXIO;
			}

			uv_handle_type type = uv_guess_handle(fd);
			if( (type != UV_NAMED_PIPE) && (type != UV_STREAM) && (type != UV_TTY))
			{
				return UV_ENXIO;
			}

			_pipe_init(loop, stream, fd);

			break;
		}
	}

	return 0;
}

static inline osc_stream_t *
osc_stream_new(uv_loop_t *loop, const char *addr, 
	const osc_stream_driver_t *driver, void *data)
{
	if(!driver || !driver->send_req || !driver->send_adv || !driver->recv_req || !driver->recv_adv)
		return NULL;

	int err;
	osc_stream_t *stream = calloc(1, sizeof(osc_stream_t));
	if(!stream)
	{
		err = UV_ENOMEM;
		goto fail;
	}

	stream->driver = driver;
	stream->data = data;

	const char *url = NULL;
	if((err = _parse_protocol(stream, addr, &url)))
		goto fail;
	if((err = _parse_url(loop, stream, url)))
		goto fail;

	return stream;

fail:
	if(stream)
	{
		_instant_err(stream, "osc_stream_new", err);
		free(stream);
	}

	return NULL;
}

static inline void
osc_stream_free(osc_stream_t *stream)
{
	if(!stream)
		return;

	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
			_udp_free(stream);
			break;
		case OSC_STREAM_TYPE_TCP:
			_tcp_free(stream);
			break;
		case OSC_STREAM_TYPE_PIPE:
			_pipe_free(stream);
			break;
	}
}

static inline void
osc_stream_flush(osc_stream_t *stream)
{
	if(!stream)
		return;

	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
			_udp_flush(stream);
			break;
		case OSC_STREAM_TYPE_TCP:
			_tcp_flush(stream);
			break;
		case OSC_STREAM_TYPE_PIPE:
			_pipe_flush(stream);
			break;
	}
}

#ifdef __cplusplus
}
#endif

#endif
