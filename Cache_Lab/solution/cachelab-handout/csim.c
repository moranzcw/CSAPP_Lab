#include "cachelab.h"
#include <stdio.h>    // fopen freopen perror
#include <stdint.h>   // uintN_t
#include <stdlib.h>   // atol exit
#include <sys/time.h> // gettimeofday
#include <unistd.h>   // getopt
#include <getopt.h>   // getopt -std=c99 POSIX macros defined in <features.h> prevents <unistd.h> from including <getopt.h>
#include <errno.h>    // errno 

#define false 0
#define true 1
typedef uint8_t Bool;

typedef struct
{
    Bool valid;    // flag whether this line is empty, true at first
    uint64_t tag;   // identifier to choose line
    uint64_t timeStamp;  // for LRU strategy
}Line;

typedef struct
{
    Line *lines;
    uint64_t length;
}Set;

typedef struct
{
    Set *sets;
    uint64_t length;
}Cache;

/*
Data structure:

+--------------+                             
| (cache0)     |        +--------------+
|  sets        +--------> (set0)       |        +-----------+
+--------------+        |  lines       +--------> (line0)   |
                        +--------------+        |  valid    |
                        | (set1)       |        |  tag      |
                        |  lines       |        |  counter  |
                        +--------------+        +-----------+
                        | (set2)       |        | (line1)   |
                        |  lines       |        |  valid    |
                        +--------------+        |  tag      |
                        | (setX)       |        |  counter  |
                        |  lines       |        +-----------+
                        +--------------+        | (lineX)   |
                                                |  valid    |
                                                |  tag      |
                                                |  counter  |
                                                +-----------+
*/

typedef struct
{
    int hit;
    int miss;
    int eviction;
}Result;

typedef struct
{
    uint64_t s; // number of sets index's bits
    uint64_t b; // number of blocks index's bits
    uint64_t S; // number of sets
    uint64_t E; // number of lines
    FILE *tracefile; // file pointer
}Options;

uint64_t GetSystemTime();

Options GetOptions(int argc, char * const argv[]);

Cache CreateCache(Options opt);

void DestroyCache(Cache cache);

Result RunCache(Cache cache, Options opt);

Result UpdateSet(Set set, Result result, uint64_t tag);


int main(int argc, char * const argv[])
{
    Options opt = GetOptions(argc, argv);

    Cache cache = CreateCache(opt);
    Result result = RunCache(cache, opt);
    DestroyCache(cache);
    printSummary(result.hit, result.miss, result.eviction);
    return 0;
}

uint64_t GetSystemTime()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return 1000.0 * tv.tv_sec + tv.tv_usec * 1000000;
}

Options GetOptions(int argc, char * const argv[])
{
    const char *help_message = "Usage: \"Your complied program\" [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n" \
                               "<s> <E> <b> should all above zero and below 64.\n" \
                               "Complied with std=c99\n";
    const char *command_options = "hvs:E:b:t:";

    Options opt;

    char ch;

    while((ch = getopt(argc, argv, command_options)) != -1)
    {
        switch(ch)
        {
            case 'h':
            {
                printf("%s", help_message);
                exit(EXIT_SUCCESS);
            }

            case 's':
            {

                if (atol(optarg) <= 0)  // at least two sets
                {
                    printf("%s", help_message);
                    exit(EXIT_FAILURE);
                }
                opt.s = atol(optarg);
                opt.S = 1 << opt.s;
                break;
            }

            case 'E':
            {
                if (atol(optarg) <= 0)
                {
                    printf("%s", help_message);
                    exit(EXIT_FAILURE);
                }
                opt.E = atol(optarg);
                break;
            }

            case 'b':
            {
                if (atol(optarg) <= 0)  // at least two sets
                {
                    printf("%s", help_message);
                    exit(EXIT_FAILURE);
                }
                opt.b = atol(optarg);
                break;
            }

            case 't':
            {
                if ((opt.tracefile = fopen(optarg, "r")) == NULL)
                {
                    perror("Failed to open tracefile");
                    exit(EXIT_FAILURE);
                }
                break;
            }

            default:
            {
                printf("%s", help_message);
                exit(EXIT_FAILURE);
            }
        }
    }

    if (opt.s == 0 || opt.b ==0 || opt.E == 0 || opt.tracefile == NULL)
    {
        printf("%s", help_message);
        exit(EXIT_FAILURE);
    }

    return opt;
}

Cache CreateCache(Options opt)
{
    Cache cache;

    if ((cache.sets = calloc(opt.S, sizeof(Set))) == NULL) // initialize the sets
    {
        perror("Failed to create sets");
        exit(EXIT_FAILURE);
    }

    cache.length = opt.S;

    for(int i = 0; i < opt.S; ++i)  // initialize the lines in set
    {
        if ((cache.sets[i].lines = calloc(opt.E, sizeof(Line))) == NULL)
        {
            perror("Failed to create lines in sets");
        }
        cache.sets[i].length = opt.E;
    }
    return cache;
}

void DestroyCache(Cache cache)
{
    for (uint64_t i = 0; i < cache.length; ++i)
    {
        free(cache.sets[i].lines);
    }
    free(cache.sets);
}

Result RunCache(Cache cache, Options opt)
{
    Result result = {0, 0, 0};
    char instruction;
    uint64_t address;
    uint64_t set_index_mask = (1 << opt.s) - 1;
    while((fscanf(opt.tracefile, " %c %lx%*[^\n]", &instruction, &address)) == 2) // read instruction and address
    {
        if (instruction != 'I') // Ignore 'I'
        {
            uint64_t set_index = (address >> opt.b) & set_index_mask;
            uint64_t tag = (address >> opt.b) >> opt.s;
            Set set = cache.sets[set_index];

            if (instruction == 'L' || instruction == 'S') // load/store
            {
                result = UpdateSet(set, result, tag);
            }

            if (instruction == 'M') // modify is treated as a load followed by a store to the same address.
            {
                result = UpdateSet(set, result, tag);  // load
                result = UpdateSet(set, result, tag);  // store
            }
        }
    }
    return result;
}

Result UpdateSet(Set set, Result result, uint64_t tag)
{
    Bool hitFlag = false;
    for (uint64_t i = 0; i < set.length; i++)
    {
        if (set.lines[i].tag == tag && set.lines[i].valid) // hit
        {
            hitFlag = true;
            result.hit++;
            set.lines[i].timeStamp = GetSystemTime();
            break;
        }
    }

    if (!hitFlag) // miss
    {
        result.miss++;

        Bool emptyFlag = false;
        for (uint64_t i = 0; i < set.length; i++)
        {
            if (!set.lines[i].valid) // empty line
            {
                emptyFlag = true;
                set.lines[i].timeStamp = GetSystemTime();
                set.lines[i].valid = true;
                set.lines[i].tag = tag;
                break;
            }
        }

        if(!emptyFlag) // eviction
        {
            result.eviction++;
            uint64_t oldestTime = UINT64_MAX;
            uint64_t oldestLine = 0;
            for (uint64_t i = 0; i < set.length; i++)
            {
                if (set.lines[i].timeStamp < oldestTime)
                {
                    oldestTime = set.lines[i].timeStamp;
                    oldestLine = i;
                }
            }
            set.lines[oldestLine].timeStamp = GetSystemTime();
            set.lines[oldestLine].tag = tag;
        }
    }

    return result;
}
