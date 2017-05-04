
#pragma once

#include <stdbool.h>

#include "cmd.h"

struct prt_ec_msg_buffer {
	struct prt_ec_msg *buffer;	// the message buffer
	struct prt_ec_msg *buffer_end;	// end of data buffer
	size_t capacity;		// maximum number of items in the buffer
	size_t count;			// number of items in the buffer
	struct prt_ec_msg *head;	// pointer to head
	struct prt_ec_msg *tail;	// pointer to tail
};

#define PRT_EC_BUFFER_INIT(name, array, size) 	\
	struct prt_ec_msg_buffer name = {	\
		.buffer = array,		\
		.buffer_end = &array[size],	\
		.capacity = size,		\
		.count = 0,			\
		.head = array, 			\
		.tail = array,			\
	}

/*
 * Initialize the buffer struct.
 */
void prt_ec_buffer_init(struct prt_ec_msg_buffer *b,
                       struct prt_ec_msg *array, size_t capacity);

/*
 * Check if the buffer is empty or full.
 */
bool prt_ec_buffer_full(struct prt_ec_msg_buffer *b);
bool prt_ec_buffer_empty(struct prt_ec_msg_buffer *b);

/*
 * Move the head or tail pointer up one message.
 *
 * Use with CAUTION!.
 */
void prt_ec_buffer_inc_tail(struct prt_ec_msg_buffer *b);
void prt_ec_buffer_inc_head(struct prt_ec_msg_buffer *b);

/*
 * Add a message or a package to the buffer.
 *
 * The package will be converted to a message before its stored, so the packet
 * pointer can be re-used when this call returns 0.
 */
int prt_ec_buffer_put_msg(struct prt_ec_msg_buffer *b, struct prt_ec_msg *msg);
int prt_ec_buffer_put_pkt(struct prt_ec_msg_buffer *b, struct prt_ec_pkt *pkt);

/*
 * Remove a message or packet from the buffer.
 */
int prt_ec_buffer_pop_msg(struct prt_ec_msg_buffer *b, struct prt_ec_msg *msg);
int prt_ec_buffer_pop_pkt(struct prt_ec_msg_buffer *b, struct prt_ec_pkt *pkt);
