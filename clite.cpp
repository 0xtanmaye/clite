/*** includes ***/

// Define feature test macros before includes to ensure portability.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// #include <fstream>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CLITE_VERSION "0.0.1"

// Clear upper 3 bits of 'k', similar to Ctrl behavior in terminal
#define CTRL_KEY(k) ((k) & 0x01f)

enum editorKey {
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

/*** data ***/

// erow - Editor row
// TOOD: Implement using std::string/vector if possible
struct erow
{
	int size;
	char *chars;
};

struct editorConfig
{
	int cx, cy;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	struct termios orig_termios;
};

struct editorConfig E;

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

/*** row operations ***/

void editorAppendRow(char *s, size_t len)
{
	// Reallocate memory for E.row to accommodate one more erow
	E.row = (erow*) realloc(E.row, sizeof(erow) * (E.numrows + 1));

	// Set 'at' to the new row index
	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = (char*) malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}


/*** file i/o ***/

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
		editorAppendRow((char*) line.c_str(), line.length());
	}

	file.close();
}
*/

void editorOpen(char *filename)
{
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
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
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
	// Scroll up if the cursor is above the visible window (set E.rowoff to E.cy)
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}

	// Scroll down if the cursor is below the visible window
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}

	// Scroll left if the cursor is beyond the left edge of the visible window
	if (E.cx < E.coloff) {
		E.coloff = E.cx;
	}

	// Scroll right if the cursor is beyond the right edge of the visible window
	if (E.cx >= E.coloff + E.screencols) {
		E.coloff = E.cx - E.screencols + 1;
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
			int len = E.row[filerow].size - E.coloff;
			if (len < 0) len = 0;
			// Truncate rendered line if it exceeds the screen width
			if (len > E.screencols) len = E.screencols;
			// Draw row by writing out the chars field of the erow
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}


		// Erase from the cursor to the end of the current line using "\x1b[K".
		abAppend(ab, "\x1b[K", 3);
		// Print cariage return and newline only if not the last row
		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
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

	char buf[32];
	// Modified H command to move cursor to (1-indexed) position
	// Adjust cursor on screen by subtracting E.rowoff, as E.cy is now file pos
	// Adjust cursor on screen by subtracting E.coloff, as E.cx is now file pos
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	// Show the cursor with "\x1b[?25h"
	abAppend(&ab, "\x1b[?25h", 6);

	// Write the contents of append buffer to screen once
	write(STDOUT_FILENO, ab.b, ab.len);
}

/*** input ***/

void editorMoveCursor(int key) {
	// Get the current row or allow the cursor to be one past the last line
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			// Move cursor right if within the current line's length, allowing one past
			if (row && E.cx < row->size) {
				E.cx++;
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

// Wait for a keypress and handle it
void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c) {
		// Handle Ctrl+Q to quit
		case CTRL_KEY('q'):
			// Clear the screen and reposition the cursor on exit
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		// For now make HOME and END key move the cursor to left/right edges
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols - 1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			// For now make PAGE UP/DOWN move the cursor to the top/bottom of the screen
			// The code is written in braces to allow creation of times variable
			{
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
	}
}

/*** init ***/

void initEditor()
{
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();
	if (argc >=2) {
		editorOpen(argv[1]);
	}

	// Keep reading single character from STDIN
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
