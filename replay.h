#ifndef _REPLAY_H_
#define _REPLAY_H_

struct response {
	char msg[LIBPATH_MAX_LEN+100];
	size_t dist;
	int success;
};

#endif
