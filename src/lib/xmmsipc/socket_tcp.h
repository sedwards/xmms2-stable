/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003	Peter Alm, Tobias Rundstr�m, Anders Gustafsson
 * 
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 * 
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *                   
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */


#ifndef XMMS_SOCKET_TCP_H
#define XMMS_SOCKET_TCP_H

#include "xmms/ipc_transport.h"

xmms_ipc_transport_t *xmms_ipc_tcp_server_init (gchar *path);
xmms_ipc_transport_t *xmms_ipc_tcp_client_init (gchar *path);

#endif /* XMMS_SOCKET_TCP_H */
