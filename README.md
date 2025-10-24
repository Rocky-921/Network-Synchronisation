# Networked Directory Synchronisation System

A multithreaded client–server application for synchronising directories across multiple networked clients using **TCP sockets** and **Linux’s inotify API**.

This project implements a **networked directory synchronisation system** that automatically reflects file and directory creation, deletion, and movement events from a central server to connected clients.

---

## Overview

The system consists of two main components:

1. **Server (`syncserver`)** – monitors a specified “sync” directory and broadcasts file system changes to connected clients.
2. **Client (`syncclient`)** – maintains a synchronised local directory and ignores specific file types based on a provided ignore list.

Both components are implemented using **multithreading** and **TCP communication**.  
File system monitoring is achieved via the **inotify API** (`<sys/inotify.h>`).

---

## Key Features

- **Recursive directory monitoring** — watches all subdirectories, even newly created ones.  
- **Real-time synchronisation** — clients immediately receive file and directory updates.  
- **Per-client ignore list** — clients can exclude specific file types from synchronisation.  
- **Multi-client support** — the server can handle multiple clients simultaneously (up to `max_clients`).  
- **Threaded design** — separate threads for directory monitoring and client communication.  
- **Lightweight and terminal-based** — works entirely from the Linux terminal using standard system calls.

---

## Usage

### Server

```bash
./syncserver <path_to_local_directory> <port> <max_clients>
```

Example:

```bash
./syncserver ./sync_folder 8080 5
```
Arguments:

 - 'path_to_local_directory' — directory to monitor for changes.

 - 'port' — TCP port number for the server.

 - 'max_clients' — maximum number of concurrent client connections.

### Client
```bash
./syncclient <path_to_local_directory> <path_to_ignore_list_file> <ip> <port>
```

Example:

```bash
./syncclient ./client_folder ./ignore.txt 127.0.0.1 8080
```

Arguments:

 - 'path_to_local_directory' — client’s local sync directory.

 - 'path_to_ignore_list_file' — file containing extensions to ignore.

 - 'ip' — IP address of the server.

 - 'port' — port number of the server.

### Ignore List Format

The ignore list is a comma-separated list of file extensions.
Example content of ignore.txt:

.mp4,.zip,.exe


When connecting, the client sends this list to the server (in a documented custom format) so that files matching these extensions are not synchronised.

### Technologies Used

 - C (POSIX)

 - inotify API

 - TCP sockets

 - pthreads

 - Linux system calls (open, read, write, send, recv, etc.)

### Notes

 - The server does not track file content modifications, only creation, deletion, and movement.

 - Use only terminal commands (mv, cp, rm, etc.) to modify files within the sync directory.

 - Tested on Linux (Ubuntu 22.04+).

```
Example Directory Flow
Server sync folder:
└── sync_folder/
    ├── docs/
    │   └── report.txt
    └── image.png

Client folder after sync:
└── client_folder/
    ├── docs/
    │   └── report.txt
    └── image.png
```

If ignore.txt contains .png, the client folder will only have:

```
└── client_folder/
    ├── docs/
    │   └── report.txt
```

---
## 🧑‍💻 Author
**Prince Garg**
