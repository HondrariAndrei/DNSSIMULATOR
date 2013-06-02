#ifndef __TRIE_H__
#define __TRIE_H__

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

/* A simple (reverse) trie interface */

/* Optional init routine.  May not be required. */
void init (int numthreads);

/* Return 1 on success, 0 on failure */
int insert (const char *string, size_t strlen, int32_t ip4_address);

/* Return 1 if the key is found, 0 if not. 
 * If ip4_address is not NULL, store the IP 
 * here.  
 */
int search(const char *string, size_t strlen, int32_t *ip4_address);

/* Return 1 if the key is found and deleted, 0 if not. */
int delete  (const char *string, size_t strlen);

/* Called when the main thread is shutting down */
void shutdown();

/* Print the structure of the tree.  Mostly useful for debugging. */
void print (); 

/* Determines whether to allow blocking until 
 * a name is available.
 */
extern int allow_squatting;
extern volatile int finished;

#define DEBUG =1
#ifdef DEBUG
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif



#endif /* __TRIE_H__ */ 
