#include <stdio.h>
#define MAX_SIZE 10

typedef struct{
	int items[MAX_SIZE];
	int front;
	int rear;
}Queue;

void initQueue(Queue * q);
int isQueueEmpty(Queue* q);
int isQueueFull(Queue * q);
void enqueue(Queue * q, int key);
void dequeue(Queue * q);

