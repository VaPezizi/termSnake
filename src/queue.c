#include "queue.h"

void initQueue(Queue * q){
	q->front = -1;
	q->rear = 0;
}
int isQueueEmpty(Queue * q){
	return q->front == q->rear - 1;
}
int isQueueFull(Queue * q){
	return q->rear == MAX_SIZE;
}
void enqueue(Queue * q, int key){
	if(isQueueFull(q)){
		return;
	}
	q->items[q->rear] = key;
	q->rear++;
}
void dequeue(Queue * q){
	if(isQueueEmpty(q)){
		return;
	}
	q->front++;
}

int peek(Queue * q){
	if(isQueueEmpty(q)){
		return -1;
	}
	return q->items[q->front + 1];
}
