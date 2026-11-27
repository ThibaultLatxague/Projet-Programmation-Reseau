#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MSG_LEN 1024

// Affiche les erreurs
static inline int die(int ret_value, char *msg)
{
	if (ret_value < 0)
	{
		perror(msg);
		exit(EXIT_FAILURE);
	}

	return 0;
}

#endif
