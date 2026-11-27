/* Tests minimaux du protocole (cf. protocole.md) :
 *  1. message reçu en plusieurs lectures
 *  2. valeur de payload invalide ou excessive
 *  3. déconnexion d'un client pendant la réception
 */

#include <sys/wait.h>
#include <time.h>

#include "protocol.h"

#define HEADER_LEN (2 + 4 + NICK_LEN + INFOS_LEN)

static int failures = 0;

#define CHECK(cond, msg)                                   \
	do {                                                   \
		printf("  [%s] %s\n", (cond) ? "OK" : "ECHEC", msg); \
		if (!(cond))                                       \
			failures++;                                    \
	} while (0)

static void build_header(unsigned char *buf, uint16_t type, uint32_t len)
{
	uint16_t t = htons(type);
	uint32_t l = htonl(len);

	memset(buf, 0, HEADER_LEN);
	memcpy(buf, &t, 2);
	memcpy(buf + 2, &l, 4);
	strcpy((char *)buf + 6, "alice");
}

static void pause_ms(long ms)
{
	struct timespec ts = { 0, ms * 1000000L };
	nanosleep(&ts, NULL);
}

/* Fils qui envoie `len` octets de `data` par morceaux de `chunk`, puis ferme. */
static int spawn_sender(const unsigned char *data, size_t len, size_t chunk)
{
	int fds[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0 )
		exit(EXIT_FAILURE);
	if (fork() == 0) {
		close(fds[0]);
		for (size_t off = 0; off < len; off += chunk) {
			size_t n = (len - off < chunk) ? len - off : chunk;
			protocol_send_all(fds[1], data + off, n);
			pause_ms(2);
		}
		close(fds[1]);
		_exit(EXIT_SUCCESS);
	}
	close(fds[1]);
	return fds[0];
}

/* Envoie la trame (en entier ou tronquée à `len` octets) et lit côté récepteur. */
static int recv_from(const unsigned char *frame, size_t len, size_t chunk, void **payload)
{
	struct message msg;
	int fd = spawn_sender(frame, len, chunk);
	int ret = protocol_recv_message(fd, &msg, payload);

	close(fd);
	wait(NULL);
	return ret;
}

int main(void)
{
	unsigned char frame[HEADER_LEN + 100];
	void *payload;

	build_header(frame, UNICAST_SEND, 100);
	memset(frame + HEADER_LEN, 'x', 100);

	printf("Test 1 : message reçu en plusieurs lectures\n");
	payload = NULL;
	CHECK(recv_from(frame, sizeof(frame), 7, &payload) == 0 &&
	      payload != NULL && memcmp(payload, frame + HEADER_LEN, 100) == 0,
	      "trame envoyée par morceaux de 7 octets reçue intacte");
	free(payload);

	printf("Test 2 : payload invalide ou excessif\n");
	build_header(frame, ECHO_SEND, PROTO_MAX_PAYLOAD + 1);
	CHECK(recv_from(frame, HEADER_LEN, HEADER_LEN, &payload) == -1,
	      "payload_len > PROTO_MAX_PAYLOAD rejeté");

	printf("Test 3 : déconnexion pendant la réception\n");
	build_header(frame, UNICAST_SEND, 100);
	memset(frame + HEADER_LEN, 'x', 100);
	CHECK(recv_from(frame, HEADER_LEN / 2, HEADER_LEN, &payload) == -1,
	      "déconnexion au milieu de l'en-tête : erreur");
	CHECK(recv_from(frame, HEADER_LEN + 40, HEADER_LEN + 40, &payload) == -1 &&
	      payload == NULL,
	      "déconnexion au milieu du payload : erreur");

	printf("\n%d échec(s)\n", failures);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
