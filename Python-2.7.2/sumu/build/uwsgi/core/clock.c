#include "uwsgi.h"

extern struct uwsgi_server uwsgi;

// in the future we will need to use the best clock source for each os/system
time_t uwsgi_now() {
        return uwsgi.clock->seconds();
}

uint64_t uwsgi_micros() {
        return uwsgi.clock->microseconds();
}


void uwsgi_register_clock(struct uwsgi_clock *clock) {
	struct uwsgi_clock *clocks = uwsgi.clocks;

	clock->next = NULL;

	if (!clocks) {
		uwsgi.clocks = clock;
		return;
	}

	while(clocks) {
		if (!clocks->next) {
			clocks->next = clock;
			return;
		}
		clocks = clocks->next;
	}
}

void uwsgi_set_clock(char *name) {
	struct uwsgi_clock *clocks = uwsgi.clocks;
	while(clocks) {
		if (!strcmp(name, clocks->name)) {
			uwsgi.clock = clocks;
			return;
		}
		clocks = clocks->next;
	}

	uwsgi_log("unable to set \"%s\" clock\n", name);
	exit(1);
}
