// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

void queue_init(void);
int queue_add(uint64_t const time, strarg_t const URL, strarg_t const client);
int queue_timedwait(uint64_t const time, strarg_t const URL, uint64_t const future);
void queue_work_loop(void *ignored);

