/* 4.1.5) Unless you have implemented sharing, only a single page 
   should refer to a frame at any given time.
   
   when multiple processes are created that use the
   same executable file, share read-only pages among those processes instead of
   creating separate copies of read-only segments for each process.
   
   mmap 사용시에만 sharing을 구현한다. 한 frame이 여러 user_page를 저장할 수 있도록 한다.
   즉, hash에 중복을 허용하여 bucket에 저장하는 함수들을 구현한다.
   frame.c:struct hash frame_table만 이 함수들을 사용하도록 한다. */

//#include "lib/kernel/hash.h"
#include "lib/debug.h"
#include "threads/malloc.h"
#include "vm/frame-table-hash.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"

#define list_elem_to_hash_elem(LIST_ELEM)                       \
        list_entry(LIST_ELEM, struct vm_ft_hash_elem, list_elem)

static struct list *find_bucket (struct vm_ft_hash *, struct vm_ft_hash_elem *);
static struct vm_ft_same_keys* find_elem (struct vm_ft_hash *, struct list *,
                                    struct vm_ft_hash_elem *);
static void insert_elem (struct vm_ft_hash *, struct list *, struct vm_ft_hash_elem *);
static struct list_elem * remove_elem (struct vm_ft_hash *, struct vm_ft_hash_elem *);
static void rehash (struct vm_ft_hash *);
static struct vm_ft_hash_elem* find_elem_exactly_identical(struct vm_ft_hash *h, 
                                  struct list *bucket, struct vm_ft_hash_elem *e);

/* Initializes hash table H to compute hash values using HASH and
   compare hash elements using LESS, given auxiliary data AUX. */
bool
vm_ft_hash_init (struct vm_ft_hash *h,
           vm_ft_hash_hash_func *hash, vm_ft_hash_less_func *less, vm_ft_hash_value_less_func* value_less , void *aux) 
{
  h->elem_cnt = 0;
  h->bucket_cnt = 4;
  h->buckets = malloc (sizeof *h->buckets * h->bucket_cnt);
  h->hash = hash;
  h->less = less;

  h->value_less = value_less;

  h->aux = aux;

  if (h->buckets != NULL) 
    {
      vm_ft_hash_clear (h, NULL);
      return true;
    }
  else
    return false;
}

/* Removes all the elements from H.
   
   If DESTRUCTOR is non-null, then it is called for each element
   in the hash.  DESTRUCTOR may, if appropriate, deallocate the
   memory used by the hash element.  However, modifying hash
   table H while hash_clear() is running, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), yields undefined behavior,
   whether done in DESTRUCTOR or elsewhere. */
void
vm_ft_hash_clear (struct vm_ft_hash *h, vm_ft_hash_action_func *destructor) 
{
  size_t i;

  for (i = 0; i < h->bucket_cnt; i++) 
    {
      struct list *bucket = &h->buckets[i];

      if (destructor != NULL) 
        while (!list_empty (bucket)) 
          {
            struct list_elem *list_elem = list_pop_front (bucket);
            struct vm_ft_hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
            destructor (hash_elem, h->aux);
          }

      list_init (bucket); 
    }    

  h->elem_cnt = 0;
}

/* Destroys hash table H.

   If DESTRUCTOR is non-null, then it is first called for each
   element in the hash.  DESTRUCTOR may, if appropriate,
   deallocate the memory used by the hash element.  However,
   modifying hash table H while hash_clear() is running, using
   any of the functions hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), or hash_delete(), yields
   undefined behavior, whether done in DESTRUCTOR or
   elsewhere. */
void
vm_ft_hash_destroy (struct vm_ft_hash *h, vm_ft_hash_action_func *destructor) 
{
  if (destructor != NULL)
    vm_ft_hash_clear (h, destructor);
  free (h->buckets);
}

/* Inserts NEW into hash table H and returns a null pointer. */   
void
vm_ft_hash_insert (struct vm_ft_hash *h, struct vm_ft_hash_elem *new)
{
  struct list *bucket = find_bucket (h, new);

  insert_elem (h, bucket, new);

  rehash (h);
}


/* Finds and returns "list of elements" equal to E in hash table H, or a
   null pointer if no equal element exists in the table. */
struct vm_ft_same_keys*
vm_ft_hash_find_same_keys (struct vm_ft_hash *h, struct vm_ft_hash_elem *e) 
{
  return find_elem (h, find_bucket (h, e), e);
}

/* E와 완전히 동일한 element를 반환한다. */
struct vm_ft_hash_elem* vm_ft_hash_find_exactly_identical(struct vm_ft_hash *h, struct vm_ft_hash_elem *e){
  return find_elem_exactly_identical(h, find_bucket (h, e), e);
}


/* PintOs list library 구조상 이미 buckets에 리스트에서 또 다른 list를 추출하는 것이 매우 까다로웠다.
   buckets의 vm_ft_hash_elem을 가르키는 배열을 나타내는 구조체를 반환하도록 하였고,
   따라서 그 배열을 삭제해주어야 한다. */
void vm_ft_same_keys_free(struct vm_ft_same_keys* arr){
  free(arr->pointers_arr_of_ft_hash_elem);
  free(arr);
}


/* Finds, removes, and returns "list of elements" equal to E in hash
   table H.  Returns a null pointer if no equal element existed
   in the table.
   
   동일한 e를 전부다 제거한다. */
struct vm_ft_same_keys*
vm_ft_hash_delete_same_keys (struct vm_ft_hash *h, struct vm_ft_hash_elem *e)
{
  struct vm_ft_same_keys* founds = find_elem(h, find_bucket(h,e), e);
  if(founds == NULL)
    return NULL;
  
  for (int i = 0; i < founds->len; ++i)
    remove_elem (h, founds->pointers_arr_of_ft_hash_elem[i]);

  rehash (h); 

  return founds;
}


struct vm_ft_hash_elem *
vm_ft_hash_delete_exactly_identical (struct vm_ft_hash *h, struct vm_ft_hash_elem *e)
{
  struct vm_ft_hash_elem *found = find_elem_exactly_identical (h, find_bucket (h, e), e);
  if (found != NULL) 
    {
      remove_elem (h, found);
      rehash (h); 
    }
  return found;
}


/* Initializes I for iterating hash table H.

   Iteration idiom:

      struct hash_iterator i;

      hash_first (&i, h);
      while (hash_next (&i))
        {
          struct foo *f = vm_ft_hash_entry (hash_cur (&i), struct foo, elem);
          ...do something with f...
        }

   Modifying hash table H during iteration, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), invalidates all
   iterators. */
void
vm_ft_hash_first (struct vm_ft_hash_iterator *i, struct vm_ft_hash *h) 
{
  ASSERT (i != NULL);
  ASSERT (h != NULL);

  i->hash = h;
  i->bucket = i->hash->buckets;
  i->elem = list_elem_to_hash_elem (list_head (i->bucket));
}

/* Advances I to the next element in the hash table and returns
   it.  Returns a null pointer if no elements are left.  Elements
   are returned in arbitrary order.

   Modifying a hash table H during iteration, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), invalidates all
   iterators. */
struct vm_ft_hash_elem *
vm_ft_hash_next (struct vm_ft_hash_iterator *i)
{
  ASSERT (i != NULL);

  i->elem = list_elem_to_hash_elem (list_next (&i->elem->list_elem));
  while (i->elem == list_elem_to_hash_elem (list_end (i->bucket)))
    {
      if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt)
        {
          i->elem = NULL;
          break;
        }
      i->elem = list_elem_to_hash_elem (list_begin (i->bucket));
    }
  
  return i->elem;
}

/* Returns the current element in the hash table iteration, or a
   null pointer at the end of the table.  Undefined behavior
   after calling hash_first() but before hash_next(). */
struct vm_ft_hash_elem *
vm_ft_hash_cur (struct vm_ft_hash_iterator *i) 
{
  return i->elem;
}

/* Returns the number of elements in H. */
size_t
vm_ft_hash_size (struct vm_ft_hash *h) 
{
  return h->elem_cnt;
}

/* Returns true if H contains no elements, false otherwise. */
bool
vm_ft_hash_empty (struct vm_ft_hash *h) 
{
  return h->elem_cnt == 0;
}


/* Returns a hash of integer I. */
unsigned
vm_ft_hash_int (int i) 
{
  return hash_bytes (&i, sizeof i);
}


/* Returns the bucket in H that E belongs in. */
static struct list *
find_bucket (struct vm_ft_hash *h, struct vm_ft_hash_elem *e) 
{
  size_t bucket_idx = h->hash (e, h->aux) & (h->bucket_cnt - 1);
  return &h->buckets[bucket_idx];
}


/* Searches BUCKET in H for a hash element equal to E.  Returns
   it if found or a null pointer otherwise.
   user_page도 일치해야한다. */
static struct vm_ft_hash_elem* 
find_elem_exactly_identical(struct vm_ft_hash *h, struct list *bucket, struct vm_ft_hash_elem *e)
{
  struct list_elem *i;

  for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) 
    {
      struct vm_ft_hash_elem *hi = list_elem_to_hash_elem (i);
      if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux)
      && !h->value_less (hi, e, h->aux) && !h->value_less (e, hi, h->aux)){
        return hi;
      }
    }
  return NULL;
}


/* Searches BUCKET in H for a hash element equal to E.  Returns
   it if found or a null pointer otherwise. */
static struct vm_ft_same_keys*
find_elem (struct vm_ft_hash *h, struct list *bucket, struct vm_ft_hash_elem *e) 
{
  struct list_elem *i;

  int same = 0;

  for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) 
    {
      struct vm_ft_hash_elem *hi = list_elem_to_hash_elem (i);
      if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux))
        ++same;
    }

  if(same == 0) return NULL;

  struct vm_ft_same_keys* ret = malloc(sizeof(struct vm_ft_same_keys));
  ret->len = same;
  ret->pointers_arr_of_ft_hash_elem = malloc(sizeof(struct vm_ft_hash_elem*) * same);

  int count = 0;
  for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) 
    {
      struct vm_ft_hash_elem *hi = list_elem_to_hash_elem (i);
      if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux)){
        ret->pointers_arr_of_ft_hash_elem[count] = hi;
        ++count;
      }
    }
    
  return ret;
}

/* Returns X with its lowest-order bit set to 1 turned off. */
static inline size_t
turn_off_least_1bit (size_t x) 
{
  return x & (x - 1);
}

/* Returns true if X is a power of 2, otherwise false. */
static inline size_t
is_power_of_2 (size_t x) 
{
  return x != 0 && turn_off_least_1bit (x) == 0;
}

/* Element per bucket ratios. */
#define MIN_ELEMS_PER_BUCKET  1 /* Elems/bucket < 1: reduce # of buckets. */
#define BEST_ELEMS_PER_BUCKET 2 /* Ideal elems/bucket. */
#define MAX_ELEMS_PER_BUCKET  4 /* Elems/bucket > 4: increase # of buckets. */

/* Changes the number of buckets in hash table H to match the
   ideal.  This function can fail because of an out-of-memory
   condition, but that'll just make hash accesses less efficient;
   we can still continue. */
static void
rehash (struct vm_ft_hash *h) 
{
  size_t old_bucket_cnt, new_bucket_cnt;
  struct list *new_buckets, *old_buckets;
  size_t i;

  ASSERT (h != NULL);

  /* Save old bucket info for later use. */
  old_buckets = h->buckets;
  old_bucket_cnt = h->bucket_cnt;

  /* Calculate the number of buckets to use now.
     We want one bucket for about every BEST_ELEMS_PER_BUCKET.
     We must have at least four buckets, and the number of
     buckets must be a power of 2. */
  new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
  if (new_bucket_cnt < 4)
    new_bucket_cnt = 4;
  while (!is_power_of_2 (new_bucket_cnt))
    new_bucket_cnt = turn_off_least_1bit (new_bucket_cnt);

  /* Don't do anything if the bucket count wouldn't change. */
  if (new_bucket_cnt == old_bucket_cnt)
    return;

  /* Allocate new buckets and initialize them as empty. */
  new_buckets = malloc (sizeof *new_buckets * new_bucket_cnt);
  if (new_buckets == NULL) 
    {
      /* Allocation failed.  This means that use of the hash table will
         be less efficient.  However, it is still usable, so
         there's no reason for it to be an error. */
      return;
    }
  for (i = 0; i < new_bucket_cnt; i++) 
    list_init (&new_buckets[i]);

  /* Install new bucket info. */
  h->buckets = new_buckets;
  h->bucket_cnt = new_bucket_cnt;

  /* Move each old element into the appropriate new bucket. */
  for (i = 0; i < old_bucket_cnt; i++) 
    {
      struct list *old_bucket;
      struct list_elem *elem, *next;

      old_bucket = &old_buckets[i];
      for (elem = list_begin (old_bucket);
           elem != list_end (old_bucket); elem = next) 
        {
          struct list *new_bucket
            = find_bucket (h, list_elem_to_hash_elem (elem));
          next = list_next (elem);
          list_remove (elem);
          list_push_front (new_bucket, elem);
        }
    }

  free (old_buckets);
}

/* Inserts E into BUCKET (in hash table H). */
static void
insert_elem (struct vm_ft_hash *h, struct list *bucket, struct vm_ft_hash_elem *e) 
{
  h->elem_cnt++;
  list_push_front (bucket, &e->list_elem);
}

/* Removes E from hash table H. */
static struct list_elem *
remove_elem (struct vm_ft_hash *h, struct vm_ft_hash_elem *e) 
{
  h->elem_cnt--;
  list_remove (&e->list_elem);
}

