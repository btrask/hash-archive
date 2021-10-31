// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

void queue_init(void);
void queue_log(size_t const n);
int queue_add(uint64_t const time, strarg_t const URL, strarg_t const client);
int queue_timedwait(uint64_t const time, strarg_t const URL, uint64_t const future);
void queue_work_loop(void *ignored);

