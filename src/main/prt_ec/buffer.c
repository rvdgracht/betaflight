
#include <errno.h>
#include <string.h>

#include "buffer.h"

void prt_ec_buffer_init(struct prt_ec_msg_buffer *b,
                        struct prt_ec_msg *array, size_t capacity)
{
	b->buffer = array;
	b->buffer_end = b->buffer + b->capacity * sizeof(struct prt_ec_msg);
	b->capacity = capacity;
	b->count = 0;
	b->head = b->buffer;
	b->tail = b->buffer;
}

bool prt_ec_buffer_full(struct prt_ec_msg_buffer *b)
{
	return b->count == b->capacity;
}

bool prt_ec_buffer_empty(struct prt_ec_msg_buffer *b)
{
	return b->count == 0;
}

void prt_ec_buffer_inc_tail(struct prt_ec_msg_buffer *b)
{
	b->tail++;
	if (b->tail == b->buffer_end)
		b->tail = b->buffer;
	b->count--;
}

void prt_ec_buffer_inc_head(struct prt_ec_msg_buffer *b)
{
	b->head++;
	if (b->head == b->buffer_end)
		b->head = b->buffer;
	b->count++;
}

int prt_ec_buffer_put_pkt(struct prt_ec_msg_buffer *b, struct prt_ec_pkt *pkt)
{
	struct prt_ec_msg *msg = b->head;

	if (prt_ec_buffer_full(b))
		return -ENOBUFS;

	msg->id = pkt->id;
	memcpy(msg->data, pkt->data, PRT_EC_PKT_DATA_WORDS * 2);

	prt_ec_buffer_inc_head(b);
	return 0;
}

int prt_ec_buffer_put_msg(struct prt_ec_msg_buffer *b, struct prt_ec_msg *msg)
{
	struct prt_ec_msg *head = b->head;

	if (prt_ec_buffer_full(b))
		return -ENOBUFS;

	head->id = msg->id;
	memcpy(head->data, msg->data, PRT_EC_PKT_DATA_WORDS * 2);

	prt_ec_buffer_inc_head(b);
	return 0;
}

int prt_ec_buffer_pop_msg(struct prt_ec_msg_buffer *b, struct prt_ec_msg *msg)
{
	struct prt_ec_msg *tail = b->tail;

	if (prt_ec_buffer_empty(b))
		return -ENODATA;

	msg->id = tail->id;
	memcpy(msg->data, tail->data, PRT_EC_PKT_DATA_WORDS * 2);

	prt_ec_buffer_inc_tail(b);
	return 0;
}

int prt_ec_buffer_pop_pkt(struct prt_ec_msg_buffer *b, struct prt_ec_pkt *pkt)
{
	struct prt_ec_msg *tail = b->tail;

	if (prt_ec_buffer_empty(b))
		return -ENODATA;

	pkt->id = tail->id;
	pkt->flags = tail->ackreq ? PRT_EC_FLAG_ACKREQ : 0;
	memcpy(pkt->data, tail->data, PRT_EC_PKT_DATA_WORDS * 2);

	prt_ec_buffer_inc_tail(b);
	return 0;
}
