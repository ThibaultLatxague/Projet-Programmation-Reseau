#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int write_all(int fd, const void *buffer, size_t length)
{
    const char *p = buffer;
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = write(fd, p + sent, length - sent);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

int send_file_chunks(int peer_fd, const char *path, const char *filename, const char *sender_nick)
{
    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }

    struct message msg = {0};
    msg.type = FILE_SEND;

    if (filename == NULL || filename[0] == '\0' ||
        strlen(filename) >= sizeof(msg.infos)) {
        fprintf(stderr, "Nom de fichier invalide ou trop long\n");
        close(file_fd);
        return -1;
        }

    snprintf(msg.infos, sizeof(msg.infos), "%s", filename);
    if (sender_nick != NULL)
        snprintf(msg.nick_sender, sizeof(msg.nick_sender), "%s", sender_nick);

    unsigned char chunk[MSG_LEN];

    for (;;) {
        ssize_t n = read(file_fd, chunk, sizeof(chunk));

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("read");
            close(file_fd);
            return -1;
        }
        if (n == 0)
            break;

        msg.pld_len = (int)n;

        if (write_all(peer_fd, &msg, sizeof(msg)) < 0 ||
            write_all(peer_fd, chunk, (size_t)n) < 0) {
            perror("write");
            close(file_fd);
            return -1;
            }
    }

    if (close(file_fd) < 0) {
        perror("close");
        return -1;
    }

    /* Marqueur de fin : le récepteur doit le reconnaître aussi. */
    msg.pld_len = 0;
    if (write_all(peer_fd, &msg, sizeof(msg)) < 0) {
        perror("write");
        return -1;
    }

    return 0;
}