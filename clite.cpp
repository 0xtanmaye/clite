#include <termios.h>
#include <unistd.h>

void enableRawMode()
{
	// Create a struct to read attributes of terminal
	struct termios raw;

	// Fetch and store the current attributes into 'raw'
	tcgetattr(STDIN_FILENO, &raw);

	// c_lflag is for "local flags" (miscellaneous flags)
	// c_iflag is for "input flags"
	// c_oflag is for "output flags"
	// c_cflag is for "control flags"
	// Turn off the ECHO attribute to disable printing user input
	raw.c_lflag &= ~(ECHO);

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
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
	return 0;
}
