/*** includes ***/

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/ioctl.h>
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

struct editorConfig
{
	int cx, cy;
	int screenrows;
	int screencols;
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

/*** append buffer ***/

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
	char *newb = (char *) realloc(ab->b, ab->len + len);

	if (newb == NULL) return;
	// Copy the string at the end of current data in buffer
	memcpy(&newb[ab->len], s, len);
	// Update the pointer and length of abuf
	ab->b = newb;
	ab->len += len;
}

/*** output ***/

// Handle drawing of each row of the buffer of the text being edited
void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows / 3) {
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
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	// Show the cursor with "\x1b[?25h"
	abAppend(&ab, "\x1b[?25h", 6);

	// Write the contents of append buffer to screen once
	write(STDOUT_FILENO, ab.b, ab.len);
}

/*** input ***/

void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols - 1) {
				E.cx++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows - 1) {
				E.cy++;
			}
			break;
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

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main()
{
	enableRawMode();
	initEditor();

	// Keep reading single character from STDIN
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
