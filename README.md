# FamGraph — local C backend + growing family tree

This is a standalone source-code project. It runs on your computer in a normal
browser. No ChatGPT extension, account, Node.js, npm, Python server, external
library, CDN, or internet connection is needed to run it.

## 1. Run on macOS

1. Extract `FamGraph-C-Source.zip`. You will get a folder named `famgraph-local`.
2. Open that folder in VS Code (File → Open Folder).
3. Open Terminal → New Terminal. Make sure the terminal is in `famgraph-local`.
4. Compile and run:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic server.c family.c -o famgraph
./famgraph
```

5. Keep that terminal open. Open this address in Chrome, Safari, or Firefox:

```text
http://127.0.0.1:8080
```

Do not double-click `index.html` and do not use VS Code Live Server: the C
program serves both the frontend and API from the same address.

If `cc` is missing on macOS, install Apple's Command Line Tools:

```sh
xcode-select --install
```

After the installation finishes, retry the compilation. `gcc` can also be used
instead of `cc` if you already have it installed.

If you prefer the Terminal app, type `cd ` (with a trailing space), drag the
extracted `famgraph-local` folder into the terminal, and press Enter. Then run
the compile and start commands above. This avoids the wrong-folder problem.

## Linux and Windows

On Linux, the same compile/run commands work with a C11 compiler installed.
On Windows, use WSL with a Linux C compiler and run the same commands in its
terminal. This code uses POSIX sockets; it does not compile unchanged in native
Windows PowerShell/MinGW. Windows browser access to WSL localhost normally works.

## 2. Try a small family

- Plant `Raj` as the originator.
- Choose `Raj` as parent and add `Amit`.
- Choose `Raj` as parent and add `Priya`.
- Choose `Amit` as parent and add `Arjun` and `Riya`.
- Search for `Amit`, or click his name on the drawing.
- His parent is Raj, sibling is Priya, and children are Arjun and Riya.

The originator sits at the trunk. Each child grows upward from the parent's
branch. Curved ink strokes, roots, offshoots, leaf clusters, and draw-on animation
create a botanical sketch rather than a boxes-and-lines diagram. Its branching
is driven by your family data; it is not a stock image. Use Zoom and scroll for
larger trees. Long names are shortened on the drawing; the full name appears
in the tooltip and relationship report.

Updates appear immediately after a successful C response. Other open tabs
refresh from the C server every two seconds. Existing branches may reposition
when a new subtree needs space; newly created branches animate their growth.

## 3. What stores the data?

**The C process is the authoritative data store.** `family.c` allocates one
`Person` per member with `calloc`. A node contains:

```c
typedef struct Person {
    unsigned id;
    char name[MAX_NAME + 1];
    struct Person *parent;
    struct Person *first_child;
    struct Person *next_sibling;
} Person;
```

This is a **general tree using first-child / next-sibling representation**.
Each node can have any number of children, within the overall demo limit.
The `next_sibling` pointer links children of the same parent; it is not an
ancestry edge. This is not a binary search tree.

- `find_id` and `find_name`: recursive depth-first search through the C tree.
- `add_person`: allocate a C node, validate its parent/name, and link it.
- `write_relations`: compute parent, children, siblings, grandparent,
  grandchildren, ancestors, and descendants from C pointers.
- `save_family`: write IDs, parent IDs, and names to `family.tsv` after every
  successful change, using a temporary file and atomic rename.
- `load_family`: read that file on startup and recreate all C nodes and pointers.
- `free_family`: postorder traversal to free dynamically allocated nodes.

The disk file stores records, not raw pointers: pointer addresses are meaningful
only inside the running process. The active data structure remains the C tree.

The browser must receive names and parent IDs to display them. JavaScript holds
that response temporarily for rendering, calculates drawing coordinates, and
sends HTTP requests. It does not create authoritative family records, calculate
kinship, or save a tree in browser storage. Clearing browser data does not erase
the C server's `family.tsv` file.

## 4. Files to study

| File | Purpose |
| --- | --- |
| `family.h` | C node definitions and function declarations |
| `family.c` | Tree insertion, DFS, relations, saving/loading, memory cleanup |
| `server.c` | Local HTTP server; routes browser requests to C functions |
| `public/index.html` | Forms and drawing area |
| `public/style.css` | Layout, colors, branch growth and leaf animation |
| `public/app.js` | HTTP requests, temporary SVG layout, drawing, UI updates |
| `Makefile` | Optional shortcut for compilation |
| `tests/test_api.py` | Optional tests using Python's standard library |
| `family.tsv` | Created automatically when you first plant a person |

Start studying `family.h`, then `family.c`, then the route function in `server.c`.
The HTTP parsing and SVG drawing are support code; the pointer tree is the DSA
part of the project.

## 5. Browser ↔ C API

| Request | Input | C behavior |
| --- | --- | --- |
| `GET /api/tree` | None | Traverse tree and return drawing snapshot |
| `POST /api/root` | `name` | Create root, persist, return its ID |
| `POST /api/child` | `parentId`, `name` | Find parent, attach new node, persist |
| `GET /api/relations?name=Amit` | Complete name | DFS search and calculate relationships |
| `GET /api/relations?id=2` | Person ID | Find node and calculate relationships |
| `POST /api/reset` | Empty form | Free tree and save an empty file |

POST requests use URL-encoded fields and `X-FamGraph: 1`. The browser code sends
both automatically. Validation failures return JSON with an `error` message.
If saving fails, additions are rolled back rather than acknowledged as saved.

## 6. Stop, restart, and troubleshoot

- **Stop:** press Ctrl+C in the terminal.
- **Restart:** run `./famgraph` from the project folder. The saved family returns.
- **Another port:** run `./famgraph 8081` and open `http://127.0.0.1:8081`.
- **Another data file:** run `./famgraph 8080 my-family.tsv`.
- **Backup:** stop the server and copy `family.tsv` elsewhere.
- **Reset:** the “Start a new tree” button asks before deleting all saved people.
- **Cannot find `server.c`:** your terminal is in the wrong folder. Run `ls`;
  it should list `server.c`, `family.c`, and `public`.
- **Address already in use:** stop the previous server or choose another port.
- **Disconnected:** keep the C program running; use its printed URL.
- **Cannot save:** run from a folder you can write to. Do not run two servers
  against the same data file at once.
- **Invalid saved data:** the server stops without replacing the file. Restore
  a backup or choose a new data filename. Do not edit the file while running.

## 7. Scope and complexity

This is a single-user local classroom project, not a public production server.
It listens only on loopback, processes requests sequentially, limits request
sizes, checks local host names, and requires the custom header for mutations.
Use one server process per data file.

- One originator and one recorded parent per person. Spouses, two-parent
  ancestry, and shared descendants would require a different model (a graph).
- Names must be unique. ASCII case-insensitive comparisons are used; Unicode
  names are preserved but full Unicode case folding is not implemented.
- Names support up to 100 UTF-8 bytes. The overall limit is 500 people and
  64 edges from the root to prevent unbounded recursive depth.
- No edit or individual deletion in this version; reset clears the entire tree.
- DFS lookup takes O(n). Ancestors take O(h). Descendants take O(k) for the
  descendants visited. Saving and a complete snapshot each take O(n).
- Adding a person includes O(n) name/parent validation and O(n) persistence.
  Appending a child traverses that parent's sibling list.
- Browser layout is recalculated from the latest snapshot. Very large trees
  require zooming/scrolling and are less readable than small classroom examples.

## Optional tests

With Python 3 and a C compiler installed:

```sh
python3 tests/test_api.py
```

Tests compile a temporary binary and use separate temporary data files, leaving
your family untouched. They cover real HTTP requests, relationships, restart
persistence, invalid inputs, more than ten children, Unicode/JSON escaping,
reset, split network requests, and failed-save rollback.

For compilers that support AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
SANITIZE=1 python3 tests/test_api.py
```

Verification in the development environment: warning-free strict C compilation,
five HTTP integration tests passed, and JavaScript syntax checked. Address and
undefined-behavior checks passed with leak detection disabled because the
container cannot run LeakSanitizer. A graphical browser check was not completed.
