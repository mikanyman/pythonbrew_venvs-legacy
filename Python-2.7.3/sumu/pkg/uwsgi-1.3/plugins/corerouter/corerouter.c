/*

   uWSGI corerouter

   requires:

   - async
   - caching
   - pcre (optional)

*/

#include "../../uwsgi.h"

extern struct uwsgi_server uwsgi;

#include "cr.h"

void uwsgi_opt_corerouter(char *opt, char *value, void *cr) {
	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;
        uwsgi_new_gateway_socket(value, ucr->name);
        ucr->has_sockets++;
}

void uwsgi_opt_corerouter_use_socket(char *opt, char *value, void *cr) {
	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;
        ucr->use_socket = 1;
	ucr->has_backends++;

        if (value) {
                ucr->socket_num = atoi(value);
        }
}

void uwsgi_opt_corerouter_use_base(char *opt, char *value, void *cr) {
	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;
        ucr->base = value;
        ucr->base_len = strlen(ucr->base);
	ucr->has_backends++;
}

void uwsgi_opt_corerouter_use_pattern(char *opt, char *value, void *cr) {
	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;
        ucr->pattern = value;
        ucr->pattern_len = strlen(ucr->pattern);
	ucr->has_backends++;
}


void uwsgi_opt_corerouter_zerg(char *opt, char *value, void *cr) {

        int j;
        int count = 8;
        struct uwsgi_gateway_socket *ugs;
	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;

        int zerg_fd = uwsgi_connect(value, 30, 0);
        if (zerg_fd < 0) {
                uwsgi_log("--- unable to connect to zerg server ---\n");
                exit(1);
        }

        int last_count = count;
        int *zerg = uwsgi_attach_fd(zerg_fd, &count, "uwsgi-zerg", 10);
        if (zerg == NULL) {
                if (last_count != count) {
                        close(zerg_fd);
                        zerg_fd = uwsgi_connect(value, 30, 0);
                        if (zerg_fd < 0) {
                                uwsgi_log("--- unable to connect to zerg server ---\n");
                                exit(1);
                        }
                        zerg = uwsgi_attach_fd(zerg_fd, &count, "uwsgi-zerg", 10);
                }
                else {
                        uwsgi_log("--- invalid data received from zerg-server ---\n");
                        exit(1);
                }
        }

        if (zerg == NULL) {
                uwsgi_log("--- invalid data received from zerg-server ---\n");
                exit(1);
        }


        close(zerg_fd);

                        for(j=0;j<count;j++) {
                                if (zerg[j] == -1) break;
                                ugs = uwsgi_new_gateway_socket_from_fd(zerg[j], ucr->name);
                                ugs->zerg = optarg;
                        }
}


void uwsgi_opt_corerouter_cs(char *opt, char *value, void *cr) {

	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;

        char *cs = uwsgi_str(value);
               char *cs_code = strchr(cs, ':');
                if (!cs_code) {
                        uwsgi_log("invalid code_string option\n");
                        exit(1);
                }
                cs_code[0] = 0;
                char *cs_func = strchr(cs_code + 1, ':');
                if (!cs_func) {
                        uwsgi_log("invalid code_string option\n");
                        exit(1);
                }
                cs_func[0] = 0;
                ucr->code_string_modifier1 = atoi(cs);
                ucr->code_string_code = cs_code + 1;
                ucr->code_string_function = cs_func + 1;

	ucr->has_backends++;

}

void uwsgi_opt_corerouter_ss(char *opt, char *value, void *cr) {

	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) cr;
        struct uwsgi_gateway_socket *ugs = uwsgi_new_gateway_socket(value, ucr->name);
        ugs->subscription = 1;
        ucr->has_subscription_sockets++;

	// this is the subscription hash table
	ucr->subscriptions = uwsgi_calloc(sizeof(struct uwsgi_subscription_slot *) * UMAX16);

	ucr->has_backends++;

}


void corerouter_send_stats(struct uwsgi_corerouter *);

void corerouter_manage_subscription(char *key, uint16_t keylen, char *val, uint16_t vallen, void *data) {

	struct uwsgi_subscribe_req *usr = (struct uwsgi_subscribe_req *) data;

	if (!uwsgi_strncmp("key", 3, key, keylen)) {
		usr->key = val;
		usr->keylen = vallen;
	}
	else if (!uwsgi_strncmp("address", 7, key, keylen)) {
		usr->address = val;
		usr->address_len = vallen;
	}
	else if (!uwsgi_strncmp("modifier1", 9, key, keylen)) {
		usr->modifier1 = uwsgi_str_num(val, vallen);
	}
	else if (!uwsgi_strncmp("cores", 5, key, keylen)) {
		usr->cores = uwsgi_str_num(val, vallen);
	}
	else if (!uwsgi_strncmp("load", 4, key, keylen)) {
		usr->load = uwsgi_str_num(val, vallen);
	}
	else if (!uwsgi_strncmp("weight", 6, key, keylen)) {
		usr->weight = uwsgi_str_num(val, vallen);
	}
	else if (!uwsgi_strncmp("unix", 4, key, keylen)) {
		usr->unix_check = uwsgi_str_num(val, vallen);
	}
	else if (!uwsgi_strncmp("sign", 4, key, keylen)) {
		usr->sign = val;
                usr->sign_len = vallen;
	}
}

static struct uwsgi_rb_timer *corerouter_reset_timeout(struct uwsgi_corerouter *, struct corerouter_session *);

void corerouter_close_session(struct uwsgi_corerouter *ucr, struct corerouter_session *cr_session) {


	if (cr_session->instance_fd != -1) {
		close(cr_session->instance_fd);
		ucr->cr_table[cr_session->instance_fd] = NULL;
	}

	if (ucr->subscriptions && cr_session->un && cr_session->un->len > 0) {
        	// decrease reference count
#ifdef UWSGI_DEBUG
               uwsgi_log("[1] node %.*s refcnt: %llu\n", cr_session->un->len, cr_session->un->name, cr_session->un->reference);
#endif
               cr_session->un->reference--;
#ifdef UWSGI_DEBUG
               uwsgi_log("[2] node %.*s refcnt: %llu\n", cr_session->un->len, cr_session->un->name, cr_session->un->reference);
#endif
	}


	if (cr_session->instance_failed) {

		if (cr_session->soopt) {
			if (!ucr->quiet)
				uwsgi_log("unable to connect() to uwsgi instance \"%.*s\": %s\n", (int) cr_session->instance_address_len, cr_session->instance_address, strerror(cr_session->soopt));
		}
		else if (cr_session->timed_out) {
			if (cr_session->instance_address_len > 0) {
				if (cr_session->status == COREROUTER_STATUS_CONNECTING) {
					if (!ucr->quiet)
						uwsgi_log("unable to connect() to uwsgi instance \"%.*s\": timeout\n", (int) cr_session->instance_address_len, cr_session->instance_address);
				}
				else if (cr_session->status  == COREROUTER_STATUS_RESPONSE) {
					uwsgi_log("timeout waiting for instance \"%.*s\"\n", (int) cr_session->instance_address_len, cr_session->instance_address);
				}
			}
		}

		// now check for dead nodes
		if (ucr->subscriptions && cr_session->un && cr_session->un->len > 0) {

                        if (cr_session->un->death_mark == 0)
                                uwsgi_log("[uwsgi-%s] %.*s => marking %.*s as failed\n", ucr->short_name, (int) cr_session->hostname_len, cr_session->hostname, (int) cr_session->instance_address_len, cr_session->instance_address);

                        cr_session->un->failcnt++;
                        cr_session->un->death_mark = 1;
                        // check if i can remove the node
                        if (cr_session->un->reference == 0) {
                                uwsgi_remove_subscribe_node(ucr->subscriptions, cr_session->un);
                        }
                        if (ucr->cheap && !ucr->i_am_cheap && !ucr->fallback && uwsgi_no_subscriptions(ucr->subscriptions)) {
                                uwsgi_gateway_go_cheap(ucr->name, ucr->queue, &ucr->i_am_cheap);
                        }

        	}
		else if (cr_session->static_node) {
			cr_session->static_node->custom = uwsgi_now();
			uwsgi_log("[uwsgi-%s] %.*s => marking %.*s as failed\n", ucr->short_name, (int) cr_session->hostname_len, cr_session->hostname, (int) cr_session->instance_address_len, cr_session->instance_address);
		}


		if (cr_session->tmp_socket_name) {
			free(cr_session->tmp_socket_name);
			cr_session->tmp_socket_name = NULL;
		}

		if (ucr->fallback) {
			// ok let's try with the fallback nodes
			if (!cr_session->fallback) {
				cr_session->fallback = ucr->fallback;
			}
			else {
				cr_session->fallback = cr_session->fallback->next;
				if (!cr_session->fallback) goto end;
			}

			cr_session->instance_address = cr_session->fallback->value;
			cr_session->instance_address_len = cr_session->fallback->len;

			// reset error and timeout
			cr_session->timeout = corerouter_reset_timeout(ucr, cr_session);
			cr_session->timed_out = 0;
			cr_session->soopt = 0;

			// reset nodes
			cr_session->un = NULL;
			cr_session->static_node = NULL;

			cr_session->pass_fd = is_unix(cr_session->instance_address, cr_session->instance_address_len);


                	cr_session->instance_fd = uwsgi_connectn(cr_session->instance_address, cr_session->instance_address_len, 0, 1);

                	if (cr_session->instance_fd < 0) {
                		cr_session->instance_failed = 1;
				cr_session->soopt = errno;
                        	corerouter_close_session(ucr, cr_session);
				return;
			}
  
			ucr->cr_table[cr_session->instance_fd] = cr_session;

                	cr_session->status = COREROUTER_STATUS_CONNECTING;
                	ucr->cr_table[cr_session->instance_fd] = cr_session;
                	event_queue_add_fd_write(ucr->queue, cr_session->instance_fd);
			return;

		}
	}

end:

	if (cr_session->tmp_socket_name) {
		free(cr_session->tmp_socket_name);
	}

	if (cr_session->buf_file)
		fclose(cr_session->buf_file);

	if (cr_session->buf_file_name) {
		if (unlink(cr_session->buf_file_name)) {
			uwsgi_error("unlink()");
		}
		free(cr_session->buf_file_name);
	}

	if (cr_session->write_queue)
		free(cr_session->write_queue);

	if (cr_session->instance_write_queue)
		free(cr_session->instance_write_queue);

	// could be used to free additional resources
	if (cr_session->close)
		cr_session->close(ucr, cr_session);

	if (cr_session->keepalive) {
		cr_session->keepalive = 0;
		return;
	}

	close(cr_session->fd);
	ucr->cr_table[cr_session->fd] = NULL;

	cr_del_timeout(ucr, cr_session);
	free(cr_session);
}

static struct uwsgi_rb_timer *corerouter_reset_timeout(struct uwsgi_corerouter *ucr, struct corerouter_session *cr_session) {
	cr_del_timeout(ucr, cr_session);
	return cr_add_timeout(ucr, cr_session);
}

static void corerouter_expire_timeouts(struct uwsgi_corerouter *ucr) {

	time_t current = uwsgi_now();
	struct uwsgi_rb_timer *urbt;
	struct corerouter_session *cr_session;

	for (;;) {
		urbt = uwsgi_min_rb_timer(ucr->timeouts);
		if (urbt == NULL)
			return;

		if (urbt->key <= current) {
			cr_session = (struct corerouter_session *) urbt->data;
			cr_session->timed_out = 1;
			if (cr_session->retry) {
				cr_session->retry = 0;
				ucr->switch_events(ucr, cr_session, -1);
				if (cr_session->retry) {
					cr_del_timeout(ucr, cr_session);
					cr_session->timeout = cr_add_fake_timeout(ucr, cr_session);
				}
				else {
					cr_session->timeout = corerouter_reset_timeout(ucr, cr_session);
				}
			}
			else {
				corerouter_close_session(ucr, cr_session);
			}
			continue;
		}

		break;
	}
}

struct corerouter_session *corerouter_alloc_session(struct uwsgi_corerouter *ucr, struct uwsgi_gateway_socket *ugs, int new_connection, struct sockaddr *cr_addr, socklen_t cr_addr_len) {

	ucr->cr_table[new_connection] = uwsgi_calloc(ucr->session_size);
        ucr->cr_table[new_connection]->fd = new_connection;
        ucr->cr_table[new_connection]->instance_fd = -1;
        ucr->cr_table[new_connection]->status = COREROUTER_STATUS_RECV_HDR;

        ucr->cr_table[new_connection]->timeout = cr_add_timeout(ucr, ucr->cr_table[new_connection]);
	ucr->cr_table[new_connection]->ugs = ugs;

	ucr->cr_table[new_connection]->recv = uwsgi_cr_simple_recv;
	ucr->cr_table[new_connection]->send = uwsgi_cr_simple_send;
	ucr->cr_table[new_connection]->instance_recv = uwsgi_cr_simple_instance_recv;
	ucr->cr_table[new_connection]->instance_send = uwsgi_cr_simple_instance_send;

	ucr->alloc_session(ucr, ugs, ucr->cr_table[new_connection], cr_addr, cr_addr_len);
	event_queue_add_fd_read(ucr->queue, new_connection);

	return ucr->cr_table[new_connection];
}

void uwsgi_corerouter_loop(int id, void *data) {

	int i;

	struct uwsgi_corerouter *ucr = (struct uwsgi_corerouter *) data;

	ucr->cr_stats_server = -1;

	ucr->cr_table = uwsgi_malloc(sizeof(struct corerouter_session *) * uwsgi.max_fd);

	for (i = 0; i < (int) uwsgi.max_fd; i++) {
		ucr->cr_table[i] = NULL;
	}

	ucr->i_am_cheap = ucr->cheap;

	void *events = uwsgi_corerouter_setup_event_queue(ucr, id);

	if (ucr->has_subscription_sockets)
		event_queue_add_fd_read(ucr->queue, ushared->gateways[id].internal_subscription_pipe[1]);


	if (!ucr->socket_timeout)
		ucr->socket_timeout = 30;

	if (!ucr->static_node_gracetime)
		ucr->static_node_gracetime = 30;

	int i_am_the_first = 1;
	for(i=0;i<id;i++) {
		if (!strcmp(ushared->gateways[i].name, ucr->name)) {
			i_am_the_first = 0;
			break;
		}
	}

	if (ucr->stats_server && i_am_the_first) {
		char *tcp_port = strchr(ucr->stats_server, ':');
		if (tcp_port) {
			// disable deferred accept for this socket
			int current_defer_accept = uwsgi.no_defer_accept;
			uwsgi.no_defer_accept = 1;
			ucr->cr_stats_server = bind_to_tcp(ucr->stats_server, uwsgi.listen_queue, tcp_port);
			uwsgi.no_defer_accept = current_defer_accept;
		}
		else {
			ucr->cr_stats_server = bind_to_unix(ucr->stats_server, uwsgi.listen_queue, uwsgi.chmod_socket, uwsgi.abstract_socket);
		}

		event_queue_add_fd_read(ucr->queue, ucr->cr_stats_server);
		uwsgi_log("*** %s stats server enabled on %s fd: %d ***\n", ucr->short_name, ucr->stats_server, ucr->cr_stats_server);
	}


	if (ucr->use_socket) {
		ucr->to_socket = uwsgi_get_socket_by_num(ucr->socket_num);
		if (ucr->to_socket) {
			// fix socket name_len
			if (ucr->to_socket->name_len == 0 && ucr->to_socket->name) {
				ucr->to_socket->name_len = strlen(ucr->to_socket->name);
			}
		}
	}

	if (!ucr->pb_base_dir) {
		ucr->pb_base_dir = getenv("TMPDIR");
		if (!ucr->pb_base_dir)
			ucr->pb_base_dir = "/tmp";
	}

	int nevents;

	time_t delta;

	struct uwsgi_rb_timer *min_timeout;

	int interesting_fd;
	int new_connection;


	if (ucr->pattern) {
		init_magic_table(ucr->magic_table);
	}

	union uwsgi_sockaddr cr_addr;
	socklen_t cr_addr_len = sizeof(struct sockaddr_un);

	struct corerouter_session *cr_session;

	ucr->mapper = uwsgi_cr_map_use_void;

			if (ucr->use_cache) {
                        	ucr->mapper = uwsgi_cr_map_use_cache;
                        }
                        else if (ucr->pattern) {
                                ucr->mapper = uwsgi_cr_map_use_pattern;
                        }
                        else if (ucr->has_subscription_sockets) {
                                ucr->mapper = uwsgi_cr_map_use_subscription;
                        }
                        else if (ucr->base) {
                                ucr->mapper = uwsgi_cr_map_use_base;
                        }
                        else if (ucr->code_string_code && ucr->code_string_function) {
                                ucr->mapper = uwsgi_cr_map_use_cs;
			}
                        else if (ucr->to_socket) {
                                ucr->mapper = uwsgi_cr_map_use_to;
                        }
                        else if (ucr->static_nodes) {
                                ucr->mapper = uwsgi_cr_map_use_static_nodes;
                        }
                        else if (ucr->use_cluster) {
                                ucr->mapper = uwsgi_cr_map_use_cluster;
                        }


	ucr->timeouts = uwsgi_init_rb_timer();

	for (;;) {

		min_timeout = uwsgi_min_rb_timer(ucr->timeouts);
		if (min_timeout == NULL) {
			delta = -1;
		}
		else {
			delta = min_timeout->key - uwsgi_now();
			if (delta <= 0) {
				corerouter_expire_timeouts(ucr);
				delta = 0;
			}
		}

		if (uwsgi.master_process && ucr->harakiri > 0) {
			ushared->gateways_harakiri[id] = 0;
		}

		nevents = event_queue_wait_multi(ucr->queue, delta, events, ucr->nevents);

		if (uwsgi.master_process && ucr->harakiri > 0) {
			ushared->gateways_harakiri[id] = uwsgi_now() + ucr->harakiri;
		}

		if (nevents == 0) {
			corerouter_expire_timeouts(ucr);
		}

		for (i = 0; i < nevents; i++) {

			interesting_fd = event_queue_interesting_fd(events, i);

			struct uwsgi_gateway_socket *ugs = uwsgi.gateway_sockets;
			int taken = 0;
			while (ugs) {
				if (ugs->gateway == &ushared->gateways[id] && interesting_fd == ugs->fd) {
					if (!ugs->subscription) {
						new_connection = accept(interesting_fd, (struct sockaddr *) &cr_addr, &cr_addr_len);
#ifdef UWSGI_EVENT_USE_PORT
                                		event_queue_add_fd_read(ucr->queue, interesting_fd);
#endif
						if (new_connection < 0) {
							taken = 1;
							break;
						}

						// set socket in non-blocking mode, on non-linux platforms, clients get the server mode
#ifdef __linux__
                                                uwsgi_socket_nb(new_connection);
#endif

						corerouter_alloc_session(ucr, ugs, new_connection, (struct sockaddr *) &cr_addr, cr_addr_len);
					}
					else if (ugs->subscription) {
						uwsgi_corerouter_manage_subscription(ucr, id, ugs);
					}

					taken = 1;
					break;
				}


				ugs = ugs->next;
			}

			if (taken) {
				continue;
			}

			if (interesting_fd == ushared->gateways[id].internal_subscription_pipe[1]) {
				uwsgi_corerouter_manage_internal_subscription(ucr, interesting_fd);
			}
			else if (interesting_fd == ucr->cr_stats_server) {
				corerouter_send_stats(ucr);
			}
			else {
				cr_session = ucr->cr_table[interesting_fd];

				// something is going wrong...
				if (cr_session == NULL)
					continue;

				if (event_queue_interesting_fd_has_error(events, i)) {
						corerouter_close_session(ucr, cr_session);
						continue;
				}

					cr_session->timeout = corerouter_reset_timeout(ucr, cr_session);
				
				// implementation specific cycle;
				ucr->switch_events(ucr, cr_session, interesting_fd);
				

			}
		}
	}

}

int uwsgi_courerouter_has_has_backends(struct uwsgi_corerouter *ucr) {

	if (ucr->has_backends) return 1;

	// check if the router has configured backends
                if (ucr->use_cache ||
                        ucr->pattern ||
                        ucr->has_subscription_sockets ||
                        ucr->base ||
                        (ucr->code_string_code && ucr->code_string_function) ||
                        ucr->to_socket ||
                        ucr->static_nodes ||
                        ucr->use_cluster
                ) {
                        return 1;
                }

	
	return 0;

}

int uwsgi_corerouter_init(struct uwsgi_corerouter *ucr) {

	int i;

	if (ucr->has_sockets) {

		if (ucr->use_cache && !uwsgi.cache_max_items) {
			uwsgi_log("you need to create a uwsgi cache to use the %s (add --cache <n>)\n", ucr->name);
			exit(1);
		}

		if (!ucr->nevents)
			ucr->nevents = 64;


		ucr->has_backends = uwsgi_courerouter_has_has_backends(ucr);


		uwsgi_corerouter_setup_sockets(ucr);

		if (ucr->processes < 1)
			ucr->processes = 1;
		if (ucr->cheap) {
			uwsgi_log("starting %s in cheap mode\n", ucr->name);
		}
		for (i = 0; i < ucr->processes; i++) {
			if (register_gateway(ucr->name, uwsgi_corerouter_loop, ucr) == NULL) {
				uwsgi_log("unable to register the %s gateway\n", ucr->name);
				exit(1);
			}
		}
	}

	return 0;
}


struct uwsgi_plugin corerouter_plugin = {

	.name = "courerouter",
};

void corerouter_send_stats(struct uwsgi_corerouter *ucr) {

	struct sockaddr_un client_src;
	socklen_t client_src_len = 0;

	int client_fd = accept(ucr->cr_stats_server, (struct sockaddr *) &client_src, &client_src_len);
#ifdef UWSGI_EVENT_USE_PORT
        event_queue_add_fd_read(ucr->queue, ucr->cr_stats_server);
#endif
	if (client_fd < 0) {
		uwsgi_error("accept()");
		return;
	}

	struct uwsgi_stats *us = uwsgi_stats_new(8192);

        if (uwsgi_stats_keyval_comma(us, "version", UWSGI_VERSION)) goto end;
        if (uwsgi_stats_keylong_comma(us, "pid", (unsigned long long) getpid())) goto end;
        if (uwsgi_stats_keylong_comma(us, "uid", (unsigned long long) getuid())) goto end;
        if (uwsgi_stats_keylong_comma(us, "gid", (unsigned long long) getgid())) goto end;

        char *cwd = uwsgi_get_cwd();
        if (uwsgi_stats_keyval_comma(us, "cwd", cwd)) goto end0;

	if (uwsgi_stats_key(us , ucr->short_name)) goto end0;
        if (uwsgi_stats_list_open(us)) goto end0;

	struct uwsgi_gateway_socket *ugs = uwsgi.gateway_sockets;
	while (ugs) {
		if (!strcmp(ugs->owner, ucr->name)) {
			if (uwsgi_stats_str(us, ugs->name)) goto end0;
			if (ugs->next) {
				if (uwsgi_stats_comma(us)) goto end0;
			}
		}
		ugs = ugs->next;
	}
	if (uwsgi_stats_list_close(us)) goto end0;
	if (uwsgi_stats_comma(us)) goto end0;

	if (ucr->has_subscription_sockets) {
		if (uwsgi_stats_key(us , "subscriptions")) goto end0;
		if (uwsgi_stats_list_open(us)) goto end0;

		int i;
		int first_processed = 0;
		for(i=0;i<UMAX16;i++) {
			struct uwsgi_subscribe_slot *s_slot = ucr->subscriptions[i];
			if (s_slot && first_processed) {
				if (uwsgi_stats_comma(us)) goto end0;
			}
			while (s_slot) {
				first_processed = 1;
				if (uwsgi_stats_object_open(us)) goto end0;
				if (uwsgi_stats_keyvaln_comma(us, "key", s_slot->key, s_slot->keylen)) goto end0;
				if (uwsgi_stats_keylong_comma(us, "hash", (unsigned long long) s_slot->hash)) goto end0;
				if (uwsgi_stats_keylong_comma(us, "hits", (unsigned long long) s_slot->hits)) goto end0;

				if (uwsgi_stats_key(us , "nodes")) goto end0;
				if (uwsgi_stats_list_open(us)) goto end0;

				struct uwsgi_subscribe_node *s_node = s_slot->nodes;
				while (s_node) {
					if (uwsgi_stats_object_open(us)) goto end0;

					if (uwsgi_stats_keyvaln_comma(us, "name", s_node->name, s_node->len)) goto end0;

					if (uwsgi_stats_keylong_comma(us, "modifier1", (unsigned long long) s_node->modifier1)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "modifier2", (unsigned long long) s_node->modifier2)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "last_check", (unsigned long long) s_node->last_check)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "requests", (unsigned long long) s_node->requests)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "tx", (unsigned long long) s_node->transferred)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "cores", (unsigned long long) s_node->cores)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "load", (unsigned long long) s_node->load)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "weight", (unsigned long long) s_node->weight)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "wrr", (unsigned long long) s_node->wrr)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "ref", (unsigned long long) s_node->reference)) goto end0;
					if (uwsgi_stats_keylong_comma(us, "failcnt", (unsigned long long) s_node->failcnt)) goto end0;
					if (uwsgi_stats_keylong(us, "death_mark", (unsigned long long) s_node->death_mark)) goto end0;

					if (uwsgi_stats_object_close(us)) goto end0;
					if (s_node->next) {
						if (uwsgi_stats_comma(us)) goto end0;
					}
					s_node = s_node->next;
				}

				if (uwsgi_stats_list_close(us)) goto end0;
				if (uwsgi_stats_object_close(us)) goto end0;
				if (s_slot->next) {
					if (uwsgi_stats_comma(us)) goto end0;
				}

				s_slot = s_slot->next;
				// check for loopy optimization
				if (s_slot == ucr->subscriptions[i])
					break;
			}
		}

			if (uwsgi_stats_list_close(us)) goto end0;
			if (uwsgi_stats_comma(us)) goto end0;
	}

	if (uwsgi_stats_keylong(us, "cheap", (unsigned long long) ucr->i_am_cheap)) goto end0;	

	if (uwsgi_stats_object_close(us)) goto end0;

        size_t remains = us->pos;
        off_t pos = 0;
        while(remains > 0) {
                ssize_t res = write(client_fd, us->base + pos, remains);
                if (res <= 0) {
                        if (res < 0) {
                                uwsgi_error("write()");
                        }
                        goto end0;
                }
                pos += res;
                remains -= res;
        }

end0:
        free(cwd);
end:
        free(us->base);
        free(us);
        close(client_fd);


}
