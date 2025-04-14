#include <cctype>
#include <cstdlib>
#include <iostream>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode()
{
	// Set terminal attributes using the modified struct
	// TCSAFLUSH argument specifies waits for all pending output to be written
	// to terminal and discards any input that hasn't been read
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
	// Fetch and store the current attributes into 'orig_termios'
	tcgetattr(STDIN_FILENO, &orig_termios);
	// Automatically call disableRawMode() when the program exits
	atexit(disableRawMode);

	// Create a struct to store modified attributes of terminal
	struct termios raw = orig_termios;


	// c_iflag is for "input flags"
	// Clear IXON attribute to disable pause (Ctrl+S) & resume (Ctrl+Q) transmission
	// Clear ICRNL attribute to disable translation of ('\r', 13) to ('\n', 10)
	raw.c_iflag &= ~(ICRNL | IXON);
	
	// c_oflag is for "output flags"
	// Clear OPOST attribute to disable output translation of '\n' to '\r\n'
	raw.c_oflag &= ~(OPOST);

	// c_cflag is for "control flags"

	// c_lflag is for "local flags" (miscellaneous flags)
	// Clear ECHO attribute to disable printing user input
	// Clear ICANON attribute to disable canonical mode (disable line-by-line)
	// Clear ISIG attribute to disable SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z/Y)
	// Clear IEXTEN attribute to disable single character literal input (Ctrl+V/O)
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	// Set terminal attributes using the modified struct
	// TCSAFLUSH argument specifies waits for all pending output to be written
	// to terminal and discards any input that hasn't been read
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main()
{
	enableRawMode();

	char c;
	// Keep reading single character from Standard input until EOF
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		if (iscntrl(c)) {
			std::cout << +c << std::endl;
		} else {
			std::cout << +c << " ('" << c << "')" << std::endl;
		}
	}
	return 0;
}
