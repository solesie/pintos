#ifndef __VM_FRAME_TABLE
#define __VM_FRAME_TABLE

/* Hash table.

   This data structure is thoroughly documented in the Tour of
   Pintos for Project 3.

   This is a standard hash table with chaining.  To locate an
   element in the table, we compute a hash function over the
   element's data and use that as an index into an array of
   doubly linked lists, then linearly search the list.

   The chain lists do not use dynamic allocation.  Instead, each
   structure that can potentially be in a hash must embed a
   struct vm_ft_hash_elemmember.  All of the hash functions operate on
   these `struct hash_elem's.  The hash_entry macro allows
   conversion from a struct vm_ft_hash_elemback to a structure object
   that contains it.  This is the same technique used in the
   linked list implementation.  Refer to lib/kernel/list.h for a
   detailed explanation. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"

/* Hash element. */
struct vm_ft_hash_elem
  {
    struct list_elem list_elem;
  };

/* Converts pointer to hash element HASH_ELEM into a pointer to
   the structure that HASH_ELEM is embedded inside.  Supply the
   name of the outer structure STRUCT and the member name MEMBER
   of the hash element.  See the big comment at the top of the
   file for an example. */
#define vm_ft_hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
        ((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem              \
                     - offsetof (STRUCT, MEMBER.list_elem)))

/* Computes and returns the hash value for hash element E, given
   auxiliary data AUX. */
typedef unsigned vm_ft_hash_hash_func (const struct vm_ft_hash_elem*e, void *aux);

/* Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
typedef bool vm_ft_hash_less_func (const struct vm_ft_hash_elem*a,
                             const struct vm_ft_hash_elem*b,
                             void *aux);

/* key도 같고, value도 같은지 확인하기 위해 정의한다. */
typedef bool vm_ft_hash_value_less_func(const struct vm_ft_hash_elem*a,
                             const struct vm_ft_hash_elem*b,
                             void *aux);

/* Performs some operation on hash element E, given auxiliary
   data AUX. */
typedef void vm_ft_hash_action_func (struct vm_ft_hash_elem*e, void *aux);

/* Hash table. */
struct vm_ft_hash 
  {
    size_t elem_cnt;            /* Number of elements in table. */
    size_t bucket_cnt;          /* Number of buckets, a power of 2. */
    struct list *buckets;       /* Array of `bucket_cnt' lists. */
    vm_ft_hash_hash_func *hash;       /* Hash function. */
    vm_ft_hash_less_func *less;       /* Comparison function. */

    vm_ft_hash_value_less_func* value_less;   /* Value 비교 함수 */

    void *aux;                  /* Auxiliary data for `hash' and `less'. */
  };

/* A hash table iterator. */
struct vm_ft_hash_iterator 
  {
    struct vm_ft_hash *hash;          /* The hash table. */
    struct list *bucket;        /* Current bucket. */
    struct vm_ft_hash_elem*elem;     /* Current hash element in current bucket. */
  };

struct vm_ft_same_keys{
  struct vm_ft_hash_elem** pointers_arr_of_ft_hash_elem;
  int len;
};

/* Basic life cycle. */
bool vm_ft_hash_init (struct vm_ft_hash *, vm_ft_hash_hash_func *, vm_ft_hash_less_func *, vm_ft_hash_value_less_func *, void *aux);
void vm_ft_hash_clear (struct vm_ft_hash *, vm_ft_hash_action_func *);
void vm_ft_hash_destroy (struct vm_ft_hash *, vm_ft_hash_action_func *);

void vm_ft_same_keys_free(struct vm_ft_same_keys* arr);

/* Search, insertion, deletion. */
void vm_ft_hash_insert (struct vm_ft_hash *, struct vm_ft_hash_elem*);
struct vm_ft_same_keys* vm_ft_hash_find_same_keys (struct vm_ft_hash *, struct vm_ft_hash_elem*);
struct vm_ft_hash_elem* vm_ft_hash_find_exactly_identical(struct vm_ft_hash *, struct vm_ft_hash_elem *);
struct vm_ft_same_keys* vm_ft_hash_delete_same_keys (struct vm_ft_hash *, struct vm_ft_hash_elem*);
struct vm_ft_hash_elem* vm_ft_hash_delete_exactly_identical (struct vm_ft_hash *h, struct vm_ft_hash_elem *e);

/* Iteration. */
void vm_ft_hash_first (struct vm_ft_hash_iterator *, struct vm_ft_hash *);
struct vm_ft_hash_elem* vm_ft_hash_next (struct vm_ft_hash_iterator *);
struct vm_ft_hash_elem* vm_ft_hash_cur (struct vm_ft_hash_iterator *);

/* Information. */
size_t vm_ft_hash_size (struct vm_ft_hash *);
bool vm_ft_hash_empty (struct vm_ft_hash *);

/* Sample hash functions. */
unsigned vm_ft_hash_int (int);

#endif /* vm/frame-table.h */
