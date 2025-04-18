/*** includes ***/

// Define feature test macros before includes to ensure portability.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
// #include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>


/*** defines ***/

#define CLITE_VERSION "0.0.1"
#define CLITE_TAB_STOP 8
#define CLITE_QUIT_TIMES 3

// Clear upper 3 bits of 'k', similar to Ctrl behavior in terminal
#define CTRL_KEY(k) ((k) & 0x01f)

enum editorKey
{
	// Backspace has no printable escape sequence like '\n' or '\r'
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

// Enum for highlighting types
enum editorHighlight
{
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)


/*** data ***/

// Syntax highlighting information for a particular filetype
struct editorSyntax
{
	const char *filetype;
	const char **filematch;
	const char *singleline_comment_start;
	int flags;
};

// erow - Editor row
// TOOD: Implement using std::string/vector if possible
struct erow
{
	int size; // Number of characters in the row
	int rsize; // Number of characters in the render (with formatting)
	char *chars; // The raw characters in the row
	char *render; // The formatted (rendered) text
	unsigned char *hl; // Array to store highlighting info for each character
};

struct editorConfig
{
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax *syntax;
	struct termios orig_termios;
};

struct editorConfig E;


/*** filetypes ***/

// Array of file extensions to match (e.g., .c, .h, .cpp). Terminated with NULL
const char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };

// HLDB - Highlight Database
struct editorSyntax HLDB[] = {
	{
		"c",
		C_HL_extensions,
		"//",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	}
};

// Constant to store the length of the HLDB array
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));


/*** terminal ***/

void die(const char *s)
{
	// Clear the screen and reposition the cursor on exit
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	// Prints a descriptive error message for global errno variable along with 's'
	perror(s);
	// Without using perror; Using strerror from "cstring"
	// if (s != NULL) {
	// 	std::cerr << s << ": " << strerror(errno) << "\n";
	// }
	exit(1);
}

void disableRawMode()
{
	// Set terminal attributes using the modified struct
	// TCSAFLUSH argument specifies waits for all pending output to be written
	// to terminal and discards any input that hasn't been read
	// tcsetattr() returns -1 on failure, handle that using die()
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	// Fetch and store the current attributes into 'E.orig_termios'
	// tcgetattr() returns -1 on failure, handle that using die()
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

	// Automatically call disableRawMode() when the program exits
	atexit(disableRawMode);

	// Create a struct to store modified attributes of terminal
	struct termios raw = E.orig_termios;


	// c_iflag is for "input flags"
	// Clear IXON attribute to disable pause (Ctrl+S) & resume (Ctrl+Q) transmission
	// Clear ICRNL attribute to disable translation of ('\r', 13) to ('\n', 10)
	// Clear BRKINT attribute to disable break condition which will send SIGINT
	// Clear INPCK attribute to disable parity checking (not for modern terminals) 
	// Clear ISTRIP attribute to disable clearing of 8th bit from input bytes
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	
	// c_oflag is for "output flags"
	// Clear OPOST attribute to disable output translation of '\n' to '\r\n'
	raw.c_oflag &= ~(OPOST);

	// c_cflag is for "control flags"
	// OR 'CS8' bit mask to set the character size (CS) to 8 bits per byte
	raw.c_cflag |= (CS8);

	// c_lflag is for "local flags" (miscellaneous flags)
	// Clear ECHO attribute to disable printing user input
	// Clear ICANON attribute to disable canonical mode (disable line-by-line)
	// Clear ISIG attribute to disable SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z/Y)
	// Clear IEXTEN attribute to disable single character literal input (Ctrl+V/O)
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	// c_cc field holds control characters, an array controlling terminal settings
	// VMIN field sets minimum input bytes for read(); set 0 to return on any input
	raw.c_cc[VMIN] = 0;
	// VTIME sets read() timeout in tenths of a second; set to 1 for 100ms
	raw.c_cc[VTIME] = 1;

	// Set terminal attributes using the modified struct
	// TCSAFLUSH argument specifies waits for all pending output to be written
	// to terminal and discards any input that hasn't been read
	// tcsetattr() returns -1 on failure, handle that using die()
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// Wait for one keypress and return it
int editorReadKey()
{
	int nread;
	char c;
	// Ignore return value of read() other than 1 i.e. single keypress
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		// In Cygwin, read() returns -1 on timeout with EAGAIN, not treated as error
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	// If an escape character is read
	if (c == '\x1b') {
		// 'seq' buffer is 3 bytes long to support longer escape sequences in future
		char seq[3];
		// Try to read two more bytes, if reads time out then assume user pressed Esc
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		// If the first byte is '[' then it's an escape sequence
		if (seq[0] == '[') {
			// Handle Page Up/Down keys (<esc>[5~ / <esc>[6~)
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				// Check for '~' at the end of the escape sequence to confirm PAGE UP/DOWN
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				// Check for arrow key escape sequence
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		// Handle different escape sequences that could be created for some keys
		} else if (seq[0] == 'O') {
			switch(seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		// If the escape sequence is not recognized, return the Esc character
		return '\x1b';
	} else {
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	// Send \x1b[6n to query terminal for cursor position (n: Device Status Report)
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	// Read the reply from standard input and store each character in buf until 'R'
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	// Null-terminate the reply stored in buf
	buf[i] = '\0';

	// Verify escape sequence, then parse cursor position into rows & cols
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	// Get terminal size using ioctl() with TIOCGWINSZ request
	// Terminal Input/Output Control Get WIN SiZe
	// Check for erroneous return value -1 or invalid size of 0
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// Move cursor to bottom-right by sending \x1b[999C (right) & \x1b[999B (down)
		// Avoids using \x1b[999;999H due to undefined behavior off-screen
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}


/*** syntax highlighting ***/

int is_separator(int c)
{
	// strchr() finds the first occurrence of `c` in the string
	// Returns a pointer to the character or NULL if not found.
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row)
{
	// Reallocate `hl` to match `render` size (`rsize`)
	row->hl = (unsigned char*) realloc(row->hl, row->rsize);
	// Set all `hl` values to HL_NORMAL (default)
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL) return;

	// Alias for easier access to single-line comment start pattern
	const char *scs = E.syntax->singleline_comment_start;
	int scs_len = scs ? strlen(scs) : 0;

	// Assume the beginning of the line is a separator
	int prev_sep = 1;

	int in_string = 0;

	int i = 0;
	// Use a while loop for future flexibility (e.g., multi-char patterns)
	while (i < row->rsize) {
		char c = row->render[i];
		// Get the highlight type of the previous character
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		// Highlight single-line comments if applicable
		if (scs_len && !in_string) {
			// Check if we encounter the start of a single-line comment
			if (!strncmp(&row->render[i], scs, scs_len)) {
				// Highlight the rest of the line
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				// End highlighting for the line
				break;
			}
		}

		// Handle string highlighting if enabled
		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				// Highlight character as part of a string
				row->hl[i] = HL_STRING;
				// Handle escaped quotes inside strings (e.g., \" or \')
				if (c == '\\' && i + 1 < row->rsize) {
					// Highlight the character after the backslash
					row->hl[i + 1] = HL_STRING;
					// Consume both characters
					i += 2;
					continue;
				}
				// End string if the closing quote is found
				if (c == in_string) in_string = 0;
				i++;
				// Closing quote is considered a separator
				prev_sep = 1;
				continue;
			} else {
				// Start string if we encounter a quote (single or double)
				if (c == '"' || c == '\'') {
					// Store the type of quote
					in_string = c;
					// Highlight the opening quote
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}

		// Handle number highlighting if enabled
		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			// If current char is a digit and the previous character is a separator
			// or part of a number (prev_hl == HL_NUMBER), or a dot (.) after a number
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
					(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				// Inside a number, not a separator.
				prev_sep = 0;
				continue;
			}
		}

		// Mark separator and move to the next character
		prev_sep = is_separator(c);
		i++;
	}
}

// Map syntax highlight value (`hl`) to corresponding ANSI color code
int editorSyntaxToColor(int hl)
{
	switch (hl) {
		case HL_COMMENT: return 36;
		case HL_STRING: return 35;
		case HL_NUMBER: return 31;
		case HL_MATCH: return 34;
		default: return 37;
	}
}

// Selects syntax highlighting based on the file extension or filename
void editorSelectSyntaxHighlight()
{
	E.syntax = NULL;
	// Exit if no filename is present
	if (E.filename == NULL) return;

	// Get the extension from the filename using strrchr()
	char *ext = strrchr(E.filename, '.');

	// Loop through all entries in the HLDB (syntax database)
	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax *s = &HLDB[j];
		unsigned int i = 0;
		// Check each filematch pattern in the current editorSyntax
		while (s->filematch[i]) {
			// Check if pattern is an extension
			int is_ext = (s->filematch[i][0] == '.');

			// Match file extension or substring of filename
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || // Extension match
					(!is_ext && strstr(E.filename, s->filematch[i]))) { // Substring match
				E.syntax = s;

				// Rehighlight each row in the file after setting E.syntax
				int filerow;
				for (filerow = 0; filerow < E.numrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
				}

				return;
			}
			i++;
		}
	}
}


/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)
{
	int rx = 0;
	// Loop through characters up to cx
	for (int j = 0; j < cx; j++) {
		// If tab, calculate space to next tab stop
		if (row->chars[j] == '\t')
			rx += (CLITE_TAB_STOP - 1) - (rx % CLITE_TAB_STOP);
		// Increment for regular character
		rx++;
	}
	// Return final render position
	return rx;
}

int editorRowRxToCx(erow *row, int rx)
{
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t')
			cur_rx += (CLITE_TAB_STOP - 1) - (cur_rx % CLITE_TAB_STOP);
		cur_rx++;
		// Return the char index once rx is reached
		if (cur_rx > rx) return cx;
	}
	// In case rx exceeds the valid range
	return cx;
}

void editorUpdateRow(erow *row)
{
	int tabs = 0;
	int j;
	// Count tabs and allocate memory for render adding 7 chars per tab
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;

	free(row->render);
	// since row->size counts 1 for each tab, we add 7 extra chars for each tab).
	row->render = (char*) malloc(row->size + tabs * (CLITE_TAB_STOP - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		// Modify the loop to handle tabs: append one space for the tab, then fill
		// with spaces until reaching the next tab stop (multiple of 8 columns).
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % CLITE_TAB_STOP != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	// After updating render, call editorUpdateSyntax to apply syntax highlighting
	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len)
{
	if (at < 0 || at > E.numrows) return;

	// Reallocate memory for E.row to accommodate one more erow
	E.row = (erow*) realloc(E.row, sizeof(erow) * (E.numrows + 1));

	// Make room for the new row at the specified index using memmove()
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	E.row[at].size = len;
	E.row[at].chars = (char*) malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row)
{
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at)
{
	if (at < 0 || at >= E.numrows) return;
	// Free memory associated with the row being deleted
	editorFreeRow(&E.row[at]);

	// Shift all rows after the deleted one up by one position
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));

	// Decrease the total row count
	E.numrows--;

	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
	// Clamp 'at' to be within [0, row->size], allowing insert at end of row
	if (at < 0 || at > row->size) at = row->size;
	// Resize row, shift chars to make room, insert new char, and update render
	row->chars = (char*) realloc(row->chars, row->size + 2);
	// Like memcpy but allows overlap of source & destination
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len)
{
	// Reallocate memory for the row to accommodate the new string and null byte
	row->chars = (char*) realloc(row->chars, row->size + len + 1);

	// Copy the string to the end of the current row
	memcpy(&row->chars[row->size], s, len);

	// Update the row size and add a null terminator at the end
	row->size += len;
	row->chars[row->size] = '\0';

	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at)
{
	// If the position is invalid (negative or beyond the row size), do nothing
	if (at < 0 || at >= row->size) return;

	// Shift characters left to overwrite the deleted one
	// memmove handles overlapping memory regions, so it's safe to use here
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);

	// Decrease row size and update
	row->size--;
	editorUpdateRow(row);

	E.dirty++;
}


/*** editor operations ***/

void editorInsertChar(int c)
{
	// If cursor is past the last row, append a new empty row first
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, (char*) "", 0);
	}

	// Insert the character at the current cursor position
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	// Move cursor forward after insertion
	E.cx++;
}

void editorInsertNewline()
{
	// If cursor is at the beginning of the line, insert a blank row before it
	if (E.cx == 0) {
		editorInsertRow(E.cy, (char*)"", 0);
	} else {
		// Otherwise, split the current row at the cursor position
		erow *row = &E.row[E.cy];

		// Insert a new row with the characters to the right of the cursor
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);

		// Reassign the row pointer (because editorInsertRow() may reallocate memory)
		row = &E.row[E.cy];

		// Truncate the current row to the cursor position
		row->size = E.cx;
		row->chars[row->size] = '\0';

		editorUpdateRow(row);
	}

	// Move the cursor to the beginning of the new row
	E.cy++;
	E.cx = 0;
}

void editorDelChar()
{
	// If the cursor is past the end of the file, do nothing
	if (E.cy == E.numrows) return;
	// If cursor is at the start of the first row, there's nothing to delete
	if (E.cx == 0 && E.cy == 0) return;

	// Get the row the cursor is currently on
	erow *row = &E.row[E.cy];

	// If there is a character to the left of the cursor, delete it
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		// Move the cursor one step left
		E.cx--;
	} else {
		// Set cursor to the end of the previous row
		E.cx = E.row[E.cy - 1].size;
		// Append current row to previous one
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		// Delete the current row
		editorDelRow(E.cy);
		// Move cursor up to the previous row
		E.cy--;
	}
}


/*** file i/o ***/

char *editorRowsToString(int *buflen)
{
	// Initialize total length of the final string
	int totlen = 0;
	int j;
	// Loop through each row and add the row size + 1 (for newline)
	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1;
	*buflen = totlen;
	char *buf = (char*)malloc(totlen);
	char *p = buf;
	for (j = 0; j < E.numrows; j++) {
		// Copy row characters into the buffer
		memcpy(p, E.row[j].chars, E.row[j].size);
		// Move pointer forward by row size
		p += E.row[j].size;
		// Add newline character after each row
		*p = '\n';
		p++;
	}

	// Return the constructed string (caller must free it)
	return buf;
}

/* Using fstream (C++ version)
void editorOpen(const char* filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) {
		die("fopen");
	}

	std::string line;
	while (std::getline(file, line)) {
		// Strip trailing newline and carriage return characters
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, (char*) line.c_str(), line.length());
	}

	file.close();
}
*/

void editorOpen(char *filename)
{
	free(E.filename);
	E.filename = strdup(filename);

	// Set E.syntax according to E.filename
	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	// Automatically allocates memory for line read when pointer is NULL & cap=0
	// getline() returns the length of the line read or -1 when reached EOF
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		// Strip trailing newline and carriage return characters
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
					line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

// TODO: Use a temporary file and rename it to the target file after writing
// to ensure a safer save process, checking for errors at each step
void editorSave()
{
	// Prompt the user for a filename when E.filename is NULL
	if (E.filename == NULL) {
		E.filename = editorPrompt((char*) "Save as: %s (ESC to cancel)", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		// Set E.syntax according to E.filename
		editorSelectSyntaxHighlight();
	}

	int len;
	// Get the editor content as a string and its length
	char *buf = editorRowsToString(&len);

	// Open the file (create it if it doesn't exist, set permissions to 0644)
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

	// open() returns -1 on error
	if (fd != -1) {
		// Truncate the file to match the content length
		// ftruncate() adjusts file size: truncates excess data or pads with 0 bytes
		// Safer than O_TRUNC since it preserves data if write() fails after truncation
		if (ftruncate(fd, len) != -1) {
			// Write the content to the file
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	// strerror() returns the error message corresponding to errno
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*** search ***/

void editorFindCallback(char *query, int key)
{
	// Stores the last match index or -1 if none
	static int last_match = -1;
	// Search direction: 1 for forward, -1 for backward
	static int direction = 1;

	// Line number where highlights need to be restored
	static int saved_hl_line;
	// Holds the previous highlight state for a row
	static char *saved_hl = NULL;

	// Restore previous highlight state if it exists
	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		// Free the saved memory after restoring
		free(saved_hl);
		// Reset the saved highlight pointer
		saved_hl = NULL;
	}

	// Exit search on Enter/Escape, else repeat search for any other key.
	if (key == '\r' || key == '\x1b') {
		// Reset last_match and direction to initial values as we are leaving search
		last_match = -1;
		direction = 1;
		return;
	} 
	// Set direction to forward if right/down arrow keys are pressed
	else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	}
	// Set direction to backward if left/up arrow keys are pressed
	else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	}
	// Reset search if any other key is pressed
	else {
		last_match = -1;
		direction = 1;
	}

	// If there was no previous match, search starts from the first row, forward
	if (last_match == -1) direction = 1;

	// Start search from the last match position
	int current = last_match;

	int i;
	// Loop through all rows to search for the query
	for (i = 0; i < E.numrows; i++) {
		current += direction;

		// Wrap around to the opposite end of the file when we reach the start/end
		if (current == -1) current = E.numrows - 1;
		else if (current == E.numrows) current = 0;

		erow *row = &E.row[current];
		// Check if query is found in the current row using strstr()
		char *match = strstr(row->render, query);
		if (match) {
			last_match = current;
			// Set cursor position to the match's location
			E.cy = current;
			// Set the column to the position of the match within the row by calculating
			// the offset between the start of the row and the match pointer
			// Then converting the match rx to cx
			E.cx = editorRowRxToCx(row, match - row->render);
			// Set rowoff to bottom to scroll the match to the top of the screen
			E.rowoff = E.numrows;

			// Save current highlight state before modifying it
			saved_hl_line = current;
			saved_hl = (char*) malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
			// Mark the matched substring as HL_MATCH in the `hl` array
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind()
{
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	// Get the search query from the user; ESC to cancel returns NULL
	char *query = editorPrompt((char*) "Search: %s (Use ESC/Arrows/Enter)",
					editorFindCallback);

	if (query) {
		free(query);
	} else {
		// If search is cancelled, restore the values we saved
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}


/*** append buffer ***/

// TODO: Implement the append buffer using std::string/vector if possible
struct abuf
{
	char *b;
	int len;

	// Use constructor supported in C++ for struct abuf instead of defined constant
	abuf() : b(NULL), len(0) {}

	// Use destructor supported in C++ for struct abuf instead of function abFree()
	~abuf()
	{
		free(b);
	}
};

// TODO: Refactor later as member function of struct if possible
void abAppend(struct abuf *ab, const char *s, int len)
{
	// Realloc to with original length + length of string to be appended
	char *newb = (char*) realloc(ab->b, ab->len + len);

	if (newb == NULL) return;
	// Copy the string at the end of current data in buffer
	memcpy(&newb[ab->len], s, len);
	// Update the pointer and length of abuf
	ab->b = newb;
	ab->len += len;
}


/*** output ***/

void editorScroll()
{
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	// Scroll up if the cursor is above the visible window (set E.rowoff to E.cy)
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}

	// Scroll down if the cursor is below the visible window
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}

	// Scroll left if the cursor is beyond the left edge of the visible window
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}

	// Scroll right if the cursor is beyond the right edge of the visible window
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

// Handle drawing of each row of the buffer of the text being edited
void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		// Check if drawing a row within the text buffer or after its end
		if (filerow >= E.numrows) {
			// Display welcome message only if the text buffer is empty
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				// Store the message in welcome buffer by interpolating the editor version
				int welcomelen = snprintf(welcome, sizeof(welcome), 
						"CLiTE editor -- version %s", CLITE_VERSION);
				// Truncate length of the string if terminal size is too small
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				// Calculate how far from the left should the welcome message start
				int padding = (E.screencols - welcomelen ) / 2;
				// Print the first character as tilde ('~')
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				// Fill the remaining space with space characters (' ')
				while (padding--) abAppend(ab, " ", 1);

				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			// Calculate and append the visible portion of the row after column offset
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			// Truncate rendered line if it exceeds the screen width
			if (len > E.screencols) len = E.screencols;
			// Set `c` & 'hl' to the correct part of `render` based on filerow & coloff
			char *c = &E.row[filerow].render[E.coloff];
			unsigned char *hl = &E.row[filerow].hl[E.coloff];

			// -1 means default color (HL_NORMAL)
			int current_color = -1;

			int j;
			for (j = 0; j < len; j++) {
				if (hl[j] == HL_NORMAL) {
					// Reset to default color when switching from highlighted to normal
					if (current_color != -1) {
						// Escape sequence "\x1b[39m": SGR command, 39 resets to default color
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else {
					// Apply color when text is highlighted
					int color = editorSyntaxToColor(hl[j]); // Get color code
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						// Apply new color
						abAppend(ab, buf, clen);
					}
					// Append the character
					abAppend(ab, &c[j], 1);
				}
			}
			// Escape sequence "\x1b[39m": SGR command, 39 resets to default color
			abAppend(ab, "\x1b[39m", 5);
		}

		// Erase from the cursor to the end of the current line using "\x1b[K".
		abAppend(ab, "\x1b[K", 3);

		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab)
{
	// <esc>[7m switches to inverted colors (white text on white background)
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];

	// Display filename (or [No Name]) and line count in the status bar.
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
			E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");

	// Display the filetype and current line number in the right status string
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
			E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

	// Cut the status string short if doesn't fit inside screen width
	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);

	while (len < E.screencols) {
		// Pad with spaces until the right-aligned status fits, then print and exit
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}

	// <esc>[m switches back to normal formatting.
	abAppend(ab, "\x1b[m", 3);

	// Add new line after status bar to make room for status msg
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
	// Clear the message bar with the <esc>[K
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	// Make sure the message will fit the width of the screen
	if (msglen > E.screencols) msglen = E.screencols;

	// Display the message, but only if the message is less than 5 seconds old
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
	editorScroll();

	struct abuf ab;

	// Escape sequences start with \x1b, followed by [ and an argument
	// before the command

	// Hide the cursor with "\x1b[?25l"
	abAppend(&ab, "\x1b[?25l", 6);
	// Write \x1b[H to position the cursor at top-left (1,1), default for 'H'
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	// Modified H command to move cursor to (1-indexed) position
	// Adjust cursor on screen by subtracting E.rowoff, as E.cy is now file pos
	// Adjust cursor on screen by subtracting E.coloff, as E.cx is now file pos
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	// Show the cursor with "\x1b[?25h"
	abAppend(&ab, "\x1b[?25h", 6);

	// Write the contents of append buffer to screen once
	write(STDOUT_FILENO, ab.b, ab.len);
}

void editorSetStatusMessage(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	// Get current time by passing NULL to time() (Unix time)
	E.statusmsg_time = time(NULL);
}


/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
	// Initial buffer size for input
	size_t bufsize = 128;
	char *buf = (char*) malloc(bufsize);
	// Track the length of input
	size_t buflen = 0;
	// Initialize the buffer to an empty string
	buf[0] = '\0';

	while (1) {
		// Display the prompt and user input in the status bar
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		// Wait for key press
		int c = editorReadKey();

		// Allow user to press Backspace (or Ctrl-H, or Delete) in the input prompt
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) buf[--buflen] = '\0';
		}
		// Allow user to press Esc to cancel the input prompt
		else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		}
		// If Enter is pressed and input is not empty, return the input
		else if (c == '\r') {
			if (buflen != 0) {
				// Clear status message
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		}
		// If a printable character is pressed, append it to the buffer
		else if (!iscntrl(c) && c < 128) {
			// Ensure the buffer has enough space, realloc if necessary
			if (buflen == bufsize - 1) {
				// Double the buffer size
				bufsize *= 2;
				buf = (char*) realloc(buf, bufsize);
			}
			// Append the character
			buf[buflen++] = c;
			// Null-terminate the string
			buf[buflen] = '\0';
		}

		if (callback) callback(buf, c);
	}
}

void editorMoveCursor(int key)
{
	// Get the current row or allow the cursor to be one past the last line
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) { // Check for first line
				// Move cursor to end of previous line if at the beginning
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			// Move cursor right if within the current line's length, allowing one past
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
				// Move cursor to beginning of next line if at the end
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			// Allow scrolling past bottom of the screen but within the file
			if (E.cy < E.numrows) {
				E.cy++;
			}
			break;
	}

	// Correct E.cx if it’s past the end of the current line after moving down
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size: 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

void editorProcessKeypress()
{
	static int quit_times = CLITE_QUIT_TIMES;

	int c = editorReadKey();

	switch (c) {
		// Handle '\r' (Enter key)
		case '\r':
			editorInsertNewline();
			break;

		// Handle Ctrl+Q to quit
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changes. "
						"Press Ctrl-Q %d more times to quit.", quit_times);
				quit_times--;
				return;
			}
			// Clear the screen and reposition the cursor on exit
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		// Handle Ctrl+S to save
		case CTRL_KEY('s'):
			editorSave();
			break;

		// For now make HOME and END key move the cursor to left/right edges
		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;

		case CTRL_KEY('f'):
			editorFind();
			break;


		// Handle Backspace (127), Ctrl-H (8) (Old Backspace), and Delete (ESC[3~)
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			// Delete (or Right Arrow + Backspace) removes the character right of the cursor
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			// The code is written in braces to allow creation of times variable
			{
				// Scroll page by moving cursor to top/bottom and simulating keypresses
				if (c == PAGE_UP) {
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows) E.cy = E.numrows;
				}

				int times = E.screenrows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;

		// Ignore Ctrl-L and Esc; screen already refreshes, and Esc avoids unwanted input
		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;
	}

	quit_times = CLITE_QUIT_TIMES;
}


/*** init ***/

void initEditor()
{
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	// E.statusmsg is empty string, so no message displayed by default
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;


	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	// Decrement E.screenrows to make room for status bar and status msg
	E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();
	if (argc >=2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
