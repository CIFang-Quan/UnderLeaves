#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include <pthread.h>

#define PORT 8888
#define SIZE 101
char bufferto[1024];
char bufferfrom[1024];
gchar *file_text;
int read_size;
int window_open;
int button_pressed = 0;
typedef struct {
    GtkTextBuffer *text_buffer;
    int socket;
} ThreadArgs;
typedef struct {
    GtkTextBuffer *text_buffer;
    gchar *text;
} RecvTextArgs;
typedef struct {
    GtkTextBuffer *text_buffer;
    gchar *text;
} SendTextArgs;


pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t text_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t text_cond = PTHREAD_COND_INITIALIZER;
gboolean text_ready = FALSE;

gboolean clear_text_buffer(gpointer data) {
    GtkTextBuffer *text_buffer = (GtkTextBuffer *)data;
    gtk_text_buffer_set_text(text_buffer, "", -1);
    
    pthread_mutex_lock(&text_mutex);
    text_ready = TRUE;
    pthread_cond_signal(&text_cond);
    pthread_mutex_unlock(&text_mutex);
    
    return G_SOURCE_REMOVE;
}


gboolean update_text_buffer(gpointer data) {
    RecvTextArgs *args = (RecvTextArgs *)data;
    GtkTextBuffer *text_buffer = args->text_buffer;
    gchar *text = args->text;
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(text_buffer, &end);
    gtk_text_buffer_insert(text_buffer, &end, text, -1);

    g_free(text);
    g_free(args);

    pthread_mutex_lock(&text_mutex);
    text_ready = TRUE;
    pthread_cond_signal(&text_cond);
    pthread_mutex_unlock(&text_mutex);

    return G_SOURCE_REMOVE;
}

int receivefile(int socket,GtkTextBuffer *text_buffer){
    text_ready = FALSE;
    gdk_threads_add_idle((GSourceFunc)clear_text_buffer, text_buffer);
    
    pthread_mutex_lock(&text_mutex);
    while (!text_ready) {
        pthread_cond_wait(&text_cond, &text_mutex);
    }
    pthread_mutex_unlock(&text_mutex);

    while(1){
        memset(bufferfrom, 0, sizeof(bufferfrom));
        read_size = read(socket, bufferfrom, sizeof(bufferfrom)-1);
        if(read_size < 0){
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }
        bufferfrom[read_size] = '\0';
        RecvTextArgs *args = g_new(RecvTextArgs, 1);
        if(strstr(bufferfrom, "EOF") != NULL){
            printf("File received\n");
            char *eof_position = strstr(bufferfrom, "EOF");
            *eof_position = '\0';
            args->text_buffer = text_buffer;
            args->text = strdup(bufferfrom);

            text_ready = FALSE;
            gdk_threads_add_idle((GSourceFunc)update_text_buffer, args);
            
            pthread_mutex_lock(&text_mutex);
            while (!text_ready) {
                pthread_cond_wait(&text_cond, &text_mutex);
            }
            pthread_mutex_unlock(&text_mutex);
            
            break;
        }
        args->text_buffer = text_buffer;
        args->text = strdup(bufferfrom);
        text_ready = FALSE;
        gdk_threads_add_idle((GSourceFunc)update_text_buffer, args);
            
        pthread_mutex_lock(&text_mutex);
        while (!text_ready) {
            pthread_cond_wait(&text_cond, &text_mutex);
        }
        pthread_mutex_unlock(&text_mutex);
    }
    memset(bufferfrom, 0, sizeof(bufferfrom));
    return 0;
}

gboolean *get_text_view_content(gpointer data) {
    SendTextArgs *args = (SendTextArgs *)data;
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(args->text_buffer, &start);
    gtk_text_buffer_get_end_iter(args->text_buffer, &end);
    args->text = gtk_text_buffer_get_text(args->text_buffer, &start, &end, FALSE);

    pthread_mutex_lock(&text_mutex);
    text_ready = TRUE;
    pthread_cond_signal(&text_cond);
    pthread_mutex_unlock(&text_mutex);

    return G_SOURCE_REMOVE;
}

int sendfile(int socket, GtkTextBuffer *buffer){
    SendTextArgs *args = g_new(SendTextArgs, 1);
    args->text =NULL;
    args->text_buffer = buffer;
    text_ready = FALSE;
    gdk_threads_add_idle((GSourceFunc)get_text_view_content, args);

    pthread_mutex_lock(&text_mutex);
    while (!text_ready) {
        pthread_cond_wait(&text_cond, &text_mutex);
    }
    pthread_mutex_unlock(&text_mutex);

    if (args->text != NULL) {
        write(socket, args->text, strlen(args->text));
        g_free(args->text);
    }

    printf("send file success\n");
    return 0;
}


void *check_buffer_thread_func(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    GtkTextBuffer *text_buffer = args->text_buffer;
    int socket = args->socket;
    
    while (window_open) {

        pthread_mutex_lock(&buffer_mutex);
        memset(bufferfrom, 0, sizeof(bufferfrom));
        memset(bufferto, 0, sizeof(bufferto));
        pthread_mutex_unlock(&buffer_mutex);

        receivefile(socket,text_buffer);
        usleep(1000000);

        sendfile(socket, text_buffer);
        usleep(1000000);
        if(window_open){
            write(socket, "EOF", 3);
        }else{
            write(socket,"windowclose",11);
            break;
        }

    }

    return NULL;
}

void on_button_clicked(GtkWidget *widget, gpointer data) {
    window_open = 0;
    button_pressed = 1;
}

gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    
    if (!button_pressed) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widget),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING,
                                                   GTK_BUTTONS_OK,
                                                   "PLESAE CLICK Finish!");
        gtk_window_set_title(GTK_WINDOW(dialog), "note");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        return TRUE;
    }

    pthread_t *thread = (pthread_t *)data;
    pthread_join(*thread, NULL); 
    return FALSE; 
}

void on_window_destroy(GtkWidget *widget) {
    gtk_main_quit(); 
}

int open_window(int sock) {
    
    button_pressed = 0;

    gtk_init(NULL, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    GtkWidget *textView = gtk_text_view_new();
    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    GtkWidget *button = gtk_button_new_with_label("Finish");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    gtk_box_pack_start(GTK_BOX(vbox), textView, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(window), vbox);

    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);

    ThreadArgs *args = malloc(sizeof(ThreadArgs));
    args->text_buffer = text_buffer;
    args->socket = sock;
    pthread_t check_buffer_thread;
    window_open = 1;
    pthread_create(&check_buffer_thread, NULL, check_buffer_thread_func, args);
    
    gtk_widget_set_size_request(window, 1080, 1080);
    gtk_widget_show_all(window);

    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), &check_buffer_thread);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    gtk_main();
    return 0;
}

int localop(int sock){
    if(write(sock, bufferto,strlen(bufferto)-1) < 0) {// send command
        perror("Send failed");
        exit(EXIT_FAILURE);
    }
    memset(bufferto, 0, sizeof(bufferto));
    memset(bufferfrom, 0, sizeof(bufferfrom));
    if (read(sock, bufferfrom, sizeof(bufferfrom)-1) < 0) {
        perror("Receive failed");
        exit(EXIT_FAILURE);
    }
    printf("%s\n", bufferfrom);// display file list and permission
    memset(bufferfrom, 0, sizeof(bufferfrom));
    memset(bufferto, 0, sizeof(bufferto));
    printf(">>>");
    fgets(bufferto,sizeof(bufferto), stdin);
    if(write(sock, bufferto,strlen(bufferto)-1) < 0) {// send file name
        perror("Send failed");
        exit(EXIT_FAILURE);
    }
    memset(bufferto, 0, sizeof(bufferto));
    open_window(sock);

    return 0;
}


int main() {
    int sock;
    struct sockaddr_in server_addr;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server\n");

    do{
        printf(">>>");
        memset(bufferto, 0, sizeof(bufferto));
        fgets(bufferto,sizeof(bufferto), stdin);
        if(strncmp(bufferto, "update",4) == 0 || strncmp(bufferto, "Update",4) == 0){
            localop(sock);
            continue;
        }

        // Send message to server
        if(write(sock, bufferto,strlen(bufferto)-1) < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        
        memset(bufferfrom, 0, sizeof(bufferfrom));
        // Receive response from server
        if (read(sock, bufferfrom, sizeof(bufferfrom)-1) < 0) {
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }

        printf("%s\n", bufferfrom);
    } 
    while ((strncmp(bufferto, "exit",4) != 0) && (strncmp(bufferto, "Exit",4) != 0));
    close(sock);
    return 0;
}
//gcc `pkg-config --cflags gtk+-3.0` myclient.c `pkg-config --libs gtk+-3.0` -lpthread -o myclient
