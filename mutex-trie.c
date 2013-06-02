/* A simple, (reverse) trie.  Only for use with 1 thread. */
#include "trie.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// local to this file only
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

// dynamic trie node. the key is allocated as part of the
//  memory for the overall-node. see new_leaf() for details
//  on how that is done.
struct trie_node
{
    struct trie_node *next;     /* parent list */
    struct trie_node *children; /* Sorted list of children */
    size_t strlen;              /* Length of the key */
    int32_t ip4_address;        /* 4 octets */
    char *key;                  /* dynamic key */
} *root = NULL;

static struct trie_node *
new_leaf(const char *string, size_t strlen, int32_t ip4_address)
{
    struct trie_node *new_node = malloc(sizeof(*new_node) +
                                        (strlen+1)*sizeof(string[0]));
    if (!new_node)
    {
        perror("Failed to allocate memory for new_leaf().\n");
        return NULL;
    }
    
    // populate new node. note the wire-up of the key pointer.
    new_node->next = new_node->children = NULL;
    new_node->strlen = strlen;
    new_node->ip4_address = ip4_address;
    new_node->key = (char *)(new_node+1);
    strncpy(new_node->key, string, strlen);
    new_node->key[strlen] = 0;
    return new_node;
}

// invoked my main() thread to setup the client stuff
void init(int numthreads)
{
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&condition, NULL);
    root = NULL;
}

// invoked by main() thread when shutdown is in progress.
void shutdown()
{
    pthread_mutex_lock(&mutex);
    finished = 1;
    pthread_mutex_unlock(&mutex);
    
    // signal a wakeup for all the squatters
    if (allow_squatting)
        pthread_cond_broadcast(&condition);
}

// local: compares two keys, returning non-zero only if at least
//  the minimum of len1 or len2 tail-chars match. however many
//  did match is populated in pKeyLen as an out-parameter if
//  successful.
static int compare_keys(const char *string1, size_t len1,
                        const char *string2, size_t len2,
                        size_t *pKeylen)
{
    size_t keylen, offset1, offset2;
    keylen = len1 < len2 ? len1 : len2;
    offset1 = len1 - keylen;
    offset2 = len2 - keylen;
    assert (keylen > 0);
    if (pKeylen)
        *pKeylen = keylen;
    return strncmp(string1 + offset1, string2 + offset2, keylen);
}

// helper function for printing the trie
static void _print (struct trie_node *node, int indent)
{
    if (!node)
        return;
    
    int i = 0;
    for (;i<indent; ++i)
        DEBUG_PRINT("  ");
    
    DEBUG_PRINT("Node: %p,  Key: %.*s, IP: %d, Next: %p, Children: %p\n",
                node, (int)node->strlen, node->key, node->ip4_address,
                node->next, node->children);
    
    _print(node->children, indent+1);
    _print(node->next, indent);
}

// external facing version of the tree printer.
void print()
{
    pthread_mutex_lock(&mutex);
    //DEBUG_PRINT("Tree: Root = %p\n", root);
    _print(root, 0);
    pthread_mutex_unlock(&mutex);
}
//////////////////////////////////////////////////////////////////////


// helper function for recursive search facility.
static struct trie_node *
_search (struct trie_node *node, const char *string, size_t strlen)
{
    // immediate exit on no-node.
    if (node == NULL)
        return NULL;
    
    // See if this key is a substring of the string passed in
    size_t keylen;
    int cmp = compare_keys(node->key, node->strlen, string, strlen, &keylen);
    if (cmp == 0)
    {
        // partial match, if the node keylen is longer than the
        //  input key, we cannot have a match, and cannot have
        //  sibling or child match, so return NULL.
        if (node->strlen > strlen)
            node = NULL;
        
        // else if there are still chars left in the input key
        //  to consume, recurse into children.
        else if (strlen > keylen)
            node = _search(node->children, string, strlen - keylen);
        
        // else i we have a winner, but only if there is an ip address
        //  if there isn't (0), then this must be considered an just an
        //  intermediate note and should not be returned as the "find"
        else if (node->ip4_address == 0)
            node = NULL;
    }
    
    // if the node's key is "less" than look to the siblings
    //  of the current node.
    else if (cmp < 0)
        node =  _search(node->next, string, strlen);
    
    // else it is greater, and there can be no possible match
    else
        node = NULL;
    
    // final return value.
    return node;
}


// external facing search algorithm.
int search(const char *string, size_t strlen, int32_t *ip4_address)
{
    int bFound = 0;
    if (strlen==0)
{
        return 0;

 }
    pthread_mutex_lock(&mutex);
    DEBUG_PRINT("search: %.*s\n", (int)strlen, string);
    struct trie_node *found = _search(root, string, strlen);
    if (found && ip4_address)
    {
        *ip4_address = found->ip4_address;
        bFound = 1;
    }
    pthread_mutex_unlock(&mutex);
    return bFound;
}
//////////////////////////////////////////////////////////////////////



/* Recursive helper function */
static int _insert (const char *string, size_t strlen, int32_t ip4_address,
                    struct trie_node *node, struct trie_node *parent, struct trie_node *left)
{
    size_t keylen = 0;
    int cmp;
    
    // First things first, check if we are NULL
    assert (node != NULL);
    assert (node->strlen < 64);
    
    // Take the minimum of the two lengths
    cmp = compare_keys (node->key, node->strlen, string, strlen, &keylen);
    if (cmp == 0)
    {
        // goes above the currentnode if its string is longer than ours.
        if (node->strlen > keylen)
        {
            struct trie_node *new_node = new_leaf(string, strlen, ip4_address);
            node->strlen -= keylen;
            new_node->children = node;
            
            if (parent)
            {
				new_node->next = parent->children;
                parent->children = new_node;
            }
            else if (left)
            {
				new_node->next = left->next;
                left->next = new_node;
            }
            else if ((!parent) || (!left))
            {
                root = new_node;
            }
            return 1;
            
        }
        else if (strlen > keylen)
        {
            if (node->children == NULL)
            {
                // Insert leaf here
                struct trie_node *new_node = new_leaf (string, strlen - keylen, ip4_address);
                node->children = new_node;
                return 1;
            }
            else
            {   // Recur on children list, store "parent" (loosely defined)
                return _insert(string, strlen - keylen, ip4_address,
                               node->children, node, NULL);
            }
        }
		else {
            assert (strlen == keylen);
            if (node->ip4_address == 0)
			{
                node->ip4_address = ip4_address;
                return 1;
            }
			return 0;
        }
    }
    else
    {   /* Is there any common substring? */
        int i, cmp2, overlap=0;
        size_t keylen2 = 0;
        for (i = 1; i < keylen; i++) {
            cmp2 = compare_keys (&node->key[i], node->strlen - i,
                                 &string[i], strlen - i, &keylen2);
            assert (keylen2 > 0);
            if (cmp2 == 0) {
                overlap = 1;
                break;
            }
        }
        
        if (overlap) {
            // Insert a common parent, recur
            struct trie_node *new_node = new_leaf (&string[i], strlen - i, 0);
            size_t diff = node->strlen - i;
            assert ((node->strlen - diff) > 0);
            node->strlen -= diff;
            new_node->children = node;
            assert ((!parent) || (!left));
            
            if (node == root) {
                new_node->next = node->next;
                node->next = NULL;
                root = new_node;
            } else if (parent) {
                assert(parent->children == node);
                new_node->next = NULL;
                parent->children = new_node;
            } else if (left) {
                new_node->next = node->next;
                node->next = NULL;
                left->next = new_node;
            } else if ((!parent) && (!left)) {
                root = new_node;
            }
            
            return _insert(string, i, ip4_address,
                           node, new_node, NULL);
        }
        else if (cmp < 0)
        {
            if (node->next == NULL)
            {
                // Insert here
                struct trie_node *new_node = new_leaf (string, strlen, ip4_address);
                node->next = new_node;
                return 1;
            }
            else
            {   // No, recur right (the node's key is "greater" than  the search key)
                return _insert(string, strlen, ip4_address, node->next, NULL, node);
            }
        }
        else
        {   // Insert here
            struct trie_node *new_node = new_leaf (string, strlen, ip4_address);
            new_node->next = node;
            if (node == root)
                root = new_node;
        }
        return 1;
    }
}

int insert(const char *string, size_t strlen, int32_t ip4_address)
{
    int ret =0;
   if (strlen == 0)
 	{       
	  return ret;
        }
  
    pthread_mutex_lock(&mutex);
    if (allow_squatting)
    {
        // so long as _search() continues to return the node, we need
        //  to wait until someone else removes it (and if no one else
        //  is around to do that, we're probably hung).
        while(!finished && _search(root, string, strlen))
        {
            DEBUG_PRINT("waiting: %.*s\n", (int)strlen, string);
            pthread_cond_wait(&condition, &mutex);
        }
        
        // leave *now* if shutting down
        if (finished)
        {
            pthread_mutex_unlock(&mutex);
            return 0l;
        }
    }
    
    DEBUG_PRINT("insert: %.*s\n", (int)strlen, string);

    /* Edge case: root is null */
    if (root == NULL)
    {
        root = new_leaf(string, strlen, ip4_address);
    
	    if(root!=NULL)
		{
            ret =1;
		}
    }
    else
    {   // recurse into tree starting at root.
        ret = _insert (string, strlen, ip4_address, root, NULL, NULL);
    }
    pthread_mutex_unlock(&mutex);
    return ret;
}
//////////////////////////////////////////////////////////////////////


/* Recursive helper function.
 * Returns a pointer to the node if found.
 * Stores an optional pointer to the
 * parent, or what should be the parent if not found.
 *
 */
static struct trie_node* _delete(struct trie_node *node,
                                 const char *string, size_t strlen)
{
    if (node == NULL)
        return NULL;
    
    // See if this key is a tail-substring of the string passed in
    size_t keylen = 0;
    int cmp = compare_keys(node->key, node->strlen, string, strlen, &keylen);
    if (cmp == 0)
    {
        // if the result-node key is longer than ours, we have to return NULL
        if (node->strlen > keylen)
            node = NULL;
        
        // if there is still keydata left to consume, recurse to children.
        else if (strlen > keylen)
        {
            // look in children list for possible match
            struct trie_node *found =  _delete(node->children, string, strlen - keylen);
            if (found)
            {
                // match returned. 
                if (found->children == NULL && found->ip4_address == 0)
                {
                    node->children = found->next;
                    free(found);
                }
            }
            else
            {   // no match on children means no match at all. therefore
                //  our return result must be NULL.
                node = NULL;
            }
        }
        else
        {   // Success! clear the ip address. the caller is responsible for
            // establishing whether to keep this based on child and sibling
            // reference pointers.
            node->ip4_address = 0;
        }
    }
    else if (cmp < 0)
    {
        // node is lesser. look to the node's sibling list.
        struct trie_node *found = _delete(node->next, string, strlen);
        if (found)
        {
            // found a match. if this match is an interior with no children
            //  we must remove it from our sibling list and free it.
            if (found->children == NULL && found->ip4_address == 0)
            {
                node->next = found->next;
                free(found);
            }
        }
    }
    else
    {   // greater than the given key, so no match is possible.
        node = NULL;
    }

    // return whatever node finally ended up being.
    return node;
}

int delete(const char *string, size_t strlen)
{
    // Skip strings of length 0
    int ret=0;
    if (strlen==0)
        return ret;
    
    pthread_mutex_lock(&mutex);

    DEBUG_PRINT("delete: %.*s\n", (int)strlen, string);
    
    // this will return the node
    struct trie_node* found = _delete(root, string, strlen);
    if (found)
    {
        // it is possible the root was the node returned. If it is,
        //  it needs to be advanced to its sibling pointer (which may
        //  be null) and free'd. but we do not free it if the node
        //  has children.
        if (found == root && found->children == NULL)
        {
            // ensure the IP address is 0 before removing this.
            root = root->next;
            free(found);
        }
        ret = 1;
        DEBUG_PRINT("Root: %p\n", root);
        _print(root,4);
    }
    
    // release the mutex
    pthread_mutex_unlock(&mutex);
    
    // then tell anyone that is listening we just deleted
    //  an item from the tree (if we did, in fact do so)
    if (ret && allow_squatting)
        pthread_cond_broadcast(&condition);
    
    return ret;
}
