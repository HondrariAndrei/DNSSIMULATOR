#include "trie.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

int allow_squatting = 0;
int simulation_length = 30;
volatile int finished = 0;

//Ahmad Zaraei

// general stress client
static void *client(void *arg)
{
    unsigned int ctx_rand =(unsigned int)(uintptr_t)arg;
    int i,j,length;
    int32_t code, ip4_addr;
    char buf[64];

    while (!finished)
    {
        /* Pick a random operation, string, and ip */
        code = rand_r(&ctx_rand);;
        length = ((code >> 2) & 0x3E) + 1;
        
        /* Generate a random string in lowercase */
        for (j = 0; j < length; j+= 6)
        {
            int32_t chars = rand_r(&ctx_rand);
            for (i = 0; i < 6 && (i+j) < length; i++)
            {
                char val = ( (chars >> (5 * i)) & 31);
                if (val > 25)
                    val = 25;
                buf[j+i] = 'a' + val;
            }
            buf[j+i] = 0;
        }
        
        switch (code % 3)
        {
            case 0: // Search
                search (buf, length, NULL);
                break;
        
            case 1: // insert
                ip4_addr = rand_r(&ctx_rand)+1;
                insert (buf, length, ip4_addr);
                break;
            
            case 2: // delete
                delete (buf, length);
                break;
        }
    }

  return NULL;
}

static void *squatter_stress(void *arg)
{
    unsigned ctx_rand = (unsigned int)(uintptr_t)arg;
    int32_t ip = rand_r(&ctx_rand);
    while (!finished)
    {
        insert ("abc", 3, ip);
        insert ("abe", 3, ip+1);
        insert ("bce", 3, ip+2);
        insert ("bcc", 3, ip+3);
        delete ("abc", 3);
        delete ("abe", 3);
        delete ("bce", 3);
        delete ("bcc", 3);
    }
    return NULL;
}

#define die(msg) do {				\
  print();					\
  fprintf(stderr, msg);					\
  exit(1);					\
  } while (0)

int self_tests()
{
    int rv;
    int32_t ip = 0;

    rv = insert ("abc", 3, 4);
    if (!rv) die ("Failed to insert key abc\n");
    rv = delete("abc", 3);
    if (!rv) die ("Failed to delete key abc\n");
    print();
    
    rv = insert ("google", 6, 5);
    if (!rv) die ("Failed to insert key google\n");
    
    rv = insert ("goggle", 6, 4);
    if (!rv) die ("Failed to insert key goggle\n");
    
    // rv = delete("goggle", 6);
    // if (!rv) die ("Failed to delete key goggle\n");
    
    rv = delete("google", 6);
    if (!rv) die ("Failed to delete key google\n");
    
    rv = insert ("ab", 2, 2);
    // rv = insert ("ab", 2, 2);//ADDED BY ME
    if (!rv) die ("Failed to insert key ab\n");
    
    rv = insert("bb", 2, 2);
    if (!rv) die ("Failed to insert key bb\n");
    
    print();
    printf("So far so good\n\n");
    
    rv = search("ab", 2, &ip);
    printf("Rv is %d\n", rv);
    if (!rv) die ("Failed to find key ab\n");
    if (ip != 2) die ("Found bad IP for key ab\n");
    
    rv = search("aa", 2, NULL);
    if (rv) die ("Found bogus key aa\n");
    
    ip = 0;
    
    rv = search("bb", 2, &ip);
    if (!rv) die ("Failed to find key bb\n");
    if (ip != 2) die ("Found bad IP for key bb\n");
    
    ip = 0;
    
    rv = delete("cb", 2);
    if (rv) die ("deleted bogus key cb\n");
    
    rv = delete("bb", 2);
    if (!rv) die ("Failed to delete real key bb\n");
    
    rv = search("ab", 2, &ip);
    if (!rv) die ("Failed to find key ab\n");
    if (ip != 2) die ("Found bad IP for key ab\n");
    
    ip = 0;
    
    rv = delete("ab", 2);
    if (!rv) die ("Failed to delete real key ab\n");
    
    printf("End of self-tests, tree is:\n");
    print();
    printf("End of self-tests\n");
    return 0;
}

void help() {
  printf ("DNS Simulator.  Usage: ./dns-[variant] [options]\n\n");
  printf ("Options:\n");
  printf ("\t-c numclients - Use numclients threads.\n");
  printf ("\t-h - Print this help.\n");
  printf ("\t-l length - Run clients for length seconds.\n");
  printf ("\t-q  - Allow a client to block (squat) if a requested name is taken.\n");
  printf ("\t-t  - Stress test name squatting.\n");
  printf ("\n\n");
}

int main(int argc, char ** argv)
{
    srand((unsigned int)time(NULL));
    
    int numthreads = 1; // default to 1
    int c, i;
    pthread_t *tinfo = NULL;
    int stress_squatting = 0;
    
    // Read options from command line:
    //   # clients from command line, as well as seed file
    //   Simulation length
    //   Block if a name is already taken ("Squat")
    //   Stress test "squatting"
    while ((c = getopt (argc, argv, "c:hl:qt")) != -1)
    {
        switch (c) {
            case 'c':
                numthreads = atoi(optarg);
                break;
            case 'h':
                help();
                return EXIT_SUCCESS;
            case 'l':
                simulation_length = atoi(optarg);
                break;
            case 'q':
                allow_squatting = 1;
                break;
            case 't':
                stress_squatting = 1;
                break;
            default:
                printf ("Unknown option\n");
                help();
                return EXIT_FAILURE;
        }
    }
    
    // Create initial data structure, populate with initial entries
    // Note: Each variant of the tree has a different init function,
    // statically compiled in
    init(numthreads);
    
    // Launch client threads
    tinfo = calloc(numthreads, sizeof(pthread_t));
    void* (*pfn)(void*) = stress_squatting ? &squatter_stress : &client;
    for (i = 0; i < numthreads; ++i)
        pthread_create(tinfo+i, NULL, pfn, (void*)(intptr_t)(i+1));
    
    // After the simulation is done, shut it down
    sleep (simulation_length);
    finished = 1;
    
    // Wait for all clients to exit.  notify implementation it needs
    //  to have all threads exit their blocking loops.
    shutdown();
    
    // join all running threads.
    fprintf(stderr, "Waiting for threads to finish...\n");
    for (i = 0; i < numthreads; i++)
        pthread_join(tinfo[i], NULL);
    
    /* Print the final tree for fun */
   #ifdef DEBUG  
/* Print the final tree for fun */
   print();
    #endif 
    return 0;
}
