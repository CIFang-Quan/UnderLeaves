# UnderLeaves
A simple online collaborative editor
## 1. Introduction
- This is a multi-user online collaborative document editing application based on the C language, utilizing solutions such as sockets, threads, and GTK. Its core functionalities are well-developed, yet there remain areas for enhancement which are anticipated to be addressed and implemented in future iterations.

## 2. Environment Setup
### 2.1. Opreating System 
It is recommended to compile and run the source files exclusively on Linux systems.We are using ubuntu-20.04.6-desktop-amd64
### 2.2. DataBase
MySQL must be installed and configured in advance.The database settings are as shown below:
```
/*-------------------------------------------------- SQL OP FUNCTION --------------------------------------------------*/

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
```
### 2.3. UI Design
GTK3 must be installed and configured prior to use.Of course, you can also use GTK4, which is the current stable version. This may require modification of the source code.

## 3. Implemented Features
- Create new account
- Login
- FileMGT(filemgt):Create/Delete files belonging to you on the server
- InviteMGT(invitemgt):Modify the permissions on your files and invite/remove other users' permissions
- Update(update)Open the file for viewing/modification, and the latest information will be synchronized in real time.
- Friendly interpersonal interactions
- The executable file exhibits a compact footprint

## 4. Compile and Execute
### 4.1. server
```
gcc server.c -l mysqlclient -lpthread -o server
```

### 4.2. client
```
gcc `pkg-config --cflags gtk+-3.0` client.c `pkg-config --libs gtk+-3.0` -lpthread -o client
```
-Note: The server must be executed before the client！
