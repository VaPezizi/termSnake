#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

/*====(Defines)====*/
#define CONTROL_KEY(k) ((k) & 0x1f)	//Sets upper 3 bits to 0. (Basically mimics how Ctrl key works in the terminal)

FILE * logFile;

/*====( Variables )====*/
enum DIRECTION{
	UP,
	DOWN,
	LEFT,
	RIGHT
};

typedef struct SnakePart{
	int posX;
	int posY;
	struct SnakePart * next;	
}SnakePart;

typedef struct Snake{
	SnakePart * HEAD;
	enum DIRECTION direction;
	int score;
}Snake;

typedef struct{
	int posX;
	int posY;
}Apple;
enum editorKey{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN
};


struct gameConfig{
	int cursorX, cursorY;
	int winRows;
	int winCols;	
	char * gameArr;
	struct Snake * snake;
	struct termios originalSettings;	//Saving the terminals original attributes here
};	


struct Snake snake;
struct gameConfig config;

/* ====(Terminal related functions (Raw mode etc))==== */

void killSnake(){
	free(snake.HEAD);
}

void closeLog(){
	fclose(logFile);
}

void kill(const char * c){
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	
	write(STDOUT_FILENO, "\x1b[?25h", 6);

	killSnake();
	closeLog();

	perror(c);
	exit(1);
}


//Simply restores the terminal to 
void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.originalSettings) == -1) kill("tcsetattr");
}

//Enables "Raw mode" (Gives data directly to the Program without interprenting special characters)
void enableRawMode(){
	if(tcgetattr(STDIN_FILENO, &config.originalSettings) == -1) kill("tcsetattr");	//Saving settings here
	atexit(disableRawMode);

	//What these Flags mean, can be found here: https://www.man7.org/linux/man-pages/man3/termios.3.html
	struct termios new = config.originalSettings;
	new.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);		//Turning off some variables (~ is a bitwise NOT operator)
	new.c_iflag &= ~(BRKINT | ISTRIP | INPCK | IXON | ICRNL);			//Note to self:
	new.c_oflag &= ~(OPOST);	//Output flags			//	ECHO, ICANON etc are bitflags
	new.c_cflag |= (CS8);						//	c_lflag is a series of 1 and 0 and for example ECHO is 1000
	new.c_cc[VMIN] = 0;						//	We set attributes here by doing a bit flip on for example ECHO and ICANON bits on c_lflag
	new.c_cc[VTIME] = 1;
	
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &new)==-1)kill("tcsetattr");	//Setting modified variables.

}

//Reads a key and returns it
int readKey(){
	int error;
	char c;
	while((error = read(STDIN_FILENO, &c, 1)) != 1){
		if(error == -1 && errno != EAGAIN) kill("read");
	}
	if(c=='\x1b'){				//If we encounter an escape character, we read two more bytes to the "seq" buffer
		char seq[3];

		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';		
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';		//If these timeout (return -1), we assume the user just pressed Escape and return that

		if(seq[0] == '['){
			switch (seq[1]){
				case 'A': return ARROW_UP;	//Up = \x1b[A
				case 'B': return ARROW_DOWN;	//Down = \x1b[B... You get the point. We translate arrow keys to wasd for easy movement
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
			}
		}
		return '\x1b';		//We return this, if its an escape character that we don't use
	}else{
		return c;		//Else just return the key pressed
	}
}

int appendLog(const char* str){
	printf("Pointer: %p", logFile);
	if(!logFile){
		kill("Log file not initalised!");
		return -1;
	}
	fprintf(logFile, str);
	return 1;
}

int getCursorPosition(int *rows, int * columns){
	char buffer[32];
	unsigned int i = 0;
	
	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;		//6n Means we are asking for the cursor position

	while (i < sizeof(buffer) - 1){
		if(read(STDIN_FILENO, &buffer[i], 1) != 1)break;	//Reading the bytes in to the buffer
		if(buffer[i] == 'R')break;				//'R' is in the "Cursor Position Report", that's why we read till there
		i++;
	}
	buffer[i] = '\0';						//Adding a '0 byte' to the end of the string

	if (buffer[0] != '\x1b' || buffer[1] != '[') return -1;		
	if (sscanf(&buffer[2], "%d;%d", rows, columns) != 2) return -1;		//Parsing the resulting two numbers and saving to rows and columns
	return 0;	
}

int getWindowSize(int *rows, int * columns){
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)==-1 || ws.ws_col == 0){	//TIOCGWINSZ is just a request wich gives the current window size (Even tough it looks scary)
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, columns);			//Using the fallback method if ioctl Fails 
	}else{
		*columns = ws.ws_col;
		*rows = ws.ws_row;
		char str[50];
		sprintf(str, "Cols: %d, Rows: %d\n", *columns, *rows);
		appendLog(str);
		
		return 0;
	}
}

void drawRows(struct gameConfig * conf){
	for(int i = 0; i < conf->winRows;i++){

		write(STDOUT_FILENO, &conf->gameArr[i * conf->winCols], sizeof(char) * conf->winCols);
		write(STDOUT_FILENO, "\r\n", 2);
	}
}

void bufferUpdateSnakePos(){
	config.gameArr[snake.HEAD->posX + (snake.HEAD->posY * config.winCols)] = 'S';
	char str[50];
	sprintf(str, "Snake posXY: %d , %d, Index: %d\n", snake.HEAD->posX, snake.HEAD->posY, snake.HEAD->posX + (snake.HEAD->posY * config.winCols));
	appendLog(str);
//	memset(&config.gameArr[snake.HEAD->posX + (snake.HEAD->posY * config.winRows)], 'S', 1);	
}

void updateGameLogic(){
	bufferUpdateSnakePos();
}

void refreshScreen(){
	//?25l
	//appendBufferAppend(&aBuff, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[?25l", 6);
	write(STDOUT_FILENO, "\x1b[2J", 4);	//Clearing the Screen
	//write(STDOUT_FILENO, "\x1b[H", 3);	//Setting the cursor to home position

	drawRows(&config);				//Draws
							
}

/* ====(Input)==== */

//Pretty self explanitory, moves the cursor to specified direction
void moveCursor(int key){
	switch (key){
		case ARROW_LEFT:
			if(config.cursorX != 0){
				config.cursorX--;
			}
			break;
		case ARROW_UP:
			if(config.cursorY != 0){
				config.cursorY--;
			}
			break;
		case ARROW_DOWN:
			if(config.cursorY != config.winRows - 1){
				config.cursorY++;
			}
			break;
		case ARROW_RIGHT:
			if(config.cursorY != config.winCols -1){
				config.cursorX++;
			}
			break;
	}
}

//Reading input goes trough this "Filter" type of function, where we look for special keys etc
void processKeyPress(){
	int c = readKey();
	switch (c) {
		case CONTROL_KEY('q'):
			
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			disableRawMode();
			write(STDOUT_FILENO, "\x1b[?25h", 6);
			exit(0);
			break;	
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			moveCursor(c);
			break;
	}

}

/* ====(init)==== */

struct Snake initSnake(int x, int y){
	SnakePart * HEAD = malloc(sizeof(SnakePart));	
	HEAD->posX = x;
	HEAD->posY = y;
	HEAD->next = NULL;
	return (Snake){HEAD, LEFT, 0};
}

FILE * initLogs(FILE * file){
	file = fopen("logs.txt", "a");
	return file;
}

void initEditor(){
	config.cursorX = 0;		//Saving inital cursor position here
	config.cursorY = 0;
	logFile = initLogs(logFile);

	if(getWindowSize(&config.winRows, &config.winCols) == -1) kill("Get Window size");
	size_t nbytes = sizeof(char) * config.winCols * config.winRows;
	config.gameArr = (char*)malloc(nbytes);
	memset(config.gameArr, '.', nbytes);
}



int main(){
	enableRawMode();	//Enabling rawmode here	
	initEditor();
	snake = initSnake(config.winCols / 2, config.winRows / 2);	

	printf("SizeX: %d, SizeY: %d\n", config.winCols, config.winRows);

	while(1){
		updateGameLogic();
		refreshScreen();
		processKeyPress();
	}

	return 0;
}
