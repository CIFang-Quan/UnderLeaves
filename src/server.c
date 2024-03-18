#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <mysql/mysql.h>
#include <pthread.h>

#define TRUE 1
#define FALSE 0
#define PORT 8888
#define SIZE 100
enum role {holder, editor, viewer};

typedef struct file_info {
    int file_id;
    char file_name[SIZE];
    enum role permissions;
    struct file_info* next;
} file_info;

typedef struct client_info {
    int socket;
    char username[SIZE];
    file_info* file_head;
    struct client_info* next;
} client_info;

client_info* clients_head = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sql_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

int read_size;

MYSQL_RES *res;
MYSQL_ROW row;
char sqlcmd[1024];
MYSQL *conn;

/*-------------------------------------------------------- SQL OP FUNCTION------------------------------------------------------------*/

/*
file table
+----------------+---------------+------+-----+---------+----------------+
| Field          | Type          | Null | Key | Default | Extra          |
+----------------+---------------+------+-----+---------+----------------+
| file_id        | int           | NO   | PRI | NULL    | auto_increment |
| file_name      | varchar(100)  | NO   |     | NULL    |                |
| file_location  | varchar(1000) | NO   |     | NULL    |                |
| file_holder_id | int           | NO   |     | NULL    |                |
| last_edit_id   | int           | YES  |     | NULL    |                |
| last_edit_time | datetime      | YES  |     | NULL    |                |
+----------------+---------------+------+-----+---------+----------------+

user table
+---------------+-------------+------+-----+---------+----------------+
| Field         | Type        | Null | Key | Default | Extra          |
+---------------+-------------+------+-----+---------+----------------+
| user_id       | int         | NO   | PRI | NULL    | auto_increment |
| user_name     | varchar(100)| NO   |     | NULL    |                |
| user_password | varchar(100)| NO   |     | NULL    |                |
| user_view_file| text        | YES  |     | NULL    |                |
| user_edit_file| text        | YES  |     | NULL    |                | 
+---------------+-------------+------+-----+---------+----------------+
*/

//check user_name is exist, user_name is string
int UNisExist(char* user_name){
    pthread_mutex_lock(&sql_mutex);
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_name FROM user WHERE user_name = '%s'", user_name);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
			mysql_free_result(res);
            pthread_mutex_unlock(&sql_mutex);
            return 1;
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

//check user_name and user_password is match, user_name and user_password is string
int UNandPWisMatch(char* user_name, char* user_password){
	pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_name FROM user WHERE user_name = '%s' AND user_password = '%s'", user_name, user_password);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
			mysql_free_result(res);
            pthread_mutex_unlock(&sql_mutex);
            return 1;
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

//add user
int addUser(char* username, char* password){
	pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "INSERT INTO user(user_name,user_password) VALUES('%s','%s')", username, password);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

//add file
int addFile(char* filename, int user_id){
    struct stat st = {0};
    char* dirlocation = (char*)calloc(1000, sizeof(char));
    snprintf(dirlocation, 999, "./File_Library/%d", user_id);
    if (stat(dirlocation, &st) == -1) {
        if (mkdir(dirlocation, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
            perror("mkdir failed");
            return -1;
        } else {
            printf("Directory created\n");
        }
    }
    free(dirlocation);

    char* filelocation = (char*)calloc(1000, sizeof(char));
    snprintf(filelocation, 999, "./File_Library/%d/%s", user_id, filename);
    if (stat(filelocation, &st) == -1) {
        FILE *file = fopen(filelocation, "w");
        if (file == NULL) {
            perror("Error opening file");
            return -1;
        }
        fclose(file);
    } else {
        printf("File already exists\n");
        return 1;
    }

    pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "INSERT INTO file(file_name,file_location,file_holder_id) VALUES('%s','%s',%d)", filename, filelocation, user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

int getUserId(char* username){
    pthread_mutex_lock(&sql_mutex);
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_id FROM user WHERE user_name = '%s'", username);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
			int user_id_get = atoi(row[0]);
            mysql_free_result(res);
            pthread_mutex_unlock(&sql_mutex);
            return user_id_get;
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

int getFileId(char* filename){
    pthread_mutex_lock(&sql_mutex);
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_id FROM file WHERE file_name = '%s'", filename);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
			int file_id_get = atoi(row[0]);
            mysql_free_result(res);
            pthread_mutex_unlock(&sql_mutex);
            return file_id_get;
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

//check file_id is in user_hold_file
int isHolder(int file_id, int user_id){
    pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_holder_id FROM file WHERE file_id = %d", file_id);
    if(mysql_query(conn,sqlcmd)){
        printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    res = mysql_use_result(conn);
    if(res){
        while((row = mysql_fetch_row(res)) != NULL){
            if(atoi(row[0]) == user_id){
                mysql_free_result(res);
                pthread_mutex_unlock(&sql_mutex);
                return 1;
            }
        }
    }
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
    return 0;
}

//check file_id is in user_view_file, user_view_file is A TEXT in sql like '1 2 3 4 5'
int isViewer(int file_id, int user_id){
    pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd, 0, sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd) - 1, "SELECT user_view_file FROM user WHERE user_id = %d", user_id);    
    if (mysql_query(conn, sqlcmd)) {
        printf("Error %d: %s\n", mysql_errno(conn), mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    res = mysql_use_result(conn);
    if (res) {
        while ((row = mysql_fetch_row(res)) != NULL) {
            if (row[0] != NULL) {
                char* p = strtok(row[0], " ");
                while (p) {
                    if (atoi(p) == file_id) {
                        mysql_free_result(res);
                        pthread_mutex_unlock(&sql_mutex);
                        return 1;
                    }
                    p = strtok(NULL, " ");
                }
            }
        }
    }
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
    return 0;
}

//check file_id is in user_edit_file, user_edit_file is A TEXT in sql like '1 2 3 4 5'
int isEditor(int file_id, int user_id){
	pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_edit_file FROM user WHERE user_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return -1;
	}
	res = mysql_use_result(conn);
    if (res) {
        while ((row = mysql_fetch_row(res)) != NULL) {
            if (row[0] != NULL) {
                char* p = strtok(row[0], " ");
                while (p) {
                    if (atoi(p) == file_id) {
                        mysql_free_result(res);
                        pthread_mutex_unlock(&sql_mutex);
                        return 1;
                    }
                    p = strtok(NULL, " ");
                }
            }
        }
    }
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return 0;
}

//add file_id to user_view_file,user_view_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
void addViewer(int file_id, int user_id){
    pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd, 0, sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd) - 1, "UPDATE user SET user_view_file = CONCAT(COALESCE(user_view_file, ''), ' %d') WHERE user_id = %d", file_id, user_id);
    if (mysql_query(conn, sqlcmd)) {
        printf("Error %d: %s\n", mysql_errno(conn), mysql_error(conn));
    }
    pthread_mutex_unlock(&sql_mutex);
}

//del file_id to user_view_file,user_view_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
void delViewer(int file_id, int user_id){
    pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd, 0, sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd) - 1, 
        "UPDATE user SET user_view_file = "
        "CASE "
            "WHEN COALESCE(user_view_file, '') = '%d' OR COALESCE(user_view_file, '') LIKE '%d %%' THEN REPLACE(COALESCE(user_view_file, ''), '%d', '') "
            "WHEN COALESCE(user_view_file, '') LIKE '%% %d %%' THEN REPLACE(COALESCE(user_view_file, ''), ' %d ', ' ') "
            "WHEN COALESCE(user_view_file, '') LIKE '%% %d' THEN REPLACE(COALESCE(user_view_file, ''), ' %d', '') "
            "ELSE user_view_file "
        "END "
        "WHERE user_id = %d", 
        file_id, file_id, file_id, file_id, file_id, file_id, file_id, user_id);
    if(mysql_query(conn, sqlcmd)){
        printf("Error %d: %s\n", mysql_errno(conn), mysql_error(conn));
    }
    pthread_mutex_unlock(&sql_mutex);
}

//add file_id to user_edit_file,user_edit_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
void addEditor(int file_id, int user_id){
	pthread_mutex_lock(&sql_mutex);
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd) - 1, "UPDATE user SET user_edit_file = CONCAT(COALESCE(user_edit_file, ''), ' %d') WHERE user_id = %d", file_id, user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
	}
    pthread_mutex_unlock(&sql_mutex);
}

//del file_id to user_edit_file,user_edit_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
void delEditor(int file_id,int user_id){
	pthread_mutex_lock(&sql_mutex);
    snprintf(sqlcmd, sizeof(sqlcmd) - 1, 
        "UPDATE user SET user_edit_file = "
        "CASE "
            "WHEN COALESCE(user_edit_file, '') = '%d' OR COALESCE(user_edit_file, '') LIKE '%d %%' THEN REPLACE(COALESCE(user_edit_file, ''), '%d', '') "
            "WHEN COALESCE(user_edit_file, '') LIKE '%% %d %%' THEN REPLACE(COALESCE(user_edit_file, ''), ' %d ', ' ') "
            "WHEN COALESCE(user_edit_file, '') LIKE '%% %d' THEN REPLACE(COALESCE(user_edit_file, ''), ' %d', '') "
            "ELSE user_edit_file "
        "END "
        "WHERE user_id = %d", 
        file_id, file_id, file_id, file_id, file_id, file_id, file_id, user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
	}
    pthread_mutex_unlock(&sql_mutex);
}

//find file_id by user_id hold
int* findUserFileHold(int user_id){
    pthread_mutex_lock(&sql_mutex);
	int* file_id = (int*)calloc(100, sizeof(int));
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_id FROM file WHERE file_holder_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        free(file_id);
        pthread_mutex_unlock(&sql_mutex);
		return NULL;
	}
	res = mysql_use_result(conn);
	if(res){
		int i = 0;
		while((row = mysql_fetch_row(res)) != NULL && i < 100){
			file_id[i] = atoi(row[0]);
			i++;
		}
        if(i < 100){
            file_id[i] = 0;
        }
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return file_id;
}

//find file_id from user_view_file,user_view_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
int* findUserFileView(int user_id){
    pthread_mutex_lock(&sql_mutex);
	int* file_id = (int*)calloc(100, sizeof(int));
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_view_file from user WHERE user_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        free(file_id);
        pthread_mutex_unlock(&sql_mutex);
		return NULL;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
            if (row[0] != NULL){
                char* p = strtok(row[0]," ");
			    int i = 0;
			    while(p&&i<100){
				    file_id[i] = atoi(p);
				    p = strtok(NULL," ");
				    i++;
			    }
                if(i < 100){
                    file_id[i] = 0;
                }
            }
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return file_id;
}

//find file_id from user_edit_file,user_edit_file is A TEXT in sql, A TEXT in sql like '1 2 3 4 5'
int* findUserFileEdit(int user_id){
    pthread_mutex_lock(&sql_mutex);
	int* file_id = (int*)calloc(100, sizeof(int));
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT user_edit_file from user WHERE user_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        free(file_id);
        pthread_mutex_unlock(&sql_mutex);
		return NULL;
	}
	res = mysql_use_result(conn);
	if(res){
		while((row = mysql_fetch_row(res)) != NULL){
            if (row[0] != NULL){
                char* p = strtok(row[0]," ");
			    int i = 0;
			    while(p&&i<100){
				    file_id[i] = atoi(p);
				    p = strtok(NULL," ");
				    i++;
			    }
                if(i < 100){
                    file_id[i] = 0;
                }
            }
		}
	}
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
	return file_id;
}


//delete user and file
void delUserAndFile(int user_id){
    pthread_mutex_lock(&sql_mutex);
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "DELETE FROM user WHERE user_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return;
	}
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "DELETE FROM file WHERE file_holder_id = %d", user_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return;
	}
    pthread_mutex_unlock(&sql_mutex);
}

//delete file
void delFile(int file_id){
    pthread_mutex_lock(&sql_mutex);
	memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "DELETE FROM file WHERE file_id = %d", file_id);
	if(mysql_query(conn,sqlcmd)){
		printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        pthread_mutex_unlock(&sql_mutex);
		return;
	}
    pthread_mutex_unlock(&sql_mutex);
}

file_info* get_file_info(int user_id){
    file_info* head = NULL;
    int *file_id_hold = findUserFileHold(user_id);
    while(*file_id_hold != 0){
        file_info* new_file = (file_info*)malloc(sizeof(file_info));
        new_file->file_id = *file_id_hold;
        pthread_mutex_lock(&sql_mutex);
        memset(sqlcmd,0,sizeof(sqlcmd));
        snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_name from file WHERE file_id = %d", *file_id_hold);
        if(mysql_query(conn,sqlcmd)){
            printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
            pthread_mutex_unlock(&sql_mutex);
            return NULL;
        }
        res = mysql_use_result(conn);
        if(res){
            while((row = mysql_fetch_row(res)) != NULL){
                strncpy(new_file->file_name, row[0], sizeof(new_file->file_name) - 1);
            }
        }
        mysql_free_result(res);
        pthread_mutex_unlock(&sql_mutex);
        new_file->permissions = holder;
        new_file->next = NULL;
        if(head == NULL){
            head = new_file;
        }
        else{
            file_info* current = head;
            while(current->next != NULL){
                current = current->next;
            }
            current->next = new_file;
        }
        file_id_hold++;
    }

    int *file_id_edit = findUserFileView(user_id);
    while(*file_id_edit != 0){
        file_info* new_file = (file_info*)malloc(sizeof(file_info));
        new_file->file_id = *file_id_edit;
        pthread_mutex_lock(&sql_mutex);
        memset(sqlcmd,0,sizeof(sqlcmd));
        snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_name from file WHERE file_id = %d", *file_id_edit);
        if(mysql_query(conn,sqlcmd)){
            printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
            pthread_mutex_unlock(&sql_mutex);
            return NULL;
        }
        res = mysql_use_result(conn);
        if(res){
            while((row = mysql_fetch_row(res)) != NULL){
                strncpy(new_file->file_name, row[0], sizeof(new_file->file_name) - 1);
            }
        }
        mysql_free_result(res);
        pthread_mutex_unlock(&sql_mutex);
        new_file->permissions = viewer;
        new_file->next = NULL;
        if(head == NULL){
            head = new_file;
        }
        else{
            file_info* current = head;
            while(current->next != NULL){
                current = current->next;
            }
            current->next = new_file;
        }
        file_id_edit++;
    }
    int *file_id_view = findUserFileEdit(user_id);
    while(*file_id_view != 0){
        file_info* new_file = (file_info*)malloc(sizeof(file_info));
        new_file->file_id = *file_id_view;
        pthread_mutex_lock(&sql_mutex);
        memset(sqlcmd,0,sizeof(sqlcmd));
        snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_name from file WHERE file_id = %d", *file_id_view);
        if(mysql_query(conn,sqlcmd)){
            printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
            pthread_mutex_unlock(&sql_mutex);
            return NULL;
        }
        res = mysql_use_result(conn);
        if(res){
            while((row = mysql_fetch_row(res)) != NULL){
                strncpy(new_file->file_name, row[0], sizeof(new_file->file_name) - 1);
            }
        }
        mysql_free_result(res);
        pthread_mutex_unlock(&sql_mutex);
        new_file->permissions = editor;
        new_file->next = NULL;
        if(head == NULL){
            head = new_file;
        }
        else{
            file_info* current = head;
            while(current->next != NULL){
                current = current->next;
            }
            current->next = new_file;
        }
        file_id_view++;
    }

    return head;
}

//open the file and send the content to socket
int openfile(int file_id, int socket){
    char buffer[1024];
    pthread_mutex_lock(&sql_mutex);
    char* filelocation = (char*)calloc(1000, sizeof(char));
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_location from file WHERE file_id = %d", file_id);
    if(mysql_query(conn,sqlcmd)){
        printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        free(filelocation);
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    res = mysql_use_result(conn);
    if(res){
        while((row = mysql_fetch_row(res)) != NULL){
            strncpy(filelocation, row[0], 999);
        }
    }
    else{
        free(filelocation);
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
    pthread_mutex_lock(&file_mutex);
    FILE *file = fopen(filelocation, "r");
    free(filelocation);
    if (file == NULL) {
        perror("Error opening file");
        return -1;
    }
    memset(buffer, 0, sizeof(buffer));
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        write(socket, buffer, strlen(buffer));
        memset(buffer, 0, sizeof(buffer));
        // read(socket, buffer, sizeof(buffer)-1);
        // memset(buffer, 0, sizeof(buffer));
    }
    printf("send file success\n");
    write(socket, "EOF", 3);
    fclose(file);
    pthread_mutex_unlock(&file_mutex);
    return 0;
}
//recive the file content from socket and save it
int savefile(int file_id,int socket){
    char buffer[1024];
    pthread_mutex_lock(&sql_mutex);
    char* filelocation = (char*)calloc(1000, sizeof(char));
    memset(sqlcmd,0,sizeof(sqlcmd));
    snprintf(sqlcmd, sizeof(sqlcmd)-1, "SELECT file_location from file WHERE file_id = %d", file_id);
    if(mysql_query(conn,sqlcmd)){
        printf("Error %d: %s\n",mysql_errno(conn),mysql_error(conn));
        free(filelocation);
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    res = mysql_use_result(conn);
    if(res){
        while((row = mysql_fetch_row(res)) != NULL){
            strncpy(filelocation, row[0], 999);
        }
    }
    else{
        free(filelocation);
        pthread_mutex_unlock(&sql_mutex);
        return -1;
    }
    mysql_free_result(res);
    pthread_mutex_unlock(&sql_mutex);
    pthread_mutex_lock(&file_mutex);
    FILE *file = fopen(filelocation, "w");
    free(filelocation);
    if (file == NULL) {
        perror("Error opening file");
        return -1;
    }
    memset(buffer, 0, sizeof(buffer));

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        read_size = read(socket, buffer, sizeof(buffer) - 1);
        if (read_size < 0) {
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }
        buffer[read_size] = '\0';

        char *eof_position = strstr(buffer, "windowclose");
        if (eof_position != NULL) {
            *eof_position = '\0';
            printf("window close\n");
            fputs(buffer, file);
            memset(buffer, 0, sizeof(buffer));
            fclose(file);
            pthread_mutex_unlock(&file_mutex);
            return 1;
        }

        eof_position = strstr(buffer, "EOF");
        if (eof_position != NULL) {
            *eof_position = '\0';
            printf("File received\n");
            fputs(buffer, file);
            break;
        }

        fputs(buffer, file);
    }
    fclose(file);
    pthread_mutex_unlock(&file_mutex);
    memset(buffer, 0, sizeof(buffer));
    return 0;
}

/*-------------------------------------------------------- SQL OP FUNCTION------------------------------------------------------------*/

/*-------------------------------------------------------- Client OP FUNCTION------------------------------------------------------------*/

client_info* find_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    client_info* current = clients_head;

    while (current != NULL) {
        if (current->socket == socket) {
            pthread_mutex_unlock(&clients_mutex);
            return current;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

void update_client(int socket, char* username) {
    client_info* existing_client = find_client(socket);

    if (existing_client) {
        int getusid = getUserId(username);
        existing_client->file_head = get_file_info(getusid);
    } else {
        client_info* new_client = (client_info*)malloc(sizeof(client_info));
        new_client->socket = socket;
        memset(new_client->username, 0, sizeof(new_client->username));
        strncpy(new_client->username, username, 100);
        int getusid = getUserId(username);
        new_client->file_head = get_file_info(getusid);
        new_client->next = NULL;

        pthread_mutex_lock(&clients_mutex);
        if (clients_head == NULL) {
            clients_head = new_client;
        } else {
            client_info* current = clients_head;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = new_client;
        }
        pthread_mutex_unlock(&clients_mutex);
    }
}

void remove_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    client_info* current = clients_head;
    client_info* previous = NULL;

    while (current != NULL) {
        if (current->socket == socket) {
            if (previous == NULL) {
                clients_head = current->next;
            } else {
                previous->next = current->next;
            }
            free(current->file_head);
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}
// get client username by socket
char* get_client_username(int socket) {
    pthread_mutex_lock(&clients_mutex);
    client_info* current = clients_head;
    while (current != NULL) {
        if (current->socket == socket) {
            pthread_mutex_unlock(&clients_mutex);
            return current->username;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

/*
fileid    filename    permissions
...       ...         ...
*/
char* get_user_file_and_permissions(int socket) {
    pthread_mutex_lock(&clients_mutex);
    client_info* current = clients_head;
    while (current != NULL) {
        if (current->socket == socket) {
            char* file_and_permissions = (char*)calloc(4096, sizeof(char));
            strcat(file_and_permissions, "\nThis is file information related to you:\nfileid\t\tfilename\t\tpermissions\n");
            file_info* current_file = current->file_head;
            while (current_file != NULL) {
                char* each_file_info = (char*)calloc(200, sizeof(char));
                snprintf(each_file_info, 199, "%d\t\t%s\t\t%d\n", current_file->file_id, current_file->file_name, current_file->permissions);
                strcat(file_and_permissions, each_file_info);
                free(each_file_info);
                current_file = current_file->next;
            }
            pthread_mutex_unlock(&clients_mutex);
            return file_and_permissions;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

/*-------------------------------------------------------- Client OP FUNCTION------------------------------------------------------------*/

int loginop(int socket){
    char buffer[1024];
    char username[SIZE];
    char password[SIZE];
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "User:");
    write(socket, buffer, sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    read(socket, buffer, sizeof(buffer)-1);
    memset(username, 0, sizeof(username));
    strncpy(username, buffer, sizeof(username) - 1);
    memset(buffer, 0, sizeof(buffer));
    if(UNisExist(username)==0){
        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "User is not exist,Do you want to create it?(yes or no)\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        read(socket, buffer, sizeof(buffer)-1);
        printf("the yes come %s\n", buffer);
        if (strncmp("yes", buffer, 3) == 0 || strncmp("Yes", buffer, 3) == 0){
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "Password:");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            read(socket, buffer, sizeof(buffer)-1);
            memset(password, 0, sizeof(password));
            strncpy(password, buffer, sizeof(password) - 1);
            memset(buffer, 0, sizeof(buffer));
            addUser(username, password);
            snprintf(buffer, sizeof(buffer), "Create new account success,PLEASE Relogin\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 0;
        }
        else{
            snprintf(buffer, sizeof(buffer), "PLEASE Relogin.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 0;
        }
    }
    else{
        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "Password:");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        read(socket, buffer, sizeof(buffer)-1);
        memset(password, 0, sizeof(password));
        strncpy(password, buffer, sizeof(password) - 1);
        memset(buffer, 0, sizeof(buffer));
        if (UNandPWisMatch(username, password) == 1){
            update_client(socket, username);
            snprintf(buffer, sizeof(buffer),
            "Welcome UNDERLEAVES!\n"
            "PLEASE READ FOLLOWING to get OPERATION help:\n"
            "FileMGT(filemgt):Create/Delete files belonging to you on the server.\n"
            "InviteMGT(invitemgt):Modify the permissions on your files and invite/remove other users' permissions\n"
            "Update(update)Open the file for viewing/modification, and the latest information will be synchronized in real time.\n"
            );
            strcat(buffer,get_user_file_and_permissions(socket));
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 1;
        }
        else{
            snprintf(buffer, sizeof(buffer), "Password is wrong,PLEASE Relogin\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 0;
        }
    }
}

int filemgtop(int socket){
    char buffer[1024];
    char filename[SIZE];
    char invite_username[SIZE];
    int fileid;
    int userid;
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", get_user_file_and_permissions(socket));
    strcat(buffer,"Please input operation(Create, Delete):\n");
    write(socket, buffer, sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    read(socket, buffer, sizeof(buffer)-1);
    //Create
    if(strncmp("Create", buffer, 6) == 0 || strncmp("create", buffer, 6) == 0){
        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "Please input the filename you want to create:\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        read(socket, buffer, sizeof(buffer)-1);
        memset(filename, 0, sizeof(filename));
        strncpy(filename, buffer, sizeof(filename) - 1);
        memset(buffer, 0, sizeof(buffer));
        if(addFile(filename, getUserId(get_client_username(socket))) == 0){
            snprintf(buffer, sizeof(buffer), "Create success.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 1;
        }
        else{
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "The file is already exist.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 1;
        }
    }
    //Delete
    else if(strncmp("Delete", buffer, 6) == 0 || strncmp("delete", buffer, 6) == 0){
        memset(buffer, 0, sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "Please input the filename you want to delete:\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        read(socket, buffer, sizeof(buffer)-1);
        memset(filename, 0, sizeof(filename));
        strncpy(filename, buffer, sizeof(filename) - 1);
        memset(buffer, 0, sizeof(buffer));
        if(getFileId(filename) == -1){
            snprintf(buffer, sizeof(buffer), "The file is not exist.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 1;
        }
        else{
            delFile(getFileId(filename));
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "Delete success.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 1;
        }
    }
    else{
        snprintf(buffer, sizeof(buffer), "The operation is not exist.\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        return 0;
    }
}

int invitemgtop(int socket){
    char buffer[1024];
    char filename[SIZE];
    char invite_username[SIZE];
    int fileid;
    int userid;
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", get_user_file_and_permissions(socket));
    strcat(buffer,"Please input the filename you want to edit permission:\n");
    write(socket, buffer, sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    read(socket, buffer, sizeof(buffer)-1);
    memset(filename, 0, sizeof(filename));
    strncpy(filename, buffer, sizeof(filename) - 1);
    memset(buffer, 0, sizeof(buffer));
    if(getFileId(filename) == -1){
        snprintf(buffer, sizeof(buffer), "The file is not exist.\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        return 0;
    }
    else{
        fileid=getFileId(filename);
        userid=getUserId(get_client_username(socket));
        if(isHolder(fileid, userid) == 1){
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "Please input operation(Invite, Remove):\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            read(socket, buffer, sizeof(buffer)-1);
            //Invite
            if(strncmp("Invite", buffer, 6) == 0 || strncmp("invite", buffer, 6) == 0){
                memset(buffer, 0, sizeof(buffer));
                snprintf(buffer, sizeof(buffer), "Please input the username you want to invite:\n");
                write(socket, buffer, sizeof(buffer)-1);
                memset(buffer, 0, sizeof(buffer));
                read(socket, buffer, sizeof(buffer)-1);
                memset(invite_username, 0, sizeof(invite_username));
                strncpy(invite_username, buffer, sizeof(invite_username) - 1);
                memset(buffer, 0, sizeof(buffer));
                if(userid==getUserId(invite_username)){
                    snprintf(buffer, sizeof(buffer), "You are %s owner,please \n", filename);
                    write(socket, buffer, sizeof(buffer)-1);
                    memset(buffer, 0, sizeof(buffer));
                    return 0;
                }
                else if(UNisExist(invite_username) == 1){
                    userid = getUserId(invite_username);
                    memset(buffer, 0, sizeof(buffer));
                    snprintf(buffer, sizeof(buffer), "Please set the user's role(viewer or editor):\n");
                    write(socket, buffer, sizeof(buffer)-1);
                    memset(buffer, 0, sizeof(buffer));
                    read(socket, buffer, sizeof(buffer)-1);
                    if(strncmp("viewer", buffer, 6) == 0 || strncmp("Viewer", buffer, 6) == 0){
                        if(isViewer(fileid,userid) == 1){
                            snprintf(buffer, sizeof(buffer), "The user is already viewer.\n");
                            write(socket, buffer, sizeof(buffer)-1);
                            memset(buffer, 0, sizeof(buffer));
                            return 1;
                        }
                        else{
                            delEditor(fileid,userid);
                            addViewer(fileid,userid);
                            snprintf(buffer, sizeof(buffer), "Add viewer success.\n");
                            write(socket, buffer, sizeof(buffer)-1);
                            memset(buffer, 0, sizeof(buffer));
                            return 1;
                        }
                    }
                    else if(strncmp("editor", buffer, 6) == 0 || strncmp("Editor", buffer, 6) == 0){
                        if(isEditor(fileid,userid) == 1){
                            snprintf(buffer, sizeof(buffer), "The user is already editor.\n");
                            write(socket, buffer, sizeof(buffer)-1);
                            memset(buffer, 0, sizeof(buffer));
                            return 1;
                        }
                        else{
                            delViewer(fileid,userid);
                            addEditor(fileid,userid);
                            snprintf(buffer, sizeof(buffer), "Add editor success.\n");
                            write(socket, buffer, sizeof(buffer)-1);
                            memset(buffer, 0, sizeof(buffer));
                            return 1;
                        }
                    }
                    else{
                        snprintf(buffer, sizeof(buffer), "The role is not exist.\n");
                        write(socket, buffer, sizeof(buffer)-1);
                        memset(buffer, 0, sizeof(buffer));
                        return 0;
                    }
                }
                else{
                    snprintf(buffer, sizeof(buffer), "The user is not exist.\n");
                    write(socket, buffer, sizeof(buffer)-1);
                    memset(buffer, 0, sizeof(buffer));
                    return 0;
                }
            }
            //Remove
            else if(strncmp("Remove", buffer, 6) == 0 || strncmp("remove", buffer, 6) == 0){
                memset(buffer, 0, sizeof(buffer));
                snprintf(buffer, sizeof(buffer), "Please input the username you want to remove:\n");
                write(socket, buffer, sizeof(buffer)-1);
                memset(buffer, 0, sizeof(buffer));
                read(socket, buffer, sizeof(buffer)-1);
                memset(invite_username, 0, sizeof(invite_username));
                strncpy(invite_username, buffer, sizeof(invite_username) - 1);
                memset(buffer, 0, sizeof(buffer));
                if(strncmp(invite_username, get_client_username(socket), sizeof(invite_username) - 1) == 0){
                    snprintf(buffer, sizeof(buffer), "You are %s owner,please use delete command\n", filename);
                    write(socket, buffer, sizeof(buffer)-1);
                    memset(buffer, 0, sizeof(buffer));
                    return 0;
                }
                else if(UNisExist(invite_username) == 1){
                    userid = getUserId(invite_username);
                    if(isViewer(fileid,userid) == 1){
                        delViewer(fileid,userid);
                        snprintf(buffer, sizeof(buffer), "Remove viewer success.\n");
                        write(socket, buffer, sizeof(buffer)-1);
                        memset(buffer, 0, sizeof(buffer));
                        return 1;
                    }
                    else if(isEditor(fileid,userid) == 1){
                        delEditor(fileid,userid);
                        snprintf(buffer, sizeof(buffer), "Remove editor success.\n");
                        write(socket, buffer, sizeof(buffer)-1);
                        memset(buffer, 0, sizeof(buffer));
                        return 1;
                    }
                    else{
                        snprintf(buffer, sizeof(buffer), "The user is not viewer or editor.\n");
                        write(socket, buffer, sizeof(buffer)-1);
                        memset(buffer, 0, sizeof(buffer));
                        return 0;
                    }
                }
                else{
                    snprintf(buffer, sizeof(buffer), "The user is not exist.\n");
                    write(socket, buffer, sizeof(buffer)-1);
                    memset(buffer, 0, sizeof(buffer));
                    return 0;
                }
            }
            else{
                snprintf(buffer, sizeof(buffer), "The operation is not exist.\n");
                write(socket, buffer, sizeof(buffer)-1);
                memset(buffer, 0, sizeof(buffer));
                return 0;
            }
        }
        else{
            snprintf(buffer, sizeof(buffer), "You are not the holder of the file.\n");
            write(socket, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            return 0;
        }
    }
}

int openop(int socket){
    char buffer[1024];
    char filename[SIZE];
    int fileid;
    int userid;
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", get_user_file_and_permissions(socket));
    strcat(buffer,"Please input the filename you want to open:\n");
    write(socket, buffer, sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    read(socket, buffer, sizeof(buffer)-1);
    memset(filename, 0, sizeof(filename));
    strncpy(filename, buffer, sizeof(filename) - 1);
    memset(buffer, 0, sizeof(buffer));
    fileid=getFileId(filename);
    userid=getUserId(get_client_username(socket));
    if(isHolder(fileid, userid) == 1 || isEditor(fileid, userid) == 1 || isViewer(fileid, userid) == 1){
        //use openfile(fileid, socket)and savfile (fileid, socket) to update the newest file toclient
        int close = 0;
        while(close==0){
            openfile(fileid, socket);
            close = savefile(fileid, socket);
        }
        memset(buffer, 0, sizeof(buffer));
        return 1;
    }
    else{
        snprintf(buffer, sizeof(buffer), "You are not the holder, editor or viewer of the file.\n");
        write(socket, buffer, sizeof(buffer)-1);
        memset(buffer, 0, sizeof(buffer));
        return 0;
    }
}

void *handle_client(void *socket_desc) {
    pthread_detach(pthread_self());
    int sock = *(int*)socket_desc;
    char buffer[1024];

    memset(buffer, 0, sizeof(buffer));
    while ((read_size = read(sock, buffer, sizeof(buffer)-1)) > 0){
        printf("%s\n", buffer);
        if(find_client(sock) != NULL){
            update_client(sock,get_client_username(sock));
        }
        else if(!(strncmp("Login", buffer, 5) == 0 || strncmp("login", buffer, 5) == 0)){
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "Please enter Login/login to LOGIN!\n");
            write(sock, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
            continue;
        }

        if(strncmp("Exit",buffer,4)==0 || strncmp("exit", buffer, 4) == 0){
            printf("Disconnected from client.\n");
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "exit\n");
            write(sock, buffer, sizeof(buffer)-1);
            break;
        }
        // Login
        else if (strncmp("Login", buffer, 5) == 0 || strncmp("login", buffer, 5) == 0){
            if(loginop(sock) != 1){
                printf("Login operation failed.\n");
            }
        }
        // FileMGT
        else if (strncmp("FileMGT", buffer, 7) == 0 || strncmp("filemgt", buffer, 7) == 0){
            if(filemgtop(sock) != 1){
                printf("File management operation failed.\n");
            }
        }
        // InviteMGT
        else if(strncmp("InviteMGT", buffer, 9) == 0 || strncmp("invitemgt", buffer, 9) == 0){
            if(invitemgtop(sock) != 1){
                printf("Invite management operation failed.\n");
            }
        }
        // Open
        else if((strncmp("Update", buffer, 6) == 0 || strncmp("update", buffer, 6) == 0)){
            if(openop(sock) != 1){
                printf("Open operation failed.\n");
            }
        }
        else{
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "Please input the right operation.\n");
            write(sock, buffer, sizeof(buffer)-1);
            memset(buffer, 0, sizeof(buffer));
        }
    }
    if (read_size == 0){
        puts("Client disconnected");
    }
    else if (read_size == -1){
        perror("recv failed");
    }

    remove_client(sock);
    close(sock);
    return 0;
}

int main()
{
    int opt = TRUE;
    int m_sckt, new_socket;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_len;
    pthread_t thread_id;

    conn=mysql_init(NULL);   
    if (!conn)
    {
        printf("Error %u: %s\n",mysql_errno(conn),mysql_error(conn));
        return EXIT_FAILURE;
    }
    conn=mysql_real_connect(conn,"localhost","root","123456","UNDERLEAVES",0,NULL,0);
    if (conn)
    {
        printf("Connection success!\n");
    }
    else{
        printf("Connection failed!\n");
        return EXIT_FAILURE;
    }



    m_sckt = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sckt == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(m_sckt, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
				   sizeof(opt)) < 0)
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(m_sckt, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(m_sckt, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Listener on port %d \n", PORT);
    puts("Waiting for connections ...");

    while (1)
    {
        clnt_addr_len = sizeof(clnt_addr);
        new_socket = accept(m_sckt, (struct sockaddr *)&clnt_addr, &clnt_addr_len);
        if (new_socket < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        printf("New connection , socket fd is %d , ip is : %s , port : %d\n", 
               new_socket, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

        if (pthread_create(&thread_id, NULL, handle_client, (void *)&new_socket) != 0)
        {
            perror("Failed to create thread");
        }
    }
    close(m_sckt);
    mysql_close(conn);
    return 0;
}

//gcc server.c -l mysqlclient -lpthread -o server
