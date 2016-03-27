#ifndef _REPLAY_H_
#define _REPLAY_H_

#define LIBPATH_MAX_LEN 100

struct response {
	char msg[LIBPATH_MAX_LEN+100];
	size_t stack_dist;
  size_t heap_dist;
	int success;
};

#endif
