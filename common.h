#pragma once

#define ERROR 1

#define ERR_WRAPPER_NOMSG(predicate, label) \
	if (predicate) {                        \
		goto label;                         \
	}

#define ERR_WRAPPER(predicate, msg, label)  \
	if (predicate) {                        \
		fprintf(stderr, "%s\n", msg);       \
		goto label;                         \
	}

#define ERRNO_WRAPPER(predicate, msg, label)\
	if (predicate) {                        \
		perror(msg);                        \
		goto label;                         \
	}
