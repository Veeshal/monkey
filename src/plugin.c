/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2010, Eduardo Silva P. <edsiper@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <err.h>

#include "config.h"
#include "plugin.h"
#include "monkey.h"
#include "request.h"
#include "scheduler.h"
#include "utils.h"
#include "str.h"
#include "file.h"
#include "header.h"
#include "memory.h"
#include "iov.h"
#include "epoll.h"

void *mk_plugin_load(char *path)
{
    void *handle;

    handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error during dlopen(): %s\n", dlerror());
        exit(1);
    }
    return handle;
}

void *mk_plugin_load_symbol(void *handler, const char *symbol)
{
    char *err;
    void *s;

    dlerror();
    s = dlsym(handler, symbol);
    if ((err = dlerror()) != NULL) {
        return NULL;
    }

    return s;
}

void mk_plugin_register_stagemap_add(struct plugin_stagem **stm, struct plugin *p)
{
    struct plugin_stagem *list, *new;

    new = mk_mem_malloc_z(sizeof(struct plugin_stagem));
    new->p = p;
    new->next = NULL;

    if (!*stm) {
        *stm = new;
        return;
    }

    list = *stm;

    while (list->next) {
        list = list->next;
    }

    list->next = new;
}

void mk_plugin_register_stagemap(struct plugin *p)
{
    if (!plg_stagemap) {
        plg_stagemap = mk_mem_malloc_z(sizeof(struct plugin_stagemap));
    };

    /* Plugin to stages */
    if (*p->hooks & MK_PLUGIN_STAGE_10) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_10, p);
    }

    if (*p->hooks & MK_PLUGIN_STAGE_20) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_20, p);
    }

    if (*p->hooks & MK_PLUGIN_STAGE_30) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_30, p);
    }

    if (*p->hooks & MK_PLUGIN_STAGE_40) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_40, p);
    }

    if (*p->hooks & MK_PLUGIN_STAGE_50) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_50, p);
    }

    if (*p->hooks & MK_PLUGIN_STAGE_60) {
        mk_plugin_register_stagemap_add(&plg_stagemap->stage_60, p);
    }
}

/* Load the plugins and set the library symbols to the
 * local struct plugin *p node
 */
struct plugin *mk_plugin_register(void *handler, char *path)
{
    struct plugin *p;

    p = mk_mem_malloc_z(sizeof(struct plugin));
    p->shortname = mk_plugin_load_symbol(handler, "_shortname");
    p->name = mk_plugin_load_symbol(handler, "_name");
    p->version = mk_plugin_load_symbol(handler, "_version");
    p->path = mk_string_dup(path);
    p->handler = handler;
    p->hooks =
        (mk_plugin_hook_t *) mk_plugin_load_symbol(handler, "_hooks");

    /* Mandatory functions */
    p->init = (int (*)()) mk_plugin_load_symbol(handler, "_mkp_init");
    p->exit = (int (*)()) mk_plugin_load_symbol(handler, "_mkp_exit");

    /* Core hooks */
    p->core.prctx = (int (*)()) mk_plugin_load_symbol(handler,
                                                      "_mkp_core_prctx()");
    p->core.thctx = (int (*)()) mk_plugin_load_symbol(handler,
                                                      "_mkp_core_thctx()");

    /* Stage hooks */
    p->stage.s10 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_10");

    p->stage.s20 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_20");

    p->stage.s30 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_30");

    p->stage.s40 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_40");

    p->stage.s40_event_read = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_40_event_read");

    p->stage.s40_event_write = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_40_event_write");

    p->stage.s50 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_50");

    p->stage.s60 = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_stage_60");

    /* Network I/O hooks */
    p->net_io.accept = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_accept");

    p->net_io.read = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_read");

    p->net_io.write = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_write");

    p->net_io.writev = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_writev");

    p->net_io.close = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_close");

    p->net_io.connect = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_io_connect");
    
    /* Network IP hooks */
    p->net_ip.addr = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_ip_addr");

    p->net_ip.maxlen = (int (*)())
        mk_plugin_load_symbol(handler, "_mkp_network_ip_maxlen");

    /* Thread key */
    p->thread_key = (pthread_key_t) mk_plugin_load_symbol(handler,
                                                          "_mkp_data");
    /* Next ! */
    p->next = NULL;

    if (!p->name || !p->version || !p->hooks) {
#ifdef TRACE
        MK_TRACE("Bad plugin definition: %s", path);
#endif
        mk_mem_free(p);
        return NULL;
    }

    /* Add Plugin to the end of the list */
    struct plugin *plg = config->plugins;
    while(plg->next){
        plg = plg->next;
    }
    plg->next = p;

    mk_plugin_register_stagemap(p);
    return p;
}

void mk_plugin_init()
{
    char *path;
    void *handle;
    struct plugin *p;
    struct plugin_api *api;
    struct mk_config *cnf;

    api = mk_mem_malloc_z(sizeof(struct plugin_api));

    /* Setup and connections list */
    api->config = config;
    api->sched_list = &sched_list;

    /* API plugins funcions */
    api->mem_alloc = (void *) mk_mem_malloc;
    api->mem_alloc_z = (void *) mk_mem_malloc_z;
    api->mem_free = (void *) mk_mem_free;
    api->str_build = (void *) m_build_buffer;
    api->str_dup = (void *) mk_string_dup;
    api->str_search = (void *) mk_string_search;
    api->str_search_n = (void *) mk_string_search_n;
    api->str_copy_substr = (void *) mk_string_copy_substr;
    api->str_split_line = (void *) mk_string_split_line;
    api->file_to_buffer = (void *) mk_file_to_buffer;
    api->file_get_info = (void *) mk_file_get_info;
    api->header_send = (void *) mk_header_send;
    api->iov_create = (void *) mk_iov_create;
    api->iov_free = (void *) mk_iov_free;
    api->iov_add_entry = (void *) mk_iov_add_entry;
    api->iov_set_entry = (void *) mk_iov_set_entry;
    api->iov_send = (void *) mk_iov_send;
    api->iov_print = (void *) mk_iov_print;
    api->pointer_set = (void *) mk_pointer_set;
    api->pointer_print = (void *) mk_pointer_print;
    api->plugin_load_symbol = (void *) mk_plugin_load_symbol;
    api->socket_cork_flag = (void *) mk_socket_set_cork_flag;
    api->socket_connect = (void *) mk_socket_connect;
    api->socket_set_tcp_nodelay = (void *) mk_socket_set_tcp_nodelay;
    api->socket_set_nonblocking = (void *) mk_socket_set_nonblocking;
    api->socket_create = (void *) mk_socket_create;
    api->config_create = (void *) mk_config_create;
    api->config_free = (void *) mk_config_free;
    api->config_getval = (void *) mk_config_getval;
    api->sched_get_connection = (void *) mk_sched_get_connection;
    api->event_add = (void *) mk_plugin_event_add;
    api->event_socket_change_mode = (void *) mk_plugin_event_socket_change_mode;

#ifdef TRACE
    api->trace = (void *) mk_utils_trace;
#endif

    /* Read configuration file */
    path = mk_mem_malloc_z(1024);
    snprintf(path, 1024, "%s/%s", config->serverconf, MK_PLUGIN_LOAD);
    cnf = mk_config_create(path);

    while (cnf) {
        if (strcasecmp(cnf->key, "LoadPlugin") == 0) {
            handle = mk_plugin_load(cnf->val);
            p = mk_plugin_register(handle, cnf->val);
            if (!p) {
                fprintf(stderr, "Plugin error: %s", cnf->val);
                dlclose(handle);
            }
            else {
                char *plugin_confdir = 0;
                unsigned long len;

                m_build_buffer(&plugin_confdir,
                               &len,
                               "%s/plugins/%s/",
                               config->serverconf, p->shortname);

                p->init(&api, plugin_confdir);
            }
        }
        cnf = cnf->next;
    }

    api->plugins = config->plugins;
    mk_mem_free(path);
}

int mk_plugin_stage_run(mk_plugin_hook_t hook,
                        unsigned int socket,
                        struct sched_connection *conx,
                        struct client_request *cr, struct request *sr)
{
    int ret;
    struct plugin_stagem *stm;

    if (hook & MK_PLUGIN_STAGE_10) {
        stm = plg_stagemap->stage_10;
        while (stm) {
#ifdef TRACE
            MK_TRACE("[%s] STAGE 10", p->shortname);
#endif
            stm->p->stage.s10();
            stm = stm->next;
        }
    }

    if (hook & MK_PLUGIN_STAGE_20) {
        stm = plg_stagemap->stage_20;
        while (stm) {
#ifdef TRACE
            MK_TRACE("[%s] STAGE 20", p->shortname);
#endif
            ret = stm->p->stage.s20(socket, conx, cr);
            switch (ret) {
            case MK_PLUGIN_RET_CLOSE_CONX:
#ifdef TRACE
                MK_TRACE("return MK_PLUGIN_RET_CLOSE_CONX");
#endif
                return MK_PLUGIN_RET_CLOSE_CONX;
            }

            stm = stm->next;
        }
    }

    if (hook & MK_PLUGIN_STAGE_30) {
        stm = plg_stagemap->stage_30;
        while (stm) {
#ifdef TRACE
            MK_TRACE("[%s] STAGE 30", p->shortname);
#endif              
            ret = stm->p->stage.s30(cr, sr);
            switch (ret) {
            case MK_PLUGIN_RET_CLOSE_CONX:
                return MK_PLUGIN_RET_CLOSE_CONX;
            }

            stm = stm->next;
        }
    }

    /* Object handler */
    if (hook & MK_PLUGIN_STAGE_40) {
        /* The request just arrived and is required to check who can
         * handle it */
        if (!sr->handled_by){
            stm = plg_stagemap->stage_40;
            while (stm) {
                /* Call stage */
#ifdef TRACE
                MK_TRACE("[%s] STAGE 40", p->shortname);
#endif
                ret = stm->p->stage.s40(stm->p, cr, sr);

                switch (ret) {
                case MK_PLUGIN_RET_NOT_ME:
                    break;
                case MK_PLUGIN_RET_CONTINUE:
                    return MK_PLUGIN_RET_CONTINUE;
                }
                stm = stm->next;
            }
        }
    }

    return -1;
}

void mk_plugin_request_handler_add(struct request *sr, struct plugin *p)
{
    if (!sr->handled_by) {
        sr->handled_by = p;
        return;
    }
}

void mk_plugin_request_handler_del(struct request *sr, struct plugin *p)
{
    if (!sr->handled_by) {
        return;
    }

    mk_mem_free(sr->handled_by);
}

/* This function is called by every created worker
 * for plugins which need to set some data under a thread
 * context 
 */
void mk_plugin_worker_startup()
{
    struct plugin *p;

    p = config->plugins;

    while (p) {
        /* Init plugin */
        if (p->core.thctx) {
            p->core.thctx();
        }

        p = p->next;
    }
}

/* This function is called by Monkey *outside* of the
 * thread context for plugins, so here's the right
 * place to set pthread keys or similar
 */
void mk_plugin_preworker_calls()
{
    int ret;
    struct plugin *p;

    p = config->plugins;

    while (p) {
        /* Init pthread keys */
        if (p->thread_key) {
            ret = pthread_key_create(&p->thread_key, NULL);
            if (ret != 0) {
                printf("\nPlugin Error: could not create key for %s", 
                       p->shortname);
                fflush(stdout);
                exit(1);
            }
        }
        p = p->next;
    }
}

int mk_plugin_event_add(int socket, struct plugin *handler,
                        struct client_request *cr, 
                        struct request *sr)
{
    struct sched_list_node *sched;
    struct plugin_event *list;
    struct plugin_event *aux;
    struct plugin_event *event;

    sched = mk_sched_get_thread_conf();
    
    if (!sched || !handler || !cr || !sr) {
        return -1;
    }
    
    /* Event node (this list exist at thread level */
    event = mk_mem_malloc(sizeof(struct plugin_event));
    event->socket = socket;
    event->handler = handler;
    event->cr = cr;
    event->sr = sr;
    event->next = NULL;

    /* Get thread event list */
    list = mk_plugin_event_get_list();
    if (!list) {
        mk_plugin_event_set_list(event);
    }
    else {
        aux = list;
        while (aux->next) {
            aux = aux->next;
        }
        
        aux->next = event;
        mk_plugin_event_set_list(aux);
    }

    /* The thread event info has been registered, now we need
       to register the socket involved to the thread epoll array */
    mk_epoll_add_client(sched->epoll_fd, event->socket, 
                        MK_EPOLL_WRITE, MK_EPOLL_BEHAVIOR_TRIGGERED);
    return 0;
}

int mk_plugin_event_socket_change_mode(int socket, int mode)
{
    struct sched_list_node *sched;

    sched = mk_sched_get_thread_conf();
    
    if (!sched) {
        return -1;
    }

    return mk_epoll_socket_change_mode(sched->epoll_fd, socket, mode);
}

struct plugin_event *mk_plugin_event_get(int socket)
{
    struct plugin_event *event;

    event = mk_plugin_event_get_list();

    while (event){
        if (event->socket == socket) {
            return event;
        }

        event = event->next;
    }
    
    return NULL;
}

int mk_plugin_event_set_list(struct plugin_event *event)
{
    return pthread_setspecific(mk_plugin_event_k, (void *) event);
}

struct plugin_event *mk_plugin_event_get_list()
{
    return (struct plugin_event *) pthread_getspecific(mk_plugin_event_k);

}

/* Plugin epoll event handlers
 * ---------------------------
 * this functions are called by connection.c functions as mk_conn_read(), 
 * mk_conn_write(),mk_conn_error(), mk_conn_close() and mk_conn_timeout 
 * 
 * Return Values:
 * -------------
 *    MK_PLUGIN_RET_EVENT_NOT_ME: There's no plugin hook associated
 */

int mk_plugin_event_read(int socket)
{
    struct plugin_event *event;

#ifdef TRACE
    MK_TRACE("Plugin, event read FD %i", socket);
#endif

    event = mk_plugin_event_get(socket);
    if (!event) {
        return MK_PLUGIN_RET_EVENT_NOT_ME;
    }

    if (event->handler->stage.s40_event_read) {
        event->handler->stage.s40_event_read(event->cr, event->sr);
    }

    return MK_PLUGIN_RET_CONTINUE;
}

int mk_plugin_event_write(int socket)
{
    struct plugin_event *event;

#ifdef TRACE
    MK_TRACE("Plugin, event write FD %i", socket);
#endif

    event = mk_plugin_event_get(socket);
    if (!event) {
        return MK_PLUGIN_RET_EVENT_NOT_ME;
    }

    if (event->handler->stage.s40_event_write) {
        event->handler->stage.s40_event_write(event->cr, event->sr);
    }

    return MK_PLUGIN_RET_CONTINUE;
}

int mk_plugin_event_error(int socket)
{
#ifdef TRACE
    MK_TRACE("Plugin, event error FD %i", socket);
#endif

    return 0;
}

int mk_plugin_event_close(int socket)
{

#ifdef TRACE
    MK_TRACE("Plugin, event close FD %i", socket);
#endif

    return 0;
}

int mk_plugin_event_timeout(int socket)
{

#ifdef TRACE
    MK_TRACE("Plugin, event timeout FD %i", socket);
#endif

    return 0;
}
