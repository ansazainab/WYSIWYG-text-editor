# WYSIWYG Text Editor
This is a collaborative WYSIWYG text editor inspired by Google Docs, implemented as a client-server system in C. Multiple clients can connect to a central server and simultaneously edit a shared markdown-like document in real time, with version updates handled at fixed time intervals.

# Features

- Real-time collaborative editing with multiple connected clients.
- Client-server architecture with synchronised document updates.
- Markdown-style formatting including headings, bold, italic, lists and links.
- Fine-grained text manipulation (insert, delete, newline control).
- Role-based permission system configurable via a `roles.txt` file.
- Command logging system to track all edits made to the document.
- Server-controlled update intervals for consistent document versioning.

# Running the Program

# Prerequisites
- GCC Compiler: Required to compile the C source files.
- Make: Required to build the project using the provided Makefile.

# Compilation
Compile the program using `make all`.

# Execution
To start the server: `./server <TIME_INTERVAL>` where `<TIME_INTERVAL>` specifies how frequently (in milliseconds) the document state is updated and synchronised across clients.
To start the client: `./client <server_pid> <username>` where `<server_pid>` is the process ID of the running server and `<username>` identifies the client.

# Commands
- `INSERT <pos> <content>` – Insert text at a specified position.
- `DEL <pos> <no_char>` – Delete characters from a position.
- `NEWLINE <pos>` – Insert a newline.
- `HEADING <level> <pos>` – Add heading (levels 1–3).
- `BOLD <pos_start> <pos_end>` – Apply bold formatting.
- `ITALIC <pos_start> <pos_end>` – Apply italic formatting.
- `BLOCKQUOTE <pos>` – Insert blockquote formatting.
- `ORDERED_LIST <pos>` – Add ordered list formatting.
- `UNORDERED_LIST <pos>` – Add unordered list formatting.
- `CODE <pos_start> <pos_end>` – Apply inline code formatting.
- `HORIZONTAL_RULE <pos>` – Insert a horizontal rule.
- `LINK <pos_start> <pos_end> <link>` – Create a hyperlink.
- `DOC?` – Display the full document.
- `PERM?` – View current user permissions.
- `LOG?` – Display full history of executed commands.
