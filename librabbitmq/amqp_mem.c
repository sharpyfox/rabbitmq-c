#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <assert.h>

#include "amqp.h"
#include "../config.h"

char const *amqp_version(void) {
  return VERSION; /* defined in config.h */
}

void init_amqp_pool(amqp_pool_t *pool, size_t pagesize) {
  pool->pagesize = pagesize ? pagesize : 4096;

  pool->pages.num_blocks = 0;
  pool->pages.blocklist = NULL;

  pool->large_blocks.num_blocks = 0;
  pool->large_blocks.blocklist = NULL;

  pool->next_page = 0;
  pool->alloc_block = NULL;
  pool->alloc_used = 0;
}

static void empty_blocklist(amqp_pool_blocklist_t *x) {
  int i;

  for (i = 0; i < x->num_blocks; i++) {
    free(x->blocklist[i]);
  }
  if (x->blocklist != NULL) {
    free(x->blocklist);
  }
  x->num_blocks = 0;
  x->blocklist = NULL;
}

void recycle_amqp_pool(amqp_pool_t *pool) {
  empty_blocklist(&pool->large_blocks);
  pool->next_page = 0;
  pool->alloc_block = NULL;
  pool->alloc_used = 0;
}

void empty_amqp_pool(amqp_pool_t *pool) {
  recycle_amqp_pool(pool);
  empty_blocklist(&pool->pages);
}

static void record_pool_block(amqp_pool_blocklist_t *x, void *block) {
  size_t blocklistlength = sizeof(void *) * (x->num_blocks + 1);

  if (x->blocklist == NULL) {
    x->blocklist = malloc(blocklistlength);
  } else {
    x->blocklist = realloc(x->blocklist, blocklistlength);
  }

  x->blocklist[x->num_blocks] = block;
  x->num_blocks++;
}

void *amqp_pool_alloc(amqp_pool_t *pool, size_t amount) {
  if (amount == 0) {
    return NULL;
  }

  amount = (amount + 7) & (~7); /* round up to nearest 8-byte boundary */

  if (amount > pool->pagesize) {
    void *result = calloc(1, amount);
    record_pool_block(&pool->large_blocks, result);
    return result;
  }

  if (pool->alloc_block != NULL) {
    assert(pool->alloc_used <= pool->pagesize);

    if (pool->alloc_used + amount <= pool->pagesize) {
      void *result = pool->alloc_block + pool->alloc_used;
      pool->alloc_used += amount;
      return result;
    }
  }

  if (pool->next_page >= pool->pages.num_blocks) {
    pool->alloc_block = calloc(1, pool->pagesize);
    record_pool_block(&pool->pages, pool->alloc_block);
    pool->next_page = pool->pages.num_blocks;
  } else {
    pool->alloc_block = pool->pages.blocklist[pool->next_page];
    pool->next_page++;
  }

  pool->alloc_used = amount;

  return pool->alloc_block;
}

void amqp_pool_alloc_bytes(amqp_pool_t *pool, size_t amount, amqp_bytes_t *output) {
  output->len = amount;
  output->bytes = amqp_pool_alloc(pool, amount);
}

amqp_bytes_t amqp_cstring_bytes(char const *cstr) {
  amqp_bytes_t result;
  result.len = strlen(cstr);
  result.bytes = (void *) cstr;
  return result;
}
