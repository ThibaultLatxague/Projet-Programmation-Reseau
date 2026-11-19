#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"

struct info{
    short s;
    long l;
};

// Affiche les erreurs
int die(int ret_value, char *msg)
{
	if (ret_value < 0)
	{
		perror(msg);
		exit(EXIT_FAILURE);
	}

	return 0;
}
