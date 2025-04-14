/*** includes ***/

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

// Clear upper 3 bits of 'k', similar to Ctrl behavior in terminal
#define CTRL_KEY(k) ((k) & 0x01f)

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s)
{
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
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	// Fetch and store the current attributes into 'orig_termios'
	// tcgetattr() returns -1 on failure, handle that using die()
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

	// Automatically call disableRawMode() when the program exits
	atexit(disableRawMode);

	// Create a struct to store modified attributes of terminal
	struct termios raw = orig_termios;


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
char editorReadKey()
{
	int nread;
	char c;
	// Ignore return value of read() other than 1 i.e. single keypress
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		// In Cygwin, read() returns -1 on timeout with EAGAIN, not treated as error
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

/*** output ***/

void editorRefreshScreen()
{
	/* Write escape sequence to terminal to clear the screen
	 * \x1b is the escape character (27 in decimal)
	 * [2J is the "Erase In Display" command with argument 2,
	 * which clears the entire screen
	 * Escape sequences start with \x1b, followed by [ and an argument
	 * before the command */
	write(STDOUT_FILENO, "\x1b[2J", 4);
	// Write \x1b[H to position the cursor at top-left (1,1), default for 'H'
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

// Wait for a keypress and handle it
void editorProcessKeypress()
{
	char c = editorReadKey();

	switch (c) {
		// Handle Ctrl+Q to quit
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}

/*** init ***/

int main()
{
	enableRawMode();

	// Keep reading single character from STDIN
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
